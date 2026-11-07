# QUIC transport measurements

These measurements compare the production TCP, WebRTC data channel, and native
QUIC backends with the same synthetic ACIP packet workload. They measure packet
delivery, queueing, and recovery. Audio numbers are packet delivery ages and
arrival gaps, rather than microphone-to-speaker latency; visual numbers stop
when a complete packet reaches the receiver, before decoding or rendering.
The mixed native/browser tests in [quic.md](quic.md) separately verify real
media capture, decoding, and rendering over WebTransport.

## Method

Build `test-transport-metrics` in Release mode with Clang and Ninja, without
coverage or sanitizers. Each pair authenticates a shared password and enables
ACIP encryption. QUIC also verifies a locally generated TLS certificate, and
WebRTC uses DTLS. These setup measurements include password key derivation,
transport establishment, and the application handshake; they exclude discovery,
public DNS, NAT traversal, TOFU prompts, and stable identity key lookup.

One producer sends control packets of 32 bytes every 100 ms, audio packets of
960 bytes every 20 ms, and visual packets of 32 KiB every 33 ms. Payloads carry
test timestamps and sequence numbers. These are scheduling probes rather than
decoded audio/video samples. TCP and WebRTC use the production outbound queue;
QUIC uses its production per-flow scheduler. Reliable QUIC uses separate
control, audio, and visual streams. The datagram variant retains reliable
control and uses negotiated, fragmented datagrams for audio and visual packets,
with the ordinary 100 ms send deadline and bounded reassembly.

Each run produces packets for ten seconds, followed by a 250 ms receive drain.
The first five seconds form the measured load phase. In the impaired profile,
Linux NetEm is installed before connection establishment and removed five
seconds after application readiness. It applies 20 ms delay with 5 ms normal
jitter, 1% packet loss, an 8 Mbit/s rate, a 200-packet queue, and seed 247.
The second five seconds measure recovery on the clean path. Each run has its
own isolated container network namespace; NetEm counters are retained before
removal to prove that the impairment applied. Seeding the emulator does not
make transport scheduling or the complete experiment deterministic.

The receiver records delivery ages by capture phase, including delayed packets
that arrive after impairment removal. An audio interruption is an inter-arrival
gap greater than 60 ms. Recovery is the interval after the five-second boundary
until the first newly generated packet delivered in under 100 ms, separately
for each flow. The removal timestamp is recorded so the small timer offset is
visible. Whole-process user and system CPU come from `wait4`; whole-run wire
bytes come from the isolated loopback interface and include both directions,
handshakes, acknowledgements, and shutdown. Queue samples must remain within
the configured 8 MiB budget. Local queue peaks do not include kernel buffers.

Reproduce in an isolated Linux container with `CAP_NET_ADMIN` and NetEm support:

```sh
cmake --preset default -B build-metrics -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTS=ON -DUSE_MUSL=OFF \
  -DASCIICHAT_ENABLE_SANITIZERS=OFF -DASCIICHAT_ENABLE_COVERAGE=OFF \
  -DASCIICHAT_ENFORCE_STATIC_RELEASE=OFF
cmake --build build-metrics --target test-transport-metrics
python3 tests/benchmarks/transport_metrics.py \
  --binary build-metrics/bin/test-transport-metrics \
  --output build-metrics/measurements --netem --runs 3 \
  --transports tcp,webrtc,quic,quic-datagrams --source-revision "$(git rev-parse HEAD)"
```

The unprivileged `transport-metrics` CTest check runs the four clean variants
with bounded timeouts, validates selected transport and datagram capability,
ordering on reliable flows, delivery in both phases, queue limits, and cleanup.
The NetEm profiles run separately through the benchmark script. The comparison
below uses an optimized build without coverage or sanitizer instrumentation.

## Large fragmented datagrams

The initial 64 KiB stress run delivered audio and control under impairment but
no complete visual packet in its first five seconds. A 64 KiB packet needs many
fragments, all of which must arrive, and cannot wait indefinitely for congestion
to clear. Missing fragments occupy bounded reassembly slots until expiry.
The harness reports zero delivery explicitly and still requires fresh visual
delivery after the path recovers. This is a limit of the tested workload, and
should be considered when choosing reliable delivery or smaller visual packets.

## Results

Collected on November 3, 2026, using Clang 23, `-O3 -DNDEBUG`, and no coverage
or sanitizers. The host was an Intel Core i7-9750H at 2.60 GHz; Docker provided
two CPUs, approximately 4 GiB RAM, and Linux 6.10.14-linuxkit. Each table value
is the median of three runs unless a range or maximum is identified. All 24
32 KiB runs and both 64 KiB stress runs completed successfully.

| Profile | Transport | Setup ms | Audio p95 ms | Audio gaps >60 ms | Control p95 ms | Visual p95 ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Clean | TCP | 145.03 | 2.41 | 0 | 2.35 | 5.29 |
| Clean | WebRTC | 195.09 | 2.32 | 0 | 2.27 | 4.05 |
| Clean | QUIC streams | 171.28 | 2.33 | 0 | 2.26 | 2.39 |
| Clean | QUIC datagrams | 165.11 | 2.33 | 0 | 2.22 | 2.40 |
| Impaired | TCP | 399.84 | 130.81 | 17 | 109.27 | 134.19 |
| Impaired | WebRTC | 528.76 | 332.90 | 46 | 319.92 | 371.73 |
| Impaired | QUIC streams | 583.30 | 53.99 | 3 | 39.61 | 2682.06 |
| Impaired | QUIC datagrams | 636.21 | 37.61 | 0 | 35.89 | See below |

| Profile | Transport | Wire MB, whole run | CPU seconds, whole run | Maximum sampled queue bytes |
| --- | --- | ---: | ---: | ---: |
| Clean | TCP | 10.17 | 0.89 | 233804 |
| Clean | WebRTC | 11.38 | 1.48 | 168268 |
| Clean | QUIC streams | 10.89 | 1.06 | 68208 |
| Clean | QUIC datagrams | 11.17 | 1.12 | 68000 |
| Impaired | TCP | 9.16 | 0.82 | 304820 |
| Impaired | WebRTC | 7.77 | 1.26 | 249376 |
| Impaired | QUIC streams | 10.87 | 1.61 | 7281920 |
| Impaired | QUIC datagrams | 7.44 | 1.44 | 264776 |

| Transport | Audio recovery ms | Visual recovery ms |
| --- | ---: | ---: |
| TCP | 229.27 | 250.26 |
| WebRTC | 225.80 | 228.14 |
| QUIC streams | 39.39 | 102.30 |
| QUIC datagrams | 98.60 | 164.99 |

QUIC streams kept control and audio responsive while the visual stream built
up a substantial backlog. The 2.68-second visual p95 is a material tradeoff:
independent streams do not eliminate congestion or stale reliable visual bytes
that have already been admitted. All sampled queues remained below 8 MiB.

The impaired datagram runs completed only 2, 0, and 0 visual packets generated
in the load phase. The two delivered packets had a p95 age of 119.46 ms; no
median across runs is meaningful. Audio continued, but large fragmented visual
datagrams were unsuitable for this workload and deadline. Fresh visual packets
returned after impairment removal in all runs. The separate 64 KiB stress run
likewise completed no visual packet in the impaired phase, then delivered 141
packets generated in recovery, with its first fresh visual packet at 210.95 ms.
Its audio p95 was 98.11 ms, with one arrival gap over 60 ms. NetEm recorded 33
dropped packets; the fixture sampled a peak queue of 400000 bytes in that run.

These are observations from one host and workload. Wire totals include all
traffic and vary with admitted or discarded media; a lower total does not
establish better efficiency. Setup and CPU include key derivation, and the
first clean TCP setup was a cold-run outlier at 4232.67 ms. The median retains
all three samples rather than removing it. NetEm removal occurred 4.63–10.93 ms
after the nominal boundary in the 32 KiB runs, so recovery measurements include
that offset. Browser WebTransport, wide-area paths, devices, codecs, and renderer
CPU require separate measurements before making end-to-end quality claims.
