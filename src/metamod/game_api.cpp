#include <extdll.h>
#ifdef SVEN_JIT_METAMOD_P
#undef min
#undef max
#endif
#include <dllapi.h>

#include <cstring>

C_DLLEXPORT int GetEntityAPI2(
    DLL_FUNCTIONS* functionTable,
    int* interfaceVersion) {
    if (!functionTable || !interfaceVersion) {
        return FALSE;
    }

    if (*interfaceVersion != INTERFACE_VERSION) {
        *interfaceVersion = INTERFACE_VERSION;
        return FALSE;
    }

    std::memset(functionTable, 0, sizeof(*functionTable));
    return TRUE;
}
