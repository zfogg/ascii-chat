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
#include "core/common.h"
#include "core/version.h"
#include "log/logging.h"
#include "options/options.h"
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

void acds_print_version(void) {
  printf("acds (ascii-chat discovery service) %s (%s, %s)\n", ASCII_CHAT_VERSION_FULL, ASCII_CHAT_BUILD_TYPE,
         ASCII_CHAT_BUILD_DATE);
  printf("\n");
  printf("Built with:\n");

#ifdef __clang__
  printf("  Compiler: Clang %s\n", __clang_version__);
#elif defined(__GNUC__)
  printf("  Compiler: GCC %d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
  printf("  Compiler: MSVC %d\n", _MSC_VER);
#else
  printf("  Compiler: Unknown\n");
#endif

#ifdef USE_MUSL
  printf("  C Library: musl\n");
#elif defined(__GLIBC__)
  printf("  C Library: glibc %d.%d\n", __GLIBC__, __GLIBC_MINOR__);
#elif defined(_WIN32)
  printf("  C Library: MSVCRT\n");
#elif defined(__APPLE__)
  printf("  C Library: libSystem\n");
#else
  printf("  C Library: Unknown\n");
#endif

  printf("\n");
  printf("For more information: https://github.com/zfogg/ascii-chat\n");
}

int main(int argc, char **argv) {
  asciichat_error_t result;

  // Parse command-line arguments using options module
  result = options_init(argc, argv, MODE_ACDS);
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
  const char *log_file = (opt_log_file[0] != '\0') ? opt_log_file : NULL;
  log_init(log_file, opt_log_level, false, false);

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
  SAFE_STRNCPY(config.address, opt_address, sizeof(config.address));
  SAFE_STRNCPY(config.address6, opt_address6, sizeof(config.address6));
  SAFE_STRNCPY(config.database_path, opt_acds_database_path, sizeof(config.database_path));
  SAFE_STRNCPY(config.key_path, opt_acds_key_path, sizeof(config.key_path));
  SAFE_STRNCPY(config.log_file, opt_log_file, sizeof(config.log_file));
  config.log_level = opt_log_level;

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
