# WebAssembly Client Tests

This directory contains unit and E2E tests for the ascii-chat WebAssembly client mode.

## Pull request checks

`.github/workflows/web-tests.yml` runs on every pull request and on pushes to
`master`/`main`. Its independent `Web unit` and `Web playwright` jobs use Node.js
24 and the workspace's pinned Vite+ version and frozen lockfile. A failed suite
fails its job; browser retries are disabled. Reports are retained for 14 days,
including screenshots, video, and traces from failed browser tests.

To reproduce these checks, run `vp install --frozen-lockfile` from `web/`, then
run these commands from `web/web/`:

```bash
vp test run --maxWorkers=4
vp exec playwright install chromium
vp run test:e2e:ci
```

The focused Chromium suite uses `playwright.ci.config.ts` and starts its own
Vite server on port 3000; it does not reuse a developer's running server. The
shared browser fixture blocks external HTTP requests so font/CDN availability
cannot delay the checks. It covers:

- Real browser WebSocket packet exchange, retry while the server is down,
  reconnection after restart, and cancellation after explicit disconnect.
- Render settings restored after reload and from a copied URL in a fresh browser
  context, default-value omission, and preservation of unrelated URL parameters.
- Device preview playback and release/reacquisition of camera streams.
- Compiled C/WASM splash animation, keyboard actions, help/log overlays, resizing,
  covered media frames, and repeated runtime teardown.
- Uploaded video with decoded audio, pause/restart/mute/stop, codec conversion,
  animated GIFs, and PNG/JPEG stills.
- Matching versioned WASM assets for each mode and raster workers.

Web units include discovery/connection lifecycle, encryption policy, settings,
media ownership, rendering, and audio pipelines. The browser transport test is
an adapter integration check, not a native-server crypto-handshake test. Native
server, hardware, and FPS benchmarks remain in the broader manual E2E suite;
the native-only network-log test in `session-ui.spec.ts` is explicitly excluded
from PR checks.

The browser job verifies and publishes the tracked WASM artifacts. A stale or
missing manifest fails the check; rebuild `mirror-web` and `client-web` and
publish their bundle before submitting C/WASM changes. No native build or camera
hardware is required for this focused suite.

## Broader browser suite

The default `vp run test:e2e` command also discovers native-server, hardware,
and performance tests. Run individual specs with `vp exec playwright test
<spec-name>` and consult the spec for its required binaries or environment
variables. Native-server fixtures use `build/bin/ascii-chat` by default, or the
path supplied by `ASCII_CHAT_TEST_BINARY`.

## Unit test development

From `web/web/`, use `vp test` for watch mode, or select a file:

```bash
vp test run tests/network/reconnection.test.ts
vp test run tests/render-settings.test.ts
vp test run tests/hooks/mirror-file.test.tsx
```

Vitest uses `vitest.config.ts`, jsdom, and `tests/setup.ts`. Playwright specs
are excluded from Vitest discovery. WASM-dependent behavior is tested in the
browser against the verified bundle rather than mocked as a second UI.

## Debugging failures

Download the failing job's `web-unit-results` or `web-playwright-results`
artifact. Unit and browser JUnit reports are under `test-results/`; the browser
HTML report is under `playwright-report/`. Open a trace locally with:

```bash
vp exec playwright show-trace test-results/<failed-test>/trace.zip
```

If port 3000 is occupied, stop its owning server before running the focused suite.
If WASM verification reports stale artifacts, use the repository's configured
CMake/Emscripten build to rebuild and publish `mirror-web` and `client-web`;
do not bypass the manifest check.
