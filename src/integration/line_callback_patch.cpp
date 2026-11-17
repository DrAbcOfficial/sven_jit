#include "integration/line_callback_patch.hpp"

#include "metamod/hlsdk.hpp"

#include <angelscript.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#ifdef SVEN_JIT_METAMOD_P
#include "integration/module_scan.hpp"
#else
// Defined by plugin_api.cpp; metamod-svencoop exposes the metahook-style
// extension APIs through this table (see mutil.h).
extern mutil_funcs_t* gpMetaUtilFuncs;
#endif

namespace svenjit::integration {
namespace {

// AngelScript request-context callback ABI (asREQUESTCONTEXTFUNC_t); also the
// ABI of the InlineHook trampoline.
using RequestContextFn = asIScriptContext* (*)(asIScriptEngine*, void*);
// CASContextPool::RequestContext ABI on gcc, which passes "this" as the first
// stack argument: the wrapper's tail jump lands here with (pool, engine).
using RequestContextTailFn = asIScriptContext* (*)(void*, asIScriptEngine*);

// '*' (0x2A) is the wildcard byte of both FindPattern and the metamod-svencoop
// pfnSearchPattern.
constexpr std::uint8_t kPatternWildcard = 0x2A;

char g_lineCallbackName[] = "as_line_callback";
char g_lineCallbackDefault[] = "0";

cvar_t g_lineCallbackCvar = {
    g_lineCallbackName,
    g_lineCallbackDefault,
    0,
    0.0f,
    nullptr,
};

bool g_cvarRegistered = false;

#ifdef SVEN_JIT_METAMOD_P
#ifdef _WIN32
// Unused on metamod-p Linux, where the tail-jump hook delegates instead.
RequestContextFn g_requestContext = nullptr;
#endif
RequestContextTailFn g_requestContextTail = nullptr;
std::uint8_t* g_patchAddress = nullptr;
std::uint8_t g_originalBytes[4] = {};
#else
hook_t* g_hook = nullptr;
void* g_trampoline = nullptr;
RequestContextFn g_requestContext = nullptr;
#endif

void RegisterCvar() noexcept {
    if (g_cvarRegistered) {
        return;
    }

    if (g_engfuncs.pfnCVarRegister) {
        g_engfuncs.pfnCVarRegister(&g_lineCallbackCvar);
    }
    g_cvarRegistered = true;
}

// The pool installs CDebugger::LineCallback on every request, including reused
// contexts, so clearing the callback here keeps both toggle directions exact:
// requests made while the cvar is disabled return an un-instrumented context,
// and requests made while it is enabled keep the debugger behavior.
asIScriptContext* ClearLineCallbackIfNeeded(asIScriptContext* context) noexcept {
    if (context && g_lineCallbackCvar.value == 0.0f) {
        context->ClearLineCallback();
    }
    return context;
}

// Unused on metamod-p Linux, where the tail-jump hook is installed instead.
#if !defined(SVEN_JIT_METAMOD_P) || defined(_WIN32)
asIScriptContext* HookedRequestContext(asIScriptEngine* engine, void* parameter) {
    return ClearLineCallbackIfNeeded(g_requestContext(engine, parameter));
}
#endif

#if defined(SVEN_JIT_METAMOD_P)
asIScriptContext* HookedRequestContextTail(void* pool, asIScriptEngine* engine) {
    return ClearLineCallbackIfNeeded(g_requestContextTail(pool, engine));
}
#endif

// The GameDLL registers its pooled context callbacks through
//   engine->SetContextCallbacks(ASRequestContext, ASReturnContext, pool)
// during CASServerManager initialization. Every context request passes through
// CASContextPool::RequestContext, which registers the CDebugger line callback
// on each context and dominates AngelScript execution time.
#ifdef _WIN32
// server.dll 0x100a18b6: the callbacks are pushed as absolute immediates.
constexpr std::uint8_t kSetContextCallbacksSignature[] = {
    0x8B, 0x07,                                        // mov eax, [edi]        ; engine
    0x8B, 0xB0, 0x18, 0x01, 0x00, 0x00,                // mov esi, [eax+0x118]  ; SetContextCallbacks
    0xE8, kPatternWildcard, kPatternWildcard, kPatternWildcard, kPatternWildcard,  // call CASContextPool::GetInstance
    0x50,                                              // push eax              ; pool parameter
    0x68, kPatternWildcard, kPatternWildcard, kPatternWildcard, kPatternWildcard,  // push ASReturnContext
    0x68, kPatternWildcard, kPatternWildcard, kPatternWildcard, kPatternWildcard,  // push ASRequestContext
    0x8B, 0xCF,                                        // mov ecx, edi
    0xFF, 0xD6,                                        // call esi
};

constexpr std::size_t kReturnContextPushOperand = 15;
constexpr std::size_t kRequestContextPushOperand = 20;

bool ResolveCallbackOperands(
    const std::uint8_t* match,
    std::uint32_t& requestAddress,
    std::uint32_t& returnAddress) noexcept {
    std::memcpy(&requestAddress, match + kRequestContextPushOperand, sizeof(requestAddress));
    std::memcpy(&returnAddress, match + kReturnContextPushOperand, sizeof(returnAddress));
    return requestAddress != 0 && returnAddress != 0 &&
        requestAddress != returnAddress;
}
#else
// server.so 0x125b8d: gcc passes the callbacks on the stack as PIC-relative
// addresses, so ASRequestContext is identified through its own wrapper body
// instead. The wrapper swaps the (engine, pool) arguments and tail-jumps to
// CASContextPool::RequestContext (server.so 0x17ea9e -> 0x17e7de).
constexpr std::uint8_t kRequestContextWrapperSignature[] = {
    0x8B, 0x44, 0x24, 0x08,                            // mov eax, [esp+8]  ; pool parameter
    0x8B, 0x54, 0x24, 0x04,                            // mov edx, [esp+4]  ; engine
    0x89, 0x54, 0x24, 0x08,                            // mov [esp+8], edx
    0x89, 0x44, 0x24, 0x04,                            // mov [esp+4], eax
    0xE9, kPatternWildcard, kPatternWildcard, kPatternWildcard, kPatternWildcard,  // jmp CASContextPool::RequestContext
};

constexpr std::size_t kWrapperTailJumpOffset = 16;
constexpr std::size_t kWrapperTailTargetOffset = 21;

// The wrapper body alone is ambiguous (server.so 0xf2c48 matches too), so the
// tail target is fingerprinted: the pool's RequestContext calls CreateContext
// (vtable+0xE8) and SetLineCallback (vtable+0x9C) through disp32 slots.
constexpr std::size_t kFingerprintScanSize = 0x200;

bool ContainsDispatch32(
    const std::uint8_t* bytes,
    std::size_t size,
    std::uint8_t lowByte) noexcept {
    const std::uint8_t pattern[4] = {lowByte, 0, 0, 0};
    if (size < sizeof(pattern)) {
        return false;
    }
    const std::size_t last = size - sizeof(pattern);
    for (std::size_t offset = 0; offset <= last; ++offset) {
        if (bytes[offset] == pattern[0] && bytes[offset + 1] == pattern[1] &&
            bytes[offset + 2] == pattern[2] && bytes[offset + 3] == pattern[3]) {
            return true;
        }
    }
    return false;
}

bool IsPoolRequestContext(
    const std::uint8_t* tailTarget,
    std::size_t scanSize) noexcept {
    const std::size_t size = scanSize < kFingerprintScanSize ? scanSize : kFingerprintScanSize;
    return ContainsDispatch32(tailTarget, size, 0xE8) &&
        ContainsDispatch32(tailTarget, size, 0x9C);
}

std::int32_t ReadTailDisplacement(const std::uint8_t* match) noexcept {
    std::int32_t displacement = 0;
    std::memcpy(&displacement, match + kWrapperTailJumpOffset + 1, sizeof(displacement));
    return displacement;
}
#endif

#ifdef SVEN_JIT_METAMOD_P

// metamod-p's mutil table has no hook extensions.
//
// Windows: the push of ASRequestContext at the registration site is redirected
// to the hook, which delegates to the original callback.
// Linux: the wrapper's tail jump (E9 rel32) is redirected to the hook, which
// delegates to the pool's RequestContext with the (pool, engine) stack order.
const char* ConnectPatchBackend(const void* gameFunctions) noexcept {
    void* anchor = FindGameDllAnchor(gameFunctions);
    if (!anchor) {
        return "GameDLL function table is unavailable for the line-callback patch";
    }

    ModuleView module;
    const char* modulePath = nullptr;
    if (!BuildModuleView(anchor, module, &modulePath)) {
        return "failed to identify the GameDLL image for the line-callback patch";
    }

#ifdef _WIN32
    (void)modulePath;
    const auto matches = FindPattern(
        module,
        kSetContextCallbacksSignature,
        sizeof(kSetContextCallbacksSignature));
    if (matches.size() != 1) {
        return "failed to locate the AngelScript context callback registration";
    }

    std::uint8_t* match = matches.front();
    auto* requestOperand = match + kRequestContextPushOperand;
    auto* returnOperand = match + kReturnContextPushOperand;

    std::uint32_t requestAddress = 0;
    std::uint32_t returnAddress = 0;
    std::memcpy(&requestAddress, requestOperand, sizeof(requestAddress));
    std::memcpy(&returnAddress, returnOperand, sizeof(returnAddress));

    const auto requestContext = reinterpret_cast<void*>(requestAddress);
    const auto returnContext = reinterpret_cast<void*>(returnAddress);
    const auto hook = reinterpret_cast<void*>(&HookedRequestContext);
    if (!module.ContainsExecutable(requestContext) ||
        !module.ContainsExecutable(returnContext) ||
        requestContext == hook) {
        return "unexpected AngelScript context callback registration layout";
    }

    RegisterCvar();

    const auto hookAddress = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(hook));
    if (!WriteExecutableMemory(requestOperand, &hookAddress, sizeof(hookAddress))) {
        return "failed to hook the AngelScript context callback registration";
    }

    g_requestContext = reinterpret_cast<RequestContextFn>(requestAddress);
    g_patchAddress = requestOperand;
    std::memcpy(g_originalBytes, requestOperand, sizeof(g_originalBytes));
    return nullptr;
#else
    (void)modulePath;
    std::uint8_t* wrapper = nullptr;
    std::uint8_t* tailTarget = nullptr;
    unsigned int passing = 0;
    for (const MemoryRange& range : module.executableRanges) {
        if (range.size < sizeof(kRequestContextWrapperSignature)) {
            continue;
        }
        const std::size_t last = range.size - sizeof(kRequestContextWrapperSignature);
        for (std::size_t offset = 0; offset <= last; ++offset) {
            std::uint8_t* match = range.begin + offset;
            if (!MatchesPattern(
                    match,
                    kRequestContextWrapperSignature,
                    sizeof(kRequestContextWrapperSignature))) {
                continue;
            }

            const auto displacement = ReadTailDisplacement(match);
            auto* tail = match + kWrapperTailTargetOffset + displacement;
            const auto tailOffset = static_cast<std::size_t>(tail - range.begin);
            if (tail < range.begin || tailOffset >= range.size) {
                continue;
            }
            if (!IsPoolRequestContext(tail, range.size - tailOffset)) {
                continue;
            }

            ++passing;
            wrapper = match;
            tailTarget = tail;
        }
    }
    if (passing != 1 || !wrapper || !tailTarget) {
        return "failed to locate the AngelScript context callback registration";
    }

    RegisterCvar();

    auto* tailJumpOperand = wrapper + kWrapperTailJumpOffset + 1;
    const auto hookAddress = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(&HookedRequestContextTail));
    const auto hookDisplacement = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(hookAddress) -
        static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(wrapper + kWrapperTailTargetOffset)));
    if (!WriteExecutableMemory(
            tailJumpOperand,
            &hookDisplacement,
            sizeof(hookDisplacement))) {
        return "failed to hook the AngelScript context callback registration";
    }

    g_requestContextTail = reinterpret_cast<RequestContextTailFn>(tailTarget);
    g_patchAddress = tailJumpOperand;
    std::memcpy(g_originalBytes, tailJumpOperand, sizeof(g_originalBytes));
    return nullptr;
#endif
}

void DisconnectPatchBackend() noexcept {
    if (g_patchAddress) {
        WriteExecutableMemory(g_patchAddress, g_originalBytes, sizeof(g_originalBytes));
        g_patchAddress = nullptr;
    }
    // The delegate pointers are kept: once the GameDLL has registered the
    // callback, the engine retains the hook pointer, so the delegates must
    // stay valid.
}

#else

// metamod-svencoop exposes metahook-style utilities; hook ASRequestContext
// through pfnInlineHook instead of patching game code by hand.
const char* ConnectPatchBackend(const void*) noexcept {
    auto* mutil = gpMetaUtilFuncs;
    if (!mutil || !mutil->pfnGetGameDllBase || !mutil->pfnSearchPattern ||
        !mutil->pfnInlineHook || !mutil->pfnUnHook ||
        !mutil->pfnIsAddressInModuleRange) {
        return "metamod-svencoop hook API is unavailable for the line-callback patch";
    }

    void* imageBase = mutil->pfnGetGameDllBase();
    if (!imageBase) {
        return "failed to get the GameDLL base for the line-callback patch";
    }

    // Prefer the executable code range when the SDK provides it.
    void* codeBase = imageBase;
    std::size_t codeSize = 0;
    if (mutil->pfnGetCodeBase && mutil->pfnGetCodeSize) {
        codeBase = mutil->pfnGetCodeBase(imageBase);
        codeSize = mutil->pfnGetCodeSize(imageBase);
    } else if (mutil->pfnGetImageSize) {
        codeSize = mutil->pfnGetImageSize(imageBase);
    }

    RegisterCvar();

#ifdef _WIN32
    if (!codeBase || codeSize < sizeof(kSetContextCallbacksSignature)) {
        return "failed to get the GameDLL code range for the line-callback patch";
    }

    auto* match = static_cast<std::uint8_t*>(mutil->pfnSearchPattern(
        codeBase,
        codeSize,
        reinterpret_cast<const char*>(kSetContextCallbacksSignature),
        sizeof(kSetContextCallbacksSignature)));
    if (!match) {
        return "failed to locate the AngelScript context callback registration";
    }

    const std::size_t remaining = codeSize -
        static_cast<std::size_t>(match + 1 - static_cast<const std::uint8_t*>(codeBase));
    if (remaining >= sizeof(kSetContextCallbacksSignature) &&
        mutil->pfnSearchPattern(
            match + 1,
            remaining,
            reinterpret_cast<const char*>(kSetContextCallbacksSignature),
            sizeof(kSetContextCallbacksSignature))) {
        return "ambiguous AngelScript context callback registration";
    }

    std::uint32_t requestAddress = 0;
    std::uint32_t returnAddress = 0;
    if (!ResolveCallbackOperands(match, requestAddress, returnAddress) ||
        !mutil->pfnIsAddressInModuleRange(
            reinterpret_cast<void*>(requestAddress), imageBase) ||
        !mutil->pfnIsAddressInModuleRange(
            reinterpret_cast<void*>(returnAddress), imageBase)) {
        return "unexpected AngelScript context callback registration layout";
    }

    auto* hookTarget = reinterpret_cast<std::uint8_t*>(requestAddress);
#else
    auto* hookTarget = static_cast<std::uint8_t*>(nullptr);
    unsigned int passing = 0;
    if (!codeBase || codeSize < sizeof(kRequestContextWrapperSignature)) {
        return "failed to get the GameDLL code range for the line-callback patch";
    }

    auto* cursor = static_cast<std::uint8_t*>(codeBase);
    std::size_t remaining = codeSize;
    while (remaining >= sizeof(kRequestContextWrapperSignature)) {
        auto* match = static_cast<std::uint8_t*>(mutil->pfnSearchPattern(
            cursor,
            remaining,
            reinterpret_cast<const char*>(kRequestContextWrapperSignature),
            sizeof(kRequestContextWrapperSignature)));
        if (!match) {
            break;
        }

        const auto displacement = ReadTailDisplacement(match);
        auto* tail = match + kWrapperTailTargetOffset + displacement;
        if (mutil->pfnIsAddressInModuleRange(tail, imageBase)) {
            const auto tailOffset = static_cast<std::size_t>(
                tail - static_cast<const std::uint8_t*>(codeBase));
            if (tailOffset < codeSize &&
                IsPoolRequestContext(tail, codeSize - tailOffset)) {
                ++passing;
                hookTarget = match;
            }
        }

        const auto consumed = static_cast<std::size_t>(match + 1 - cursor);
        if (consumed >= remaining) {
            break;
        }
        remaining -= consumed;
        cursor = match + 1;
    }
    if (passing != 1 || !hookTarget) {
        return "failed to locate the AngelScript context callback registration";
    }
#endif

    g_trampoline = nullptr;
    g_hook = mutil->pfnInlineHook(
        hookTarget,
        reinterpret_cast<void*>(&HookedRequestContext),
        &g_trampoline,
        false);
    if (!g_hook || !g_trampoline) {
        if (g_hook) {
            mutil->pfnUnHook(g_hook);
        }
        g_hook = nullptr;
        g_trampoline = nullptr;
        return "failed to hook the AngelScript request-context callback";
    }

    g_requestContext = reinterpret_cast<RequestContextFn>(g_trampoline);
    return nullptr;
}

void DisconnectPatchBackend() noexcept {
    if (g_hook && gpMetaUtilFuncs && gpMetaUtilFuncs->pfnUnHook) {
        gpMetaUtilFuncs->pfnUnHook(g_hook);
    }
    g_hook = nullptr;
    g_trampoline = nullptr;
    g_requestContext = nullptr;
}

#endif

}

const char* ConnectLineCallbackPatch(const void* gameFunctions) noexcept {
    if (
#ifdef SVEN_JIT_METAMOD_P
        g_patchAddress
#else
        g_hook
#endif
    ) {
        return nullptr;
    }

    return ConnectPatchBackend(gameFunctions);
}

void DisconnectLineCallbackPatch() noexcept {
    DisconnectPatchBackend();
}

}
