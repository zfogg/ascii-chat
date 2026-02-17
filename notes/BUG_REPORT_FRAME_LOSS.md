# Bug Report: Frame Loss in WebSocket Client Mode

**Date:** 2026-02-17
**Severity:** HIGH
**Status:** BUGS FOUND AND FIXED — INVESTIGATING REMAINING STALL
**Component:** WebSocket Server / LWS Implementation (`lib/network/websocket/server.c`, `transport.c`)

## Executive Summary

The server displays the same ASCII frame for 250-400ms intervals when serving web browser clients over
WebSocket. The browser sends video frames at ~30 FPS but the server only receives unique frames at ~3-4
FPS. The remaining frames queue up and are superseded.

**The bug is in how we use libwebsockets, not in TCP or data volume.** The native TCP client sends the
exact same 921KB frames at 60 FPS without issues — TCP can handle this fine. Something about our LWS
server implementation causes each 921KB WebSocket message to take ~190ms to receive, even on loopback.

## Bugs Found and Fixed

### Bug 1: Thread-safety violation in `websocket_send()` (`transport.c`)

`websocket_send()` called `lws_callback_on_writable(ws_data->wsi)` from a non-service thread. Per LWS
docs, this is not thread-safe and can corrupt LWS internal state. Only `lws_cancel_service()` is safe
to call from threads other than the service thread.

**Fix:** Removed the call. `lws_cancel_service()` (already there) wakes the service loop, which then
calls `lws_callback_on_writable_all_protocol()` safely from `LWS_CALLBACK_EVENT_WAIT_CANCELLED`.

### Bug 2: `lws_service(ctx, 0)` does not mean non-blocking (`server.c`)

Passing `0` to `lws_service()` does NOT produce a non-blocking `poll(timeout=0)`. In LWS source:
```c
if (timeout_ms < 0) timeout_ms = 0;    // only negative gives poll(0)
else timeout_ms = LWS_POLL_WAIT_LIMIT; // positive gets replaced with ~23 days
```
The actual poll timeout is then driven by LWS's internal SUL timer system, not the argument.

**Fix:** Changed to `lws_service(ctx, -1)` for true non-blocking poll.

### Bug 3: Crash from `LWS_CALLBACK_FILTER_NETWORK_CONNECTION` handler (`server.c`)

Attempted to set TCP socket options at this callback stage. The WSI/socket is not in a state where
`lws_get_socket_fd()` is valid yet, causing a server crash ("CRASH DETECTED").

**Fix:** Removed the handler. Moved socket option setup to `LWS_CALLBACK_ESTABLISHED` where the socket
is ready.

### Bug 4: `LWS_WITHOUT_EXTENSIONS=ON` in both LWS builds (`cmake/`)

Both the system LWS package (`/usr/lib/libwebsockets.so.20`) and the musl FetchContent build were
compiled with `LWS_WITHOUT_EXTENSIONS=ON`. This silently disabled all WebSocket extensions including
`permessage-deflate`. The server code that sets `info.extensions` was being ignored.

**Fix:** Updated `cmake/dependencies/Libwebsockets.cmake` to build LWS from source (v4.5.2) instead of
using the system package, with `-DLWS_WITHOUT_EXTENSIONS=OFF -DLWS_WITH_ZLIB=ON
-DLWS_WITH_BUNDLED_ZLIB=ON`. Updated `cmake/dependencies/MuslDependencies.cmake` with the same flags.

### Bug 5: TCP socket options not set correctly (`server.c`)

`TCP_QUICKACK` and `TCP_NODELAY` were not set on WebSocket connections. Linux resets `TCP_QUICKACK`
after each ACK, so it must be re-enabled per fragment.

**Fix:** Set `TCP_QUICKACK` + `TCP_NODELAY` in `LWS_CALLBACK_ESTABLISHED`. Re-set `TCP_QUICKACK` on
every `LWS_CALLBACK_RECEIVE` call. Removed manual `SO_RCVBUF` — setting it disables Linux TCP
autotuning and locks the buffer at `rmem_max * 2` instead of allowing the kernel to scale freely.

## Fragment Timing (from `[WS_FRAG]` instrumentation)

After all fixes above, one 921KB frame still takes ~188ms to assemble:

```
#1–4:    0–1ms,     131KB
#5–7:    4–5ms,     131KB
#8–10:   33–34ms,   131KB  ← ~30ms gap
#11–13:  66–69ms,   131KB  ← ~30ms gap
#14–16:  99–104ms,  131KB  ← ~30ms gap
#17–19:  128–133ms, 131KB  ← ~30ms gap
#20–22:  159–171ms, 131KB  ← ~30ms gap
#23:     188ms,     4.7KB
Total: 188ms
```

The ~30ms gaps between each 131KB batch are the remaining unexplained stall. The native TCP client does
NOT have this problem with the same data — so this is a bug in our LWS usage, not a hardware or TCP
limitation. Computers trivially handle 921KB of data over TCP. The gaps are caused by something in how
our LWS server code handles data receipt.

## Per-Stage Timing (from `[WS_TIMING]` instrumentation)

### Browser Side

```
serialize=7.5ms  encrypt=2.5ms  wrap=7.7ms  ws_send=3.0ms  total=~21ms  size=921708 bytes
```

### Server Side

```
LWS assembly:    ~188ms      (cause of the bug — gaps between 131KB batches)
websocket_recv:  ~1ms        (condition variable — fixed in previous session)
Decrypt:         ~1ms        (libsodium on 921KB — fast)
on_image_frame:  ~1–3ms      (hash + memcpy — fast)
```

## Disproved Theories

| Theory | Why It Was Wrong |
|--------|-----------------|
| Data volume too large for TCP | Native TCP client handles identical 921KB at 60 FPS |
| LWS event loop starvation | First frame: 0 WRITEABLE callbacks interleaved during assembly |
| `lws_service(0)` blocking on timers | Changing to `-1` did not change the ~30ms gaps |
| SO_RCVBUF too small | Increasing it made no difference; was actually disabling autotuning |
| Server processing speed | Decrypt + callback is ~2ms — not the bottleneck |
| Recv queue polling latency | Fixed (cond_timedwait), confirmed ~1ms pickup |

## Cross-Mode Comparison

| Mode | Transport | FPS | Bug? |
|------|-----------|-----|------|
| Mirror (web WASM) | Local, no network | ~60 FPS | No |
| Client (native terminal) | TCP socket, 921KB/frame | ~60 FPS | No |
| Client (web WASM via server) | WebSocket, 921KB/frame | **~4 FPS** | **YES** |

The native TCP path uses blocking `recv()` in a dedicated thread — the simplest possible I/O pattern.
The WebSocket path uses LWS's event loop. Something about how we drive that event loop causes 30ms stalls
between every 131KB of received data.

## Open Investigation

What causes the ~30ms gaps between 131KB batches in LWS receive?

Candidates to investigate:
- LWS internal flow control / backpressure mechanism being triggered
- Something in our callback code that blocks the event loop between service calls
- LWS RX buffer / `rx_buffer_size` interaction with large messages
- Whether LWS is doing something unexpected with `TCP_CORK` or kernel-level buffering on send side
  that affects Chrome's ACK behavior

## Files Modified

- **`lib/network/websocket/server.c`** — LWS event loop, TCP socket options, permessage-deflate extensions
- **`lib/network/websocket/transport.c`** — Thread-safety fix (removed lws_callback_on_writable from send thread)
- **`cmake/dependencies/MuslDependencies.cmake`** — LWS build flags: extensions + zlib enabled
- **`cmake/dependencies/Libwebsockets.cmake`** — Build LWS from source with extensions for dev builds

## Instrumentation

```bash
# View all timing data
./build/bin/ascii-chat --log-level info server --grep "/WS_TIMING|WS_FRAG|WS_SOCKET/i"
```

| Log Tag | Location | Measures |
|---------|----------|---------|
| `[WS_FRAG]` | `server.c` LWS_CALLBACK_RECEIVE | Per-fragment timing: size, offset, gap since first fragment |
| `[WS_TIMING]` | `server.c` LWS_CALLBACK_RECEIVE | Full assembly: total bytes, fragment count, WRITEABLE interleaves |
| `[WS_TIMING]` | `transport.c` websocket_recv | Poll wait time and dequeue timestamp |
| `[WS_TIMING]` | `acip/server.c` | Decrypt time and handler dispatch time per packet type |
| `[WS_SOCKET]` | `server.c` LWS_CALLBACK_ESTABLISHED | Actual SO_RCVBUF/SO_SNDBUF values |

## E2E Test Results

```
Performance test: FPS = 27-28 (expected >= 30) — FAILED
Connection test: PASSED
```
