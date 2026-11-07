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

CTest uses all available cores across executables; Criterion uses one worker per executable to avoid multiplying pools. Pytest batches reserve the detected core count and use xdist logical cores. Many legacy integration/contracts use `RUN_SERIAL` for shared resources. Inspect effective commands and fixture isolation before changing serialization.

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

The locked uv project provides pytest and pytest-xdist. `tests/python/` is the
only default collection tree; its modules must not start processes or parse CLI
arguments at import time. Fixtures in `tests/python/conftest.py` provide the
native binary, isolated environment, managed services, and PTY/ConPTY terminals. Pass `--terminal-probe` to exercise the native terminal fixture in direct pytest runs. CTest registers `python-unit`,
`python-cli`, `python-media`, `python-network`, `python-discovery`, `python-mcp`, `python-terminal`, `python-telemetry`, `python-lifecycle`, and `python-capture`; each emits
`build/Testing/python-*.xml` and reserves all cores while xdist runs.

```sh
uv sync --project tests/ascii_chat_tests --frozen
uv run --project tests/ascii_chat_tests --frozen python -m pytest tests/python/unit
uv run --project tests/ascii_chat_tests --frozen python -m pytest tests/python --ascii-chat-binary build/bin/ascii-chat --terminal-probe build/bin/test-terminal-pty --lifecycle-probe build/bin/test-client-like-lifecycle --media-capture-probe build/bin/test-media-capture-open
uv run --project tests/ascii_chat_tests --frozen python -m pytest tests/python/integration/cli/test_options.py --ascii-chat-binary build/bin/ascii-chat -k seek -n 0
ctest --test-dir build -R '^python-(unit|cli|media|network|discovery|mcp|terminal|telemetry|lifecycle|capture)$' --no-tests=error --output-on-failure --parallel
```

Use the platform executable suffix on Windows. pytest defaults to `-n logical`;
`-n 0` is useful to debug one case. Exit code 5 means no cases were collected,
not a pass. `--parallel` without a number uses the available cores; `--parallel 0`
is unbounded in current CTest and can oversubscribe the host. Resource-sensitive
legacy CTest cases retain their explicit serialization. Add pytest coverage
incrementally; many remaining harnesses are executable scripts with import-time
work. Do not collect the entire legacy tree or run a migrated case twice.


```sh
cmake --build build --target ascii-chat
uv run --project tests/ascii_chat_tests --frozen python -m pytest tests/python/integration/network --ascii-chat-binary build/bin/ascii-chat
uv run --project tests/ascii_chat_tests --frozen python -m pytest tests/python/integration/network/test_server_client_lifecycle.py --ascii-chat-binary build/bin/ascii-chat
python tests/integration/terminal_ui.py --binary build/bin/ascii-chat
python tests/integration/network_terminal_backpressure.py \
  --binary build/bin/ascii-chat --output build/backpressure
# Harness unit tests are separate from exercising real processes:
uv run --project tests/ascii_chat_tests --frozen python -m pytest tests/python/unit/test_receive_timing.py
```

On Windows pass the actual native `.exe` path; host Python cannot run a Linux Docker executable. `terminal_ui.py` requires pyte, ffmpeg, and pywinpty on Windows / pexpect on POSIX. tmux probes require tmux and a POSIX terminal. `os.fork`/PTY probes do not prove native Windows behavior; inspect `windows_console.py`, `terminal_resize_windows.py`, and other native harnesses for that surface. Audio scripts under `tests/audio/` have separate dependencies and may require hardware; inspect prerequisites individually.

Prefer registered CTest wrappers for lifecycle, backpressure, terminal-resize, media capture, dependency paths and crypto logging. They supply fixture executable paths and timeout/skip/serialization properties. Some standalone targets are `EXCLUDE_FROM_ALL`; enabling tests does not necessarily build every fixture. Look up target dependencies in `tests/CMakeLists.txt` and build the required target rather than inventing compile/link commands. Preserve intentional skip semantics (for example media capture return code 77).

Use temporary configs/HOME, free loopback ports, generated media, readiness deadlines, and cleanup in `finally`/context managers. Assert actual frames, state transitions or terminal cells. Preserve failed-run logs, media and JSON measurements when needed for diagnosis.
