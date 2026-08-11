# sven_jit

`sven_jit` is a 32-bit x86 Metamod plugin that enables the AngelScript 2.36.1 JIT runtime in Sven Co-op Dedicated Server. It uses `asext` to obtain Sven Co-op's script engine before scripts are compiled and binds the bundled `angelscript_jit_x86` runtime to it.

## Requirements

- CMake 3.24 or newer
- A C++20 compiler with 32-bit x86 support
- Sven Co-op Dedicated Server
- The matching `metamod-fallguys` and `asext` versions from the submodule

## Build

Initialize the submodules before configuring the project.

Windows:

```text
cmake -S . -B build-win32 -A Win32
cmake --build build-win32 --config Release
```

Linux:

```text
cmake -S . -B build-linux32 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS=-m32 \
  -DCMAKE_CXX_FLAGS=-m32
cmake --build build-linux32 -j
```

The build rejects 64-bit configurations.

## Install

Copy `sven_jit.dll` on Windows or `sven_jit.so` on Linux to `svencoop/addons/metamod/dlls`. Add the matching line to `svencoop/addons/metamod/plugins.ini`:

```text
win32 addons/metamod/dlls/sven_jit.dll
linux addons/metamod/dlls/sven_jit.so
```

Load the plugin at server startup. Runtime loading and unloading are intentionally disabled because AngelScript retains the JIT compiler for the lifetime of its script engine. A successful startup prints `AngelScript JIT enabled` in the Metamod log.

## License

This project is licensed under the GNU General Public License version 3. See `LICENCE`.
