# Standalone test executables

Ordinary regression tests belong in Criterion (`tests/unit`, `tests/integration`,
or the explicitly linked white-box suites in `tests/contracts`). Do not add a
custom `main()` merely to run a sequence of assertions. Criterion provides case
isolation, filtering, timeouts, and XML results. Use
`tests/contracts/worker_assert.h` for assertions in test-owned worker threads;
it reports a failure without unwinding to the test thread's jump buffer.

The remaining entry points have external process or build requirements:

| Entry point | Reason to keep a standalone process |
| --- | --- |
| `errno/errno_test.c` | Signal handlers, pending error diagnostics, and intentional assertion termination are observed by an external process. Ordinary stack/scope/history tests are in `contracts/errno`. |
| `network/prepare_test.c` | Python supplies a live WebSocket echo URL. Wire-format and TCP contract cases are in `contracts/network/prepare_test.c`. |
| `network/vectored_test.c` | CLI peer/server roles and microbenchmark options are used outside the test runner. Correctness cases are in `contracts/network/vectored_test.c`. |
| `network/backpressure_live.c` | Python coordinates real transport/codec producer and consumer processes and inspects their output. Admission and budget contracts use Criterion. |
| `media/capture_open_test.c` | Python creates media files and drives camera/open/cancel scenarios in bounded subprocesses. |
| `logging/crypto_logging.c` | The parent inspects the completed process's real log for secrets and rate-limited diagnostics. |
| `media/clipboard_windows_test.c`, `media/audio_priority_test.c`, `session/terminal_resize_test.c` | Exercise Windows clipboard, thread priority, and console APIs; Criterion is unsupported on Windows. |
| `crc32/test.c`, `upnp/test.c` | Dependency-isolated compilation matrices exercise hardware-disabled/masked CPUs and different gateway API/availability configurations, including Windows. These verify configurations independent of the native application dependency graph. |
| `log_io/test.c` | The parent verifies exact stdout/stderr restoration after real descriptor redirection, including an injected second-redirect failure. A test runner would itself own those streams. |

Other deliberate process fixtures outside this directory:

- `tests/integration/ui_native_probe.c`, `sync_regressions.c`, and
  `client_like_lifecycle.c` are driven by terminal/process harnesses. They include
  input, signal, retry, and deliberately stuck-lock scenarios.
- `tests/simd/test_dispatch.c` tests startup dispatch overrides and Linux SVE
  vector lengths; `ASCII_EMISSION_STANDALONE` runs the same Criterion emission
  contracts under an isolated renderer/ISA harness. The Emscripten entry point in
  `digital_rain_test.c` runs those contracts where Criterion cannot run.
- `tests/benchmarks` and `tests/simd/benchmark_*.c` are measurements, not unit suites.
- `tests/cmake/*/main.c`, `tests/wasm/dependencies/main.c`, and
  `cmake/test/test_lib.c` are build/link/install consumer probes. Keeping the
  consumer free of a test framework is part of what they verify.

Run converted contracts through CTest after building their targets:

```sh
ctest --test-dir build_docker -L contract --output-on-failure
```

The dependency-isolated CRC32, UPnP, and log-I/O matrices are also CTest tests.
In a native coverage build their Python harnesses retain the separately compiled
production-source profiles under the build directory, so the final coverage
report includes the implementation they exercise without counting test sources.

The audio processor regression is now `tests/unit/audio/processor_test.c` and is
registered with the normal Criterion unit inventory.
