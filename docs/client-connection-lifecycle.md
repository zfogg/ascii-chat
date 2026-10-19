# Shared connection ownership

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

The reusable state machine lives in `src/common/session/connection.c`. It owns
startup/stop state and the controlling-thread boundary, and enforces interruption,
joining, then release. Mode adapters supply resource-specific operations. Native
client attempts, discovery participants, and discovery WebRTC media workers all
use that sequence. Discovery still coordinates signaling, host control, media
transport switching, and host migration.

Workers, ACIP callbacks, and crypto negotiation receive their connection context
explicitly. There is no current-connection singleton or thread-local replacement.
Each attempt holds its own crypto callbacks and user data. Multiple owners can
coexist on one thread or in one program. Application media devices, capture, and
display are borrowed explicitly; applications must provide distinct resources
when running independent media sessions. The CLI still runs one interactive
session. Signal handlers publish exit/resize requests without walking owners.

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

The Criterion-free `session-connections` test runs two live TCP owners with
blocked receive workers, stops one while checking that the other keeps receiving,
injects a failed join, checks controlling-thread cleanup, and unwinds partial
startup. `participant-media` exercises discovery participant failures, repeated
media lifecycles, transport transfer, recovery, and host-control preservation.
`discovery-session-process` covers production discovery state transitions.
Discovery media teardown also retains its stack dependencies until worker joins
succeed; NAT-worker join failures retain the discovery session for a safe retry.
