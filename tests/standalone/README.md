# Standalone native tests and fixtures

Native standalone test runners use Catch2 v3 as well. They remain separate
executables when they need process, console, socket, hardware, dependency, or
resource isolation; CTest labels them `standalone` and applies the same JUnit
and coverage rules as the consolidated unit runner.

Programs in this directory that are launched by Python or inspected by a
parent process are fixtures or probes, not test runners. They intentionally
retain a small `main()` because their process boundary is the behavior under
test. Every such exception must be listed here and must not contain a hidden
second test suite.

Examples of fixture/probe exceptions include signal and error-popup probes,
live transport producers/consumers, media extraction helpers, mDNS advertisers,
QUIC dependency probes, and programs whose stdout/stderr or exit status is
examined by a parent harness.

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
