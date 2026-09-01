#include "integration/game_engine_bridge.hpp"

#include "metamod/hlsdk.hpp"

#include "asext_api.h"

fnASEXT_RegisterDocInitCallback ASEXT_RegisterDocInitCallback = nullptr;
fnASEXT_GetServerManager ASEXT_GetServerManager = nullptr;

namespace svenjit::integration {
namespace {

EngineReadyCallback g_engineReadyCallback = nullptr;

asIScriptEngine* ScriptEngineFromAsext() noexcept {
    if (!ASEXT_GetServerManager) {
        return nullptr;
    }
    CASServerManager* manager = ASEXT_GetServerManager();
    return manager ? manager->GetScriptEngine() : nullptr;
}

void OnDocumentationInit(CASDocumentation*) {
    EngineReadyCallback callback = g_engineReadyCallback;
    g_engineReadyCallback = nullptr;
    if (!callback) {
        return;
    }

    asIScriptEngine* engine = ScriptEngineFromAsext();
    if (!engine) {
        LOG_ERROR(PLID, "Failed to get AngelScript engine from asext");
        return;
    }
    callback(engine);
}

bool ImportAsext(void* asextHandle) noexcept {
    ASEXT_RegisterDocInitCallback = reinterpret_cast<fnASEXT_RegisterDocInitCallback>(
        DLSYM((DLHANDLE)asextHandle, "ASEXT_RegisterDocInitCallback"));
    ASEXT_GetServerManager = reinterpret_cast<fnASEXT_GetServerManager>(
        DLSYM((DLHANDLE)asextHandle, "ASEXT_GetServerManager"));
    return ASEXT_RegisterDocInitCallback && ASEXT_GetServerManager;
}

}

const char* ConnectGameEngine(
    void*,
    EngineReadyCallback callback) noexcept {
    if (!callback) {
        return "invalid game engine integration arguments";
    }

    void* asextHandle = nullptr;
#ifdef _WIN32
    LOAD_PLUGIN(PLID, "addons/metamod/dlls/asext.dll", PLUG_LOADTIME::PT_ANYTIME, &asextHandle);
#else
    LOAD_PLUGIN(PLID, "addons/metamod/dlls/asext.so", PLUG_LOADTIME::PT_ANYTIME, &asextHandle);
#endif
    if (!asextHandle) {
        return "asext dll handle not found";
    }
    if (!ImportAsext(asextHandle)) {
        return "failed to import asext API";
    }

    if (asIScriptEngine* engine = ScriptEngineFromAsext()) {
        callback(engine);
        return nullptr;
    }

    g_engineReadyCallback = callback;
    if (!ASEXT_RegisterDocInitCallback(&OnDocumentationInit)) {
        g_engineReadyCallback = nullptr;
        return "failed to register AngelScript documentation callback";
    }
    return nullptr;
}

void DisconnectGameEngine() noexcept {
    g_engineReadyCallback = nullptr;
}

}
