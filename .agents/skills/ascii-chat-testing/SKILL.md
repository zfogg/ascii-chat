---
name: ascii-chat-testing
description: Write, select, run, and diagnose ascii-chat C and Python tests across Windows, macOS, and Linux using Catch2 3, CTest, XML results, and Codecov coverage.
---

# ascii-chat testing

Run commands from the repository root unless stated otherwise. Read `AGENTS.md`, then inspect `tests/CMakeLists.txt` and `CMakePresets.json` for the affected target. Catch2 3 is the native C++ test framework; CTest is the scheduler and result collector. Current configuration takes precedence over historical commands. Plain Ninja/CMake builds use all available cores by default; do not add an explicit build job count. `ctest --parallel` is allowed with no numeric count so CTest can schedule tests across all available cores.

## Select the evidence

| Type                  | Use for                                                                                                   | Location                                           |
| --------------------- | --------------------------------------------------------------------------------------------------------- | -------------------------------------------------- |
| Catch2 unit            | Isolated API behavior, boundaries, ownership, validation/error paths                                      | `tests/unit/`                                      |
| Catch2 integration     | Interacting subsystems, handshakes, network/session lifecycles                                            | C++ files in `tests/integration/`                  |
| Catch2 benchmarks      | Rendering/throughput regressions with correctness checks and controlled measurements                      | `tests/benchmarks/`                                |
| Catch2 contracts       | Explicitly linked white-box production contracts                                                          | `tests/contracts/`                                 |
| Python pytest         | Python helper contracts and migrated native-process scenarios                                             | `tests/python/`                                    |
| Python fixture driver | Support program launched by a pytest case or native test                                                   | `tests/python/ascii_chat_tests/`, `tests/scripts/` |
| Standalone C probes   | Necessary separate process, native platform facility, dependency/build boundary, or Python-driven fixture | `tests/standalone/`, selected integration fixtures |

The primary native categories are **unit, integration, benchmarks**. Contract is an additional style/label; migrated suites retain unit/integration labels when assigned. CTest labels describe scope, not language: `integration` also includes registered Python harnesses.

Write new C++ tests with Catch2 3 (`TEST_CASE`, sections, tags, generators, and `TEMPLATE_TEST_CASE` where useful). C sources can be linked into Catch2 drivers or remain standalone only when a separate process/platform boundary genuinely requires it. Use `BENCHMARK` in `tests/benchmarks/` for performance cases and keep correctness checks outside the timed body. A source file's presence does not prove registration, build inclusion, or coverage participation.

There is no active Criterion dependency in the current CMake test graph. If a
legacy C file still includes Criterion, do not expand that usage: either
migrate it to a Catch2-compatible C++ test driver when it is in scope, or
record it as legacy/dead test code and verify that it is not registered by
CMake. The target end state is Catch2/CTest for native tests, with Python
pytest remaining the separate Python framework.

## Configure and discover

Use Clang and Ninja. On Windows, test natively first with the Windows preset/toolchain; WSL or Docker are optional additional environments for Linux-specific evidence, not prerequisites. Preserve build/dependency caches and unrelated work; reconfigure rather than deleting `build/`. Configure the test preset:

```sh
uv sync --frozen
cmake --preset tests -B build
cmake --build build --target tests
ctest --test-dir build -N
ctest --test-dir build --print-labels
ctest --test-dir build -L '^unit$' --no-tests=error --output-on-failure --parallel
ctest --test-dir build -L '^integration$' --no-tests=error --output-on-failure --parallel
ctest --test-dir build -L '^benchmarks$' --no-tests=error --output-on-failure --parallel
```

The same Catch2/CTest workflow is supported on Windows, Linux, and macOS. Verify discovery with `ctest -N`; zero selected tests is not a pass. Use Catch2's `--list-tests`, tags, sections, generators, and XML reporters for focused local runs. XML results are written below `build/Testing/xml/`; preserve that directory for CI artifacts and Codecov/test-report processing. Coverage builds use the repository coverage preset/options and upload the generated coverage data after the tests complete.

Read [running-tests.md](references/running-tests.md) for focused Catch2/CTest commands, native Windows/macOS/Linux workflows, Python harnesses, and standalone fixtures. Read [coverage-and-ci.md](references/coverage-and-ci.md) for XML generation, Codecov uploads, and CI diagnostics. Use the cross-platform skill only when an additional WSL/SSH/Docker environment is specifically needed.

## Python tests

Use the locked root uv project. New Python tests go under
`tests/python/tests/` and must be import-safe: create processes, parse arguments, and
mutate environment only in fixtures or test functions. Share non-pytest mechanics
through `ascii_chat_tests`; use pytest fixtures for isolation and lifecycle.
Run `uv run --frozen ruff check tests` and
`uv run --frozen ty check tests` after Python
changes. Both check the entire test tree. CTest also registers these as
`python-ruff` and `python-ty`.
Pass the built binary with `--ascii-chat-binary` (or `ASCII_CHAT_TEST_BINARY`).
Pytest uses xdist on all logical cores by default. CTest coordinates separate
pytest batches using `PROCESSORS` to avoid nested oversubscription; preserve
explicit CTest serialization for existing tests that share exclusive resources.
Keep pytest collection under `tests/python/tests/`; reusable helpers are under
`tests/python/ascii_chat_tests/`, and manual scripts are not collected.
See [running-tests.md](references/running-tests.md) for direct and CTest commands.

## Shared native test code

Reusable native test support belongs under `lib/tests/` with public declarations
under `include/ascii-chat/tests/` when callers outside the implementation need
them. Add shared fixtures, process/socket helpers, logging setup, and test
resource utilities there instead of copying them into individual test files.
Keep this code framework-neutral where practical so Catch2 tests, C tests,
standalone probes, and future platform test drivers can reuse it. Link the
appropriate shared test source through the CMake test target helpers and keep
ownership, cleanup, and platform behavior explicit. Do not put Catch2-specific
assertions or a Catch2 `main` in `lib/tests/`; assertions belong in the test
translation unit and the Catch2 driver is supplied by CMake.

## Write useful C and Catch2 tests

- Reproduce observable failure with concrete inputs and expected outputs/state. Cover meaningful success, rejection, boundary, partial-failure, rollback, and cleanup paths. Do not mirror the implementation or refresh a golden without independently establishing correct behavior.
- Use Catch2 fixtures/helpers only for real shared setup; prefer local setup and `SECTION`s when scope is clear. Isolate global options, errno, environment, temporary files, and sockets. Cases must work independently and in different orders.
- Use Catch2 assertions (`REQUIRE`, `CHECK`, `REQUIRE_THAT`) with decomposable expressions kept simple. Do not put chained `&&`/`||` comparisons directly inside decomposed assertions; split them or parenthesize intentionally. Use tags such as `[unit]`, `[integration]`, `[network]`, and feature tags consistently. Use generators for meaningful input variation rather than duplicated cases.
- Check `asciichat_error_t` and meaningful error context. Clear expected errno between independent operations. Follow project allocation tracking, ownership, platform/thread/socket abstractions, and `log_*`; raw debug `printf`/`fprintf` bypass filtering and disrupt terminal tests.
- For worker-thread checks, do not call Catch2 assertions from worker threads. Join workers and report results on the runner thread. Prefer readiness signals and bounded deadlines to sleeps as synchronization. Use libplatform process APIs for subprocesses and clean them up on failure.
- Prefer generated media/test patterns and loopback to hardware, public URLs, or credentials. Skip only for genuinely absent prerequisites and report why. Verify rejected mutations leave prior state intact. A quiet log is not positive evidence that frames or transitions occurred.
- For a CI-safe synthetic client fixture, use `--test-pattern --test-audio --no-audio-playback`: the first two replace camera and microphone input, while disabling playback prevents PortAudio from requiring a speaker device. If audio behavior is outside the test's scope, use `--test-pattern --audio=false` instead. `--test-pattern` is a video source, so do not combine it with `--file`, `--url`, `--waveform`, or `--fft`; file/URL tests retain their media source, while waveform/FFT tests use an audio source instead.
- Check benchmark output correctness before timing. Use Catch2 `BENCHMARK`/`BENCHMARK_ADVANCED` with stable inputs, compare equivalent optimized builds, record CPU/SIMD/configuration, and avoid fragile timing thresholds on shared/emulated hosts. Coverage instrumentation changes performance.
- Use supported ASan/UBSan checks when memory risks warrant them; inspect `cmake/platform/Sanitizers.cmake` for actual options. Debug alone is not proof instrumentation ran. Do not suppress findings just to obtain green tests.

## Verify and report

Build the changed suite and run focused cases, then the relevant category/process scenario. Reconfigure after adding globbed sources. Inspect `ctest -N -V` and CMake for exclusions, platform guards, fixture dependencies, serialization and deadlines. Report SHA/dirty patch, OS/architecture, commands, exits, counts/skips, XML paths, coverage generation, accepted uploads, and native runtime evidence. Native Windows evidence is valid and expected; WSL/Docker evidence does not replace native Windows or macOS verification.
