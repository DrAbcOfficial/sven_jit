#include "integration/game_engine_bridge.hpp"

#include "metamod/hlsdk.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace svenjit::integration {
namespace {

constexpr std::uint8_t kWildcard = 0x2A;
constexpr std::size_t kServerManagerEngineOffset = 12;

struct MemoryRange {
    std::uint8_t* begin;
    std::size_t size;
};

struct ModuleView {
    std::vector<MemoryRange> executableRanges;

    bool ContainsExecutable(const void* address) const noexcept {
        const auto value = reinterpret_cast<std::uintptr_t>(address);
        for (const MemoryRange& range : executableRanges) {
            const auto begin = reinterpret_cast<std::uintptr_t>(range.begin);
            if (value >= begin && value < begin + range.size) {
                return true;
            }
        }
        return false;
    }
};

struct CallPatch {
    std::uint8_t* address;
    std::array<std::uint8_t, 5> original;
};

class CASDocumentation;
class CASServerManager;

#ifdef _WIN32
#define SC_SERVER_DECL __fastcall
#define SC_SERVER_DUMMY_ARGUMENT int
#define SC_SERVER_PASS_DUMMY_ARGUMENT dummy,
#else
#define SC_SERVER_DECL
#define SC_SERVER_DUMMY_ARGUMENT
#define SC_SERVER_PASS_DUMMY_ARGUMENT
#endif

using RegisterObjectTypeFn = int (SC_SERVER_DECL *)(
    CASDocumentation*,
#ifdef _WIN32
    SC_SERVER_DUMMY_ARGUMENT,
#endif
    const char*,
    const char*,
    int,
    unsigned int);

CASServerManager** g_serverManager = nullptr;
RegisterObjectTypeFn g_registerObjectType = nullptr;
EngineReadyCallback g_engineReadyCallback = nullptr;
std::vector<CallPatch> g_callPatches;

constexpr std::uint8_t kRegisterObjectTypeSignature[] = {
#ifdef _WIN32
    0x68, 0x01, 0x00, 0x04, 0x00, 0x6A, 0x00, 0x68,
    kWildcard, kWildcard, kWildcard, kWildcard, 0x68,
    kWildcard, kWildcard, kWildcard, kWildcard, 0x8B, 0xCE, 0xE8,
#else
    0xC7, 0x44, 0x24, kWildcard, 0x01, 0x00, 0x04, 0x00,
    0xC7, 0x44, 0x24, kWildcard, 0x00, 0x00, 0x00, 0x00,
    0x8D, kWildcard, kWildcard, kWildcard, kWildcard, kWildcard,
    0x89, kWildcard, kWildcard, kWildcard,
    0x8D, kWildcard, kWildcard, kWildcard, kWildcard, kWildcard,
    0x89, kWildcard, kWildcard, kWildcard,
    0x8B, kWildcard, kWildcard, kWildcard, kWildcard, 0x00, 0x00,
    0x89, 0x04, 0x24, 0xE8,
#endif
};

#ifdef _WIN32
constexpr std::uint8_t kServerManagerSignature[] = {
    0xC6, 0x45, kWildcard, 0x01, 0xA3,
    kWildcard, kWildcard, kWildcard, kWildcard,
    0x6A, 0x01, 0xFF, 0x70, kWildcard, 0xFF, 0x75, kWildcard, 0xE8,
};
#else
constexpr std::uint8_t kServerManagerPicSignature[] = {
    0x83, 0xEC, kWildcard, 0xE8,
    kWildcard, kWildcard, kWildcard, kWildcard,
    0x81, kWildcard, kWildcard, kWildcard, kWildcard, kWildcard,
    0x8B, kWildcard, 0x24, kWildcard,
    0x8B, kWildcard, kWildcard, kWildcard, kWildcard, 0x00,
    0x85, kWildcard, 0x74, kWildcard, 0x0F, kWildcard, kWildcard, 0x06,
};

constexpr const char* kServerManagerSymbol =
    "_ZZN16CASServerManager11GetInstanceEvE9pInstance";
constexpr const char* kRegisterObjectTypeSymbol =
    "_ZN16CASDocumentation18RegisterObjectTypeEPKcS1_im";
#endif

bool MatchesPattern(
    const std::uint8_t* address,
    const std::uint8_t* pattern,
    std::size_t patternSize) noexcept {
    for (std::size_t index = 0; index < patternSize; ++index) {
        if (pattern[index] != kWildcard && address[index] != pattern[index]) {
            return false;
        }
    }
    return true;
}

std::vector<std::uint8_t*> FindPattern(
    const ModuleView& module,
    const std::uint8_t* pattern,
    std::size_t patternSize) {
    std::vector<std::uint8_t*> matches;
    for (const MemoryRange& range : module.executableRanges) {
        if (range.size < patternSize) {
            continue;
        }
        const std::size_t last = range.size - patternSize;
        for (std::size_t offset = 0; offset <= last; ++offset) {
            std::uint8_t* candidate = range.begin + offset;
            if (MatchesPattern(candidate, pattern, patternSize)) {
                matches.push_back(candidate);
            }
        }
    }
    return matches;
}

void* ResolveRelativeCall(const std::uint8_t* call) noexcept {
    if (!call || call[0] != 0xE8) {
        return nullptr;
    }
    std::int32_t displacement = 0;
    std::memcpy(&displacement, call + 1, sizeof(displacement));
    return const_cast<std::uint8_t*>(call + 5 + displacement);
}

bool WriteRelativeCall(std::uint8_t* call, void* destination) noexcept {
    const auto sourceAfterCall = reinterpret_cast<std::uintptr_t>(call + 5);
    const auto target = reinterpret_cast<std::uintptr_t>(destination);
    const auto displacement = static_cast<std::uint32_t>(target - sourceAfterCall);

#ifdef _WIN32
    DWORD oldProtection = 0;
    if (!VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &oldProtection)) {
        return false;
    }
#else
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) {
        return false;
    }
    const auto pageMask = static_cast<std::uintptr_t>(pageSize - 1);
    const auto begin = reinterpret_cast<std::uintptr_t>(call) & ~pageMask;
    const auto end = (reinterpret_cast<std::uintptr_t>(call + 5) + pageMask) & ~pageMask;
    if (mprotect(
            reinterpret_cast<void*>(begin),
            end - begin,
            PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        return false;
    }
#endif

    call[0] = 0xE8;
    std::memcpy(call + 1, &displacement, sizeof(displacement));

#ifdef _WIN32
    FlushInstructionCache(GetCurrentProcess(), call, 5);
    DWORD ignored = 0;
    VirtualProtect(call, 5, oldProtection, &ignored);
#else
    __builtin___clear_cache(
        reinterpret_cast<char*>(call),
        reinterpret_cast<char*>(call + 5));
    mprotect(
        reinterpret_cast<void*>(begin),
        end - begin,
        PROT_READ | PROT_EXEC);
#endif
    return true;
}

bool RestoreCall(const CallPatch& patch) noexcept {
#ifdef _WIN32
    DWORD oldProtection = 0;
    if (!VirtualProtect(patch.address, patch.original.size(), PAGE_EXECUTE_READWRITE, &oldProtection)) {
        return false;
    }
#else
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) {
        return false;
    }
    const auto pageMask = static_cast<std::uintptr_t>(pageSize - 1);
    const auto begin = reinterpret_cast<std::uintptr_t>(patch.address) & ~pageMask;
    const auto end =
        (reinterpret_cast<std::uintptr_t>(patch.address + patch.original.size()) + pageMask) &
        ~pageMask;
    if (mprotect(
            reinterpret_cast<void*>(begin),
            end - begin,
            PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        return false;
    }
#endif

    std::memcpy(patch.address, patch.original.data(), patch.original.size());

#ifdef _WIN32
    FlushInstructionCache(GetCurrentProcess(), patch.address, patch.original.size());
    DWORD ignored = 0;
    VirtualProtect(patch.address, patch.original.size(), oldProtection, &ignored);
#else
    __builtin___clear_cache(
        reinterpret_cast<char*>(patch.address),
        reinterpret_cast<char*>(patch.address + patch.original.size()));
    mprotect(
        reinterpret_cast<void*>(begin),
        end - begin,
        PROT_READ | PROT_EXEC);
#endif
    return true;
}

void RemoveCallPatches() noexcept {
    for (const CallPatch& patch : g_callPatches) {
        RestoreCall(patch);
    }
    g_callPatches.clear();
}

asIScriptEngine* GetScriptEngine() noexcept {
    CASServerManager* serverManager = g_serverManager ? *g_serverManager : nullptr;
    if (!serverManager) {
        return nullptr;
    }
    auto* manager = reinterpret_cast<std::uint8_t*>(serverManager);
    asIScriptEngine* engine = nullptr;
    std::memcpy(&engine, manager + kServerManagerEngineOffset, sizeof(engine));
    return engine;
}

bool NotifyEngineReady() noexcept {
    if (!g_engineReadyCallback) {
        return true;
    }
    asIScriptEngine* engine = GetScriptEngine();
    if (!engine) {
        return false;
    }
    EngineReadyCallback callback = g_engineReadyCallback;
    g_engineReadyCallback = nullptr;
    RemoveCallPatches();
    callback(engine);
    return true;
}

bool IsDocumentationReadyMarker(
    const char* docs,
    const char* name,
    unsigned int flags) noexcept {
    return docs && name && flags == 0x40001u &&
        std::strcmp(name, "CSurvivalMode") == 0 &&
        std::strcmp(docs, "Survival Mode handler") == 0;
}

int SC_SERVER_DECL HookedRegisterObjectType(
    CASDocumentation* documentation,
#ifdef _WIN32
    SC_SERVER_DUMMY_ARGUMENT dummy,
#endif
    const char* docs,
    const char* name,
    int size,
    unsigned int flags) {
    if (g_engineReadyCallback && IsDocumentationReadyMarker(docs, name, flags)) {
        NotifyEngineReady();
    }

    return g_registerObjectType(
        documentation,
#ifdef _WIN32
        SC_SERVER_PASS_DUMMY_ARGUMENT
#endif
        docs,
        name,
        size,
        flags);
}

void* FindGameDllAnchor(gamedll_funcs_t* gameFunctions) noexcept {
    if (!gameFunctions || !gameFunctions->dllapi_table) {
        return nullptr;
    }

    DLL_FUNCTIONS* functions = gameFunctions->dllapi_table;
    const std::array<void*, 4> candidates = {
        reinterpret_cast<void*>(functions->pfnGameInit),
        reinterpret_cast<void*>(functions->pfnSpawn),
        reinterpret_cast<void*>(functions->pfnServerActivate),
        reinterpret_cast<void*>(functions->pfnStartFrame),
    };
    for (void* candidate : candidates) {
        if (candidate) {
            return candidate;
        }
    }
    return nullptr;
}

#ifdef _WIN32
bool BuildModuleView(void* anchor, ModuleView& module) {
    MEMORY_BASIC_INFORMATION memory{};
    if (!VirtualQuery(anchor, &memory, sizeof(memory)) || !memory.AllocationBase) {
        return false;
    }

    auto* base = static_cast<std::uint8_t*>(memory.AllocationBase);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned int index = 0; index < nt->FileHeader.NumberOfSections; ++index) {
        if ((section[index].Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) {
            continue;
        }
        const std::size_t size = section[index].Misc.VirtualSize;
        if (size != 0) {
            module.executableRanges.push_back({base + section[index].VirtualAddress, size});
        }
    }
    return !module.executableRanges.empty() && module.ContainsExecutable(anchor);
}
#else
struct ModuleSearchContext {
    std::uintptr_t anchor;
    ModuleView* module;
    const char* path;
};

int FindContainingModule(dl_phdr_info* info, std::size_t, void* data) {
    auto* context = static_cast<ModuleSearchContext*>(data);
    bool containsAnchor = false;
    for (ElfW(Half) index = 0; index < info->dlpi_phnum; ++index) {
        const ElfW(Phdr)& header = info->dlpi_phdr[index];
        if (header.p_type != PT_LOAD) {
            continue;
        }
        const auto begin = static_cast<std::uintptr_t>(info->dlpi_addr + header.p_vaddr);
        if (context->anchor >= begin && context->anchor < begin + header.p_memsz) {
            containsAnchor = true;
            break;
        }
    }
    if (!containsAnchor) {
        return 0;
    }

    for (ElfW(Half) index = 0; index < info->dlpi_phnum; ++index) {
        const ElfW(Phdr)& header = info->dlpi_phdr[index];
        if (header.p_type == PT_LOAD && (header.p_flags & PF_X) != 0 && header.p_memsz != 0) {
            context->module->executableRanges.push_back({
                reinterpret_cast<std::uint8_t*>(info->dlpi_addr + header.p_vaddr),
                static_cast<std::size_t>(header.p_memsz),
            });
        }
    }
    context->path = info->dlpi_name;
    return 1;
}

bool BuildModuleView(void* anchor, ModuleView& module, const char*& modulePath) {
    ModuleSearchContext context{
        reinterpret_cast<std::uintptr_t>(anchor),
        &module,
        nullptr,
    };
    dl_iterate_phdr(&FindContainingModule, &context);
    modulePath = context.path;
    return !module.executableRanges.empty() && module.ContainsExecutable(anchor);
}

void* FindSymbol(const char* modulePath, const char* symbol) noexcept {
    if (!modulePath || !*modulePath) {
        return nullptr;
    }
    void* handle = dlopen(modulePath, RTLD_NOW | RTLD_NOLOAD);
    if (!handle) {
        return nullptr;
    }
    void* address = dlsym(handle, symbol);
    dlclose(handle);
    return address;
}
#endif

bool LocateServerManager(
    const ModuleView& module
#ifndef _WIN32
    , const char* modulePath
#endif
) {
#ifdef _WIN32
    const auto matches = FindPattern(
        module,
        kServerManagerSignature,
        sizeof(kServerManagerSignature));
    if (matches.size() != 1) {
        return false;
    }
    std::memcpy(&g_serverManager, matches.front() + 5, sizeof(g_serverManager));
    return g_serverManager != nullptr;
#else
    g_serverManager = static_cast<CASServerManager**>(
        FindSymbol(modulePath, kServerManagerSymbol));
    if (g_serverManager) {
        return true;
    }

    const auto matches = FindPattern(
        module,
        kServerManagerPicSignature,
        sizeof(kServerManagerPicSignature));
    if (matches.size() != 1) {
        return false;
    }

    std::uint8_t* signature = matches.front();
    std::uint8_t* addInstruction = signature + 8;
    std::int32_t gotDisplacement = 0;
    std::memcpy(&gotDisplacement, addInstruction + 2, sizeof(gotDisplacement));
    std::uint8_t* globalOffsetTable = addInstruction + gotDisplacement;

    std::uint8_t* managerInstruction = signature + 18;
    std::int32_t managerDisplacement = 0;
    std::memcpy(&managerDisplacement, managerInstruction + 2, sizeof(managerDisplacement));
    g_serverManager = reinterpret_cast<CASServerManager**>(
        globalOffsetTable + managerDisplacement);
    return g_serverManager != nullptr;
#endif
}

std::vector<std::uint8_t*> FindDirectCallsTo(
    const ModuleView& module,
    const void* target) {
    std::vector<std::uint8_t*> calls;
    for (const MemoryRange& range : module.executableRanges) {
        if (range.size < 5) {
            continue;
        }
        for (std::size_t offset = 0; offset <= range.size - 5; ++offset) {
            std::uint8_t* candidate = range.begin + offset;
            if (candidate[0] == 0xE8 && ResolveRelativeCall(candidate) == target) {
                calls.push_back(candidate);
            }
        }
    }
    return calls;
}

bool LocateRegisterObjectType(
    const ModuleView& module
#ifndef _WIN32
    , const char* modulePath
#endif
) {
    const auto signatureMatches = FindPattern(
        module,
        kRegisterObjectTypeSignature,
        sizeof(kRegisterObjectTypeSignature));
    if (!signatureMatches.empty()) {
        std::uint8_t* call =
            signatureMatches.front() + sizeof(kRegisterObjectTypeSignature) - 1;
        g_registerObjectType = reinterpret_cast<RegisterObjectTypeFn>(
            ResolveRelativeCall(call));
        return g_registerObjectType && module.ContainsExecutable(
            reinterpret_cast<void*>(g_registerObjectType));
    }

#ifndef _WIN32
    g_registerObjectType = reinterpret_cast<RegisterObjectTypeFn>(
        FindSymbol(modulePath, kRegisterObjectTypeSymbol));
    if (!g_registerObjectType || !module.ContainsExecutable(
            reinterpret_cast<void*>(g_registerObjectType))) {
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool InstallCallPatches(const std::vector<std::uint8_t*>& calls) {
    for (std::uint8_t* call : calls) {
        CallPatch patch{call, {}};
        std::memcpy(patch.original.data(), call, patch.original.size());
        if (!WriteRelativeCall(
                call,
                reinterpret_cast<void*>(&HookedRegisterObjectType))) {
            RemoveCallPatches();
            return false;
        }
        g_callPatches.push_back(patch);
    }
    return true;
}

}

const char* ConnectGameEngine(
    void* gameFunctions,
    EngineReadyCallback callback) noexcept {
    if (!gameFunctions || !callback) {
        return "invalid game engine integration arguments";
    }

    auto* functions = static_cast<gamedll_funcs_t*>(gameFunctions);
    void* anchor = FindGameDllAnchor(functions);
    if (!anchor) {
        return "GameDLL function table is unavailable";
    }

    ModuleView module;
#ifdef _WIN32
    if (!BuildModuleView(anchor, module)) {
#else
    const char* modulePath = nullptr;
    if (!BuildModuleView(anchor, module, modulePath)) {
#endif
        return "failed to identify the GameDLL image";
    }

    if (!LocateServerManager(
            module
#ifndef _WIN32
            , modulePath
#endif
            )) {
        return "failed to locate the AngelScript server manager";
    }

    g_engineReadyCallback = callback;
    if (asIScriptEngine* engine = GetScriptEngine()) {
        g_engineReadyCallback = nullptr;
        callback(engine);
        return nullptr;
    }

    if (!LocateRegisterObjectType(
            module
#ifndef _WIN32
            , modulePath
#endif
            )) {
        g_engineReadyCallback = nullptr;
        return "failed to locate the AngelScript registration function";
    }

    const std::vector<std::uint8_t*> calls = FindDirectCallsTo(
        module,
        reinterpret_cast<void*>(g_registerObjectType));
    if (calls.empty()) {
        g_engineReadyCallback = nullptr;
        return "failed to locate calls to the AngelScript registration function";
    }
    if (!InstallCallPatches(calls)) {
        g_engineReadyCallback = nullptr;
        return "failed to hook the AngelScript registration function";
    }
    return nullptr;
}

void DisconnectGameEngine() noexcept {
    RemoveCallPatches();
    g_engineReadyCallback = nullptr;
    g_registerObjectType = nullptr;
    g_serverManager = nullptr;
}

}
