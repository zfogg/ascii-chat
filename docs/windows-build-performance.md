# Windows build performance

The native build keeps the Debug sanitizers enabled. Performance measurements
should use the same sources, compiler, dependency SDK and build configuration,
with other builds idle. A clean build with ccache disabled measures compilation;
a clean build with a populated cache measures cache restoration. An unchanged
Ninja invocation measures neither.

## Build concurrency

On the Ryzen 7 5800X workstation, clean native builds took 89.1 seconds with
eight jobs, 97.0 seconds with twelve, and 107.0 seconds with sixteen. These are
individual measurements, not a guarantee for other machines or workloads.

Windows Ninja builds default to a compile pool matching physical cores when
there is no explicit parallelism setting. An existing
`CMAKE_BUILD_PARALLEL_LEVEL` or `CMAKE_JOB_POOL_COMPILE` setting is respected.
Configure-time environment changes do not persist into a later terminal process;
the Ninja pool does.

For this workstation, use `cmake --build build --target ascii-chat --parallel 8`.
To persist that limit in an existing build directory with a different global
environment setting, configure with:

```powershell
cmake --preset default -B build `
  -DCMAKE_JOB_POOLS=asciichat_compile=8 `
  -DCMAKE_JOB_POOL_COMPILE=asciichat_compile
```

## Unity builds

```powershell
cmake --preset windows-unity -B build
cmake --build build --target ascii-chat --parallel 8
```

This is a sanitized Debug build. On Windows the unity option batches project
library sources as well as application mode sources. Vendored sources stay
separate so DLL export filtering retains their source paths. Library batches
contain four files; the executable uses CMake's normal batch size.

The first unity experiment took 72.7 seconds versus 89.1 seconds without unity.
Rebuilding an individual source can require recompiling its batch, and source
name or macro conflicts can arise as code changes. Ordinary development builds
therefore keep unity disabled by default. Switch back with:

```powershell
cmake --preset default -B build -DASCIICHAT_ENABLE_UNITY_BUILDS=OFF
```

## Export generation and headers

DLL export generation scans all active object files in one `llvm-nm` invocation
using a response file. PCH files and vendored objects are excluded, filename
headings are discarded, and extraction failures stop the build. Bulk filtering
and one output write avoid per-symbol file operations. A comparison of the same
338 manifest entries produced the same 2,694 exports in 0.39 seconds instead of
27.5 seconds.

Clang time traces showed roughly 0.8–1 second of Windows-header parsing in sampled
audio, crypto and application C sources without PCH. Existing PCH coverage made
that cost much smaller in covered sources. Extending PCH to more modules created
a C++ macro conflict; a C-only variant compiled, but its full build took 104.2
seconds and did not demonstrate a benefit. The existing coverage was retained.

Later validation builds overlapped unrelated workstation workloads, so their
wall-clock times should not be treated as a controlled speed comparison.

## Compiler cache

Native launchers pass `max_size=12G compression=true compression_level=1`
directly to ccache when its installed version supports configuration arguments.
PCH targets also pass `sloppiness=pch_defines,time_macros` and Clang's
`-fno-pch-timestamp`. This avoids launching one or two extra CMake processes per
source file. Older ccache versions retain the environment-launcher fallback;
the Emscripten compiler wrapper remains unchanged.

A full native unity build with an isolated empty cache took 90.9 seconds. After
cleaning all objects and rebuilding, all 211 compiler calls were direct cache
hits with zero misses, and the build took 18.2 seconds. This includes restoring
objects, regenerating exports and linking; it is not an unchanged-build timing.
These two runs also switch from the old environment launchers to the direct
native launchers, so their timing combines cache hits and launcher savings.

The C/C++ PCH regression and Windows export tests can be run separately from
Criterion:

```powershell
python tests/cmake/test_ccache_pch.py
python tests/cmake/test_windows_exports.py
```
