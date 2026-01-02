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

// Global server instance for signal handler
static acds_server_t *g_server = NULL;

/**
 * @brief Signal handler for clean shutdown
 */
static void signal_handler(int sig) {
  (void)sig;
  if (g_server) {
    atomic_store(&g_server->tcp_server.running, false);
  }
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

  log_info("Discovery server stopped");
  return result;
}
