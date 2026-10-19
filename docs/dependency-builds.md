# Dependency builds

Native builds normally use installed packages where supported. Configure with
`-DASCIICHAT_BUILD_DEPS_FROM_SOURCE=ON` to exercise the source implementations of
the converted dependencies. This still requires the platform SDK and packages
for dependencies that do not have an upstream CMake build, including OpenSSL.

The source implementations use upstream CMake targets for PCRE2, Opus, zstd,
zlib, PortAudio, FreeType, Expat, Abseil, miniupnpc, libdatachannel and its networking
dependencies, libwebsockets, WebRTC AEC3, x265, mimalloc, and utf8proc. mdns uses
its upstream interface target. FFTW, libuv and yyjson already use upstream
CMake builds. libvterm has a small local target because upstream supplies a
Makefile; its generated encoding tables are tracked build outputs.

`FetchContent` downloads pinned sources during configuration. The upstream
targets compile when `cmake --build` runs, so Ninja handles their ordering,
incremental rebuilds, compiler settings, include paths and transitive links.
Upstream options are scoped to each dependency. Sources can be cached between
configurations, while each build directory keeps its own generated files and
objects.

`ASCIICHAT_SHARED_DEPS=ON` selects shared native dependencies;
`ASCIICHAT_SHARED_DEPS=OFF` selects static source targets. WASM, iOS and musl
retain their static dependency policy. AEC3's upstream components remain static
and can be linked into either the shared ascii-chat library or an executable.
CMake records the circular dependency between its factory and implementation,
so consumers do not need whole-archive flags or hand-written archive lists.

FFmpeg and Fontconfig consume installed codec/font dependencies through
pkg-config. Small staging targets install x265, FreeType and Expat into the
build tree before those external builds run. This keeps their configure scripts
out of the top-level configure step without replacing their supported upstream
build systems. libsodium, OpenSSL, ALSA, x264, BearSSL and libexecinfo likewise
retain their upstream build systems where source builds are required.

On Windows, native builds use Clang, Ninja and the Visual Studio SDK environment.
CMake selects the MSVC-compatible C runtime for both application code and
third-party DLLs. No `SHELL` environment override is required. WASM's remaining
Autotools dependency, libsodium, uses Bash and Make from one MSYS2 installation;
`ASCIICHAT_MSYS2_ROOT` can identify it when automatic discovery is insufficient.
Windows shared builds select Abseil's upstream single DLL option, including
when Clang uses its GNU-style command line with the MSVC ABI.

## Testing source targets

The dependency smoke executable exercises compression round trips, an Opus
encoder, PCRE2 compilation, FreeType, PortAudio metadata, UPnP, XML parsing,
terminal allocation, Unicode widths, TLS/WebRTC library initialization,
an AEC3 factory and mimalloc allocation. It does not open cameras, audio
devices or network connections.

```sh
cmake -S tests/cmake/dependencies -B build/dependency-static -G Ninja \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Release -DASCIICHAT_SHARED_DEPS=OFF
cmake --build build/dependency-static
ctest --test-dir build/dependency-static --output-on-failure

cmake -S tests/cmake/dependencies -B build/dependency-shared -G Ninja \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Release -DASCIICHAT_SHARED_DEPS=ON
cmake --build build/dependency-shared
ctest --test-dir build/dependency-shared --output-on-failure
```

Set `OPENSSL_ROOT_DIR` when OpenSSL is outside the normal search paths. Windows
must also be able to load its OpenSSL DLLs when running the smoke executable.
The WASM build has a separate `wasm-dependencies` target and CTest entry.
