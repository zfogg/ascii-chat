# Measured host selection and handoff

Discovery participants share one C selector (`network/consensus/election.c`).
The handoff coordinator (`network/consensus/handoff.c`) runs on ACDS, whose
authenticated connection registry supplies membership and fences host changes.
Native discovery and the browser use the same control codec. Browser hosting
uses `session/host.c`; TypeScript schedules browser APIs and manages peer
connections, without a second election algorithm.

`PARTICIPANT_LIMIT`, defined in `common.h`, is 128. Discovery admission,
membership storage, ring topology, native hosting, browser hosting, and audio
source bounds use this shared limit. This is a capacity bound, not a claim that
every machine can mix 128 simultaneous video streams at the maximum frame rate.

## Measurements

Every five seconds each participant sends a start marker followed by a bounded
64 KiB probe on its existing authenticated ACDS connection. ACDS measures the
interval between the complete packets with its own monotonic clock. It derives
delivered kilobits per second from bytes actually received. A probe taking over
five seconds is invalid; a completed observation expires after fifteen seconds.
Sequence numbers prevent reuse of a completed probe. The coordinator never
compares clocks from different machines.

This is an **ACDS-path throughput observation**, including transport and scheduling
effects. It is not an estimate of the maximum Internet uplink, the capacity of
every candidate-to-participant route, or wire bandwidth. The traffic meter remains
an application-byte meter and is not used as capacity evidence.

Native adapters reuse positively observed LAN/public-interface/router-mapping
information when available. A STUN reflexive address alone is not proof of a
public interface or a particular NAT type. Browser NAT classification, RTT, and
STUN success percentage remain unknown unless the adapter can actually measure
them. Unknown fields earn no favorable score contribution. A missing observation
never becomes the old 50 Mbps/25 ms/90 percent default.

## Selection and readiness

The active host is excluded from successor selection. Candidates need fresh,
valid observations and a supported hosting capability. The existing weighted
integer score is authoritative; ties compare NAT tier, upload throughput, then
participant UUID bytes. Array/arrival order does not resolve ties. The legacy
ring state machine stores results from this same selector rather than calculating
a different score.

ACDS sends `PREPARE` to the selected candidate. Native discovery creates and starts
its host context; a browser creates its C host engine. Only a matching `READY`
acknowledgment publishes a `PLAN`. Membership generations and round numbers fence
late acknowledgments. A rejected or timed-out preparation is excluded, allowing
the next eligible candidate to be tried. Preparation does not change the active
media host.

On host departure, ACDS commits the prepared successor and updates the session
record. An expired host observation also triggers recovery when a stalled host
retains its signaling socket. Native discovery stops its old media workers before
applying the new role. Participants negotiate fresh WebRTC connections to the
committed participant ID; the successor starts its host media loop. Browser capture
and page settings remain owned by the existing page while its session adapter
changes role.

The control stream uses versioned packet 6090, a fixed 64-byte header, explicit
big-endian integers, and exact-length validation. ACDS checks session and sender
IDs against the joined transport identity. Clients cannot submit election decisions.
Legacy host announcements cannot overwrite an already fenced host. No eligible
successor produces an explicit failure after a bounded recovery window.

The current discovery creation path creates WebRTC sessions. Native-only testing
therefore uses native executables over WebRTC, including real ACDS TCP signaling.
The TCP-listener capability is reserved in the codec; adapters do not claim it
without a supported, verified migration route. Discovery recovery requires ACDS;
the earlier draft's claim of ACDS-independent failover does not describe this
implementation.

## Validation

The consensus unit suite covers wire validation, missing/stale observations,
ties, preparation rejection, membership changes, failure deadlines, and the
128/129 participant boundary. Coordinator fixtures inject observations explicitly.

`web/web/tests/e2e/run-handoff.mjs` starts actual ACDS and native processes and
launches Chromium through Playwright. Its socket proxy throttles one participant's
probe path so the expected successor is known before killing the host. Scenarios:

- Native host and two native participants: kill the host with `SIGKILL`, assert
  that the expected successor enters its host media loop, and require freshly
  rendered frames after the other participant reconnects.
- Browser host and two browser participants: kill the host's separate browser
  process and require new, distinct decoded ASCII images on both survivors.
- Native host, browser successor, and the React `/discovery` page: kill the native
  host, require a new connected peer connection on the page, then require new
  changing ASCII images after that reconnection.

Set `ASCII_CHAT_TEST_BINARY`, `PLAYWRIGHT_CORE_PATH`, `CHROMIUM_PATH`,
`HANDOFF_WEB_URL`, and `HANDOFF_ARTIFACTS` for the test environment. Serve `web/web`
with Vite+, with the test origin allowed and cross-origin isolation enabled.
The browser and native processes must be able to reach each other's ICE addresses.
Set `HANDOFF_SCENARIOS=native,browser,mixed` to run the complete matrix. Result JSON,
process logs, and screenshots are written to the artifact directory. The test uses
changing synthetic video, disables native audio devices, and does not claim audio
continuity or TURN-route coverage.


## Audio and forced relay experiment

Run `web/web/tests/e2e/run-handoff-audio.mjs` with the same binary,
Chromium, website, Playwright and artifact settings as the video runner.
`HANDOFF_AUDIO_SCENARIOS=browser,mixed` selects a browser host or native
host followed by browser successors. Native-only audio uses the companion runner below.
The probe uses the production AudioPipeline, deterministic `test2` capture,
Opus packets, the C host mixer and decoded remote PCM playback callback.
The native host uses `--test-audio`; hardware microphones are unnecessary.
The visible page reports decoded non-silent blocks and sample counts.
These observations establish decode and playback submission, not physical
speaker output, perceptual audio quality or exact sample preservation.

The default is three abrupt host process kills, with 30 seconds of media
activity before each loss. Each round creates a replacement participant,
waits for an acknowledged successor, then kills the current host. Both
survivors must observe a new commit and receive five changing images plus
25 new non-silent decoded audio blocks after that commit. Set
`HANDOFF_LOSSES` (2–20) and `HANDOFF_DWELL_MS` (at least 1000) to extend
sessions; record overrides with the result. `HANDOFF_PORT` selects the
base signaling port (default 31000; mixed uses base + 20).

`HANDOFF_ROUTE=relay` additionally requires `HANDOFF_TURN_URL`,
`HANDOFF_TURN_USERNAME` and `HANDOFF_TURN_PASSWORD`. Use a reachable
credentialed TURN server; the runner does not silently fall back to direct
routing. Both browser ICE policies and the native host use relay-only mode.
In relay mode, before and after every loss, getStats must identify selected
successful candidate pairs with nonzero sent/received bytes; every selected
pair must have relay candidates at both ends. Direct mode records available
selected-pair diagnostics but relies on decoded media for continuity: native
interop may omit selected-pair traffic counters. Credentials are not written into result
JSON. A configured TURN URL or a gathered relay candidate alone is insufficient.

Each result records pre-loss and post-commit counters, selected pairs,
wall-clock recovery-observation time, and decoded-audio callback gaps using
the receiver's monotonic clock. The interruption includes the last pre-kill
observation through the first new block after commit; it is an upper bound
on the audible gap, not a synchronized one-way latency. Screenshots and
browser/native logs accompany the JSON, and failed runs retain an explicit
`passed: false` result. A failed or unexecuted scenario is not covered by
the original video-only validation claims.

The experiment exposed browser worker admission dropping the synchronous
SERVER_STATE emitted before the C participant ID is registered. The worker
now retains the logical admission recipient during that call; the focused
worker regression checks delivery and rejection of unknown recipients.


Validation on Windows x64, Clang/Ninja and Chrome 155 executed all six rows
below with three real host process losses per session and 30-second dwell
periods. Browser runs used the rebuilt source-matched Release WASM bundle.
Both browser observers recovered new changing video and non-silent decoded
audio after every commit. Mixed runs first proved native host audio while
only one browser receiver was connected. Every forced-relay measurement
verified selected relay-to-relay pairs carrying bytes before and after loss.

| Scenario | Host losses | Callback interruption upper bound | Recovery observation |
| --- | ---: | ---: | ---: |
| browser-audio-direct | 3 | 2.08–16.90 s | 2.71–17.67 s |
| browser-audio-relay | 3 | 2.27–11.77 s | 2.84–12.40 s |
| mixed-audio-direct | 3 | 0.17–1.46 s | 0.94–2.11 s |
| mixed-audio-relay | 3 | 0.22–4.99 s | 0.98–5.67 s |
| native-audio-direct | 3 | 2.15–3.14 s | 2.84–4.09 s |
| mixed-audio-relay-slow-video-and-promotion | 3 | 7.34–10.63 s | 8.29–11.30 s |

[Result JSON](validation/handoff-audio-2026-10-21/results.json) records counters,
selected pairs, timestamps, binary hashes and the WASM manifest. Final
[browser relay](validation/handoff-audio-2026-10-21/browser-relay-final.png) and
[mixed relay](validation/handoff-audio-2026-10-21/mixed-relay-final.png)
screenshots show recovered media and decoded audio sample counts. Local raw
logs, screenshots and process exit evidence remain in the directories listed
in the JSON. These results demonstrate recovery with interruptions, not
seamless audio or general Internet reliability.

Run `web/web/tests/e2e/run-handoff-native-audio.mjs` for native-only audio.
It keeps three live participants by adding a replacement between losses,
uses `--test-audio` with muted local speakers, and requires fresh decoded
remote PCM plus 25 decoded video frames after reconnect. Test-source decode
observations are emitted after successful playback submission. Saved log
offsets use the chronological log file; stderr is retained separately.
Strictly newer receiver monotonic timestamps prevent buffered pre-loss
observations from satisfying recovery. `HANDOFF_NATIVE_LOSSES` accepts 2–3;
`HANDOFF_DWELL_MS` and `HANDOFF_PORT` work as above. Native-only TURN
routing was not exercised. Physical speaker output, sample-perfect recovery
and large participant counts are not covered.

Both WASM targets were rebuilt using Emscripten 5.0.0 with its default
`dlmalloc` allocator. An experimental `emmalloc` override aborted actual
C client initialization despite loading assets successfully. The added
Playwright runtime-init regression exercises `initClientWasm` before cleanup
so asset-only checks cannot miss that failure. Publication verifies the
source fingerprint and each generated file hash.

Additional production repairs transfer the live probe protocol and its five-second
clock into the promoted host, preserving sequence, generation and freshness during
slow runtime initialization (ACDS replay/rate limits remain unchanged), preserve healthy ACDS signaling while a failed media queue enters its
bounded successor wait (queue congestion limits remain unchanged), send native
SERVER_STATE only after peer negotiation is ready, and inject native host audio
into its local memory participant on an independent capture worker, joined before
the host role destroys its capture context. Audio draining is independent of video
frame rate. Adopted browser startup has a 30-second deadline; readiness cancels it.

The final relay stress row sets `HANDOFF_NATIVE_FPS=1` and
`HANDOFF_PROMOTION_DELAY_MS=7000`. An eight-second exclusive native-source
observation decoded 401 blocks, including 291 non-silent blocks (72.6 percent),
before introducing another browser sender. Every injected loss produced exactly
one host commit while the promoted worker start was delayed seven seconds.
The longer measured interruptions in that row include the intentional delay.
A fake-clock regression also holds runtime initialization for twelve seconds and
verifies uninterrupted five-second probing; lifecycle tests cover ready and hung
startup cleanup. All 388 web unit tests in 56 files passed.

These runtime results use implementation commit
`bd4869c10c3aafd4c1ba5c79635f1d501f48d4a1`. The synthetic PR merge against
master `b1090964d6898fa2525289936a6c535a696efa1b` changes additional hashed
C sources and correctly fails prebuilt-WASM freshness validation. Rebuild and
revalidate the bundle after integrating those source changes; these measurements
do not certify that synthetic merge. macOS amd64 CI configuration also failed
while resolving ffmpeg.org, before compiling the changed source.
