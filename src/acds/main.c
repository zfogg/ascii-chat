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
#include <getopt.h>
#include <signal.h>

#include "acds/main.h"
#include "acds/server.h"
#include "acds/identity.h"
#include "acds/strings.h"
#include "common.h"
#include "version.h"
#include "log/logging.h"
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

void acds_print_usage(const char *program_name) {
  (void)program_name;

  printf("🔍 acds - ascii-chat discovery service\n");
  printf("\n");
  printf("String registry, session management, and WebRTC signaling for ascii-chat.\n");
  printf("\n");
  printf("USAGE:\n");
  printf("  acds [options] [address1] [address2]\n");
  printf("\n");
  printf("OPTIONS:\n");
  printf("  --port PORT              TCP listen port (default: 27225)\n");
  printf("  --database PATH          SQLite database path (default: ~/.config/ascii-chat/acds.db)\n");
  printf("  --key PATH               Ed25519 identity key path (default: ~/.config/ascii-chat/acds_identity)\n");
  printf("  -L --log-file FILE       Log file path (default: stderr)\n");
  printf("  --log-level LEVEL        Log level: dev, debug, info, warn, error, fatal (default: info)\n");
  printf("  -h --help                Show this help\n");
  printf("  -v --version             Show version\n");
  printf("\n");
  printf("POSITIONAL ARGUMENTS (BIND ADDRESSES):\n");
  printf("  address1                 IPv4 or IPv6 bind address (optional, 0-2 addresses)\n");
  printf("  address2                 Second bind address (must be different IP version)\n");
  printf("\n");
  printf("EXAMPLES:\n");
  printf("  acds                     Start on all interfaces (IPv4 and IPv6)\n");
  printf("  acds 0.0.0.0             Listen on IPv4 only\n");
  printf("  acds ::                  Listen on IPv6 only\n");
  printf("  acds 0.0.0.0 ::          Listen on both IPv4 and IPv6\n");
  printf("  acds --port 9443         Use port 9443 instead of default 27225\n");
  printf("\n");
  printf("🔗 https://github.com/zfogg/ascii-chat\n");
}

asciichat_error_t acds_parse_args(int argc, char **argv, acds_config_t *config) {
  if (!config) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "config is NULL");
  }

  // Set defaults
  config->port = 27225;
  config->address[0] = '\0';       // Empty = all IPv4 interfaces
  config->address6[0] = '\0';      // Empty = all IPv6 interfaces
  config->database_path[0] = '\0'; // Will be set to default path later
  config->key_path[0] = '\0';      // Will be set to default path later
  config->log_file[0] = '\0';      // Empty = stderr
  config->log_level = LOG_INFO;

  // Long options
  static struct option long_options[] = {
      {"port", required_argument, 0, 'p'},      {"database", required_argument, 0, 'd'},
      {"key", required_argument, 0, 'k'},       {"log-file", required_argument, 0, 'L'},
      {"log-level", required_argument, 0, 'l'}, {"help", no_argument, 0, 'h'},
      {"version", no_argument, 0, 'v'},         {0, 0, 0, 0}};

  int opt;
  int option_index = 0;

  while ((opt = getopt_long(argc, argv, "p:d:k:L:l:hv", long_options, &option_index)) != -1) {
    switch (opt) {
    case 'p': {
      // Parse port
      char *endptr;
      long port = strtol(optarg, &endptr, 10);
      if (*endptr != '\0' || port < 1 || port > 65535) {
        fprintf(stderr, "Error: Invalid port '%s' (must be 1-65535)\n", optarg);
        return ERROR_USAGE;
      }
      config->port = (int)port;
      break;
    }

    case 'd':
      SAFE_STRNCPY(config->database_path, optarg, sizeof(config->database_path));
      break;

    case 'k':
      SAFE_STRNCPY(config->key_path, optarg, sizeof(config->key_path));
      break;

    case 'L':
      SAFE_STRNCPY(config->log_file, optarg, sizeof(config->log_file));
      break;

    case 'l': {
      // Parse log level
      if (strcmp(optarg, "dev") == 0 || strcmp(optarg, "DEV") == 0) {
        config->log_level = LOG_DEV;
      } else if (strcmp(optarg, "debug") == 0 || strcmp(optarg, "DEBUG") == 0) {
        config->log_level = LOG_DEBUG;
      } else if (strcmp(optarg, "info") == 0 || strcmp(optarg, "INFO") == 0) {
        config->log_level = LOG_INFO;
      } else if (strcmp(optarg, "warn") == 0 || strcmp(optarg, "WARN") == 0) {
        config->log_level = LOG_WARN;
      } else if (strcmp(optarg, "error") == 0 || strcmp(optarg, "ERROR") == 0) {
        config->log_level = LOG_ERROR;
      } else if (strcmp(optarg, "fatal") == 0 || strcmp(optarg, "FATAL") == 0) {
        config->log_level = LOG_FATAL;
      } else {
        fprintf(stderr, "Error: Invalid log level '%s'. Valid values: dev, debug, info, warn, error, fatal\n", optarg);
        return ERROR_USAGE;
      }
      break;
    }

    case 'h':
      acds_print_usage(argv[0]);
      exit(0);

    case 'v':
      acds_print_version();
      exit(0);

    default:
      acds_print_usage(argv[0]);
      return ERROR_USAGE;
    }
  }

  // Parse positional arguments: 0-2 bind addresses (IPv4 and/or IPv6)
  int num_addresses = 0;
  bool has_ipv4 = false;
  bool has_ipv6 = false;

  while (optind < argc && argv[optind] != NULL && argv[optind][0] != '-' && num_addresses < 2) {
    const char *addr_arg = argv[optind];

    // Simple IPv4 vs IPv6 detection (basic pattern matching)
    bool is_ipv6 = (strchr(addr_arg, ':') != NULL);
    bool is_ipv4 = !is_ipv6; // If no colon, assume IPv4

    if (is_ipv4) {
      if (has_ipv4) {
        fprintf(stderr, "Error: Cannot specify multiple IPv4 addresses.\n");
        fprintf(stderr, "Already have: %s\n", config->address);
        fprintf(stderr, "Cannot add: %s\n", addr_arg);
        return ERROR_USAGE;
      }
      SAFE_STRNCPY(config->address, addr_arg, sizeof(config->address));
      has_ipv4 = true;
      num_addresses++;
    } else if (is_ipv6) {
      if (has_ipv6) {
        fprintf(stderr, "Error: Cannot specify multiple IPv6 addresses.\n");
        fprintf(stderr, "Already have: %s\n", config->address6);
        fprintf(stderr, "Cannot add: %s\n", addr_arg);
        return ERROR_USAGE;
      }
      SAFE_STRNCPY(config->address6, addr_arg, sizeof(config->address6));
      has_ipv6 = true;
      num_addresses++;
    }

    optind++;
  }

  // Check for unexpected extra arguments
  if (optind < argc) {
    fprintf(stderr, "Error: Too many arguments. Maximum 2 bind addresses allowed.\n");
    fprintf(stderr, "Unexpected argument: %s\n", argv[optind]);
    acds_print_usage(argv[0]);
    return ERROR_USAGE;
  }

  // Set default paths if not specified
  if (config->database_path[0] == '\0') {
    char *config_dir = get_config_dir();
    if (!config_dir) {
      return SET_ERRNO(ERROR_CONFIG, "Failed to get config directory");
    }
    snprintf(config->database_path, sizeof(config->database_path), "%sacds.db", config_dir);
    free(config_dir);
  }

  if (config->key_path[0] == '\0') {
    char *config_dir = get_config_dir();
    if (!config_dir) {
      return SET_ERRNO(ERROR_CONFIG, "Failed to get config directory");
    }
    snprintf(config->key_path, sizeof(config->key_path), "%sacds_identity", config_dir);
    free(config_dir);
  }

  return ASCIICHAT_OK;
}

int main(int argc, char **argv) {
  asciichat_error_t result;

  // Parse command-line arguments
  acds_config_t config;
  result = acds_parse_args(argc, argv, &config);
  if (result != ASCIICHAT_OK) {
    return result;
  }

  // Initialize platform layer
  result = platform_init();
  if (result != ASCIICHAT_OK) {
    fprintf(stderr, "Platform initialization failed\n");
    return result;
  }

  // Initialize logging
  const char *log_file = (config.log_file[0] != '\0') ? config.log_file : NULL;
  log_init(log_file, config.log_level, false, false);

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

  log_info("Loading identity key from %s", config.key_path);
  result = acds_identity_load(config.key_path, public_key, secret_key);

  if (result != ASCIICHAT_OK) {
    log_info("Identity key not found, generating new key...");

    result = acds_identity_generate(public_key, secret_key);
    if (result != ASCIICHAT_OK) {
      log_error("Failed to generate identity key");
      return result;
    }

    result = acds_identity_save(config.key_path, public_key, secret_key);
    if (result != ASCIICHAT_OK) {
      log_error("Failed to save identity key to %s", config.key_path);
      return result;
    }

    log_info("Saved new identity key to %s", config.key_path);
  }

  // Display server fingerprint
  char fingerprint[65];
  acds_identity_fingerprint(public_key, fingerprint);
  log_info("Discovery server identity: SHA256:%s", fingerprint);
  printf("🔑 Server fingerprint: SHA256:%s\n", fingerprint);

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
