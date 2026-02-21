# WebSocket Fragmentation Crash Analysis

**Issue**: SIGABRT crash during multi-fragment WebSocket transmission

## Reproduction Steps

1. Run `scripts/test-websocket-server-client.sh`
2. Server starts and WebSocket server initializes
3. Client connects and completes handshake
4. Client sends capabilities packet (26 bytes) - succeeds
5. Client attempts to send large video frame (230+ KB)
6. Frame is fragmented into 4096-byte chunks
7. Fragment 1 sends successfully
8. **Fragment 2 causes SIGABRT crash**

## Root Cause

The crash occurs in libwebsockets' `rops_handle_POLLIN_ws()` which asserts a failure condition during frame processing. The backtrace shows:

```
Signal 6 (SIGABRT)
  ↓
rops_handle_POLLIN_ws()          (libwebsockets frame handling)
  ↓
websocket_send()                 (ascii-chat: lib/network/websocket/transport.c)
  ↓
lws_write() returning 4096       (First fragment OK)
  [State not properly managed]
  ↓
lws_write() for Fragment 2       (Fails assertion)
```

## Key Evidence from Logs

**Fragment 1 Success**:
```
[19:29:17.407846] WEBSOCKET_SEND: lws_write returned 4096 (requested 4096 bytes)
[19:29:17.418... Fragment 1 SUCCESS - sent 4096 bytes (total progress: 4096/230446)
```

**Fragment 2 Crash**:
```
[19:29:17.422565] Fragment 2 - offset=4096, chunk=4096 bytes, flags=LWS_WRITE_CONTINUATION
[19:29:17.424751] ERROR: CRASH DETECTED - Signal 6 (SIGABRT)
```

## Associated Issues

1. **Session String Validation**: Random generation creates 4-char strings (valid range: 5-47)
2. **Server State Transmission**: "Failed to send initial server state: Invalid parameter"
3. **Thread Cleanup**: Threads don't terminate cleanly, require force-kill after 5 retries
4. **Lock Contention**: Multiple locks held 2-40x longer than threshold

## Investigation Notes

- The `websocket_send()` function in `lib/network/websocket/transport.c` manages fragmentation
- Each fragment is sent via `lws_write()` with different flags:
  - Fragment 1: `LWS_WRITE_BINARY` with `start=1, final=0`
  - Fragment 2+: `LWS_WRITE_CONTINUATION` with `start=0, final=0`
  - Final: `LWS_WRITE_CONTINUATION` with `final=1`
- The issue appears to be in state management between fragment calls

## Next Steps

1. Review `lib/network/websocket/transport.c:websocket_send()` fragmentation logic
2. Check libwebsockets version and API compliance
3. Verify buffer lifecycle and ownership
4. Test with smaller payloads to find threshold
5. Add more detailed logging around `lws_write()` calls
