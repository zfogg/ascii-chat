# SCTP accounting review follow-up

The review identified that libdatachannel raises SCTP SO_SNDBUF to at least the
local maximum message size. The earlier WebRTC rows reserved 64 KiB while the
library's default message limit imposed a 256 KiB floor. Those historical rows
under-accounted retained SCTP capacity by 192 KiB; they are not current hard-bound
verification. They remain preserved with their original source fingerprints.

Commit `e93f254a9` explicitly sets rtcConfiguration.maxMessageSize to the same
64 KiB value as rtcSctpSettings.sendBufferSize and the outbound reservation.
Both native endpoints assert the 64 KiB value in generated SDP and the negotiated
channel limit. The crypto fixture separately advertises a 256 KiB peer receive
limit, verifies the effective send limit remains 64 KiB, and successfully
exchanges ACIP packets larger than 64 KiB in all eight crypto/auth scenarios.
This exercises existing packet splitting and reassembly with asymmetric limits.

All eight repeated WebRTC saturation/recovery cases passed:

| Platform | Codec | Peak accounted queue KiB | Final delivered FPS | Final mean frame age ms | Recovery keyframes |
|---|---|---:|---:|---:|---:|
| Windows | raw | 118.5 | 56.50 | 3.58 | 0 |
| Windows | h264 | 378.5 | 33.50 | 8.74 | 31 |
| Linux | raw | 115.9 | 46.50 | 21.02 | 0 |
| Linux | h264 | 377.2 | 56.50 | 3.57 | 18 |
| Linux | h265 | 425.2 | 43.50 | 32.56 | 17 |
| macOS | raw | 115.9 | 57.50 | 0.94 | 0 |
| macOS | h264 | 374.1 | 57.00 | 1.70 | 22 |
| macOS | h265 | 375.0 | 57.50 | 3.75 | 23 |

All three systems tested native source SHA256
`c12291d035c64a81f52c20289efa8ebe6543bd2ba42dc423076aab6b0d712f8f`.
Queue ownership tests and all eight crypto/auth scenarios passed on each system.
CSV archives and summaries retain the same fields and workload as the original
matrix; only the SCTP buffer/message configuration and regression assertions
changed. Windows tests raw/H.264; Linux/macOS also test H.265.

The original TCP/WebSocket measurements remain applicable to those unchanged
transports. These follow-up WebRTC measurements supersede the old SCTP
reservation accounting. Receiver age can include a picture already committed to
the network during the throttle; final-window ages and full-recovery mean/max
ages are recorded separately.
