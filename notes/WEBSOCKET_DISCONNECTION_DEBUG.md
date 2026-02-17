# WebSocket Client Disconnection Issue - Debug Notes

## Status
WebSocket browser clients receive 0 FPS and disconnect immediately after connection establishment (within ~100ms of first data fragment).

## Root Cause Identified
- LWS_CALLBACK_CLOSED fires immediately after first WebSocket fragment is received
- Connection closes at the LWS/WebSocket protocol layer, not due to our code
- The handshake and initial setup completes successfully
- Clients can send CLIENT_CAPABILITIES, but connection drops before server can send frames

## Fixes Applied
1. **Commit 04fccc2c**: Added blocking wait in websocket_client_handler to keep handler thread alive while client is connected (matches TCP handler pattern)
   - This ensures handler thread doesn't exit prematurely
   - But doesn't fix the disconnection itself

2. **Commit 200c2ed1**: Skip remote logging for WebSocket clients (socket != INVALID_SOCKET_VALUE checks)
   - Fixed "Invalid socket descriptor" errors that were interrupting handlers
   
3. **Previous commit 69f40701**: Enabled permessage-deflate compression (RFC 7692)
   - Already working correctly, reduces frame size from 921KB → 50-100KB

## Investigation Results

### Symptoms
- Initial WebSocket connection succeeds (LWS_CALLBACK_ESTABLISHED fires)
- Handler thread: add_webrtc_client(), crypto_handshake, start_webrtc_client_threads all succeed
- Receive thread: calls websocket_recv() and blocks waiting for data
- Browser: sends CLIENT_CAPABILITIES packet
- Server: receives fragment, processes partial message
- **~100ms later**: LWS_CALLBACK_CLOSED fires unexpectedly
- Transport is_connected set to false
- Receive thread: websocket_recv() returns "Connection closed while waiting for data"

### Timeline Example (from server logs)
```
[15:16:42.094004] WS_FRAG #1 received: +18446744073709.5ms, 1024 bytes (first=1 final=0)
[15:16:42.094923] LWS_CALLBACK_CLOSED WebSocket client disconnected
[15:16:42.104658] websocket_recv(): Connection closed error
[15:16:42.739188] ACIP receive/dispatch failed for client (disconnecting)
```

## Likely Causes to Investigate

1. **LWS Protocol Violation**
   - We may be sending packets that violate WebSocket RFC
   - Check packet headers, fragmentation flags, extensions handling
   - Large timestamp values in logs suggest potential calculation issues

2. **Multi-Fragment Message Handling**
   - First fragment arrives with `first=1, final=0` (incomplete message)
   - Something about our fragment buffering may trigger LWS close
   - Check websocket_server_callback RECEIVE fragment handling

3. **LWS Configuration/Timeout**
   - LWS may have inactivity or per-connection timeouts
   - permessage-deflate extension may not be initialized correctly
   - Need to verify lws_set_timeout usage

4. **Race Condition in Handler**
   - Handler thread might be interfering with LWS event loop
   - Blocking wait loop could cause lock contention
   - Need to verify thread safety of handler thread creation

## Recommended Next Steps

### High Priority
1. **Trace LWS events more carefully**
   - Add logging for ALL LWS callbacks not just the ones we handle
   - Check for LWS_CALLBACK_CLOSED reason codes
   - Monitor for any error callbacks (LWS_CALLBACK_CLIENT_CONNECTION_ERROR, etc.)

2. **Debug Fragment Processing**
   - The first fragment triggers the disconnect
   - Verify fragment_buffer allocation and state machine
   - Check if ringbuffer operations are thread-safe

3. **Check Protocol Compliance**
   - Verify WebSocket frame structure matches RFC 6455
   - Check permessage-deflate extension handling
   - Ensure no protocol violations in packet assembly

### Medium Priority
1. **Implement Async Message Dispatch** (mentioned in bug report as solution)
   - Current bottleneck: receive thread blocked in acip_server_receive_and_dispatch
   - Solution: Queue received packets and process asynchronously
   - This prevents backpressure from blocking WebSocket receive

2. **Profile Handler Execution Time**
   - Measure time in start_webrtc_client_threads
   - Current gap: 188ms between call and completion
   - Check if render thread creation is slow
   - Verify cryptographic operations aren't blocking

3. **Add Defensive Error Handling**
   - Return proper error codes from callbacks
   - Ensure no nullptr dereferences in fragment handling
   - Verify lws_write errors are handled correctly

## Files Modified
- `src/server/main.c`: websocket_client_handler blocking wait
- `lib/network/websocket/server.c`: RECEIVE/CLOSED callback handling
- `lib/network/websocket/transport.c`: Fragment buffering and recv()
- `cmake/dependencies/Libwebsockets.cmake`: permessage-deflate config

## Build/Test Commands
```bash
# Build
cmake --preset default -B build
cmake --build build

# Run E2E test
cd web/web.ascii-chat.com
npx playwright test tests/e2e/performance-check.spec.ts --project=chromium

# Check server logs
tail -200 .server-debug-*.log | grep "LWS_CALLBACK\|WS_FRAG\|WebSocket"
```

## Related Documentation
- WebSocket RFC 6455: https://tools.ietf.org/html/rfc6455
- permessage-deflate RFC 7692: https://tools.ietf.org/html/rfc7692
- libwebsockets API: https://libwebsockets.org/lws-api-reference.html
