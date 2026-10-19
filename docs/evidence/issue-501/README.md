# Issue 501 saturation and recovery evidence

The [SCTP review follow-up](sctp-review/README.md) contains the current WebRTC
measurements. The original WebRTC rows below under-accounted SCTP capacity by
192 KiB because libdatachannel raised its send buffer to the default maximum
message size. The follow-up fixes that configuration and repeats all WebRTC
cases with matching regression assertions.

These are receiver measurements from real native TCP, libwebsockets, and
libdatachannel connections, not sender admission counts. Each case runs a
2-second baseline, 3-second paced saturation, and 10-second recovery while a
separate healthy connection remains active. The final two recovery seconds
must have mean frame age below 250 ms. CSV files retain queue bytes, queued
frame age, receiver frame age and cumulative delivered frames throughout.

| Platform | Transport | Codec | Peak accounted queue KiB | Final delivered FPS | Final mean frame age ms | Recovery keyframes |
|---|---|---|---:|---:|---:|---:|
| Windows | tcp | raw | 182.4 | 7.50 | 8.25 | 0 |
| Windows | tcp | h264 | 454.7 | 44.50 | 22.53 | 29 |
| Windows | websocket | raw | 177.6 | 39.50 | 17.93 | 0 |
| Windows | websocket | h264 | 453.4 | 26.00 | 39.21 | 25 |
| Windows | webrtc | raw | 127.3 | 54.00 | 5.18 | 0 |
| Windows | webrtc | h264 | 375.4 | 49.00 | 8.51 | 40 |
| Linux | tcp | raw | 207.4 | 59.00 | 3.58 | 0 |
| Linux | tcp | h264 | 475.2 | 58.00 | 4.51 | 21 |
| Linux | tcp | h265 | 499.7 | 54.00 | 7.81 | 20 |
| Linux | websocket | raw | 200.4 | 57.50 | 4.36 | 0 |
| Linux | websocket | h264 | 466.9 | 57.50 | 5.61 | 23 |
| Linux | websocket | h265 | 470.4 | 59.00 | 7.77 | 24 |
| Linux | webrtc | raw | 109.8 | 57.50 | 1.22 | 0 |
| Linux | webrtc | h264 | 384.6 | 58.00 | 2.84 | 21 |
| Linux | webrtc | h265 | 382.9 | 58.00 | 6.46 | 21 |
| macOS | tcp | raw | 222.3 | 56.50 | 5.12 | 0 |
| macOS | tcp | h264 | 497.0 | 57.50 | 5.92 | 20 |
| macOS | tcp | h265 | 494.0 | 58.00 | 9.32 | 20 |
| macOS | websocket | raw | 206.5 | 57.00 | 4.57 | 0 |
| macOS | websocket | h264 | 493.5 | 57.00 | 5.82 | 20 |
| macOS | websocket | h265 | 495.7 | 57.00 | 8.67 | 20 |
| macOS | webrtc | raw | 110.3 | 57.50 | 1.09 | 0 |
| macOS | webrtc | h264 | 378.9 | 56.50 | 1.71 | 25 |
| macOS | webrtc | h265 | 378.9 | 56.50 | 6.02 | 25 |

Peak queues include the measured application/backend bytes and fixed socket/SCTP
reservation. The hard accounted limit is 8192 KiB per connection. The CSV's
`oldest_queued_frame_ms` covers application-owned video; receiver age also
includes committed bytes in the kernel, receiver, and network. Full recovery
mean/max ages in each JSON deliberately retain the old pictures delivered
immediately after the throttle is removed. Final FPS is decoder/raw-packet
delivery, not terminal presentation.

All H.264/H.265 pictures were decoded and checked for corruption. Every IDR
recreated the decoder, proving recovery parameter sets and references were
self-contained. Each encoded case required fresh keyframe requests after drops.
Raw frames are 80x45 RGB; encoded pictures are 160x90, padded with legal filler
NAL units to at least 16 KiB. TCP/WebSocket permit 4 KiB per 64 ms during the
throttle; WebRTC uses a paced loopback UDP relay, with selected ICE port checked.

The source produces up to 60 FPS. `source` in each summary records actual
production rate; the healthy connection must deliver at least 90% of generated
frames during saturation. This avoids attributing source scheduling delays to
another connection. Queue allocations must return to zero after disconnect with
pending output.

Windows uses its LGPL FFmpeg bundle and tests raw/H.264; its HEVC hardware
encoder is unavailable. Linux and macOS cover HEVC with software encoders.
Each JSON records platform, binary SHA256 and a normalized native-source SHA256.
The source fingerprint excludes generated version.h and normalizes line endings.

Reproduce with the default Ninja/Clang preset in Dev mode, then:

```
cmake --build build --target ascii-chat test-backpressure test-backpressure-live test-network-events test-webrtc-security
build/bin/test-backpressure
build/bin/test-network-events
build/bin/test-webrtc-security
python tests/integration/transport_backpressure.py --binary build/bin/test-backpressure-live --output build/backpressure
# Add --codecs raw,h264 on the Windows LGPL bundle.
```

Additional validation: all 185 browser tests passed, with the final deadline
change rechecked by the 7 admission tests; TypeScript checking and the Vite
production build passed. The mirror/client WASM targets were rebuilt and their
published manifests verified. Native event/lifecycle and WebRTC crypto fixtures
were run separately; see the PR validation notes for any platform-specific limits.

## Scope and build identity

The 24 carrier cases test native source from commit `bba3955c1`, with matching
normalized source SHA256 `d545a733c4f89f037d141330f265e86f98491df2ae6ea2f41924a782e681c24f`
on all three systems. A subsequent test-only change explicitly binds the
separate crypto fixture to loopback; that fixture then passed all eight modes
on Windows, Linux, and macOS. Carrier implementation and workload are unchanged.

Native queue, event I/O, and crypto fixtures pass on all three systems. The full
native application was built and `--version` loaded successfully on each system.
Windows reuses its installed dependency cache; Linux uses
`ascii-chat-tests-local:latest`; macOS uses the installed Clang toolchain.

These are freshness/dependency/ownership recovery checks, not a 60-FPS throughput
certification. The Windows TCP raw final window measured only 7.5 delivered FPS,
while delivered frames averaged 8.25 ms old; that low rate is retained above.
The runner records actual source and healthy-peer rates rather than treating
admission or source rate as delivered FPS. Larger-resolution or sustained
throughput requirements need separate measurements.
