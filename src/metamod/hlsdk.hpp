#pragma once

#if defined(SVEN_JIT_METAMOD_P) && defined(_DEBUG)
// The legacy SDK's debug-only helpers pass string literals to mutable char*.
#define SVEN_JIT_RESTORE_DEBUG_MACRO
#undef _DEBUG
#endif

#include <extdll.h>
#include <dllapi.h>
#include <meta_api.h>

#ifdef SVEN_JIT_RESTORE_DEBUG_MACRO
#define _DEBUG 1
#undef SVEN_JIT_RESTORE_DEBUG_MACRO
#endif

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif
