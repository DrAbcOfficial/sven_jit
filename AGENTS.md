# Agent and Contributor Guide

## Scope

This repository contains the `sven_jit` Metamod plugin and two pinned
third-party submodules. Keep changes focused on the plugin, its build files,
documentation, and the AngelScript JIT integration.

## Repository Layout

- `src/` contains the plugin and AngelScript bridge.
- `thirdparty/metamod/` contains the Metamod SDK and `asext` integration.
- `thirdparty/angelscript_jit_x86/` contains the pinned JIT submodule.
- `CMakeLists.txt` controls the 32-bit SSE2 and optional AVX2 variants.

## Build and Test

Use a 32-bit toolchain and CMake 3.24 or newer. Configure a fresh build
directory for each instruction-set variant:

```text
cmake -S . -B build-win32 -A Win32
cmake --build build-win32 --config Release
```

For the AVX2 variant, configure with
`-DSVEN_JIT_ENABLE_AVX2=ON`. The resulting plugin requires AVX2 and does not
fall back at runtime. Keep generated build directories out of commits.

Run JIT tests from a separate submodule build:

```text
cmake -S thirdparty/angelscript_jit_x86 -B build-jit-tests -A Win32 \
  -DASJITX86_BUILD_SHARED=OFF -DASJITX86_BUILD_TESTS=ON
cmake --build build-jit-tests --config Release
ctest --test-dir build-jit-tests -C Release --output-on-failure
```

## Submodule Policy

`thirdparty/angelscript_jit_x86` must always point to the latest tested commit
on `origin/master` of the JIT repository. After changing the JIT:

1. Commit and push the JIT repository first.
2. Update the submodule checkout to that commit.
3. Commit the changed gitlink in `sven_jit`.
4. Push `sven_jit` only after the gitlink resolves from the remote.

Do not vendor a second copy of the JIT or commit files inside the submodule.

## Coding Practices

- Preserve 32-bit x86 compatibility and C++20 support.
- Keep CPU feature checks consistent with emitted instructions.
- Maintain interpreter/JIT result parity; add or extend consistency scripts for
  behavioral changes.
- Keep SSE2 and AVX2 code paths explicit and preserve the scalar fallback where
  it is required for compatibility.
- Match the existing formatting and avoid unrelated refactors.

## Commit and Review Checklist

- Run the default SSE2 tests and, when SIMD code changes, the AVX2 tests.
- Run `git diff --check` in both repositories.
- Confirm `git submodule status` resolves to the newest pushed JIT commit.
- Use a short imperative commit subject describing the change.
- Push both repositories after their respective commits; do not leave a dirty
  submodule or an uncommitted gitlink update.
