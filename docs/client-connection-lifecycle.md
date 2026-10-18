# Native client connection ownership

`src/client/connection.c` owns one native connection attempt, including dialing,
handshake state, transport/client handle, worker pool, outbound audio queue,
video encoder, audio pipeline, and protocol/snapshot state. Each retry allocates
a fresh owner. Capture devices, media sources, terminal UI, and reconnect policy
belong to the application and may outlive an attempt. WebSocket clients also
retain one application runtime context: libwebsockets 4.5 tears down process TLS
when its last context is destroyed, so an application reference keeps TLS alive
across retries. It is released after the final connection and application media
cleanup. See [the upstream TLS destructor](https://github.com/warmcat/libwebsockets/blob/v4.5-stable/lib/tls/openssl/openssl-tls.c).

Startup moves from STARTING to RUNNING. Acquisition failures, peer disconnects,
and application shutdown all leave through `client_connection_destroy()`.
Startup records initialized synchronization objects separately from workers so
partial queue initialization also unwinds. No "all workers started" flag gates
cleanup.

Workers and ACIP callbacks receive their connection context. Fixed-signature
application/crypto adapters borrow `client_connection_current()`. The controlling
thread publishes that binding before starting transport owners or workers and
clears it only after joining them and quiescing the transport. Signal handlers
publish application exit only; they never dereference the connection binding.

Shutdown publishes STOPPING, interrupts network I/O without taking the send
mutex, and wakes the audio sender before joining workers. Worker join failure
retains the owner and all dependencies; the controlling thread keeps waiting
before allowing application cleanup or reconnect. Queue synchronization,
encoders, audio pipeline, and crypto state are released only after their users
stop. The embedded connection handle remains the sole transport/client owner
throughout startup and teardown.

The audio device context persists, but its pipeline and buffered samples are
reset for each connection. Snapshot teardown interrupts I/O before joining
capture. The former encoder flush allocated and logged buffered output without
transmitting it; teardown no longer runs that operation concurrently with capture.

## Validation

Build with the default Debug preset and run:

```sh
python3 tests/integration/client_connection_lifecycle.py build/bin/ascii-chat
python3 tests/integration/server_client_lifecycle.py build/bin/ascii-chat
python3 tests/integration/server_lifecycle.py build/bin/ascii-chat
```

Use `ascii-chat.exe` on Windows. The new suite also registers as the Debug CTest
`client-connection-lifecycle`. It injects failures after 13 acquisition stages,
repeats partial-startup failures in one process, reconnects one live client three
times over both TCP and WebSocket, and races abrupt peer loss with local interrupt.
It verifies each attempt releases its workers and a fresh attempt starts with
empty protocol state. These cases use test video and software test audio with playback disabled,
exercising the audio pipeline, capture, and sender without webcam or audio hardware.

`ASCII_CHAT_TEST_CLIENT_FAIL_STAGE` selects an acquisition checkpoint in Debug
builds only. It has no effect in release builds.
