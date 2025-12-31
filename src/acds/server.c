/**
 * @file acds/server.c
 * @brief 🌐 Discovery server TCP connection manager
 *
 * Uses lib/network/tcp_server abstraction for:
 * - Dual-stack IPv4/IPv6 binding
 * - Per-client handler threads
 * - select()-based accept loop
 *
 * ACDS-specific functionality:
 * - Session registry management
 * - SQLite persistence
 * - ACIP packet dispatch to session/signaling handlers
 */

#include "acds/server.h"
#include "acds/database.h"
#include "acds/session.h"
#include "acds/signaling.h"
#include "acds/protocol.h"
#include "log/logging.h"
#include "platform/socket.h"
#include "network/network.h"
#include "network/tcp_server.h"
#include "util/ip.h"
#include <string.h>

asciichat_error_t acds_server_init(acds_server_t *server, const acds_config_t *config) {
  if (!server || !config) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "server or config is NULL");
  }

  memset(server, 0, sizeof(*server));
  memcpy(&server->config, config, sizeof(acds_config_t));

  // Initialize session registry
  server->sessions = SAFE_MALLOC(sizeof(session_registry_t), session_registry_t *);
  if (!server->sessions) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate session registry");
  }

  asciichat_error_t result = session_registry_init(server->sessions);
  if (result != ASCIICHAT_OK) {
    SAFE_FREE(server->sessions);
    return result;
  }

  // Open database
  result = database_init(config->database_path, &server->db);
  if (result != ASCIICHAT_OK) {
    session_registry_destroy(server->sessions);
    SAFE_FREE(server->sessions);
    return result;
  }

  // Load sessions from database
  result = database_load_sessions(server->db, server->sessions);
  if (result != ASCIICHAT_OK) {
    log_warn("Failed to load sessions from database (continuing anyway)");
  }

  // Configure TCP server
  tcp_server_config_t tcp_config = {
      .port = config->port,
      .ipv4_address = (config->address[0] != '\0') ? config->address : NULL,
      .ipv6_address = (config->address6[0] != '\0') ? config->address6 : NULL,
      .bind_ipv4 = (config->address[0] != '\0') || (config->address[0] == '\0' && config->address6[0] == '\0'),
      .bind_ipv6 = (config->address6[0] != '\0') || (config->address[0] == '\0' && config->address6[0] == '\0'),
      .accept_timeout_sec = 1,
      .client_handler = acds_client_handler,
      .user_data = server,
  };

  // Initialize TCP server
  result = tcp_server_init(&server->tcp_server, &tcp_config);
  if (result != ASCIICHAT_OK) {
    database_close(server->db);
    session_registry_destroy(server->sessions);
    SAFE_FREE(server->sessions);
    return result;
  }

  log_info("Discovery server initialized successfully");
  return ASCIICHAT_OK;
}

asciichat_error_t acds_server_run(acds_server_t *server) {
  if (!server) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "server is NULL");
  }

  log_info("Discovery server accepting connections on port %d", server->config.port);

  // Delegate to TCP server abstraction
  return tcp_server_run(&server->tcp_server);
}

void acds_server_shutdown(acds_server_t *server) {
  if (!server) {
    return;
  }

  // Shutdown TCP server (closes listen sockets, stops accept loop)
  tcp_server_shutdown(&server->tcp_server);

  // TODO: Wait for all client handler threads to exit

  // Close database
  if (server->db) {
    database_close(server->db);
    server->db = NULL;
  }

  // Destroy session registry
  if (server->sessions) {
    session_registry_destroy(server->sessions);
    SAFE_FREE(server->sessions);
  }

  log_info("Server shutdown complete");
}

void *acds_client_handler(void *arg) {
  tcp_client_context_t *ctx = (tcp_client_context_t *)arg;
  if (!ctx) {
    log_error("Client handler: NULL context");
    return NULL;
  }

  acds_server_t *server = (acds_server_t *)ctx->user_data;
  socket_t client_socket = ctx->client_socket;

  // Get client IP for logging
  char client_ip[INET6_ADDRSTRLEN] = {0};
  int addr_family = (ctx->addr.ss_family == AF_INET) ? AF_INET : AF_INET6;
  format_ip_address(addr_family, (struct sockaddr *)&ctx->addr, client_ip, sizeof(client_ip));

  log_info("Client handler started for %s", client_ip);

  // Register client in TCP server registry
  // For now, no per-client data (NULL), will add session info later
  if (tcp_server_add_client(&server->tcp_server, client_socket, NULL) != ASCIICHAT_OK) {
    log_error("Failed to register client %s in registry", client_ip);
    socket_close(client_socket);
    SAFE_FREE(ctx);
    return NULL;
  }

  log_debug("Client %s registered (socket=%d, total=%zu)", client_ip, client_socket,
            tcp_server_get_client_count(&server->tcp_server));

  // TODO: Crypto handshake (when crypto support is added)
  // For now, accept plain connections

  // Main packet processing loop
  while (atomic_load(&server->tcp_server.running)) {
    packet_type_t packet_type;
    void *payload = NULL;
    size_t payload_size = 0;

    // Receive packet (blocking with system timeout)
    int result = receive_packet(client_socket, &packet_type, &payload, &payload_size);
    if (result < 0) {
      // Connection error - client disconnected
      log_info("Client %s disconnected", client_ip);
      if (payload) {
        SAFE_FREE(payload);
      }
      break;
    }

    log_debug("Received packet type 0x%02X from %s, length=%zu", packet_type, client_ip, payload_size);

    // Dispatch based on packet type
    switch (packet_type) {
    case PACKET_TYPE_ACIP_SESSION_CREATE:
      // TODO: Call session_create() when implemented
      log_debug("SESSION_CREATE packet from %s (handler not yet implemented)", client_ip);
      break;

    case PACKET_TYPE_ACIP_SESSION_LOOKUP:
      // TODO: Call session_lookup() when implemented
      log_debug("SESSION_LOOKUP packet from %s (handler not yet implemented)", client_ip);
      break;

    case PACKET_TYPE_ACIP_SESSION_JOIN:
      // TODO: Call session_join() when implemented
      log_debug("SESSION_JOIN packet from %s (handler not yet implemented)", client_ip);
      break;

    case PACKET_TYPE_ACIP_SESSION_LEAVE:
      // TODO: Call session_leave() when implemented
      log_debug("SESSION_LEAVE packet from %s (handler not yet implemented)", client_ip);
      break;

    case PACKET_TYPE_ACIP_WEBRTC_SDP:
      // TODO: Call signaling_relay_sdp() when implemented
      log_debug("WEBRTC_SDP packet from %s (handler not yet implemented)", client_ip);
      break;

    case PACKET_TYPE_ACIP_WEBRTC_ICE:
      // TODO: Call signaling_relay_ice() when implemented
      log_debug("WEBRTC_ICE packet from %s (handler not yet implemented)", client_ip);
      break;

    case PACKET_TYPE_ACIP_DISCOVERY_PING:
      // Send PONG response
      log_debug("PING from %s, sending PONG", client_ip);
      send_packet(client_socket, PACKET_TYPE_PONG, NULL, 0);
      break;

    default:
      log_warn("Unknown packet type 0x%02X from %s", packet_type, client_ip);
      // Send error response
      acip_error_t error = {.error_code = 1}; // Unknown packet type
      SAFE_STRNCPY(error.error_message, "Unknown packet type", sizeof(error.error_message));
      send_packet(client_socket, PACKET_TYPE_ACIP_ERROR, &error, sizeof(error));
      break;
    }

    // Free payload
    if (payload) {
      SAFE_FREE(payload);
    }
  }

  // Cleanup
  // Remove client from registry
  tcp_server_remove_client(&server->tcp_server, client_socket);
  log_debug("Client %s unregistered (total=%zu)", client_ip, tcp_server_get_client_count(&server->tcp_server));

  socket_close(client_socket);
  SAFE_FREE(ctx);

  log_info("Client handler finished for %s", client_ip);
  return NULL;
}
