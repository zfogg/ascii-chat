# Network copy reduction

Issue [#36](https://github.com/zfogg/ascii-chat/issues/36) is implemented through a
portable scatter/gather path and an automatic Linux kernel-copy-avoidance path. The
ACIP wire format, encryption coverage, nonce generation, compression format, and
CRC32-C polynomial are unchanged.

## Implementation

| Work item | Implementation |
| --- | --- |
| Baseline and measurement | Native correctness/benchmark executable, reconstructed assembly baseline, process CPU measurements, send/completion counters, repeatable JSON output |
| Portable scatter/gather | `socket_buffer_t`, `socket_sendv`, `socket_sendv_all`; Linux/macOS `sendmsg`, Windows synchronous `WSASend` |
| Transport and media integration | ACIP payload vectors; PCM, Opus, ASCII-frame and raw-image slices; one gather for encryption; encrypt directly into final owned wire storage |
| Reception | Transfer the existing decrypted allocation, removing the normalization copy; validate framing before allocating payloads; tolerate zero-copy completion readiness on the receive side |
| Validation | Native Windows, Docker/Linux, and macOS tests; encrypted exchanges between each OS pair; existing Linux transport, CRC, compression and crypto suites |
| Linux kernel copy avoidance | `SO_ZEROCOPY`/`MSG_ZEROCOPY`, completion consumption, bounded mapping lifetime, cancellation, copied-completion detection, unsupported/resource-pressure fallback |

The original basic packet sender already avoided concatenating the packet header
and payload, but used two separate send operations. It now submits both slices
together. ACIP previously concatenated them, and its audio/video helpers also
assembled media headers and payloads. Those intermediate media copies are removed.
Incremental CRC32-C permits checksumming slices without assembling them.

Unencrypted TCP borrows the slices until the synchronous send completes. Encrypted
TCP gathers the inner packet once because the existing authenticated encryption
API requires contiguous input. It writes ciphertext directly after space reserved
for the outer header, eliminating the separate ciphertext-to-wire-buffer copy.
Existing mixer, encoder, compressor and buffer-pool outputs remain usable; network
I/O is not moved into an audio callback. WebSocket and WebRTC use a contiguous
fallback and retain their existing queue ownership and message boundaries.

Vectored I/O removes application assembly copies; it does not itself eliminate
the ordinary kernel copy. Ordinary anonymous mappings do not eliminate receive
copies either. TCP is a byte stream, so receiving vectors or `recvmmsg` entries
cannot be interpreted as individual ACIP packets. The receive path therefore
keeps explicit header/payload framing rather than introducing packet batching.
File-transfer APIs (`sendfile`, `splice`, `TransmitFile`) do not apply to this
transformed, encrypted in-memory media path.

## Deadlines and ownership

- One deadline covers every partial write and retry of a vectored packet.
- The TCP transport's send mutex covers encryption/nonce allocation and the
  complete send, including partial writes and Linux completion processing.
- A failure after transmission may have started terminates the stream. Retrying
  a whole partially written packet would corrupt framing.
- macOS and Windows also bound the underlying send syscall using the remaining
  `SO_SNDTIMEO`, then restore the original timeout. The Mac blocked-peer test
  exposed a large `sendmsg(MSG_DONTWAIT)` stalling against a tiny socket buffer;
  polling alone was insufficient. Socket blocking mode is preserved.
- Library callers using raw packet/socket APIs must serialize complete packets;
  a TCP send syscall is not an application-packet atomicity guarantee.

## Linux kernel zero-copy

Copy avoidance is automatic; there is no CLI or environment switch. Native TCP
streams submit large owned buffers through the event loop. Linux completion
checks are nonblocking, and unsupported or copied submissions fall back within
the same event backend. See [event-driven I/O](event-driven-io.md) for ownership,
deadlines, and shutdown behavior. Portable copy reductions are always active.

TCP wire buffers at least 64 KiB are eligible. Linux allocates private mappings
for those owned buffers. The synchronous utility adapter encrypts directly into
them; the native event adapter copies each queued wire packet into owned storage. One submission is outstanding at a time, with an allocation ceiling of
32 MiB; the protocol/crypto limits impose smaller practical packet sizes. The
sender consumes the socket's IPv4/IPv6 zero-copy error-queue completion before
submitting again. This avoids an unbounded pinned-memory queue and cookie ordering
or wraparound assumptions. The sender must exclusively own zero-copy submissions
and error-queue consumption on that socket.

Unsupported kernels and resource pressure fall back to ordinary sends. After a
copied completion, the unsent remainder is sent normally and future zero-copy
attempts on that transport are disabled. Already-sent bytes are never resubmitted.
On timeout/disconnect the stream is shut down and the mapping discarded with
`munmap`, never overwritten or recycled by a userspace allocator. Kernel references
keep any still-pinned physical pages alive independently of the mapping.

The [Linux kernel documentation](https://docs.kernel.org/networking/msg_zerocopy.html)
describes completion ownership and copied notifications. Docker loopback and the
tested Docker Desktop/SSH routes returned copied completions. These tests validate
submission, completion and fallback; they do not establish a real-NIC zero-copy
speedup. Small audio packets never request kernel zero-copy.

## Reproduce

```sh
cmake --preset default -B build -DBUILD_TESTS=ON
cmake --build build --target test-network-vectors
ctest --test-dir build -R '^network-vectors$' --output-on-failure
python3 tests/benchmarks/network_copy_benchmark.py \
  build/bin/test-network-vectors --runs 3 --output network-results.json
```

On Windows use `python` and the `.exe` binary. CTest configures vcpkg DLL paths;
direct benchmark invocation also needs the dependency DLL directories on `PATH`.
The test does not require Criterion and links the production library.

The suite verifies CRC continuity, empty slices, length rejection, short writes,
blocked-peer deadlines, malformed headers/CRCs, partial-header disconnects,
concurrent senders, full-duplex encrypted traffic, maximum sizes, ownership
transfer, media payload compatibility, message-transport fallback, and Linux
zero-copy cancellation. Round trips exercise kernel zero-copy explicitly; the application automatically
attempts copy avoidance for eligible writes and falls back within the same event
backend when unsupported or when the kernel reports copied completions.

For cross-host checks, run `test-network-vectors --serve PORT` and
`test-network-vectors --peer PORT IPV4`. This test-only listener accepts one
connection with a 60-second accept deadline, exchanges ephemeral keys, then verifies
encrypted payloads in both directions. The Mac exchanges were run through an SSH
tunnel; Windows/Linux used Docker Desktop's host route.

The benchmark alternates assembled/vectored processes, three runs per mode. Each
process sends 100 packets per size (0, 256, 4096, 65536, and 1048554 bytes), both
plaintext and encrypted. A 4 KiB send buffer forces partial writes. Every received
payload is checked. The assembled baseline reconstructs the prior ACIP/TCP assembly
copies, using the same current socket helper and receive path; it is not a benchmark
of the entire historical application. Results include process CPU seconds,
throughput, send latency percentiles, syscall attempts, and zero-copy completions.
CPU includes sender/receiver threads, crypto setup and logging. These are synthetic
packet measurements, not end-to-end audio latency or memory-bus measurements.

## Validation results (2026-10-10)

All three native test runs passed before opening the PR:

- Windows x64 on the development machine, Clang 23 Debug build.
- Linux x86_64 in Docker (`ascii-chat-tests-local:latest`), Clang 23.1.1 Debug build.
  Existing TCP transport, CRC32, compression and crypto-network suites also passed.
- Intel MacBook (`loomen@zachbook-pro`), Clang 23.1.2 Debug build with sanitizers.

Encrypted bidirectional exchanges passed Windows/macOS, Linux/macOS and
Linux/Windows. The Linux peers recorded matching submissions/completions and the
copied-completion fallback. Unrelated remote/local checkouts were preserved.

Representative medians from three runs per mode:

| Environment | CPU seconds, assembled → vectored | Encrypted 4 KiB p95 µs, assembled → vectored | Encrypted ~1 MiB MiB/s, assembled → vectored |
| --- | ---: | ---: | ---: |
| Windows | 3.297 → 3.078 | 180.2 → 142.8 | 74.22 → 74.85 |
| Docker/Linux | 4.166 → 4.002 | 273.0 → 258.5 | 64.33 → 67.35 |
| macOS | 4.450 → 4.287 | 318.3 → 229.3 | 54.56 → 56.50 |

The measurements support lower overall CPU cost in this workload, but throughput
and tail latency vary; earlier Windows samples also showed lower large-packet throughput.
They do not substantiate the issue's projected universal percentage improvements.
Kernel zero-copy is attempted automatically for eligible writes. These routes
required copied completions, which disable further copy-avoidance attempts on
that connection while ordinary writes continue through the same event backend.
