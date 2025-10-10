# Network Timeout Handling

This document describes the network timeout handling features implemented in the ASCII chat application.

## Overview

The application now includes comprehensive timeout handling for all network operations to improve reliability and prevent hanging connections.

## Timeout Constants

All timeout values are defined in `network.h`:

- `CONNECT_TIMEOUT`: 10 seconds - Timeout for establishing connections
- `SEND_TIMEOUT`: 5 seconds - Timeout for sending data
- `RECV_TIMEOUT`: 5 seconds - Timeout for receiving data
- `ACCEPT_TIMEOUT`: 30 seconds - Timeout for accepting new connections

## Keep-Alive Settings

TCP keep-alive is enabled with the following settings:

- `KEEPALIVE_IDLE`: 60 seconds - Time before first keep-alive probe
- `KEEPALIVE_INTERVAL`: 10 seconds - Interval between keep-alive probes
- `KEEPALIVE_COUNT`: 3 - Number of failed probes before connection is considered dead

## Implementation Details

### Network Utility Functions

The `network.c` module provides the following functions:

- `connect_with_timeout()`: Non-blocking connection with timeout
- `send_with_timeout()`: Send data with timeout and partial send handling
- `recv_with_timeout()`: Receive data with timeout
- `accept_with_timeout()`: Accept connections with timeout
- `set_socket_timeout()`: Set socket-level timeouts
- `set_socket_keepalive()`: Enable TCP keep-alive
- `network_error_string()`: Human-readable error messages

### Error Handling

The application now provides detailed error messages for network failures:

- Connection timeouts
- Connection refused
- Network unreachable
- Host unreachable
- Broken pipe
- Connection reset by peer

### Server Improvements

- Accept operations now timeout after 30 seconds
- Send operations timeout after 5 seconds
- Keep-alive is enabled on accepted connections
- Better error reporting for failed operations

### Client Improvements

- Connection attempts timeout after 10 seconds
- Receive operations timeout after 5 seconds
- Automatic reconnection with exponential backoff
- Keep-alive is enabled on established connections
- Detailed error messages for connection failures

## Usage Examples

### Server Side
```c
// Accept with timeout
connfd = accept_with_timeout(listenfd, NULL, NULL, ACCEPT_TIMEOUT);
if (connfd < 0) {
    if (errno == ETIMEDOUT) {
        printf("Accept timeout, continuing...\n");
        continue;
    }
    perror("accept failed");
    continue;
}

// Send with timeout
ssize_t sent = send_with_timeout(connfd, data, len, SEND_TIMEOUT);
if (sent < 0) {
    printf("Send failed: %s\n", network_error_string());
}
```

### Client Side
```c
// Connect with timeout
if (!connect_with_timeout(sockfd, &addr, sizeof(addr), CONNECT_TIMEOUT)) {
    printf("Connect failed: %s\n", network_error_string());
    // Handle reconnection...
}

// Receive with timeout
ssize_t received = recv_with_timeout(sockfd, buffer, size, RECV_TIMEOUT);
if (received < 0) {
    printf("Receive failed: %s\n", network_error_string());
}
```

## Benefits

1. **Prevents Hanging**: No more infinite waits for network operations
2. **Better Reliability**: Automatic detection of dead connections
3. **Improved User Experience**: Clear error messages and automatic recovery
4. **Resource Management**: Proper cleanup of failed connections
5. **Network Resilience**: Handles temporary network issues gracefully

## Configuration

Timeout values can be adjusted by modifying the constants in `network.h`. For production use, consider:

- Increasing `CONNECT_TIMEOUT` for slow networks
- Decreasing `SEND_TIMEOUT`/`RECV_TIMEOUT` for real-time applications
- Adjusting keep-alive settings based on network characteristics
