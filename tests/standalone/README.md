# Standalone native tests and fixtures

Native standalone test runners use Catch2 v3 as well. They remain separate
executables when they need process, console, socket, hardware, dependency, or
resource isolation; CTest labels them `standalone` and applies the same JUnit
and coverage rules as the consolidated unit runner.

Programs in this directory that are launched by Python or inspected by a
parent process are fixtures or probes, not test runners. They intentionally
retain a small `main()` because their process boundary is the behavior under
test. Every such exception must be listed here and must not contain a hidden
second test suite. Anything not listed here is a Catch2 test and must be
registered with `Catch2::Catch2WithMain` (or discovered from a shared Catch2
executable).

The current process-boundary exceptions are:

* `errno/errno_test.c`, `errno/errno_tui_fixture.c`, and
  `errno/error_popup_fixture.c`: subprocesses launched by Python for signal,
  assertion, TUI, and error-popup behavior.
* `logging/crypto_logging.c`: the crypto-logging subprocess.
* `network/backpressure_live.c`, `network/client_ping_test.c`,
  `network/mdns_advertiser_fixture.c`, `network/prepare_test.c`,
  `network/quic_http3.c`, `network/quic_stream.c`, `network/quic_tls.c`,
  `network/traffic_metadata.c`, `network/transport_metrics.c`, and
  `network/vectored_test.c`: live transport,
  ACIP, QUIC, mDNS, or compatibility processes whose lifetime and wire I/O are
  inspected by a parent harness.
* `media/capture_open_test.c`, `media/extractor_audio_probe.c`,
  `media/fake_yt_dlp.c`, `media/otel_media_test.c`, and
  `media/otel_contract_test.c`: media, extractor, and telemetry subprocesses
  whose stdout, exit status, or external lifecycle is part of the contract.
* `media/clipboard_windows_test.c` and `platform/terminal_pty_test.c`: OS
  integration probes launched as child processes.
* `quic_tls_check/dependencies.c`: a dependency/linkage probe.
* `crc32/test.c`, `log_io/test.c`, and `upnp/test.c`: isolated compiler tests;
  their Python drivers create temporary source trees and compile each ABI or
  platform variant independently, which is the behavior under test.
* `tui/splash_init.c`: the fallback terminal lifecycle probe used only when
  the Catch2 contract target is unavailable. The normal native test build uses
  `contracts/tui/splash_init_test.cpp`.

These exceptions must remain thin fixtures/probes. Their actual assertions
belong in the Python harness or in the maintained Catch2 suites under
`tests/unit/` and `tests/contracts/`; do not add a second test suite inside a
fixture.

Catch2 standalone runners should use semantic tags such as `[standalone]`,
`[platform]`, `[network]`, `[serial]`, or `[slow]`. Use CTest labels for
scheduling and process policy, for example:

```sh
ctest --test-dir build -L standalone --no-tests=error --output-on-failure --parallel 8
ctest --test-dir build -L 'benchmark|serial' --no-tests=error --parallel 1
```

The Catch2 XML reports are written below
`build/Testing/catch2-xml/`; CTest's aggregate report remains
`build/Testing/ctest-results.xml`. Keep benchmark timing runs separate from
coverage and sanitizer runs.
