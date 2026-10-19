# Transport backpressure and media admission

Every native connection owns a plaintext outbound scheduler. Browser WebSocket,
WebRTC and hosted peer sessions use the same contract through `OutboundQueue`.
Backend event loops remain independent; none decides whether video is disposable.

## Capacity and ownership

The hard per-connection accounted budget is 8 MiB. Video cannot use the final
1 MiB: 512 KiB is reserved for audio and another 512 KiB for control. Packet-count
limits reserve descriptors too (448 video, 480 audio, 512 total). Accounting
reserves plaintext, two wire/encryption owners, protocol overhead and descriptors.
Native TCP/WebSocket request a 64 KiB socket send buffer and reserve 128 KiB
within the budget, covering Linux socket accounting. Receive buffers retain
their normal tuning. Native SCTP send space is capped and reserves 64 KiB within this budget,
because libdatachannel does not expose that retained amount in buffered bytes.
Snapshots report the fixed reservation separately from measured backend bytes.
Backend pending bytes are additional; Receive buffers and bytes already in the network are outside this outbound budget.

Media submissions copy the borrowed plaintext before returning accepted. Expected
congestion is a separate admission disposition, not a network failure. The public
`acip_outbound_submitv` returns `asciichat_error_t` plus an `acip_admission_t`.
Existing convenience send functions translate non-admission to `ERROR_BUFFER_FULL`;
callers retain a usable connection and distinguish that from fatal write failure.
Audio/control rejection is explicit. Already admitted reliable packets are never
silently removed. A persistently blocked reliable queue closes the connection:
five seconds for waiting audio, ten seconds for waiting control. A transport that has
already accepted bytes remains responsible for its own failure/deadline handling.

One native worker serializes each connection's final write order. Handshake and
setup/control calls (except ping/pong) wait until backend submission, preserving negotiation barriers.
Enqueueing is serialized with admission and budget reservation. The caller must
quiesce producers before destroying a transport, as with the existing transport
lifetime contract. Destruction stops admission, interrupts writes, joins the
worker, and releases every queued owner before backend resources are freed.

## Scheduling and codec recovery

Independent raw image and ASCII snapshots replace older wholly unsent snapshots
of the same stream. Video expires after 100 ms waiting in the application queue.
Audio and ping/pong may precede unsent video, retaining FIFO order within their
traffic classes and never crossing a setup/teardown/negotiation barrier. Other
controls remain FIFO barriers. Encryption happens after these decisions. Neither
ciphertext nor a partly submitted packet can be reordered, discarded, or retried.

Backends receive one complete ACIP packet at a time. Scheduling waits for
reported pending bytes to drain before committing the next packet of any class.
This keeps tiny control packets from building a long opaque FIFO behind video.
Liveness pings/pongs enqueue asynchronously; setup and negotiation calls retain
their synchronous submission barriers.

Bytes already in a socket, SCTP, or the network cannot be removed. A frame
straddling a receiver throttle can therefore be older than 100 ms; that age is
measured rather than hidden.

H.264 and H.265 share conservative dependency handling. After any queued encoded
frame is discarded, all unsent pictures for that codec/stream are removed and
subsequent dependent pictures are rejected until a keyframe is admitted. Native
and browser capture request a fresh keyframe from their encoder. Native encoders
request closed recovery pictures with repeated parameter sets. Browser WebCodecs
continues its Annex B parameter-set handling. Discarding that recovery frame
invalidates the stream again. Stream state is bounded to 64 codec/stream pairs.

`acip_outbound_snapshot` and browser `snapshot()` distinguish local admission,
backend submission, drops, congestion, recovery frames, application/backend bytes,
and oldest queued video age. Submission is not receiver delivery. Server logging
now calls its counter admission FPS; the live harness measures receiver FPS.

## Verification

Build `ascii-chat`, `test-backpressure`, `test-backpressure-live`, `test-network-events`, and
`test-webrtc-security`. Run the deterministic test before the live matrix:

```
build/bin/test-backpressure
python tests/integration/transport_backpressure.py \
  --binary build/bin/test-backpressure-live --output build/backpressure
```

The live test opens real TCP, libwebsockets and libdatachannel connections. TCP and
WebSocket throttle the receive path to 4 KiB every 64 ms for three seconds.
WebSocket uses a bounded transparent TCP relay and an independent receiver service
thread, preserving the production libwebsockets flow-control behavior after a two-second baseline. WebRTC
advertises a UDP relay as its ICE candidate, verifies the selected remote port,
and paces its datagrams during the same interval. A healthy connection runs
alongside the saturated one. Each case restores capacity for ten seconds, verifies fresh frame
and control delivery, then disconnects with queued data. Audio sequence checks
verify ordering. For both encoded codecs, every keyframe starts a brand-new
FFmpeg decoder; every delivered frame must decode successfully with no decoder corruption flags.
Legal codec filler NAL units pad encoded packets to at least 16 KiB so encoder bitrate
defaults cannot accidentally bypass saturation. Raw packets contain an 80x45 RGB
image. Control pings are interleaved with every generated frame.

CSV records application/backend bytes, oldest queued video age, delivered count,
drops, and the latest receiver-observed frame age. Logs and `summary.json` record
peak queues, mean/max receiver frame age, delivered FPS by phase, healthy-peer FPS,
control delivery, and codec recovery. Decoder delivery is measured here; terminal
presentation is separately covered by `network_fps_windows.py`.

The Windows LGPL FFmpeg bundle may lack a working HEVC encoder. Use
`--codecs raw,h264` there and run the full raw/H.264/H.265 matrix with a software
HEVC-enabled FFmpeg build on Linux/macOS. An unavailable codec is a failed scenario
in the runner, never silently counted as a pass.
