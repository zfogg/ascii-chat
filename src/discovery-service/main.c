/**
 * @file acds/main.c
 * @brief ascii-chat Discovery Service (acds) main entry point
 *
 * Discovery server for session management and WebRTC signaling using
 * ACIP binary protocol over raw TCP. Uses the server_like abstraction
 * for TCP/WebSocket/mDNS/UPnP lifecycle management.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <errno.h>

#include "discovery-service/main.h"
#include "discovery-service/server.h"
#include "common/session/server_like.h"
#include <ascii-chat/discovery/identity.h>
#include <ascii-chat/discovery/strings.h>
#include <ascii-chat/common.h>
#include <ascii-chat/version.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/util/path.h>

/* ============================================================================
 * Static State
 * ============================================================================ */

static acds_server_t g_server;

/* ============================================================================
 * server_like Callbacks
 * ============================================================================ */

static void acds_interrupt_fn(void) {
  // Set TCP server running flag to false so select() exits
  tcp_server_t *tcp = session_server_like_get_tcp_server();
  if (tcp) {
    atomic_store_bool(&tcp->running, false);
    // Close listen sockets to interrupt select()
    if (tcp->listen_socket != INVALID_SOCKET_VALUE) {
      socket_close(tcp->listen_socket);
      tcp->listen_socket = INVALID_SOCKET_VALUE;
    }
    if (tcp->listen_socket6 != INVALID_SOCKET_VALUE) {
      socket_close(tcp->listen_socket6);
      tcp->listen_socket6 = INVALID_SOCKET_VALUE;
    }
  }
}

static asciichat_error_t acds_init_fn(void *user_data) {
  (void)user_data;
  asciichat_error_t result;

  const options_t *opts = options_get();

  log_info("ascii-chat Discovery Service (acds) starting...");
  log_info("Version: %s (%s, %s)", ASCII_CHAT_VERSION_FULL, ASCII_CHAT_BUILD_TYPE, ASCII_CHAT_BUILD_DATE);

  // Initialize session string generator (libsodium)
  result = acds_string_init();
  if (result != ASCIICHAT_OK) {
    log_error("Failed to initialize session string generator");
    return result;
  }

  // Load or generate identity keys
  uint8_t public_key[32];
  uint8_t secret_key[64];

  const char *acds_key_path = GET_OPTION(encrypt_key);
  char default_key_path[PLATFORM_MAX_PATH_LENGTH] = {0};
  if (!acds_key_path || acds_key_path[0] == '\0') {
    const char *config_dir = get_config_dir();
    if (config_dir) {
      safe_snprintf(default_key_path, sizeof(default_key_path), "%sdiscovery_identity", config_dir);
      acds_key_path = default_key_path;
    }
  }
  log_info("Loading identity key from %s", acds_key_path);
  result = acds_identity_load(acds_key_path, public_key, secret_key);

  if (result != ASCIICHAT_OK) {
    log_info("Identity key not found, generating new key...");

    result = acds_identity_generate(public_key, secret_key);
    if (result != ASCIICHAT_OK) {
      log_error("Failed to generate identity key");
      return result;
    }

    result = acds_identity_save(acds_key_path, public_key, secret_key);
    if (result != ASCIICHAT_OK) {
      log_error("Failed to save identity key to %s", acds_key_path);
      return result;
    }

    log_info("Saved new identity key to %s", acds_key_path);
  }

  // Display server fingerprint
  char fingerprint[65];
  acds_identity_fingerprint(public_key, fingerprint);
  log_info("Discovery server identity: SHA256:%s", fingerprint);
  char msg[256];
  safe_snprintf(msg, sizeof(msg), "Server fingerprint: SHA256:%s", fingerprint);
  log_console(LOG_INFO, msg);

  // Create config from options for server initialization
  acds_config_t config;
  int port_num = GET_OPTION(port);
  if (port_num < 1 || port_num > 65535) {
    log_error("Invalid port: %d (must be 1-65535)", port_num);
    return ERROR_INVALID_PARAM;
  }
  config.port = port_num;

  const char *address = opts && opts->address[0] != '\0' ? opts->address : "127.0.0.1";
  const char *address6 = opts && opts->address6[0] != '\0' ? opts->address6 : "::1";
  const char *log_file = opts && opts->log_file[0] != '\0' ? opts->log_file : "acds.log";
  SAFE_STRNCPY(config.address, address, sizeof(config.address));
  SAFE_STRNCPY(config.address6, address6, sizeof(config.address6));
  SAFE_STRNCPY(config.database_path, GET_OPTION(discovery_database_path), sizeof(config.database_path));
  SAFE_STRNCPY(config.key_path, acds_key_path, sizeof(config.key_path));
  SAFE_STRNCPY(config.log_file, log_file, sizeof(config.log_file));
  config.log_level = GET_OPTION(log_level);
  config.require_server_identity = GET_OPTION(require_server_identity) != 0;
  config.require_client_identity = GET_OPTION(require_client_identity) != 0;

  if (config.require_server_identity) {
    log_info("Security: Requiring signed identity from servers creating sessions");
  }
  if (config.require_client_identity) {
    log_info("Security: Requiring signed identity from clients joining sessions");
  }

  // Parse STUN servers from comma-separated list
  config.stun_count = 0;
  memset(config.stun_servers, 0, sizeof(config.stun_servers));
  const char *stun_servers_str = GET_OPTION(stun_servers);
  if (stun_servers_str && stun_servers_str[0] != '\0') {
    char stun_copy[OPTIONS_BUFF_SIZE];
    SAFE_STRNCPY(stun_copy, stun_servers_str, sizeof(stun_copy));

    char *saveptr = NULL;
    char *token = platform_strtok_r(stun_copy, ",", &saveptr);
    while (token && config.stun_count < 4) {
      while (*token == ' ' || *token == '\t')
        token++;
      size_t len = strlen(token);
      while (len > 0 && (token[len - 1] == ' ' || token[len - 1] == '\t')) {
        token[--len] = '\0';
      }

      if (len > 0 && len < sizeof(config.stun_servers[0].host)) {
        config.stun_servers[config.stun_count].host_len = (uint8_t)len;
        SAFE_STRNCPY(config.stun_servers[config.stun_count].host, token,
                     sizeof(config.stun_servers[config.stun_count].host));
        log_info("Added STUN server: %s", token);
        config.stun_count++;
      } else if (len > 0) {
        log_warn("STUN server URL too long (max 63 chars): %s", token);
      }

      token = platform_strtok_r(NULL, ",", &saveptr);
    }
  }

  // Parse TURN servers from comma-separated list
  config.turn_count = 0;
  memset(config.turn_servers, 0, sizeof(config.turn_servers));
  const char *turn_servers_str = GET_OPTION(turn_servers);
  const char *turn_username_str = GET_OPTION(turn_username);
  const char *turn_credential_str = GET_OPTION(turn_credential);

  if (turn_servers_str && turn_servers_str[0] != '\0') {
    char turn_copy[OPTIONS_BUFF_SIZE];
    SAFE_STRNCPY(turn_copy, turn_servers_str, sizeof(turn_copy));

    char *saveptr = NULL;
    char *token = platform_strtok_r(turn_copy, ",", &saveptr);
    while (token && config.turn_count < 4) {
      while (*token == ' ' || *token == '\t')
        token++;
      size_t len = strlen(token);
      while (len > 0 && (token[len - 1] == ' ' || token[len - 1] == '\t')) {
        token[--len] = '\0';
      }

      if (len > 0 && len < sizeof(config.turn_servers[0].url)) {
        config.turn_servers[config.turn_count].url_len = (uint8_t)len;
        SAFE_STRNCPY(config.turn_servers[config.turn_count].url, token,
                     sizeof(config.turn_servers[config.turn_count].url));

        if (turn_username_str && turn_username_str[0] != '\0') {
          size_t username_len = strlen(turn_username_str);
          if (username_len < sizeof(config.turn_servers[0].username)) {
            config.turn_servers[config.turn_count].username_len = (uint8_t)username_len;
            SAFE_STRNCPY(config.turn_servers[config.turn_count].username, turn_username_str,
                         sizeof(config.turn_servers[config.turn_count].username));
          }
        }

        if (turn_credential_str && turn_credential_str[0] != '\0') {
          size_t credential_len = strlen(turn_credential_str);
          if (credential_len < sizeof(config.turn_servers[0].credential)) {
            config.turn_servers[config.turn_count].credential_len = (uint8_t)credential_len;
            SAFE_STRNCPY(config.turn_servers[config.turn_count].credential, turn_credential_str,
                         sizeof(config.turn_servers[config.turn_count].credential));
          }
        }

        log_info("Added TURN server: %s (username: %s)", token,
                 turn_username_str && turn_username_str[0] ? turn_username_str : "<none>");
        config.turn_count++;
      } else if (len > 0) {
        log_warn("TURN server URL too long (max 63 chars): %s", token);
      }

      token = platform_strtok_r(NULL, ",", &saveptr);
    }
  }

  // Copy TURN secret for dynamic credential generation
  const char *turn_secret_str = GET_OPTION(turn_secret);
  if (turn_secret_str && turn_secret_str[0] != '\0') {
    SAFE_STRNCPY(config.turn_secret, turn_secret_str, sizeof(config.turn_secret));
    log_info("TURN dynamic credential generation enabled");
  } else {
    config.turn_secret[0] = '\0';
  }

  // Copy identity keys to server struct (needed by handlers for handshake)
  memcpy(g_server.identity_public, public_key, 32);
  memcpy(g_server.identity_secret, secret_key, 64);

  // Initialize server (DB, rate limiter, worker pool — TCP server comes from server_like)
  result = acds_server_init(&g_server, &config);
  if (result != ASCIICHAT_OK) {
    log_error("Server initialization failed");
    return result;
  }

  log_info("Discovery service initialized");
  return ASCIICHAT_OK;
}

static void acds_cleanup_fn(void *user_data) {
  (void)user_data;
  log_info("Shutting down discovery server...");
  acds_server_shutdown(&g_server);
  log_info("Discovery server stopped");
}

/* ============================================================================
 * Entry Point
 * ============================================================================ */

int acds_main(void) {
  session_server_like_config_t config = {
      // Required callbacks
      .init_fn = acds_init_fn,
      .init_user_data = NULL,
      .interrupt_fn = acds_interrupt_fn,

      // Cleanup
      .cleanup_fn = acds_cleanup_fn,
      .cleanup_user_data = NULL,

      // No status screen for ACDS (for now)
      .status_fn = NULL,
      .status_user_data = NULL,

      // TCP handler
      .tcp_handler = acds_client_handler,
      .tcp_user_data = &g_server,

      // WebSocket
      .websocket =
          {
              .enabled = true,
              .handler = acds_websocket_client_handler,
              .user_data = &g_server,
          },

      // mDNS
      .mdns =
          {
              .enabled = true,
              .service_name = "ascii-chat-Discovery-Service",
              .service_type = "_ascii-chat-discovery-service._tcp",
          },

      // UPnP
      .upnp =
          {
              .enabled = true,
              .description = "ascii-chat ACDS",
          },

      // System
      .raise_fd_limit = true,
      .fd_limit_target = 65536,
  };

  return session_server_like_run(&config);
}
