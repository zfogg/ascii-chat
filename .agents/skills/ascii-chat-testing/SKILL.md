---
name: ascii-chat-testing
description: Write, select, run, and diagnose ascii-chat C and Python tests across Windows, macOS, and Linux, including Criterion, Docker, JUnit results, and Codecov coverage.
---

# ascii-chat testing

Run commands from the repository root unless stated otherwise. Read `AGENTS.md`, then inspect `tests/CMakeLists.txt`, `cmake/deps/Criterion.cmake`, and `CMakePresets.json` for the affected target. Current configuration takes precedence over historical commands.

## Select the evidence

| Type | Use for | Location |
| --- | --- | --- |
| Criterion unit | Isolated API behavior, boundaries, ownership, validation/error paths | `tests/unit/` |
| Criterion integration | Interacting subsystems, handshakes, network/session lifecycles | C files in `tests/integration/` |
| Criterion performance | Rendering/throughput regressions with correctness checks and controlled measurements | `tests/performance/` |
| Criterion contracts | Explicitly linked white-box production contracts | `tests/contracts/` |
| Python integration | Real executables, CLI, subprocesses, media, terminal/ConPTY/tmux behavior | Python files in `tests/integration/` |
| Standalone C probes | Necessary separate process, native platform facility, dependency/build boundary, or Python-driven fixture | `tests/standalone/`, selected integration fixtures |

The three primary Criterion categories are **unit, integration, performance**. Contract is an additional style/label; migrated suites retain unit/integration labels when assigned. Some contracts have only `criterion;contract`, so inspect discovery before claiming complete coverage. CTest labels describe scope, not language: `integration` also includes registered Python harnesses.

Prefer Criterion for new ordinary C regressions and extend existing suites. Standalone tests should be uncommon: retain them when a process/platform boundary justifies a separate `main()`, rather than duplicating the test framework. A source file's presence does not prove registration, build inclusion, or coverage participation.

## Configure and discover

Use Clang and Ninja. Preserve build/dependency caches and unrelated work; reconfigure rather than deleting `build/`. The default preset currently disables tests, so enable them explicitly:

```sh
cmake --preset default -B build -DBUILD_TESTS=ON
cmake --build build --target tests
ctest --test-dir build -N
ctest --test-dir build --print-labels
ctest --test-dir build -L '^unit$' --no-tests=error --output-on-failure --parallel 1
ctest --test-dir build -L '^integration$' --no-tests=error --output-on-failure
ctest --test-dir build -L '^performance$' --no-tests=error --output-on-failure
```

Criterion works natively on **macOS, including Intel, and glibc Linux**. This supersedes older Intel-Mac Docker-only testing guidance. Windows and musl builds disable Criterion; from Windows use Linux Docker. Release/RelWithDebInfo disable it by default: add `-DASCIICHAT_FORCE_CRITERION_TESTS=ON` with `BUILD_TESTS=ON` on supported platforms. Verify discovery; zero selected tests is not a pass.

Read [running-tests.md](references/running-tests.md) for focused Criterion commands, Windows Docker, Python harnesses, and standalone fixtures. Read [coverage-and-ci.md](references/coverage-and-ci.md) for XML generation, Codecov uploads, and CI diagnostics. Use [ascii-chat-cross-platform](../ascii-chat-cross-platform/SKILL.md) for SSH, source transfer, compiler setup, and persistent environments.

## Write useful C and Criterion tests

- Reproduce observable failure with concrete inputs and expected outputs/state. Cover meaningful success, rejection, boundary, partial-failure, rollback, and cleanup paths. Do not mirror the implementation or refresh a golden without independently establishing correct behavior.
- Use `TestSuite` `.init`/`.fini` fixtures for logging/options/resources; follow nearby suites and `include/ascii-chat/tests/{common,logging}.h` / `lib/tests/`. Isolate global options, errno, environment, temporary files, and sockets. Cases must work independently and in different orders.
- Use typed Criterion assertions with helpful diagnostics: equality, strings, memory and floating-point tolerance where appropriate. Fatal assertions establish prerequisites before dereference; nonfatal expectations are suitable only when continuing is safe. Never put required operations inside C `assert()`, which disappears with `NDEBUG`.
- Check `asciichat_error_t` and meaningful error context. Clear expected errno between independent operations. Follow project allocation tracking, ownership, platform/thread/socket abstractions, and `log_*`; raw debug `printf`/`fprintf` bypass filtering and disrupt terminal tests.
- For worker-thread checks, inspect `tests/contracts/worker_assert.h`; ordinary Criterion assertions may require runner-thread context. Join workers and propagate failures. Prefer readiness signals and bounded deadlines to sleeps as synchronization. Clean up subprocesses on failure.
- Prefer generated media/test patterns and loopback to hardware, public URLs, or credentials. Skip only for genuinely absent prerequisites and report why. Verify rejected mutations leave prior state intact. A quiet log is not positive evidence that frames or transitions occurred.
- Check performance output correctness before timing. Compare equivalent optimized builds, record CPU/SIMD/configuration, and avoid fragile timing thresholds on shared/emulated hosts. Coverage instrumentation changes performance.
- Use supported ASan/UBSan checks when memory risks warrant them; inspect `cmake/platform/Sanitizers.cmake` for actual options. Debug alone is not proof instrumentation ran. Do not suppress findings just to obtain green tests.

## Verify and report

Build the changed suite and run focused cases, then the relevant category/process scenario. Reconfigure after adding globbed C sources. Inspect `ctest -N -V` and CMake for exclusions, platform guards, fixture dependencies, serialization and deadlines. Report SHA/dirty patch, OS/architecture, commands, exits, counts/skips, and artifact paths. Distinguish compilation, executed assertions, coverage generation, accepted uploads, and native runtime evidence. Docker proves Linux behavior, not native Windows or macOS behavior.
