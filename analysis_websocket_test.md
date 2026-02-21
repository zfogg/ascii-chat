# WebSocket Server-Client Test Analysis

## Test Execution
- **Date**: 2026-02-20
- **Test Script**: `scripts/test-websocket-server-client.sh`
- **Multiple runs**: 3 successful completions, consistent failures

## Summary
**Status**: ❌ FAILURE - Client unable to connect to server

## Root Cause
**WebSocket server fails to initialize on the server side**

### Server-Side Failure (First Run - Port 38617)
```
[19:32:26.259922] [INFO] lib/network/websocket/server.c:853@websocket_server_init(): WebSocket server initialized on port 38617 with static file serving
[19:32:26.260819] [INFO] src/server/main.c:1910@server_main(): WebSocket server initialized on port 38617
```

Actually, that's misleading. Let me check the second run (Port 36092):

### Server-Side Failure (Second Run - Port 36092)
```
[19:32:49.179849] [ERROR] lib/network/websocket/server.c:850@websocket_server_init(): 
SET_ERRNO: Failed to create libwebsockets context (code: 41, meaning: Cannot bind to network port)
[19:32:49.180591] [WARN] src/server/main.c:1908@server_main(): 
Failed to initialize WebSocket server - browser clients will not be supported
```

### Client-Side Error (Consequence of Server Failure)
```
[19:32:56.582836] [ERROR] lib/network/websocket/transport.c:199@websocket_callback(): 
WebSocket connection error: conn fail: ECONNREFUSED
[19:33:01.628530] [ERROR] lib/network/websocket/transport.c:1056@acip_websocket_client_transport_create(): 
WebSocket connection timeout after 5000 ms
```

## Key Findings

1. **Inconsistent Behavior**: 
   - Run 1: WebSocket server appears to initialize successfully (port 38617)
   - Run 2: WebSocket server fails to bind to port (port 36092)

2. **Error Code 41**: "Cannot bind to network port"
   - Indicates port binding failure in libwebsockets context creation
   - Could be transient (port in use) or persistent (permissions/config issue)

3. **Client Connection Timeout**:
   - Client waits 5 seconds for WebSocket connection to establish
   - Gets ECONNREFUSED when server fails
   - Snapshot mode does NOT retry, exits after first failure

4. **No Frames Transmitted**:
   - Client never establishes connection, so no video frames are sent
   - No audio frames received by client (underruns observed)
   - Test completes but without actual data transfer

5. **Audio Processing Observed**:
   - Audio worker threads ARE running
   - Audio pipeline initialized successfully  
   - No frames from network (playback underruns)
   - Continuous logs: "Network playback underrun: got 0/480 samples"

## Intermittency Pattern

The first run succeeded in starting the WebSocket server, but subsequent runs on random ports failed. This suggests:
- Port allocation/deallocation timing issue
- libwebsockets context creation bug with certain port numbers
- Residual socket state from previous test run

## What Happens in Test

1. ✅ Server binary launches
2. ✅ Audio mixer initialized
3. ✅ mDNS advertised
4. ⚠️ WebSocket server init: INCONSISTENT
   - Sometimes succeeds (port 38617)
   - Sometimes fails with code 41 (port 36092)
5. ❌ Client connects to ws://localhost:<port>
6. ❌ Gets ECONNREFUSED
7. ✅ Test completes after 5-second timeout
8. ✅ No crashes detected

## Recommendations for Fix

Investigate why libwebsockets context creation fails on certain ports:
1. Check if previous test ports are properly cleaned up
2. Verify SO_REUSEADDR is set for WebSocket listening socket
3. Check system file descriptor limits
4. Consider adding server-side port availability retry logic
