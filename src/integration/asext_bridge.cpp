#include "integration/asext_bridge.hpp"

#include <extdll.h>
#include <meta_api.h>

#include <asext_api.h>

namespace svenjit::integration {
namespace {

fnASEXT_RegisterDocInitCallback g_registerDocInitCallback = nullptr;
fnASEXT_GetServerManager g_getServerManager = nullptr;
EngineReadyCallback g_engineReadyCallback = nullptr;

void OnDocumentationInitialized(CASDocumentation*) {
    CASServerManager* manager = g_getServerManager ? g_getServerManager() : nullptr;
    asIScriptEngine* engine = manager ? manager->GetScriptEngine() : nullptr;
    if (engine && g_engineReadyCallback) {
        g_engineReadyCallback(engine);
    }
}

}

const char* ConnectAsext(
    void* utilityTable,
    void* pluginId,
    EngineReadyCallback callback) noexcept {
    if (!utilityTable || !pluginId || !callback) {
        return "invalid asext integration arguments";
    }

    auto* utilities = static_cast<mutil_funcs_t*>(utilityTable);
    auto* plugin = static_cast<plugin_info_t*>(pluginId);

    void* handle = nullptr;
#ifdef _WIN32
    constexpr const char* path = "addons/metamod/dlls/asext.dll";
#else
    constexpr const char* path = "addons/metamod/dlls/asext.so";
#endif

    if (utilities->pfnLoadPlugin(plugin, path, PT_STARTUP, &handle) != 0 || !handle) {
        return "failed to load asext";
    }

    g_registerDocInitCallback = reinterpret_cast<fnASEXT_RegisterDocInitCallback>(
        utilities->pfnGetProcAddress(
            reinterpret_cast<DLHANDLE>(handle),
            "ASEXT_RegisterDocInitCallback"));
    g_getServerManager = reinterpret_cast<fnASEXT_GetServerManager>(
        utilities->pfnGetProcAddress(
            reinterpret_cast<DLHANDLE>(handle),
            "ASEXT_GetServerManager"));

    if (!g_registerDocInitCallback || !g_getServerManager) {
        return "required asext API is unavailable";
    }

    g_engineReadyCallback = callback;
    if (!g_registerDocInitCallback(&OnDocumentationInitialized)) {
        g_engineReadyCallback = nullptr;
        return "AngelScript is already initialized; load sven_jit at server startup";
    }

    return nullptr;
}

}
