#include "metamod/hlsdk.hpp"

#include <cstring>

enginefuncs_t g_engfuncs{};
globalvars_t* gpGlobals = nullptr;

#ifdef _WIN32
extern "C"
#else
C_DLLEXPORT
#endif
void WINAPI GiveFnptrsToDll(
    enginefuncs_t* engineFunctions,
    globalvars_t* globals) {
    if (engineFunctions) {
        std::memcpy(&g_engfuncs, engineFunctions, sizeof(g_engfuncs));
    }
    gpGlobals = globals;
}
