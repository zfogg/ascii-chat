# TCP vs WebSocket Frame Handling: Line-by-Line Analysis

**Issue**: hq-eseb - Debug: TCP vs WebSocket frame handling comparison
**Task**: Find exact divergence point and identify missing/broken WebSocket code

## Summary of Divergence

The TCP client (`lib/network/tcp/client.c`) is a **complete, production-grade implementation** with full lifecycle management and frame handling. The WebSocket client (`lib/network/websocket/client.c`) is a **thin stub** that delegates everything to the ACIP transport layer, creating an impedance mismatch with the expected API.

### File Sizes as Indicator
- **TCP client**: 502 lines (16.4 KB) - comprehensive implementation
- **WebSocket client**: 181 lines (4.5 KB) - delegation stub

## Function-by-Function Comparison

### 1. Creation & Initialization

**TCP Client (lines 95-120)**:
```c
tcp_client_t *tcp_client_create(void) {
  tcp_client_t *client = SAFE_MALLOC(sizeof(tcp_client_t), tcp_client_t *);
  memset(client, 0, sizeof(*client));

  client->sockfd = INVALID_SOCKET_VALUE;
  atomic_store(&client->connection_active, false);
  atomic_store(&client->connection_lost, false);
  atomic_store(&client->should_reconnect, false);  // ← IMPORTANT: Reconnection flag
  client->my_client_id = 0;
  memset(client->server_ip, 0, sizeof(client->server_ip));
  client->encryption_enabled = false;              // ← IMPORTANT: Encryption state

  if (mutex_init(&client->send_mutex) != 0) {     // ← CRITICAL: Thread-safety mutex
    // error handling
  }
  return client;
}
```

**WebSocket Client (lines 24-42)**:
```c
websocket_client_t *websocket_client_create(void) {
  websocket_client_t *client = SAFE_MALLOC(sizeof(websocket_client_t), websocket_client_t *);
  memset(client, 0, sizeof(*client));

  atomic_store(&client->connection_active, false);
  atomic_store(&client->connection_lost, false);
  client->transport = NULL;                        // ← Only stores transport reference

  // NO MUTEX INITIALIZATION
  // NO RECONNECTION FLAG
  // NO ENCRYPTION STATE FLAG
  // NO CLIENT ID

  return client;
}
```

**Divergence Point #1**: WebSocket doesn't initialize `send_mutex`. This is critical for frame handling thread-safety.

### 2. Packet Transmission (The Core Frame Handler)

**TCP Client (lines 246-269)**:
```c
int tcp_client_send_packet(tcp_client_t *client, packet_type_t type,
                           const void *data, size_t len) {
  if (!client) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL client");
  }

  if (!atomic_load(&client->connection_active)) {
    return SET_ERRNO(ERROR_NETWORK, "Connection not active");
  }

  // Acquire send mutex for thread-safe transmission  ← MUTEX PROTECTION
  mutex_lock(&client->send_mutex);

  // Send packet without encryption (crypto handled at app_client layer)
  asciichat_error_t result = (asciichat_error_t)send_packet(
      client->sockfd, type, data, len
  );

  mutex_unlock(&client->send_mutex);               ← MUTEX RELEASE

  if (result != ASCIICHAT_OK) {
    log_debug("Failed to send packet type %d: %s", type,
              asciichat_error_string(result));
    return -1;
  }

  return 0;
}
```

**WebSocket Client**: **COMPLETELY MISSING**

This is the single most critical divergence. There is NO frame transmission function in the WebSocket client. The WebSocket client has:
- No `websocket_client_send_packet()` equivalent
- No mutex-protected frame sending
- No packet type routing
- No error checking for connection state

**Divergence Point #2**: WebSocket lacks any packet transmission mechanism.

### 3. Heartbeat/Ping-Pong Support

**TCP Client (lines 272-287)**:
```c
int tcp_client_send_ping(tcp_client_t *client) {
  if (!client)
    return -1;
  return tcp_client_send_packet(client, PACKET_TYPE_PING, NULL, 0);
}

int tcp_client_send_pong(tcp_client_t *client) {
  if (!client)
    return -1;
  return tcp_client_send_packet(client, PACKET_TYPE_PONG, NULL, 0);
}
```

**WebSocket Client**: **COMPLETELY MISSING**

No ping/pong support exists in WebSocket client.

**Divergence Point #3**: WebSocket lacks heartbeat/keepalive frame support.

### 4. Connection Establishment

**TCP Client (lines 311-501)** - Comprehensive 190-line connection function:
- IPv4/IPv6 dual-stack DNS resolution (lines 332-410)
- Localhost special handling (lines 334-399)
- Socket creation with fallback (lines 356-379, 418)
- Connection with timeout (lines 359, 382, 429)
- Server IP address extraction (lines 434-440)
- Local port → client ID assignment (lines 462-479)
- Socket configuration:
  - Keepalive (line 490)
  - Buffer configuration (lines 494-497)
- Reconnection delay with exponential backoff (lines 327-330)
- Full state initialization (lines 482-485)

```c
int tcp_client_connect(tcp_client_t *client, const char *address, int port,
                       int reconnect_attempt,
                       bool first_connection,
                       bool has_ever_connected) {
  // ... massive connection logic ...

  // Extract local port for client ID
  struct sockaddr_storage local_addr = {0};
  socklen_t addr_len = sizeof(local_addr);
  if (getsockname(client->sockfd, (struct sockaddr *)&local_addr,
                  &addr_len) == -1) {
    // error handling
  }

  int local_port = 0;
  if (((struct sockaddr *)&local_addr)->sa_family == AF_INET) {
    local_port = NET_TO_HOST_U16(
        ((struct sockaddr_in *)&local_addr)->sin_port
    );
  } else if (((struct sockaddr *)&local_addr)->sa_family == AF_INET6) {
    local_port = NET_TO_HOST_U16(
        ((struct sockaddr_in6 *)&local_addr)->sin6_port
    );
  }
  client->my_client_id = (uint32_t)local_port;    ← CLIENT ID FROM PORT
}
```

**WebSocket Client (lines 141-170)** - Simple delegation:
```c
acip_transport_t *websocket_client_connect(websocket_client_t *client,
                                           const char *url,
                                           struct crypto_context_t *crypto_ctx) {
  if (!client || !url) {
    log_error("Invalid arguments to websocket_client_connect");
    return NULL;
  }

  log_info("Connecting WebSocket client to %s", url);

  // Store URL for reference
  strncpy(client->url, url, sizeof(client->url) - 1);
  client->url[sizeof(client->url) - 1] = '\0';

  // Create transport using ACIP layer
  acip_transport_t *transport = acip_websocket_client_transport_create(
      url, crypto_ctx
  );
  if (!transport) {
    log_error("Failed to create WebSocket transport");
    atomic_store(&client->connection_lost, true);
    return NULL;
  }

  client->transport = transport;
  atomic_store(&client->connection_active, true);
  atomic_store(&client->connection_lost, false);

  return transport;  ← JUST RETURNS TRANSPORT, CALLER MANAGES IT
}
```

**Divergence Point #4**: WebSocket doesn't implement connection logic, delegates to ACIP layer. Caller must manage returned transport.

### 5. State Query Functions

**TCP Client** (lines 149-179):
```c
bool tcp_client_is_active(const tcp_client_t *client) {
  if (!client) return false;
  return atomic_load(&client->connection_active);
}

bool tcp_client_is_lost(const tcp_client_t *client) {
  if (!client) return false;
  return atomic_load(&client->connection_lost);
}

socket_t tcp_client_get_socket(const tcp_client_t *client) {  ← SOCKET ACCESS
  return client ? client->sockfd : INVALID_SOCKET_VALUE;
}

uint32_t tcp_client_get_id(const tcp_client_t *client) {      ← CLIENT ID ACCESS
  return client ? client->my_client_id : 0;
}
```

**WebSocket Client** (lines 68-83):
```c
bool websocket_client_is_active(const websocket_client_t *client) {
  if (!client) return false;
  return atomic_load(&client->connection_active);
}

bool websocket_client_is_lost(const websocket_client_t *client) {
  if (!client) return false;
  return atomic_load(&client->connection_lost);
}

// NO websocket_client_get_socket() - not applicable to transport abstraction
// NO websocket_client_get_id() - client ID not tracked
```

**Plus WebSocket only (lines 175-180)**:
```c
acip_transport_t *websocket_client_get_transport(
    const websocket_client_t *client) {
  if (!client) return NULL;
  return client->transport;
}
```

**Divergence Point #5**: WebSocket has no socket accessor (delegated to transport). WebSocket has no client ID accessor.

### 6. Connection Control

**TCP Client** (lines 188-234):
```c
void tcp_client_signal_lost(tcp_client_t *client) {
  if (!client) return;

  if (!atomic_load(&client->connection_lost)) {
    atomic_store(&client->connection_lost, true);
    atomic_store(&client->connection_active, false);
    log_info("Connection lost signaled");
  }
}

void tcp_client_close(tcp_client_t *client) {
  if (!client) return;

  log_debug("Closing client connection");
  atomic_store(&client->connection_active, false);

  if (socket_is_valid(client->sockfd)) {
    close_socket_safe(client->sockfd);  ← CLOSES SOCKET
    client->sockfd = INVALID_SOCKET_VALUE;
  }

  client->my_client_id = 0;  ← CLEARS CLIENT ID
}

void tcp_client_shutdown(tcp_client_t *client) {
  if (!client) return;

  atomic_store(&client->connection_active, false);

  if (socket_is_valid(client->sockfd)) {
    socket_shutdown(client->sockfd, SHUT_RDWR);  ← SHUTDOWN FOR SIGNALS
  }
}
```

**WebSocket Client** (lines 88-131):
```c
void websocket_client_signal_lost(websocket_client_t *client) {
  if (!client) return;
  atomic_store(&client->connection_lost, true);
  atomic_store(&client->connection_active, false);
  log_debug("WebSocket connection marked as lost");
}

void websocket_client_close(websocket_client_t *client) {
  if (!client) return;

  log_debug("Closing WebSocket client");

  if (client->transport) {
    acip_transport_close(client->transport);  ← DELEGATES TO TRANSPORT
  }

  atomic_store(&client->connection_active, false);
  // NO CLIENT ID RESET (because it doesn't track one)
}

void websocket_client_shutdown(websocket_client_t *client) {
  if (!client) return;

  log_debug("Shutting down WebSocket client");

  if (client->transport) {
    acip_transport_close(client->transport);  ← SAME AS CLOSE
  }

  atomic_store(&client->connection_active, false);
  atomic_store(&client->connection_lost, true);
}
```

**Divergence Point #6**: WebSocket delegates socket management to transport layer instead of managing sockets directly.

## Critical Missing Pieces in WebSocket Client

### Missing #1: Packet Transmission Function
**Impact**: Cannot send frames directly. Application code expecting `websocket_client_send_packet()` will fail.

### Missing #2: Ping/Pong Support
**Impact**: No heartbeat mechanism. Connection will timeout on idle without keepalive.

### Missing #3: Client ID Assignment
**Impact**: No local port-derived client ID. Server cannot identify clients by connection source.

### Missing #4: Reconnection Logic
**Impact**: No exponential backoff. Rapid reconnection attempts on failure will hammer server.

### Missing #5: Thread-Safe Packet Mutex
**Impact**: Concurrent frame transmission can corrupt data. WebSocket has no `send_mutex`.

### Missing #6: Socket Configuration
**Impact**: Buffers not optimized, keepalive not enabled. Network performance degraded.

### Missing #7: Encryption State Tracking
**Impact**: Cannot distinguish between encrypted and unencrypted connections.

## Implementation Strategy

The WebSocket client needs:

1. **Add `websocket_client_send_packet()` function**
   - Must wrap packet transmission through the transport layer
   - Should use mutex for thread-safety
   - Should check connection state before sending

2. **Add `websocket_client_send_ping()` and `websocket_client_send_pong()` functions**
   - Route through the send_packet function

3. **Add client ID tracking**
   - Assign from URL hash or transport-provided ID
   - Store in websocket_client_t

4. **Add reconnection state tracking**
   - Add `should_reconnect` flag to websocket_client_t
   - Implement exponential backoff in caller or transport wrapper

5. **Add `send_mutex` to websocket_client_t**
   - Initialize in create(), destroy in destroy()
   - Use in packet transmission

6. **Add proper connection state transitions**
   - Mark state after successful transport creation
   - Track encryption separately from connection state

## Comparison Summary

| Feature | TCP Client | WebSocket Client | Status |
|---------|-----------|-----------------|--------|
| Packet transmission | `tcp_client_send_packet()` | **MISSING** | ❌ |
| Ping/Pong support | `send_ping()`, `send_pong()` | **MISSING** | ❌ |
| Thread-safe sending | `send_mutex` protected | **NOT PRESENT** | ❌ |
| Client ID assignment | From local port | **MISSING** | ❌ |
| Connection establishment | Full implementation | Delegates to ACIP | ⚠️ |
| Socket management | Direct socket API | Transport abstraction | ⚠️ |
| State query functions | `get_socket()`, `get_id()` | `get_transport()` only | ⚠️ |
| Keepalive config | `socket_set_keepalive()` | Delegated to transport | ⚠️ |
| Buffer config | `socket_configure_buffers()` | Delegated to transport | ⚠️ |
| Reconnection backoff | Exponential: 100ms + 200ms×N | **MISSING** | ❌ |
| Encryption flag | `encryption_enabled` field | **MISSING** | ❌ |
| Connection signal | `signal_lost()` with check | Simple assignment | ⚠️ |
| Close operation | Socket closure + cleanup | Transport delegation | ⚠️ |

## Root Cause

The WebSocket client was created as a **thin transport wrapper** rather than as a **drop-in replacement** for the TCP client. This breaks code expecting symmetric interfaces between transport types. The ACIP abstraction layer handles too much, leaving the websocket_client_t as a near-empty shell.

## Next Steps

See implementation task hq-cv-5zo2s for detailed fixes required.
