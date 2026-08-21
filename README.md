# Sven JIT

`sven_jit` is a 32-bit x86 Metamod plugin for Sven Co-op Dedicated Server. It
connects Sven Co-op's AngelScript engine to the bundled
[`angelscript_jit_x86`](https://github.com/DrAbcOfficial/angelscript_jit_x86)
runtime before script modules are compiled. It discovers the loaded GameDLL
from its function table and hooks AngelScript initialization directly; the
runtime does not require the `asext` plugin or a hard-coded GameDLL module name.

## Requirements

- CMake 3.24 or newer
- A C++20 compiler with 32-bit x86 support
- A Sven Co-op Dedicated Server installation
- The recursive `metamod-fallguys`, `metamod-p`, and `angelscript_jit_x86`
  submodules

Only the Metamod SDK headers are used at build time. At runtime, engine
discovery does not use Metamod's version-specific binary-analysis or
inline-hook extensions.

The build must target the Metamod implementation installed on the server.
`SVEN_JIT_METAMOD` accepts `metamod-fallguys` (the default) or `metamod-p`.
Each output uses the selected SDK's exact `META_INTERFACE_VERSION`, so the two
outputs are distributed separately and are not interchangeable.

The default build uses the SSE2 JIT path and requires SSE2 at runtime. The
optional AVX2 build uses packed SSE2/AVX2 code and requires CPU and operating
system AVX2 support; it is intentionally rejected on incompatible hosts rather
than falling back to the default binary.

## Checkout

```text
git clone --recurse-submodules https://github.com/DrAbcOfficial/sven_jit.git
cd sven_jit
```

If the repository was cloned without submodules, initialize them with:

```text
git submodule update --init --recursive
```

`thirdparty/angelscript_jit_x86` is a pinned Git submodule. Keep its gitlink at
the newest tested commit from `origin/master` whenever the JIT is updated.

## Build

### Windows

> [!WARNING]
> Do not mix Debug and Release binaries. In particular, never load a Debug
> build of `sven_jit` into the Release Sven Co-op Dedicated Server. The JIT
> operates directly on AngelScript objects allocated by the server, so mixing
> the Debug and Release MSVC runtimes can cause heap corruption. Use
> `RelWithDebInfo` when source-level debugging and PDB symbols are required.

Configure and build the default SSE2 binary:

```text
cmake -S . -B build-win32 -A Win32
cmake --build build-win32 --config Release
```

The command above targets metamod-fallguys. To build for metamod-p, use a
separate build directory:

```text
cmake -S . -B build-win32-metamod-p -A Win32 \
  -DSVEN_JIT_METAMOD=metamod-p
cmake --build build-win32-metamod-p --config Release
```

Build the AVX2 variant separately when all target servers support AVX2:

```text
cmake -S . -B build-win32-avx2 -A Win32 -DSVEN_JIT_ENABLE_AVX2=ON
cmake --build build-win32-avx2 --config Release
```

### Linux

Use a multilib-capable compiler for the 32-bit build:

```text
cmake -S . -B build-linux32 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS=-m32 \
  -DCMAKE_CXX_FLAGS=-m32
cmake --build build-linux32 -j
```

Add `-DSVEN_JIT_METAMOD=metamod-p` to target metamod-p, or leave the option at
its default value to target metamod-fallguys. Add `-DSVEN_JIT_ENABLE_AVX2=ON`
to configure an AVX2 build. Use separate build directories for every Metamod
and instruction-set combination. CMake rejects 64-bit configurations.

## Test

Configure the JIT submodule separately to run its AngelScript consistency and
CPU compatibility tests:

```text
cmake -S thirdparty/angelscript_jit_x86 -B build-jit-tests -A Win32 \
  -DASJITX86_BUILD_SHARED=OFF \
  -DASJITX86_BUILD_TESTS=ON
cmake --build build-jit-tests --config Release
ctest --test-dir build-jit-tests -C Release --output-on-failure
```

The JIT test suite executes each script with both the interpreter and JIT and
compares their results. The CPU test also verifies the selected instruction-set
requirements. Add `-DASJITX86_ENABLE_AVX2=ON` and use a separate build
directory to validate the AVX2 path.

## Install

Copy the matching output to `svencoop/addons/metamod/dlls`:

- Windows: `sven_jit.dll`
- Linux: `sven_jit.so`

Release archive names identify both the Metamod target (`metamod-p` or
`metamod-fallguys`) and the instruction-set variant (`sse` or `sse-avx2`).
Install the archive matching the server's Metamod implementation.

Register the plugin in `svencoop/addons/metamod/plugins.ini`:

```text
linux addons/metamod/dlls/sven_jit.so
```

Use the equivalent DLL path on Windows. A successful startup reports
`AngelScript JIT enabled` in the Metamod log. Runtime loading and unloading are
intentionally unsupported because AngelScript retains the JIT compiler for the
life of its script engine.

## License

This project is licensed under the GNU General Public License version 3. See
[`LICENCE`](LICENCE). Third-party components retain their own licenses.
