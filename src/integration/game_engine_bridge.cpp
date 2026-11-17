#include "integration/game_engine_bridge.hpp"

#include "metamod/hlsdk.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "integration/line_callback_patch.hpp"
#include "integration/module_scan.hpp"

#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace svenjit::integration {
namespace {

constexpr std::size_t kServerManagerEngineOffset = 12;

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
    return WriteExecutableMemory(call + 1, &displacement, sizeof(displacement));
}

bool RestoreCall(const CallPatch& patch) noexcept {
    return WriteExecutableMemory(
        patch.address,
        patch.original.data(),
        patch.original.size());
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

#ifdef _WIN32
bool LocateServerManager(const ModuleView& module) {
    const auto matches = FindPattern(
        module,
        kServerManagerSignature,
        sizeof(kServerManagerSignature));
    if (matches.size() != 1) {
        return false;
    }
    std::memcpy(&g_serverManager, matches.front() + 5, sizeof(g_serverManager));
    return g_serverManager != nullptr;
}
#else
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

bool LocateServerManager(const ModuleView& module, const char* modulePath) {
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
}
#endif

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

    if (const char* patchError = ConnectLineCallbackPatch(gameFunctions)) {
        LOG_ERROR(PLID, "%s", patchError);
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
    if (!BuildModuleView(anchor, module, &modulePath)) {
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
    DisconnectLineCallbackPatch();
    g_engineReadyCallback = nullptr;
    g_registerObjectType = nullptr;
    g_serverManager = nullptr;
}

}
