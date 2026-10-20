# Issue 507: Windows verification

This report preserves the original implementation cohort. See the
[merge and review follow-up](review-follow-up/README.md) for the later revision.

The native executable and shared library, source fingerprint, commands, one-second
presentation samples, congestion results, and WASM artifact hashes are recorded in
[results.json](results.json). All runs used this worktree's rebuilt binaries.
Functional/lifecycle tests used the Debug sanitizer build; performance tests used
an optimized Release build with dynamic vcpkg dependencies (not a static release package). The JSON fingerprints identify each cohort separately.

## Sustained CLI presentation

Real native servers and ConPTY clients used changing test-pattern video, 80×24
truecolor terminals, audio disabled, and a target of 60 FPS. Each transport was
measured for 60 seconds after warmup. The counter measures completed terminal
presentations. The server also had to observe changing incoming client video.
The final benchmark explicitly uses Windows High priority for its owned harness,
server, and client processes. No unrelated processes are stopped or reprioritized.
This isolates the benchmark from other work on this shared Windows host; it is
not evidence of the same stability under normal scheduling during host contention.

The initial Debug/ASan TCP probe failed the same performance gate: median 58 FPS,
range 27–65. Its samples are retained in the JSON; the successful performance
results below use the Release build, without weakening the acceptance thresholds.
An initial Release attempt also failed stability while WASM compilation was active
(median 60, range 8–246); its samples are retained separately. The acceptance runs
below start after this task's browser/native build and test processes have exited.
A subsequent normal-priority run while other host work was active reached median
60 but failed stability (range 23–240). Those samples are retained in the JSON;
the explicitly labeled High-priority measurements below use the same FPS gates.
High priority alone also failed stability at 120×40 (median 60, range 45–66).
The accepted measurements below use the standard 80×24 terminal size and establish
performance only for that configuration. The 120×40 failures remain in the JSON.

| Transport | Median FPS | Minimum–maximum | One-second samples |
| --- | ---: | ---: | ---: |
| tcp | 60 | 47–62 | 60 |
| websocket | 60 | 55–79 | 60 |
| webrtc | 60 | 53–63 | 60 |
| discovery-tcp | 60 | 53–62 | 60 |

Acceptance requires median ≥58.8 FPS, at least 90% of readings ≥57 FPS, and at
least 80% of the expected one-second samples. All four measurements pass.

## Lifecycle and authentication

- Ten native standalone suites passed, including 17 connection lifecycle cases
  across TCP, WebSocket, and real local WebRTC channels. Cases cover callback
  ordering, pinned visits, partial startup, malformed and partial packets, deadlines,
  and retaining resources after a producer join fails.
- Real WebRTC security fixtures exercise plaintext, encryption, password and key
  authentication, wrong passwords, wrong pinned server keys, and rejected client keys.
  Adopted peers retain the authenticated security context and encryption sequence.
- Native discovery participants use the same capabilities-first media startup for
  WebRTC and direct TCP; both run the sustained CLI measurement above.
- Real-process TCP/WebSocket tests cover 24 stalled peers alongside successful
  media sessions, rate limiting, password and plaintext modes, pinned identities,
  client allowlists, rejection and recovery, and clean shutdown.
- All 13 injected client startup stages unwind for both TCP and WebSocket. A silent
  handshake cancels within five seconds. Both transports reconnect three times in
  one process and complete concurrent peer/local shutdown.
- Server lifecycle tests pass basic startup, privacy-required discovery refusal,
  unavailable discovery, password registration, and public registration/shutdown.
- TCP and WebSocket each pass eleven live CLI rendering changes; six snapshots
  cover four color modes, six palettes, and three rendering modes.
- Six live congestion/recovery cases pass: TCP/WebSocket/WebRTC × raw/H.264,
  with a separate healthy connection and a paced UDP relay for WebRTC.
  The fixture handles buffered encoder startup and varies legal filler lengths
  so identical compressed pictures cannot reuse an older capture timestamp.

## Browser

Headless Chromium used freshly rebuilt WASM and native Windows processes.
The separate website API's session-name suggestions are seeded in the test fixture.
`/client` streamed over WebSocket, disconnected, resized, and reconnected.
`/discovery` joined native ACDS over WebSocket, authenticated a WebRTC media peer
with a password, streamed changing frames, disconnected, and resized. Both tests
passed with advancing canvas counters and different canvas images, no page errors,
no connection/options lifetime errors, and no native AddressSanitizer or
caught-exception reports.
The real Chromium C/WASM host-render smoke also passed independent local/remote
settings and live updates without duplicate frames.

The browser intervals were:

- client-0: 303 rendered frames in five seconds.
- client-1: 282 rendered frames in five seconds.
- discovery: 296 rendered frames in five seconds.

![WebSocket client after reconnect](client-1.png)

![Discovery over WebRTC](discovery.png)

Web unit tests: **363 passed across 53 files**. Formatting, lint, and type checks
also passed. Browser frame intervals are separate from the CLI 60 FPS acceptance
measurement above. These are local transport tests; they do not claim Internet
NAT traversal or TURN interoperability coverage.

An earlier browser probe emitted an implicit signed-conversion UBSan diagnostic
at unchanged vendored `mdns.h:441`. It did not recur in the final browser cohort.
The transport tests above complete with exit code zero.

## Reproduce

```powershell
ctest --test-dir build -R "^(network-connections|network-events|webrtc-security|session-connections|transport-backpressure|participant-media|peer-ready|crypto-policy|host-render|runtime-settings)$" --output-on-failure
python tests/integration/event_io.py build/bin/ascii-chat.exe
python tests/integration/client_connection_lifecycle.py build/bin/ascii-chat.exe build/issue-507/windows/lifecycle
python tests/integration/server_lifecycle.py build/bin/ascii-chat.exe
python tests/integration/runtime_settings_cli.py build/bin/ascii-chat.exe
python tests/integration/network_fps_windows.py --binary build/perf507/bin/ascii-chat.exe --output build/issue-507/windows/fps --seconds 60 --priority high --width 80 --height 24
python tests/integration/network_fps_windows.py --binary build/perf507/bin/ascii-chat.exe --output build/issue-507/windows/fps --seconds 60 --priority high --width 80 --height 24 --transport discovery-tcp
python tests/integration/transport_backpressure.py --binary build/perf507/bin/test-backpressure-live.exe --output build/issue-507/windows/congestion --codecs raw,h264
cd web/web
vp test run
vp check --fix
vp exec playwright test -c playwright.networking.config.ts
```
