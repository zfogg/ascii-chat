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
#include "network/rate_limit/rate_limit.h"
#include "network/rate_limit/sqlite.h"
#include "network/errors.h"
#include "log/logging.h"
#include "platform/socket.h"
#include "network/network.h"
#include "network/tcp_server.h"
#include "util/ip.h"
#include <string.h>
#include <time.h>

/**
 * @brief Background thread for periodic rate limit cleanup
 *
 * Wakes up every 5 minutes to remove old rate limit events from the database.
 * This prevents the rate_events table from growing unbounded.
 */
static void *cleanup_thread_func(void *arg) {
  acds_server_t *server = (acds_server_t *)arg;
  if (!server) {
    return NULL;
  }

  log_info("Rate limit cleanup thread started");

  while (atomic_load(&server->cleanup_running)) {
    // Sleep for 5 minutes (or until shutdown)
    for (int i = 0; i < 300 && atomic_load(&server->cleanup_running); i++) {
      platform_sleep_ms(1000); // Sleep 1 second at a time for responsive shutdown
    }

    if (!atomic_load(&server->cleanup_running)) {
      break;
    }

    // Run cleanup (delete events older than 1 hour)
    log_debug("Running rate limit cleanup...");
    asciichat_error_t result = rate_limiter_cleanup(server->rate_limiter, 3600);
    if (result != ASCIICHAT_OK) {
      log_warn("Rate limit cleanup failed");
    }
  }

  log_info("Rate limit cleanup thread exiting");
  return NULL;
}

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

  // Initialize rate limiter with SQLite backend
  server->rate_limiter = rate_limiter_create_sqlite(NULL); // NULL = externally managed DB
  if (!server->rate_limiter) {
    database_close(server->db);
    session_registry_destroy(server->sessions);
    SAFE_FREE(server->sessions);
    return SET_ERRNO(ERROR_MEMORY, "Failed to create rate limiter");
  }

  // Set the database handle for the rate limiter
  rate_limiter_set_sqlite_db(server->rate_limiter, server->db);

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

  // Start rate limit cleanup thread
  atomic_store(&server->cleanup_running, true);
  if (ascii_thread_create(&server->cleanup_thread, cleanup_thread_func, server) != 0) {
    log_warn("Failed to create rate limit cleanup thread (continuing without cleanup)");
    atomic_store(&server->cleanup_running, false);
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

  // Stop cleanup thread
  if (atomic_load(&server->cleanup_running)) {
    atomic_store(&server->cleanup_running, false);
    ascii_thread_join(&server->cleanup_thread, NULL);
    log_debug("Rate limit cleanup thread stopped");
  }

  // Destroy rate limiter
  if (server->rate_limiter) {
    rate_limiter_destroy(server->rate_limiter);
    server->rate_limiter = NULL;
  }

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

  // Register client in TCP server registry with allocated client data
  acds_client_data_t *client_data = SAFE_MALLOC(sizeof(acds_client_data_t), acds_client_data_t *);
  if (!client_data) {
    log_error("Failed to allocate client data for %s", client_ip);
    socket_close(client_socket);
    SAFE_FREE(ctx);
    return NULL;
  }
  memset(client_data, 0, sizeof(*client_data));
  client_data->joined_session = false;

  if (tcp_server_add_client(&server->tcp_server, client_socket, client_data) != ASCIICHAT_OK) {
    log_error("Failed to register client %s in registry", client_ip);
    SAFE_FREE(client_data);
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
    case PACKET_TYPE_ACIP_SESSION_CREATE: {
      log_debug("SESSION_CREATE packet from %s", client_ip);

      if (payload_size < sizeof(acip_session_create_t)) {
        log_warn("SESSION_CREATE packet too small from %s", client_ip);
        break;
      }

      // Rate limiting check
      if (!check_and_record_rate_limit(server->rate_limiter, client_ip, RATE_EVENT_SESSION_CREATE, client_socket,
                                       "SESSION_CREATE")) {
        break;
      }

      acip_session_create_t *req = (acip_session_create_t *)payload;
      acip_session_created_t resp;
      memset(&resp, 0, sizeof(resp));

      asciichat_error_t create_result = session_create(server->sessions, req, &resp);
      if (create_result == ASCIICHAT_OK) {
        // Success - send SESSION_CREATED response
        send_packet(client_socket, PACKET_TYPE_ACIP_SESSION_CREATED, &resp, sizeof(resp));
        log_info("Session created: %.*s (UUID: %02x%02x...)", resp.session_string_len, resp.session_string,
                 resp.session_id[0], resp.session_id[1]);

        // Save to database - look up session entry first
        rwlock_rdlock(&server->sessions->lock);
        session_entry_t *session = NULL;
        HASH_FIND(hh, server->sessions->sessions, resp.session_string, strlen(resp.session_string), session);
        if (session) {
          database_save_session(server->db, session);
        }
        rwlock_rdunlock(&server->sessions->lock);
      } else {
        // Error - send error response using proper error code
        send_error_packet_message(client_socket, create_result, "Failed to create session");
        log_warn("Session creation failed for %s: %s", client_ip, asciichat_error_string(create_result));
      }
      break;
    }

    case PACKET_TYPE_ACIP_SESSION_LOOKUP: {
      log_debug("SESSION_LOOKUP packet from %s", client_ip);

      if (payload_size < sizeof(acip_session_lookup_t)) {
        log_warn("SESSION_LOOKUP packet too small from %s", client_ip);
        break;
      }

      // Rate limiting check
      if (!check_and_record_rate_limit(server->rate_limiter, client_ip, RATE_EVENT_SESSION_LOOKUP, client_socket,
                                       "SESSION_LOOKUP")) {
        break;
      }

      acip_session_lookup_t *req = (acip_session_lookup_t *)payload;
      acip_session_info_t resp;
      memset(&resp, 0, sizeof(resp));

      // Null-terminate session string for lookup
      char session_string[49] = {0};
      size_t copy_len =
          (req->session_string_len < sizeof(session_string) - 1) ? req->session_string_len : sizeof(session_string) - 1;
      memcpy(session_string, req->session_string, copy_len);

      asciichat_error_t lookup_result = session_lookup(server->sessions, session_string, &resp);
      if (lookup_result == ASCIICHAT_OK) {
        send_packet(client_socket, PACKET_TYPE_ACIP_SESSION_INFO, &resp, sizeof(resp));
        log_info("Session lookup for '%s' from %s: %s", session_string, client_ip, resp.found ? "found" : "not found");
      } else {
        send_error_packet_message(client_socket, lookup_result, "Session lookup failed");
        log_warn("Session lookup failed for %s: %s", client_ip, asciichat_error_string(lookup_result));
      }
      break;
    }

    case PACKET_TYPE_ACIP_SESSION_JOIN: {
      log_debug("SESSION_JOIN packet from %s", client_ip);

      if (payload_size < sizeof(acip_session_join_t)) {
        log_warn("SESSION_JOIN packet too small from %s", client_ip);
        break;
      }

      // Rate limiting check
      if (!check_and_record_rate_limit(server->rate_limiter, client_ip, RATE_EVENT_SESSION_JOIN, client_socket,
                                       "SESSION_JOIN")) {
        break;
      }

      acip_session_join_t *req = (acip_session_join_t *)payload;
      acip_session_joined_t resp;
      memset(&resp, 0, sizeof(resp));

      asciichat_error_t join_result = session_join(server->sessions, req, &resp);
      if (join_result == ASCIICHAT_OK && resp.success) {
        send_packet(client_socket, PACKET_TYPE_ACIP_SESSION_JOINED, &resp, sizeof(resp));

        // Update client data in registry (update in place)
        void *retrieved_data = NULL;
        if (tcp_server_get_client(&server->tcp_server, client_socket, &retrieved_data) == ASCIICHAT_OK &&
            retrieved_data) {
          acds_client_data_t *client_data = (acds_client_data_t *)retrieved_data;
          memcpy(client_data->session_id, resp.session_id, 16);
          memcpy(client_data->participant_id, resp.participant_id, 16);
          client_data->joined_session = true;
        }

        log_info("Client %s joined session (participant %02x%02x...)", client_ip, resp.participant_id[0],
                 resp.participant_id[1]);

        // Save to database - look up session entry first
        rwlock_rdlock(&server->sessions->lock);
        session_entry_t *session_iter, *session_tmp;
        session_entry_t *joined_session = NULL;
        HASH_ITER(hh, server->sessions->sessions, session_iter, session_tmp) {
          if (memcmp(session_iter->session_id, resp.session_id, 16) == 0) {
            joined_session = session_iter;
            break;
          }
        }
        if (joined_session) {
          database_save_session(server->db, joined_session);
        }
        rwlock_rdunlock(&server->sessions->lock);
      } else {
        send_packet(client_socket, PACKET_TYPE_ACIP_SESSION_JOINED, &resp, sizeof(resp));
        log_warn("Session join failed for %s: %s", client_ip, resp.error_message);
      }
      break;
    }

    case PACKET_TYPE_ACIP_SESSION_LEAVE: {
      log_debug("SESSION_LEAVE packet from %s", client_ip);

      if (payload_size < sizeof(acip_session_leave_t)) {
        log_warn("SESSION_LEAVE packet too small from %s", client_ip);
        break;
      }

      acip_session_leave_t *req = (acip_session_leave_t *)payload;

      asciichat_error_t leave_result = session_leave(server->sessions, req->session_id, req->participant_id);
      if (leave_result == ASCIICHAT_OK) {
        log_info("Client %s left session", client_ip);

        // Update client data to mark as not joined
        void *retrieved_data = NULL;
        if (tcp_server_get_client(&server->tcp_server, client_socket, &retrieved_data) == ASCIICHAT_OK &&
            retrieved_data) {
          acds_client_data_t *client_data = (acds_client_data_t *)retrieved_data;
          client_data->joined_session = false;
        }

        // Save updated session to database - look up session entry first
        rwlock_rdlock(&server->sessions->lock);
        session_entry_t *session_iter, *session_tmp;
        session_entry_t *left_session = NULL;
        HASH_ITER(hh, server->sessions->sessions, session_iter, session_tmp) {
          if (memcmp(session_iter->session_id, req->session_id, 16) == 0) {
            left_session = session_iter;
            break;
          }
        }
        if (left_session) {
          database_save_session(server->db, left_session);
        }
        rwlock_rdunlock(&server->sessions->lock);
      } else {
        send_error_packet(client_socket, leave_result);
        log_warn("Session leave failed for %s: %s", client_ip, asciichat_error_string(leave_result));
      }
      break;
    }

    case PACKET_TYPE_ACIP_WEBRTC_SDP: {
      log_debug("WEBRTC_SDP packet from %s", client_ip);

      if (payload_size < sizeof(acip_webrtc_sdp_t)) {
        log_warn("WEBRTC_SDP packet too small from %s", client_ip);
        break;
      }

      acip_webrtc_sdp_t *sdp = (acip_webrtc_sdp_t *)payload;

      asciichat_error_t relay_result = signaling_relay_sdp(server->sessions, &server->tcp_server, sdp, payload_size);
      if (relay_result != ASCIICHAT_OK) {
        send_error_packet_message(client_socket, relay_result, "SDP relay failed");
        log_warn("SDP relay failed from %s: %s", client_ip, asciichat_error_string(relay_result));
      }
      break;
    }

    case PACKET_TYPE_ACIP_WEBRTC_ICE: {
      log_debug("WEBRTC_ICE packet from %s", client_ip);

      if (payload_size < sizeof(acip_webrtc_ice_t)) {
        log_warn("WEBRTC_ICE packet too small from %s", client_ip);
        break;
      }

      acip_webrtc_ice_t *ice = (acip_webrtc_ice_t *)payload;

      asciichat_error_t relay_result = signaling_relay_ice(server->sessions, &server->tcp_server, ice, payload_size);
      if (relay_result != ASCIICHAT_OK) {
        send_error_packet_message(client_socket, relay_result, "ICE relay failed");
        log_warn("ICE relay failed from %s: %s", client_ip, asciichat_error_string(relay_result));
      }
      break;
    }

    case PACKET_TYPE_ACIP_DISCOVERY_PING:
      // Send PONG response
      log_debug("PING from %s, sending PONG", client_ip);
      send_packet(client_socket, PACKET_TYPE_PONG, NULL, 0);
      break;

    default:
      log_warn("Unknown packet type 0x%02X from %s", packet_type, client_ip);
      send_error_packet(client_socket, ERROR_UNKNOWN_PACKET);
      break;
    }

    // Free payload
    if (payload) {
      SAFE_FREE(payload);
    }
  }

  // Cleanup
  tcp_server_remove_client(&server->tcp_server, client_socket);
  log_debug("Client %s unregistered (total=%zu)", client_ip, tcp_server_get_client_count(&server->tcp_server));

  socket_close(client_socket);
  SAFE_FREE(ctx);

  log_info("Client handler finished for %s", client_ip);
  return NULL;
}
