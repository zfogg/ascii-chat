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
