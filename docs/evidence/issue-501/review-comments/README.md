# PR review corrections

This working-tree run follows merge `cb920b5a2` and tests the admission timestamp
under the queue mutex, closed WebRTC channel failure propagation, and reliable
browser-host congestion handling. The summary records the native source and
binary SHA256 fingerprints. The archive contains all attempted CSV/log files.

The native queue test (including concurrent producers), all 188 browser tests,
TypeScript, and targeted format/lint checks passed. The browser regression verifies
that frame congestion retains a peer but reliable congestion removes only that
peer, leaving the healthy peer and signaling connected. Both raw/H.264 WebRTC
runs also require a closed channel to report ERROR_NETWORK within two seconds.
The source-SHA lookup was checked from outside the repository and with missing
Git, command failure, and timeout; all fallback cases passed.

| Transport | Codec | Peak accounted queue KiB | Final delivered FPS | Final mean frame age ms | Recovery keyframes |
|---|---|---:|---:|---:|---:|
| tcp | raw | 182.9 | 51.00 | 7.25 | 0 |
| tcp | h264 | 441.7 | 39.50 | 21.62 | 33 |
| websocket | raw | 178.9 | 46.50 | 13.23 | 0 |
| websocket | h264 | 438.7 | 52.50 | 16.94 | 31 |
| webrtc | raw | 139.9 | 50.00 | 5.76 | 0 |
| webrtc | h264 | 378.0 | 31.50 | 10.87 | 23 |

All six supported Windows raw/H.264 scenarios passed. H.265 attempts on all three
transports failed during hevc_amf encoder warmup before the workload; the WebRTC
attempt also crashed during fatal-exit cleanup. The runner therefore exited 1.
These are validation limits, not passing H.265 rows. Earlier Linux/macOS H.265
results remain historical evidence; this follow-up did not rerun those systems.

These measurements ran alongside builds and do not establish a 60 FPS guarantee.
Final-window ages differ from full-recovery ages: a picture already handed to the
network before throttling can arrive later. The JSON/CSV retain both measurements.
