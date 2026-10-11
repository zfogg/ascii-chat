# Native event-driven I/O

Native server and discovery-service TCP connections use one libuv event loop per
listener group. The native client adopts its connected socket into a dedicated
event loop before the crypto handshake. Libuv uses epoll on Linux, kqueue on
macOS/BSD, and IOCP on Windows. IOCP reports completed operations; epoll and
kqueue report readiness. `platform/event.h` exposes asynchronous reads and writes
so networking code does not depend on either model.

`lib/platform/event.c` owns handles, wakeups, deadlines, and worker lifetimes.
`lib/network/packet/decoder.c` incrementally assembles ACIP headers and payloads,
validates lengths before allocation, checks CRCs, and emits complete packets.
`lib/network/tcp/async.c` implements the ACIP transport with owned output buffers
and encryption. The server mode owns handshake state and media scheduling.

## Ownership and shutdown

Adoption invalidates the caller's socket. Only the event loop closes it. Reads
borrow a 64 KiB stream buffer; a stream worker pauses reads until its completion
callback. At most one worker runs per stream. Handshakes, packet handlers, SQLite
work, and client cleanup execute outside the event loop in libuv's worker pool.
Render threads remain per client, but the default server TCP path no longer
creates per-client socket receive, dispatch, and send threads.

Writes copy caller buffers before returning and preserve enqueue order. Accepted
writes invoke their completion callback exactly once, including cancellation,
before the stream's close callback. Completion reports a local write, not peer
acknowledgement. A successful ACIP send and the frame-send counter mean admission
to the output queue.
An error after encryption closes the stream because its sequence counter may
already have advanced.

Shutdown stops acceptance, closes streams, cancels pending output, and drains
worker and close callbacks before destroying the loop. External producers must
stop before destroying their transport. The client retains a blocking receive
adapter for application consumers, backed by a condition variable and complete
packet queue; that consumer does not perform socket I/O.

## Bounds and deadlines

- Each stream permits at most 16 MiB or 4096 pending writes, including submitted
  writes. The client receive queue permits 16 MiB or 4096 packets.
- New ASCII frames are skipped while output exceeds 256 KiB. New audio batches
  are skipped above 512 KiB. Already queued encrypted packets are never removed
  or reordered. Control traffic can use the remaining queue capacity; exhaustion
  terminates the connection.
- Each loop admits at most 512 simultaneous streams and 1024 posted commands.
- Media handshakes have a 30-second absolute deadline. Partial application
  packets have a 30-second deadline; receiving another fragment does not extend
  the current packet's deadline.
- Discovery handshakes have a 30-second deadline, partial packets five seconds,
  and idle connections 60 seconds. Discovery payloads are limited to 64 KiB.

## Compatibility boundaries

WebSocket and WebRTC continue to use libwebsockets and libdatachannel's event
loops and their existing ACIP adapters. Browser/WASM builds do not link libuv.
DNS resolution and outbound connection establishment retain the existing
connection code. Outbound discovery requests retain their synchronous adapter.
The explicit `--network-zerocopy` option retains the existing threaded TCP path,
including Linux `MSG_ZEROCOPY` completion handling; the default uses event I/O.
This implementation does not claim a measured throughput improvement.

## Build and verification

Install libuv development files (`libuv1-dev` on Debian/Ubuntu, `libuv` with
Homebrew or vcpkg). The musl build fetches libuv as a static dependency.

```sh
cmake --preset default -B build
cmake --build build --target ascii-chat test-network-events
./build/bin/test-network-events
python3 tests/integration/event_io.py build/bin/ascii-chat
python3 tests/integration/server_lifecycle.py build/bin/ascii-chat
python3 tests/integration/server_client_lifecycle.py build/bin/ascii-chat
```

Use `.exe` executable names on Windows. The standalone test covers fragmented
and coalesced packets, corrupt and oversized input, worker-driven echo, deadlines,
queue saturation, and exactly-once cancellation. Integration tests exercise
encrypted snapshots with stalled peers, reconnects, password, pinned identity and plaintext modes,
WebSocket coexistence, discovery sessions, and graceful shutdown. With
`BUILD_TESTS=ON`, the new tests register as `network-events` and
`event-io-integration` in CTest.
