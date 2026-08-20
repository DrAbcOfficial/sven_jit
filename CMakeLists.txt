cmake_minimum_required(VERSION 3.24)

project(sven_jit VERSION 0.0.4 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(SVEN_JIT_ENABLE_AVX2 "Enable the SSE2+AVX2 JIT variant" OFF)

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

set(METAMOD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/metamod")

add_library(sven_jit SHARED
    src/integration/game_engine_bridge.cpp
    src/jit/jit_service.cpp
    src/metamod/engine_exports.cpp
    src/metamod/game_api.cpp
    src/metamod/plugin_api.cpp
)

if(MSVC)
    target_compile_options(sven_jit PRIVATE /arch:IA32)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sven_jit PRIVATE
        -mno-sse -mno-sse2 -mno-avx -mno-avx2)
endif()

target_include_directories(sven_jit PRIVATE
    src
    "${METAMOD_ROOT}/hlsdk/common"
    "${METAMOD_ROOT}/hlsdk/dlls"
    "${METAMOD_ROOT}/hlsdk/engine"
    "${METAMOD_ROOT}/hlsdk/pm_shared"
    "${METAMOD_ROOT}/metamod"
    "${METAMOD_ROOT}/thirdparty/angelscript-sdk/angelscript/include"
)

target_link_libraries(sven_jit PRIVATE as_jit_x86 ${CMAKE_DL_LIBS})

target_compile_definitions(sven_jit PRIVATE
    SVEN_JIT_VERSION="${PROJECT_VERSION}"
    $<$<PLATFORM_ID:Windows>:WIN32;_WINDOWS;_USRDLL;NOMINMAX;_CRT_SECURE_NO_WARNINGS>
    $<$<PLATFORM_ID:Linux>:linux;LINUX;_LINUX;PLATFORM_POSIX>
)

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
