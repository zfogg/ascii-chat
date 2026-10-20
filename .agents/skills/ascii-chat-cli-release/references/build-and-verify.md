# Build and artifact verification

## Current pipeline

Release builds Linux amd64/arm64 on Ubuntu 24.04, macOS amd64 on macos-15-intel and arm64 on macos-26, and Windows amd64/arm64. Recheck the matrix before each release. CMake uses Clang and Ninja. Supply `-B` with a new candidate-specific directory; do not delete `build/` or reuse another task's tree.

Linux formats include TGZ, STGZ, DEB, and RPM; macOS includes TGZ, STGZ, and PKG; Windows includes ZIP and amd64 MSI. Windows ARM64 has no MSI because WiX v3 is x86-only. Runtime archives include CLI, man page, and completions. Development archives include the independently versioned library, headers, and build integration; package filenames can follow the CLI version while library metadata follows `lib/v*`. The full source tarball includes submodules for maintainers. Consult the workflow for signing and checksum outputs.

## Matching local builds

Read the dependency action and `scripts/install-deps.sh` for prerequisites. Inventory existing static installations and built archives before configuring; reuse compatible dependencies while keeping application outputs fresh. On the Intel Mac, `/usr/local/lib` contains static Homebrew archives and `/Users/loomen/src/ascii-chat-cmake-dependencies-20261019/build/dependency-static` contains source-built dependency archives; verify their current contents, versions, and compatibility. Do not rebuild Mac dependencies merely because a fresh application build directory lacks them. Recent CMake refactors put dependencies under the binary tree and some modules force source builds: identify and reuse the built archives explicitly before running the full graph. If local reuse needs a different compilation route, record that limitation and retain CI verification of the complete packaging graph. Copy source into an isolated Mac checkout. Install only missing prerequisites needed for the task.

Set `CC=clang`, `CXX=clang++`, the candidate CLI version, and the highest verified `lib/v*` version. In PowerShell quote each complete `-DNAME=value` argument, especially dotted versions, to avoid argument splitting. Use the actual library version.

Linux in Ubuntu 24.04 Docker with musl and CI prerequisites:

Keep build outputs on a Linux volume. Prefer a source snapshot on the Linux filesystem too: Windows bind-mount reads can make Doxygen take minutes even when the same inputs finish quickly natively. Record the snapshot commit and any release fixes copied into it.

```sh
cmake --preset release -B build_release_CANDIDATE_linux \
  -DCMAKE_INSTALL_PREFIX=/usr -DUSE_MUSL=ON \
  -DASCIICHAT_ENFORCE_STATIC_RELEASE=ON \
  -DASCIICHAT_SIMD_MODE=auto -DASCIICHAT_RELEASE_CPU_TUNE=portable \
  -DPROJECT_VERSION_FROM_GIT="$CLI_VERSION" -DASCIICHAT_LIB_VERSION="$LIB_VERSION"
cmake --build build_release_CANDIDATE_linux --target all static-lib docs
(cd build_release_CANDIDATE_linux && cpack --verbose)
```

Use the musl toolchain for dependencies too. Glibc headers leaking into dependencies can produce unresolved `mmap64`, `__sysconf`, or `gnu_get_libc_version` symbols. Fix include/toolchain selection. Linux does not package a shared library when static FFmpeg cannot link into it.

Windows uses `--preset windows-release`, triplet `x64-windows-static` or `arm64-windows-static`, `ASCIICHAT_ENABLE_NSIS=OFF`, `ASCIICHAT_ENABLE_ANALYZERS=OFF`, and `ASCIICHAT_ENFORCE_STATIC_RELEASE=ON`. Match `ASCIICHAT_TARGET_ARM64` and `ASCIICHAT_SKIP_RUNTIME_GENERATED_DOCS` to the architecture. Use the MSVC SDK environment with Clang; pass a prebuilt `ASCIICHAT_DEFER_TOOL` only if available and compatible. Build `ascii-chat`, `static-lib`, `shared-lib`, `docs` (amd64), and `package`. Criterion is unsupported on Windows.

macOS uses `--preset release`, `ASCIICHAT_SHARED_DEPS=OFF`, and `ASCIICHAT_ENFORCE_STATIC_RELEASE=ON`. Select `SDKROOT` using `env -u SDKROOT xcrun --sdk macosx --show-sdk-path` so the SDK matches the active Apple toolchain. Do not force the CommandLineTools SDK: the macOS 27 SDK's new architecture entries broke an older active linker during the v0.14.0 OpenSSL build. Both current macOS runners disable mimalloc, fast math, and release IPO. Intel omits `ASCIICHAT_LIBCXX_STATIC_ROOT`; ARM uses official LLVM 21.1.8 static libc++ libraries. The workflow downloads LLVM 19.1.6 on Intel but does not pass that root. Match current compiler selection. Build `all`, `static-lib`, `shared-lib`, and `package`; Intel currently skips Doxygen because its runner binary crashes. ARM builds docs.

Generated version headers may read `VERSION` or `PROJECT_VERSION_OVERRIDE` independently of `PROJECT_VERSION_FROM_GIT`; inspect `cmake/install/Version.cmake`. For pre-tag builds set a matching environment override and check reported version and package names. Clear it when switching candidates. Final verification must use the tag's exact version.

## Static audits and execution

Run `cmake/utils/check_static_linking.sh BINARY PLATFORM AUDIT_TOOL` (macOS needs no third argument). It runs after linking with release enforcement; rerun on extracted final assets.

- Linux: `llvm-readelf --dynamic` and `--program-headers`: no `NEEDED`, no `INTERP`. Run amd64 and arm64 in matching Docker architectures (QEMU allowed when available), ideally minimal runtime images to expose hidden dependencies.
- macOS: `otool -L` permits only `/usr/lib/`, `/System/Library/`, and `/Library/Apple/System/Library/`. Reject Homebrew, `@rpath`, and other third-party dylibs. Run amd64 on `loomen@zachbook-pro`. Intel cannot natively run ARM macOS: retain native ARM CI `--version` evidence or use an ARM Mac. Independently inspect ARM load commands even if execution comes from CI.
- Windows: `llvm-readobj --coff-imports` must show only the checker's system DLL allowlist. No DLL files in ZIP is insufficient: check imported DLLs too, including delay imports when present. Run amd64 locally; retain native ARM CI execution evidence or use an ARM host.

Run `ascii-chat --version` for each runtime architecture, recording exit, expected version, and machine architecture. Extract formats separately. Inspect installer executables (DEB/RPM payload tools, PKG expansion, MSI administrative extraction, STGZ extraction). Compare hashes or loadable segments if packaging rewrites non-loaded metadata; differing payloads need independent audits. Avoid installing into live system prefixes merely to inspect packages.
