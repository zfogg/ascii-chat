# Portable SIMD builds

Release binaries use a fixed CPU baseline (x86-64 or ARMv8-A). CMake's
`ASCIICHAT_SIMD_MODE=auto` compiles every backend supported by the **target
toolchain**, regardless of the build machine's CPU: SSE2/SSSE3/AVX2 on x86-64,
NEON on ARM64, and SVE on Linux ARM64. Scalar is always available.

ISA flags apply only to backend source files. These files are excluded from
unity compilation, shared precompiled headers, and LTO. Ordinary code, startup,
feature detection, and the dispatch table remain baseline code. Release CPU
tuning defaults to `portable`; `native`, `custom`, and explicit x86 microarchitecture
profiles deliberately raise the minimum CPU requirement and are for local builds.

PIE compile flags apply only to executable targets. Library objects stay PIC,
including vendored archives: ThinLTO can import TLS access into those objects,
where PIE would produce relocations that cannot link into a shared library.

The platform layer checks CPUID and OSXSAVE/XCR0 on x86 and Linux HWCAP on ARM.
CRC32 uses the same runtime detection with function-local target attributes.
Each thread caches an immutable luminance dispatch table. SVE kernels use the
calling thread's current vector length, predicated loads, and predicated stores;
there is no fixed-size SVE scratch buffer in the dispatched kernel.

Monochrome, foreground, background, and indexed-color formatters share the same
output code and call the selected RGB24-to-luminance kernel once per row. This
preserves palette selection, ANSI formatting, and run-length encoding across
backends. Half-block rendering retains its existing implementation. Legacy
architecture-specific renderer entry points remain compiled, but are not the
automatic luminance dispatch path.

For diagnosis, set `ASCII_CHAT_SIMD=scalar|sse2|ssse3|avx2|neon|sve` **before
starting the process**. `auto` (or an unset value) chooses the highest available
backend. Unknown, uncompiled, or unsupported requests safely select scalar.
An override never permits unsupported instructions. Debug startup logging reports
the selected luminance backend.

## Tests

The standalone suite uses the production detector, selector, and kernels without
requiring the application's media dependencies or Criterion:

```sh
cmake -S tests/simd -B build_simd -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Release
cmake --build build_simd
ctest --test-dir build_simd --output-on-failure
```

On Windows run from a Visual Studio developer environment and add
`-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` for static CRT linkage. On Linux add
`-DCMAKE_EXE_LINKER_FLAGS=-static` for the emulated compatibility tests.
The suite checks every synthetic feature mask, all 16,777,216 RGB inputs per
available backend, every row length from 0 to 1025, unaligned buffers, and output
sentinels. The static test binary can also be run with QEMU on non-SVE ARM64,
at different SVE vector lengths, and on x86 without AVX/OSXSAVE:

```sh
cmake -S tests/simd -B build_simd_arm -G Ninja -DCMAKE_C_COMPILER=clang \
  -DCMAKE_C_COMPILER_TARGET=aarch64-linux-gnu -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_SYSTEM_PROCESSOR=aarch64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXE_LINKER_FLAGS=-static
cmake --build build_simd_arm
qemu-aarch64 -cpu cortex-a53 build_simd_arm/simd-test neon
for vl in 16 32 64 128 256; do
  qemu-aarch64 -cpu max build_simd_arm/simd-test sve "$vl"
done
qemu-x86_64 -cpu qemu64 build_simd/simd-test sse2
qemu-x86_64 -cpu Nehalem build_simd/simd-test ssse3
qemu-x86_64 -cpu max,-xsave build_simd/simd-test ssse3
```

Cross-compilation uses Clang with an installed ARM64 Linux sysroot. To compare
the actual application's rendered output across backends:

```sh
python3 tests/simd/smoke.py build_release/bin/ascii-chat auto sse2 ssse3 avx2
python3 tests/simd/audit_flags.py build_release/compile_commands.json
```

Full release validation must also run the application (including rendering and
shutdown) and its static-link audit. On macOS, static release means that third-party
dependencies are static; required Apple system libraries remain dynamic.
