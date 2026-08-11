#include <cstring>

#include <extdll.h>
#include <meta_api.h>

#include "integration/asext_bridge.hpp"
#include "jit/jit_service.hpp"

meta_globals_t* gpMetaGlobals = nullptr;
gamedll_funcs_t* gpGamedllFuncs = nullptr;
mutil_funcs_t* gpMetaUtilFuncs = nullptr;

plugin_info_t Plugin_info = {
    META_INTERFACE_VERSION,
    "Sven AngelScript JIT",
    "1.0.0",
    "2026-08-11",
    "sven_jit contributors",
    "",
    "SVEN_JIT",
    PT_STARTUP,
    PT_NEVER,
};

namespace {

META_FUNCTIONS g_metaFunctions = {
    nullptr,
    nullptr,
    GetEntityAPI2,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

void OnEngineReady(asIScriptEngine* engine) noexcept {
    if (svenjit::jit::Install(engine)) {
        LOG_MESSAGE(PLID, "AngelScript JIT enabled");
    } else {
        LOG_ERROR(PLID, "Failed to enable AngelScript JIT");
    }
}

}

C_DLLEXPORT int Meta_Query(
    const char* interfaceVersion,
    plugin_info_t** pluginInfo,
    mutil_funcs_t* utilities) {
    if (!interfaceVersion || !pluginInfo || !utilities) {
        return FALSE;
    }

    if (std::strcmp(interfaceVersion, META_INTERFACE_VERSION) != 0) {
        utilities->pfnLogError(
            PLID,
            "Metamod interface mismatch: expected %s, got %s",
            META_INTERFACE_VERSION,
            interfaceVersion);
        return FALSE;
    }

    *pluginInfo = &Plugin_info;
    gpMetaUtilFuncs = utilities;
    return TRUE;
}

C_DLLEXPORT int Meta_Attach(
    PLUG_LOADTIME,
    META_FUNCTIONS* functionTable,
    meta_globals_t* metaGlobals,
    gamedll_funcs_t* gameFunctions) {
    if (!functionTable || !metaGlobals || !gameFunctions || !gpMetaUtilFuncs) {
        return FALSE;
    }

    gpMetaGlobals = metaGlobals;
    gpGamedllFuncs = gameFunctions;
    std::memcpy(functionTable, &g_metaFunctions, sizeof(g_metaFunctions));

    const char* error = svenjit::integration::ConnectAsext(
        gpMetaUtilFuncs,
        PLID,
        &OnEngineReady);
    if (error) {
        LOG_ERROR(PLID, "%s", error);
        return FALSE;
    }

    return TRUE;
}

C_DLLEXPORT int Meta_Detach(PLUG_LOADTIME, PL_UNLOAD_REASON) {
    return TRUE;
}
