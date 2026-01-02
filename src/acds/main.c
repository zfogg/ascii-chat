/**
 * @file acds/main.c
 * @brief 🔍 ASCII-Chat Discovery Service (acds) main entry point
 *
 * Discovery server for session management and WebRTC signaling using
 * ACIP binary protocol over raw TCP.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>

#include "acds/main.h"
#include "acds/server.h"
#include "acds/identity.h"
#include "acds/strings.h"
#include "common.h"
#include "version.h"
#include "log/logging.h"
#include "options/options.h"
#include "options/rcu.h" // For RCU-based options access
#include "options/acds.h"
#include "platform/abstraction.h"
#include "platform/init.h"
#include "util/path.h"
#include "network/nat/upnp.h"
#include "network/mdns.h"

// Global server instance for signal handler
static acds_server_t *g_server = NULL;

// Global UPnP context for cleanup on signal
static nat_upnp_context_t *g_upnp_ctx = NULL;

// Global mDNS context for LAN service discovery
// Allows clients on local network to discover the ACDS server without knowing its IP
static asciichat_mdns_t *g_mdns_ctx = NULL;

/**
 * @brief Signal handler for clean shutdown
 */
static void signal_handler(int sig) {
  (void)sig;
  if (g_server) {
    atomic_store(&g_server->tcp_server.running, false);
  }
  // UPnP context will be cleaned up after server shutdown
}

int main(int argc, char **argv) {
  asciichat_error_t result;

  // Parse command-line arguments using options module
  // Note: ACDS is a separate binary, so argv[0] is the program name
  // We need to insert "acds" mode argument for options parsing
  char **acds_argv = SAFE_MALLOC((size_t)(argc + 2) * sizeof(char *), char **);
  if (!acds_argv) {
    return ERROR_MEMORY;
  }
  acds_argv[0] = argv[0];
  acds_argv[1] = "acds";
  for (int i = 1; i < argc; i++) {
    acds_argv[i + 1] = argv[i];
  }
  acds_argv[argc + 1] = NULL;

  result = options_init(argc + 1, acds_argv);

  SAFE_FREE(acds_argv);
  if (result != ASCIICHAT_OK) {
    return result;
  }

  // Initialize platform layer
  result = platform_init();
  if (result != ASCIICHAT_OK) {
    fprintf(stderr, "Platform initialization failed\n");
    return result;
  }

  // Initialize logging using parsed options
  const options_t *opts = options_get();

  // Handle --help and --version early
  if (opts && opts->help) {
    usage(stdout, MODE_ACDS);
    return 0;
  }

  if (opts && opts->version) {
    printf("ascii-chat-acds version %s (%s, %s)\n", ASCII_CHAT_VERSION_FULL, ASCII_CHAT_BUILD_TYPE,
           ASCII_CHAT_BUILD_DATE);
    return 0;
  }

  const char *log_file = opts && opts->log_file[0] != '\0' ? opts->log_file : "acds.log";
  log_level_t log_level = GET_OPTION(log_level);
  log_init(log_file, log_level, false, false);

  log_info("ASCII-Chat Discovery Service (acds) starting...");
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

  log_info("Loading identity key from %s", opt_acds_key_path);
  result = acds_identity_load(opt_acds_key_path, public_key, secret_key);

  if (result != ASCIICHAT_OK) {
    log_info("Identity key not found, generating new key...");

    result = acds_identity_generate(public_key, secret_key);
    if (result != ASCIICHAT_OK) {
      log_error("Failed to generate identity key");
      return result;
    }

    result = acds_identity_save(opt_acds_key_path, public_key, secret_key);
    if (result != ASCIICHAT_OK) {
      log_error("Failed to save identity key to %s", opt_acds_key_path);
      return result;
    }

    log_info("Saved new identity key to %s", opt_acds_key_path);
  }

  // Display server fingerprint
  char fingerprint[65];
  acds_identity_fingerprint(public_key, fingerprint);
  log_info("Discovery server identity: SHA256:%s", fingerprint);
  printf("🔑 Server fingerprint: SHA256:%s\n", fingerprint);

  // Create config from options for server initialization
  acds_config_t config;
  config.port = opt_acds_port;
  const char *address = opts && opts->address[0] != '\0' ? opts->address : "127.0.0.1";
  const char *address6 = opts && opts->address6[0] != '\0' ? opts->address6 : "::1";
  SAFE_STRNCPY(config.address, address, sizeof(config.address));
  SAFE_STRNCPY(config.address6, address6, sizeof(config.address6));
  SAFE_STRNCPY(config.database_path, opt_acds_database_path, sizeof(config.database_path));
  SAFE_STRNCPY(config.key_path, opt_acds_key_path, sizeof(config.key_path));
  SAFE_STRNCPY(config.log_file, log_file, sizeof(config.log_file));
  config.log_level = GET_OPTION(log_level);
  config.require_server_identity = GET_OPTION(require_server_identity) != 0;
  config.require_client_identity = GET_OPTION(require_client_identity) != 0;

  // Log security policy
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
    char *token = strtok_r(stun_copy, ",", &saveptr);
    while (token && config.stun_count < 4) {
      // Trim whitespace
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

      token = strtok_r(NULL, ",", &saveptr);
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
    char *token = strtok_r(turn_copy, ",", &saveptr);
    while (token && config.turn_count < 4) {
      // Trim whitespace
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

        // Set username if provided
        if (turn_username_str && turn_username_str[0] != '\0') {
          size_t username_len = strlen(turn_username_str);
          if (username_len < sizeof(config.turn_servers[0].username)) {
            config.turn_servers[config.turn_count].username_len = (uint8_t)username_len;
            SAFE_STRNCPY(config.turn_servers[config.turn_count].username, turn_username_str,
                         sizeof(config.turn_servers[config.turn_count].username));
          }
        }

        // Set credential if provided
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

      token = strtok_r(NULL, ",", &saveptr);
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

  // Initialize server
  acds_server_t server;
  memset(&server, 0, sizeof(server));
  g_server = &server;

  result = acds_server_init(&server, &config);
  if (result != ASCIICHAT_OK) {
    log_error("Server initialization failed");
    g_server = NULL;
    return result;
  }

  // =========================================================================
  // UPnP Port Mapping (Quick Win for Direct TCP)
  // =========================================================================
  // Try to open port via UPnP so direct TCP works for ~70% of home users.
  // If this fails, clients fall back to WebRTC automatically - not fatal.
  //
  // Strategy:
  //   1. UPnP (works on ~90% of home routers)
  //   2. NAT-PMP fallback (Apple routers)
  //   3. If both fail: use ACDS + WebRTC (reliable, but slightly higher latency)
  if (GET_OPTION(enable_upnp) && !GET_OPTION(no_upnp)) {
    asciichat_error_t upnp_result = nat_upnp_open(config.port, "ASCII-Chat ACDS", &g_upnp_ctx);

    if (upnp_result == ASCIICHAT_OK && g_upnp_ctx) {
      char public_addr[22];
      if (nat_upnp_get_address(g_upnp_ctx, public_addr, sizeof(public_addr)) == ASCIICHAT_OK) {
        printf("🌐 Public endpoint: %s (direct TCP)\n", public_addr);
        log_info("UPnP: Port mapping successful, public endpoint: %s", public_addr);
      }
    } else {
      log_info("UPnP: Port mapping unavailable or failed - will use WebRTC fallback");
      printf("📡 Clients behind strict NATs will use WebRTC fallback\n");
    }
  } else {
    if (GET_OPTION(no_upnp)) {
      log_info("UPnP: Disabled via --no-upnp option");
    } else {
      log_info("UPnP: Disabled via environment variable or configuration");
    }
    printf("📡 WebRTC will be used for all clients\n");
  }

  // Initialize mDNS for LAN discovery of ACDS server
  // This allows clients on the local network to discover the discovery service itself
  log_debug("Initializing mDNS for ACDS LAN service discovery...");
  g_mdns_ctx = asciichat_mdns_init();
  if (!g_mdns_ctx) {
    LOG_ERRNO_IF_SET("Failed to initialize mDNS (non-fatal, LAN discovery disabled)");
    log_warn("mDNS disabled for ACDS - LAN discovery of discovery service will not be available");
  } else {
    // Advertise ACDS service on the LAN
    // Build hostname if available
    char hostname[256] = {0};
    gethostname(hostname, sizeof(hostname) - 1);

    asciichat_mdns_service_t service = {
        .name = "ASCII-Chat-Discovery-Service",
        .type = "_ascii-chat-discovery-service._tcp",
        .host = hostname,
        .port = config.port,
        .txt_records = NULL,
        .txt_count = 0,
    };

    asciichat_error_t mdns_advertise_result = asciichat_mdns_advertise(g_mdns_ctx, &service);
    if (mdns_advertise_result != ASCIICHAT_OK) {
      LOG_ERRNO_IF_SET("Failed to advertise ACDS mDNS service");
      log_warn("mDNS advertising failed for ACDS - LAN discovery disabled");
      asciichat_mdns_shutdown(g_mdns_ctx);
      g_mdns_ctx = NULL;
    } else {
      printf("🌐 mDNS: ACDS advertised as '_ascii-chat-discovery-service._tcp.local' on LAN\n");
      log_info("mDNS: ACDS advertised as '_ascii-chat-discovery-service._tcp.local' (port=%d)", config.port);
    }
  }

  // Install signal handlers for clean shutdown
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  // Run server
  log_info("Discovery server listening on port %d", config.port);
  printf("🌐 Listening on port %d\n", config.port);
  printf("📊 Database: %s\n", config.database_path);
  printf("Press Ctrl+C to stop\n\n");

  result = acds_server_run(&server);
  if (result != ASCIICHAT_OK) {
    log_error("Server run failed");
  }

  // Cleanup
  log_info("Shutting down discovery server...");
  acds_server_shutdown(&server);
  g_server = NULL;

  // Clean up UPnP port mapping
  if (g_upnp_ctx) {
    nat_upnp_close(&g_upnp_ctx);
    log_debug("UPnP port mapping closed");
  }

  // Clean up mDNS context
  if (g_mdns_ctx) {
    asciichat_mdns_shutdown(g_mdns_ctx);
    g_mdns_ctx = NULL;
    log_debug("mDNS context shut down");
  }

  log_info("Discovery server stopped");
  return result;
}
