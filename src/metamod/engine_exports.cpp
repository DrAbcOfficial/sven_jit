#include <extdll.h>
#ifdef SVEN_JIT_METAMOD_P
#undef min
#undef max
#endif
#include <meta_api.h>

#include <cstring>

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
