# sven_jit

32-bit Metamod plugin. Plugin is `PT_STARTUP` / `PT_NEVER`; AngelScript keeps the JIT for process life — `AsJitDestroyEngine` is never called.

- metamod-fallguys: loads `asext` (`LOAD_PLUGIN`) and installs the JIT from `ASEXT_RegisterDocInitCallback` / `ASEXT_GetServerManager`.
- metamod-p: hooks the GameDLL (signature/symbol scan of `CASDocumentation::RegisterObjectType` / `CASServerManager`).

## Layout

- `src/` — plugin only (`metamod/`, `integration/`, `jit/`).
- `src/integration/asext_engine_bridge.cpp` — fallguys + asext.
- `src/integration/game_engine_bridge.cpp` — metamod-p signature scan.
- `thirdparty/metamod/` — metamod-fallguys (default).
- `thirdparty/metamod-p/` — metamod-p.
- `thirdparty/angelscript_jit_x86/` — pinned JIT submodule. Do not vendor a second copy or commit files inside it.

Headers from the Metamod SDKs. Fallguys also includes `asext/include`. Do not treat `thirdparty/metamod/CLAUDE.md` as this plugin's guide.

## Build

CMake 3.24+, C++20, 32-bit toolchain. Separate build dirs per Metamod × SIMD combo.

```text
cmake -S . -B build-win32 -A Win32
cmake --build build-win32 --config Release
```

- `-DSVEN_JIT_METAMOD=metamod-p` (default `metamod-fallguys`). Outputs are not interchangeable (`META_INTERFACE_VERSION`).
- `-DSVEN_JIT_ENABLE_AVX2=ON` — no runtime fallback; host must have AVX2.
- Linux: `-DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32` (and `g++-multilib`).
- CMake forces the JIT static, tests off, SSE on, AVX2 from `SVEN_JIT_ENABLE_AVX2`.
- Plugin/JIT C++ is compiled `/arch:IA32` / `-mno-sse`; SIMD exists only in asmjit-generated code.

Never load a Debug `sven_jit` into a Release server. Use `RelWithDebInfo` for debugging. Release tags `v*` build RelWithDebInfo.

Windows stdcall export: `src/metamod/sven_jit.def` (`GiveFnptrsToDll`). GameDLL thiscall is hooked as `__fastcall` plus a dummy `int`. `hlsdk.hpp` undefs `_DEBUG` around metamod-p headers.

## Tests

No plugin unit tests. Run the JIT suite from a **separate** tree (do not enable tests in the plugin configure):

```text
cmake -S thirdparty/angelscript_jit_x86 -B build-jit-tests -A Win32 \
  -DASJITX86_BUILD_SHARED=OFF -DASJITX86_BUILD_TESTS=ON
cmake --build build-jit-tests --config Release
ctest --test-dir build-jit-tests -C Release --output-on-failure
```

Add `-DASJITX86_ENABLE_AVX2=ON` and another build dir when SIMD emit changes.

## Submodule

`thirdparty/angelscript_jit_x86` must track the latest tested `origin/master` of the JIT repo. Push JIT first, then the gitlink. `git submodule status` should resolve.

## Conventions

- Interpreter/JIT result parity; extend `tests/scripts` + the list in `jit_consistency.cpp` (in the JIT repo) for behavioral changes.
- Keep SSE2 vs AVX2 paths explicit.
- `Meta_Query` takes `char*` on metamod-p, `const char*` on fallguys.
- Engine-ready marker is `RegisterObjectType` of `CSurvivalMode` / `"Survival Mode handler"` / flags `0x40001`.
