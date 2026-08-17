# sven_jit

`sven_jit` is a 32-bit x86 Metamod plugin that enables the AngelScript 2.36.1 JIT runtime in Sven Co-op Dedicated Server. It uses [asext](https://github.com/hzqst/metamod-fallguys) to obtain Sven Co-op's script engine before scripts are compiled and binds the bundled `angelscript_jit_x86` runtime to it.

## Requirements

- CMake 3.24 or newer
- A C++20 compiler with 32-bit x86 support
- Sven Co-op Dedicated Server
- The matching `metamod-fallguys` and `asext` versions from the submodule

## Build

```text
git clone --recurse-submodules https://github.com/DrAbcOfficial/sven_jit.git
```

```text
cd sven_jit
```

<details>
<summary>Windows</summary>

```text
cmake -S . -B build-win32 -A Win32
```

```text
cmake --build build-win32 --config Release
```

> SSE2 JIT code generation is enabled by default. Add
`-DSVEN_JIT_ENABLE_AVX2=ON` when configuring to build the SSE2+AVX2 variant.
During Metamod initialization, the default build requires SSE2 and the
SSE2+AVX2 build requires AVX2 CPU and operating system support. An incompatible
build is rejected instead of falling back to another instruction set.

---

</details>

<details>
<summary>Linux</summary>

```text
cmake -S . -B build-linux32 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS=-m32 \
  -DCMAKE_CXX_FLAGS=-m32
cmake --build build-linux32 -j
```

> The build rejects 64-bit configurations.

---

</details>

## Install

<details>
<summary>Windows</summary>

- Copy `sven_jit.dll` to `svencoop/addons/metamod/dlls`

- Register the plugin at `svencoop/addons/metamod/plugins.ini`:
  ```text
  linux addons/metamod/dlls/sven_jit.dll
  ```

---

</details>

<details>
<summary>Linux</summary>

- Copy `sven_jit.so` to `svencoop/addons/metamod/dlls`

- Register the plugin at `svencoop/addons/metamod/plugins.ini`:
  ```text
  linux addons/metamod/dlls/sven_jit.so
  ```

---

</details>

Load the plugin at server startup.

Runtime loading and unloading are intentionally disabled because AngelScript retains the JIT compiler for the lifetime of its script engine.

A successful startup prints `AngelScript JIT enabled` in the Metamod log.

## License

This project is licensed under the GNU General Public License version 3.
> See [LICENCE](https://github.com/DrAbcOfficial/sven_jit?tab=GPL-3.0-1-ov-file).
