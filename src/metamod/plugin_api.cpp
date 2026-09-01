#include "metamod/hlsdk.hpp"

#include <cstring>

#include <as_jit_x86.h>

#include "integration/game_engine_bridge.hpp"
#include "jit/jit_service.hpp"

meta_globals_t* gpMetaGlobals = nullptr;
gamedll_funcs_t* gpGamedllFuncs = nullptr;
mutil_funcs_t* gpMetaUtilFuncs = nullptr;

char g_pluginInterfaceVersion[] = META_INTERFACE_VERSION;
char g_pluginName[] = "Sven AngelScript JIT";
char g_pluginVersion[] = SVEN_JIT_VERSION;
char g_pluginDate[] = "2026-09-01";
char g_pluginAuthor[] = "DrAbc";
char g_pluginUrl[] = "";
char g_pluginLogTag[] = "SVEN_JIT";

plugin_info_t Plugin_info = {
    g_pluginInterfaceVersion,
    g_pluginName,
    g_pluginVersion,
    g_pluginDate,
    g_pluginAuthor,
    g_pluginUrl,
    g_pluginLogTag,
    PT_STARTUP,
    PT_NEVER,
};

namespace {

META_FUNCTIONS g_metaFunctions = [] {
    META_FUNCTIONS functions{};
    functions.pfnGetEntityAPI2 = GetEntityAPI2;
    return functions;
}();

void OnEngineReady(asIScriptEngine* engine) noexcept {
    if (svenjit::jit::Install(engine)) {
        LOG_MESSAGE(PLID, "AngelScript JIT enabled");
    } else {
        LOG_ERROR(PLID, "Failed to enable AngelScript JIT");
    }
}

}

C_DLLEXPORT int Meta_Query(
#ifdef SVEN_JIT_METAMOD_P
    char* interfaceVersion,
#else
    const char* interfaceVersion,
#endif
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

    const char* compatibilityError = AsJitGetCompatibilityError();
    if (compatibilityError) {
        utilities->pfnLogError(PLID, "%s", compatibilityError);
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

    const char* error = svenjit::integration::ConnectGameEngine(
        gpGamedllFuncs,
        &OnEngineReady);
    if (error) {
        LOG_ERROR(PLID, "%s", error);
        return FALSE;
    }

    return TRUE;
}

C_DLLEXPORT int Meta_Detach(PLUG_LOADTIME, PL_UNLOAD_REASON) {
    svenjit::integration::DisconnectGameEngine();
    return TRUE;
}
