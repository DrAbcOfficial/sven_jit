cmake_minimum_required(VERSION 3.24)

project(sven_jit VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(SVEN_JIT_ENABLE_AVX2 "Enable the SSE2+AVX2 JIT variant" OFF)
set(SVEN_JIT_METAMOD "metamod-fallguys" CACHE STRING
    "Metamod SDK compatibility target (metamod-fallguys or metamod-p)")
set_property(CACHE SVEN_JIT_METAMOD PROPERTY STRINGS
    metamod-fallguys
    metamod-p)

if(NOT WIN32 AND NOT UNIX)
    message(FATAL_ERROR "sven_jit supports Windows and Linux only")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "sven_jit targets 32-bit x86 only. "
        "Windows: configure with -A Win32. Linux: use -DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32")
endif()

if(UNIX)
    add_compile_options(-m32)
    add_link_options(-m32)
endif()

set(ASJITX86_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ASJITX86_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASJITX86_ENABLE_SSE ON CACHE BOOL "" FORCE)
set(ASJITX86_ENABLE_AVX2 ${SVEN_JIT_ENABLE_AVX2} CACHE BOOL "" FORCE)
add_subdirectory(thirdparty/angelscript_jit_x86 EXCLUDE_FROM_ALL)

if(SVEN_JIT_METAMOD STREQUAL "metamod-fallguys")
    set(METAMOD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/metamod")
elseif(SVEN_JIT_METAMOD STREQUAL "metamod-p")
    set(METAMOD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/metamod-p")
else()
    message(FATAL_ERROR
        "Unsupported SVEN_JIT_METAMOD value: ${SVEN_JIT_METAMOD}. "
        "Expected metamod-fallguys or metamod-p.")
endif()

if(NOT EXISTS "${METAMOD_ROOT}/metamod/meta_api.h")
    message(FATAL_ERROR
        "The ${SVEN_JIT_METAMOD} SDK is unavailable at ${METAMOD_ROOT}. "
        "Initialize the repository submodules before configuring.")
endif()

message(STATUS "Configuring sven_jit for ${SVEN_JIT_METAMOD}")

add_library(sven_jit SHARED
    src/integration/game_engine_bridge.cpp
    src/jit/jit_service.cpp
    src/metamod/engine_exports.cpp
    src/metamod/game_api.cpp
    src/metamod/plugin_api.cpp
)

if(WIN32)
    target_sources(sven_jit PRIVATE
        src/metamod/sven_jit.def)
endif()

if(MSVC)
    target_compile_options(sven_jit PRIVATE
        /arch:IA32
        /Gy
        $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:/GS->
        $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:/Oi>
        $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:/Oy>
        $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:/Gw>)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sven_jit PRIVATE
        -mno-sse -mno-sse2 -mno-avx -mno-avx2
        -fno-semantic-interposition
        -ffunction-sections
        -fdata-sections)
endif()

target_include_directories(sven_jit PRIVATE
    src
    "${METAMOD_ROOT}/hlsdk/common"
    "${METAMOD_ROOT}/hlsdk/dlls"
    "${METAMOD_ROOT}/hlsdk/engine"
    "${METAMOD_ROOT}/hlsdk/pm_shared"
    "${METAMOD_ROOT}/metamod"
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/angelscript_jit_x86/third_party/angelscript/sdk/angelscript/include"
)

target_link_libraries(sven_jit PRIVATE as_jit_x86 ${CMAKE_DL_LIBS})

target_compile_definitions(sven_jit PRIVATE
    SVEN_JIT_VERSION="${PROJECT_VERSION}"
    $<$<PLATFORM_ID:Windows>:WIN32;_WINDOWS;_USRDLL;NOMINMAX;_CRT_SECURE_NO_WARNINGS>
    $<$<PLATFORM_ID:Linux>:linux;LINUX;_LINUX;PLATFORM_POSIX>
)

if(SVEN_JIT_METAMOD STREQUAL "metamod-p")
    target_compile_definitions(sven_jit PRIVATE
        SVEN_JIT_METAMOD_P=1)
endif()

set_target_properties(sven_jit PROPERTIES
    PREFIX ""
    OUTPUT_NAME "sven_jit"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

if(MSVC)
    target_compile_options(sven_jit PRIVATE /W4 /permissive-)
else()
    target_compile_options(sven_jit PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(sven_jit PRIVATE "LINKER:--exclude-libs,ALL")
endif()

include(GNUInstallDirs)
install(TARGETS sven_jit
    RUNTIME DESTINATION addons/metamod/dlls
    LIBRARY DESTINATION addons/metamod/dlls
)
install(FILES README.md LICENCE DESTINATION share/doc/sven_jit)
