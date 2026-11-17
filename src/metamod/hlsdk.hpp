#pragma once

#if defined(SVEN_JIT_METAMOD_P) && defined(_DEBUG)
// The legacy SDK's debug-only helpers pass string literals to mutable char*.
#define SVEN_JIT_RESTORE_DEBUG_MACRO
#undef _DEBUG
#endif

#if defined(SVEN_JIT_METAMOD_P) && !defined(_WIN32)
// The SDK's extdll.h defines min/max macros and then pulls in C++ standard
// headers through its own includes; parse the affected stdlib headers first
// so they never see the macros.
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>
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
