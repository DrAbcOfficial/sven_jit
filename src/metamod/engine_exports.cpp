#include <cstring>

#include <extdll.h>
#include <meta_api.h>

enginefuncs_t g_engfuncs{};
globalvars_t* gpGlobals = nullptr;

C_DLLEXPORT void WINAPI GiveFnptrsToDll(
    enginefuncs_t* engineFunctions,
    globalvars_t* globals) {
    if (engineFunctions) {
        std::memcpy(&g_engfuncs, engineFunctions, sizeof(g_engfuncs));
    }
    gpGlobals = globals;
}
