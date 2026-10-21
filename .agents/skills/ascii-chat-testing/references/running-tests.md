# Running tests

## Native Criterion

macOS uses Homebrew LLVM and Criterion discovered through pkg-config; Linux uses Clang and distribution dependencies. Consult `.github/actions/install-deps/action.yml` and its scripts for maintained prerequisites. Criterion supports native Intel/ARM macOS and glibc Linux. Missing Criterion is an environment/configuration problem, not grounds to silently omit requested tests.

CMake flattens ordinary source paths and removes a trailing `_test`: `tests/unit/crypto/crypto_handshake_test.c` becomes `test_unit_crypto_crypto_handshake`. Explicit contracts use other names. Discover with `ctest --test-dir build -N -V` rather than guessing.

```sh
cmake --build build --target test_unit_crypto_crypto_handshake
ctest --test-dir build -R '^test_unit_crypto_crypto_handshake$' --no-tests=error --output-on-failure
# Replace case_name with an actual case. Criterion filters are globs; CTest -R is regex.
TESTING=1 CRITERION_TEST=1 BUILD_DIR="$PWD/build" \
  build/bin/test_unit_crypto_crypto_handshake --filter '*case_name*' --jobs 1 --verbose
ctest --test-dir build -L '^contract$' --no-tests=error --output-on-failure
```

CTest normally supplies `TESTING`, `CRITERION_TEST`, and `BUILD_DIR`; retain them for direct binary runs. Prefer CTest when wrappers, environment, deadlines or serialization matter. CMake supplies `--xml=<build>/Testing/criterion-xml/NAME.xml` for registered Criterion runs; direct invocations need an explicit `--xml=PATH` to retain equivalent reports.

CTest parallelizes executables; Criterion parallelizes cases within each executable. Avoid multiplying both blindly. Current ordinary defaults are 25-second Criterion / 30-second CTest deadlines, with suite overrides; coverage uses one Criterion worker and longer deadlines. Many integration/contracts use `RUN_SERIAL` for shared ports/resources. Inspect effective commands before changing timeouts or serialization.

## Windows Docker

Native Windows supports selected CTest library/standalone/Python checks, but Criterion is disabled. Use Linux test containers for Criterion and native Windows harnesses for Windows-specific claims.

```powershell
./tests/scripts/run-docker-tests.ps1 unit
./tests/scripts/run-docker-tests.ps1 unit crypto
./tests/scripts/run-docker-tests.ps1 integration
./tests/scripts/run-docker-tests.ps1 performance
./tests/scripts/run-docker-tests.ps1 -Filter 'runtime-settings'
```

The wrapper builds `tests/Dockerfile` when needed, mounts source at `/app`, uses `build_docker/`, and retains ccache/dependency volumes. `-Build` rebuilds the image; `-NoBuild` skips image creation but still builds tests. Avoid `-Clean` routinely. It removes the fixed container `ascii-chat-test-runner`, so inspect ownership before concurrent use. Filters select CTest names, not individual Criterion cases. The wrapper lacks `--no-tests=error`; inspect selected counts or use a direct command with that option.

Compose persists build/dependency/ccache named volumes. Inspect `docker compose -f tests/docker-compose.yml config`. The current Arch service points at a missing root `Dockerfile`; use a temporary override setting `services.ascii-chat-tests.build.dockerfile: tests/Dockerfile`, or build the matching image first:

```powershell
docker build -f tests/Dockerfile -t zfogg/ascii-chat:latest .
# Check $LASTEXITCODE before continuing after native commands.
docker compose -f tests/docker-compose.yml run --rm ascii-chat-tests
docker compose -f tests/docker-compose.yml run --rm ascii-chat-tests bash -lc 'cmake --preset docker && cmake --build build_docker --target tests && ctest --test-dir build_docker -L "^unit$" --no-tests=error --output-on-failure'
```

The Ubuntu service uses `tests/Dockerfile.ubuntu` but hardcodes `DOCKER_OS=arm64`; align the cache namespace with actual architecture. Preserve volumes and reuse the same Compose project name. For direct Docker mounts read the cross-platform skill: retain `build_docker/`, `.deps-cache-docker/` mapped to `/app/.deps-cache`, and ccache. Separate native/Linux/architecture caches.

Compose adds `SYS_PTRACE` and disables LeakSanitizer detection through its environment workaround. A passing run there is not leak-freedom evidence; use a supported environment with leak detection enabled for that claim.

## Python integration and standalone fixtures

Many Python files are executable-specific harnesses, not pytest suites. Inspect docstrings, imports, argument parsing and CMake registration. Do not assume every script accepts `--binary`, or that unittest discovery exercises the actual binary.

```sh
cmake --build build --target ascii-chat
python tests/integration/server_lifecycle.py build/bin/ascii-chat
python tests/integration/server_client_lifecycle.py build/bin/ascii-chat
python tests/integration/terminal_ui.py --binary build/bin/ascii-chat
python tests/integration/network_terminal_backpressure.py \
  --binary build/bin/ascii-chat --output build/backpressure
# Harness unit tests are separate from exercising real processes:
python -m unittest discover -s tests/integration -p 'test_network_terminal_backpressure.py'
```

On Windows pass the actual native `.exe` path; host Python cannot run a Linux Docker executable. `terminal_ui.py` requires pyte, ffmpeg, and pywinpty on Windows / pexpect on POSIX. tmux probes require tmux and a POSIX terminal. `os.fork`/PTY probes do not prove native Windows behavior; inspect `windows_console.py`, `terminal_resize_windows.py`, and other native harnesses for that surface. Audio scripts under `tests/audio/` have separate dependencies and may require hardware; inspect prerequisites individually.

Prefer registered CTest wrappers for lifecycle, backpressure, terminal-resize, media capture, dependency paths and crypto logging. They supply fixture executable paths and timeout/skip/serialization properties. Some standalone targets are `EXCLUDE_FROM_ALL`; enabling tests does not necessarily build every fixture. Look up target dependencies in `tests/CMakeLists.txt` and build the required target rather than inventing compile/link commands. Preserve intentional skip semantics (for example media capture return code 77).

Use temporary configs/HOME, free loopback ports, generated media, readiness deadlines, and cleanup in `finally`/context managers. Assert actual frames, state transitions or terminal cells. Preserve failed-run logs, media and JSON measurements when needed for diagnosis.
