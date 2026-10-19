# CMake workflows

Use Clang and Ninja on every platform. `default` keeps the usual `build/`
directory, and `release` keeps `build_release/` for existing packaging scripts.
Other build variants get distinct directories under `build/<preset>/`, except
presets with explicit CI or tooling paths. The directory convention is the same
on Windows, Linux and macOS.

```sh
cmake --workflow --preset check-default
cmake --workflow --preset check-shared
cmake --workflow --preset check-static
```

These workflows configure, build the executable and run its `--version` smoke
test. `shared` explicitly selects system libc and shared dependencies. `static`
uses the release platform policy, including musl on supported Linux hosts.
Static and shared library targets remain available independently of the
executable's dependency policy.

On Criterion-supported systems, `cmake --workflow --preset tests` builds the
test target and runs the complete suite. The `unit`, `integration` and
`performance` test presets use the tests-enabled configuration. Windows does
not support Criterion; use the executable smoke workflows there. On Intel
macOS, run Criterion tests through Docker as described in AGENTS.md.

Dependency source downloads can be shared. New configurations keep legacy
dependency build/install outputs inside their own binary directories. Explicit
cache-directory overrides remain supported. Once configured, a build tree
rejects changes to compiler, target ABI, CRT or static/shared dependency policy;
select another preset or use `cmake --fresh` to change those settings.

## Musl toolchain

`USE_MUSL=ON` selects `cmake/toolchains/Musl.cmake` before `project()`.
It selects Clang's target triple, musl headers and CRT startup files and keeps
host package searches out of the target build. Programs such as Perl and Make
still run on the host. `ASCIICHAT_MUSL_SYSROOT` can select a complete target SDK;
otherwise the toolchain discovers the distribution's packaged musl paths.

The platform module stages the matching Alpine C++ runtime and non-CMake
libraries. Source dependencies use the same compiler/header settings through
`MuslFlags.cmake`. Project C++ sources use those runtime headers as well.
Use a fresh configuration when adopting the toolchain in a pre-existing musl
build tree. A separate shared/glibc preset produces libraries for glibc hosts.
