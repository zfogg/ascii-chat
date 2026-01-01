/**
 * @file acds.c
 * @ingroup options
 * @brief ACDS mode option parsing and help text
 *
 * ACDS-specific command-line argument parsing with support for:
 * - Dual-stack binding (0-2 IPv4/IPv6 addresses)
 * - Database configuration
 * - Identity key management
 * - Logging configuration
 */

#include "options/acds.h"
#include "options/builder.h"
#include "options/common.h"

#include "asciichat_errno.h"
#include "common.h"
#include "log/logging.h"
#include "options/options.h"
#include "options/validation.h"
#include "util/ip.h"
#include "util/path.h"
#include "version.h"

#ifdef _WIN32
#include "platform/windows/getopt.h"
#else
#include <getopt.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// ACDS Option Globals
// ============================================================================

int opt_acds_port = 27225;
char opt_acds_database_path[OPTIONS_BUFF_SIZE] = "";
char opt_acds_key_path[OPTIONS_BUFF_SIZE] = "";

// ============================================================================
// ACDS Options Array
// ============================================================================

// Note: ACDS uses manual parsing due to its unique global variables
// (opt_acds_database_path, opt_acds_key_path, opt_acds_port) that aren't
// part of the common options_t structure. ACDS is a separate binary from
// the unified ascii-chat binary, so it makes sense for it to have custom parsing.

static struct option acds_options[] = {{"port", required_argument, NULL, 'p'},
                                       {"db", required_argument, NULL, 'd'},
                                       {"key", required_argument, NULL, 'K'},
                                       {"log-file", required_argument, NULL, 'L'},
                                       {"log-level", required_argument, NULL, 'l'},
                                       {"require-server-identity", no_argument, NULL, 'S'},
                                       {"require-client-identity", no_argument, NULL, 'C'},
                                       {"help", no_argument, NULL, 'h'},
                                       {"version", no_argument, NULL, 'v'},
                                       {0, 0, 0, 0}};

// ============================================================================
// ACDS Option Parsing
// ============================================================================

asciichat_error_t acds_options_parse(int argc, char **argv, options_t *opts) {
  const char *optstring = ":p:d:K:L:l:SChv";

  // Pre-pass: Check for --help or --version first
  for (int i = 1; i < argc; i++) {
    if (argv[i] == NULL) {
      break;
    }
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      acds_usage(stdout);
      (void)fflush(stdout);
      _exit(0);
    }
    if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
      acds_print_version();
      (void)fflush(stdout);
      _exit(0);
    }
  }

  // Main parsing loop
  int longindex = 0;
  while (1) {
    longindex = 0;
    int c = getopt_long(argc, argv, optstring, acds_options, &longindex);
    if (c == -1)
      break;

    char argbuf[1024];
    switch (c) {
    case 'p': { // --port
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "port", MODE_ACDS);
      if (!value_str)
        return option_error_invalid();
      char *endptr;
      long port = strtol(value_str, &endptr, 10);
      if (*endptr != '\0' || port < 1 || port > 65535) {
        (void)fprintf(stderr, "Error: Invalid port '%s' (must be 1-65535)\n", value_str);
        return option_error_invalid();
      }
      opt_acds_port = (int)port;
      SAFE_SNPRINTF(opts->port, OPTIONS_BUFF_SIZE, "%d", opt_acds_port);
      break;
    }

    case 'd': { // --db
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "db", MODE_ACDS);
      if (!value_str)
        return option_error_invalid();
      SAFE_STRNCPY(opt_acds_database_path, value_str, sizeof(opt_acds_database_path));
      break;
    }

    case 'K': { // --key
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "key", MODE_ACDS);
      if (!value_str)
        return option_error_invalid();
      SAFE_STRNCPY(opt_acds_key_path, value_str, sizeof(opt_acds_key_path));
      break;
    }

    case 'L': { // --log-file
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "log-file", MODE_ACDS);
      if (!value_str)
        return option_error_invalid();
      SAFE_STRNCPY(opts->log_file, value_str, sizeof(opts->log_file));
      break;
    }

    case 'l': { // --log-level
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "log-level", MODE_ACDS);
      if (!value_str)
        return option_error_invalid();
      if (parse_log_level_option(value_str, opts) != ASCIICHAT_OK)
        return option_error_invalid();
      break;
    }

    case 'S': // --require-server-identity
      opts->require_server_identity = 1;
      break;

    case 'C': // --require-client-identity
      opts->require_client_identity = 1;
      break;

    case 'h': // --help
      acds_usage(stdout);
      (void)fflush(stdout);
      _exit(0);

    case 'v': // --version
      acds_print_version();
      (void)fflush(stdout);
      _exit(0);

    case ':': {
      const char *option_name = acds_options[longindex].name;
      (void)fprintf(stderr, "acds: option '--%s' requires an argument\n", option_name);
      return option_error_invalid();
    }

    case '?':
    default: {
      const char *unknown = argv[optind - 1];
      if (unknown && unknown[0] == '-' && unknown[1] == '-') {
        (void)fprintf(stderr, "acds: unknown option '%s'\n", unknown);
      } else {
        (void)fprintf(stderr, "acds: invalid option\n");
      }
      return option_error_invalid();
    }
    }
  }

  // Parse positional arguments: 0-2 bind addresses
  int num_addresses = 0;
  bool has_ipv4 = false;
  bool has_ipv6 = false;

  while (optind < argc && argv[optind] != NULL && argv[optind][0] != '-' && num_addresses < 2) {
    const char *addr_arg = argv[optind];

    // Parse IPv6 address (remove brackets if present)
    char parsed_addr[OPTIONS_BUFF_SIZE];
    if (parse_ipv6_address(addr_arg, parsed_addr, sizeof(parsed_addr)) == 0) {
      addr_arg = parsed_addr;
    }

    // Check if it's IPv4 or IPv6
    if (is_valid_ipv4(addr_arg)) {
      if (has_ipv4) {
        (void)fprintf(stderr, "Error: Cannot specify multiple IPv4 addresses.\n");
        (void)fprintf(stderr, "Already have: %s\n", opts->address);
        (void)fprintf(stderr, "Cannot add: %s\n", addr_arg);
        return option_error_invalid();
      }
      SAFE_SNPRINTF(opts->address, OPTIONS_BUFF_SIZE, "%s", addr_arg);
      has_ipv4 = true;
      num_addresses++;
    } else if (is_valid_ipv6(addr_arg)) {
      if (has_ipv6) {
        (void)fprintf(stderr, "Error: Cannot specify multiple IPv6 addresses.\n");
        (void)fprintf(stderr, "Already have: %s\n", opts->address6);
        (void)fprintf(stderr, "Cannot add: %s\n", addr_arg);
        return option_error_invalid();
      }
      SAFE_SNPRINTF(opts->address6, OPTIONS_BUFF_SIZE, "%s", addr_arg);
      has_ipv6 = true;
      num_addresses++;
    } else {
      (void)fprintf(stderr, "Error: Invalid IP address '%s'.\n", addr_arg);
      (void)fprintf(stderr, "ACDS bind addresses must be valid IPv4 or IPv6 addresses.\n");
      (void)fprintf(stderr, "Examples:\n");
      (void)fprintf(stderr, "  acds 0.0.0.0\n");
      (void)fprintf(stderr, "  acds ::1\n");
      (void)fprintf(stderr, "  acds 0.0.0.0 ::1\n");
      return option_error_invalid();
    }

    optind++;
  }

  // Check for unexpected extra arguments
  if (optind < argc) {
    (void)fprintf(stderr, "Error: Too many arguments. Maximum 2 bind addresses allowed.\n");
    (void)fprintf(stderr, "Unexpected argument: %s\n", argv[optind]);
    acds_usage(stderr);
    return option_error_invalid();
  }

  // Set default bind addresses if not specified
  if (!has_ipv4 && !has_ipv6) {
    // No addresses specified - bind to localhost only (secure default)
    SAFE_STRNCPY(opts->address, "127.0.0.1", OPTIONS_BUFF_SIZE);
    SAFE_STRNCPY(opts->address6, "::1", OPTIONS_BUFF_SIZE);
  }

  // Set default paths if not specified
  if (opt_acds_database_path[0] == '\0') {
    char *config_dir = get_config_dir();
    if (!config_dir) {
      return SET_ERRNO(ERROR_CONFIG, "Failed to get config directory");
    }
    snprintf(opt_acds_database_path, sizeof(opt_acds_database_path), "%sacds.db", config_dir);
    free(config_dir);
  }

  if (opt_acds_key_path[0] == '\0') {
    char *config_dir = get_config_dir();
    if (!config_dir) {
      return SET_ERRNO(ERROR_CONFIG, "Failed to get config directory");
    }
    snprintf(opt_acds_key_path, sizeof(opt_acds_key_path), "%sacds_identity", config_dir);
    free(config_dir);
  }

  return ASCIICHAT_OK;
}

// ============================================================================
// ACDS Version Text
// ============================================================================

void acds_print_version(void) {
  (void)fprintf(stdout, "acds (ascii-chat discovery service) %s (%s, %s)\n", ASCII_CHAT_VERSION_FULL,
                ASCII_CHAT_BUILD_TYPE, ASCII_CHAT_BUILD_DATE);
  (void)fprintf(stdout, "\n");
  (void)fprintf(stdout, "Built with:\n");

#ifdef __clang__
  (void)fprintf(stdout, "  Compiler: Clang %s\n", __clang_version__);
#elif defined(__GNUC__)
  (void)fprintf(stdout, "  Compiler: GCC %d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
  (void)fprintf(stdout, "  Compiler: MSVC %d\n", _MSC_VER);
#else
  (void)fprintf(stdout, "  Compiler: Unknown\n");
#endif

#ifdef USE_MUSL
  (void)fprintf(stdout, "  C Library: musl\n");
#elif defined(__GLIBC__)
  (void)fprintf(stdout, "  C Library: glibc %d.%d\n", __GLIBC__, __GLIBC_MINOR__);
#elif defined(_WIN32)
  (void)fprintf(stdout, "  C Library: MSVCRT\n");
#elif defined(__APPLE__)
  (void)fprintf(stdout, "  C Library: libSystem\n");
#else
  (void)fprintf(stdout, "  C Library: Unknown\n");
#endif

  (void)fprintf(stdout, "\n");
  (void)fprintf(stdout, "For more information: https://github.com/zfogg/ascii-chat\n");
}

// ============================================================================
// ACDS Usage Text
// ============================================================================

// ACDS-specific usage lines (different from server/client)
#define USAGE_PORT_ACDS_LINE                                                                                           \
  USAGE_INDENT "-p --port PORT          " USAGE_INDENT "discovery service TCP listen port (default: 27225)\n"

#define USAGE_KEY_ACDS_LINE                                                                                            \
  USAGE_INDENT "-k --key PATH           " USAGE_INDENT                                                                 \
               "Ed25519 identity key for server: /path/to/key, gpg:keyid, or 'ssh' for auto-detect (default: "         \
               "~/.config/ascii-chat/acds_identity on Unix, %%APPDATA%%\\ascii-chat\\acds_identity on Windows)\n"

void acds_usage(FILE *desc) {
  (void)fprintf(desc, "acds - ascii-chat discovery service\n\n");
  (void)fprintf(desc, "String registry, session management, and WebRTC signaling for ascii-chat.\n\n");
  (void)fprintf(desc, "USAGE:\n");
  (void)fprintf(desc, "  acds [address1] [address2] [options...]\n\n");
  (void)fprintf(desc, "BIND ADDRESSES (Positional Arguments):\n");
  (void)fprintf(desc, "  0 arguments: Bind to localhost only (127.0.0.1 and ::1) - secure default\n");
  (void)fprintf(desc, "  1 argument:  Bind only to this IPv4 OR IPv6 address\n");
  (void)fprintf(desc, "  2 arguments: Bind to both (must be one IPv4 AND one IPv6, order-independent)\n\n");
  (void)fprintf(desc, "EXAMPLES:\n");
  (void)fprintf(desc, "  acds                          # Localhost only (127.0.0.1 + ::1) - secure default\n");
  (void)fprintf(desc,
                "  acds 0.0.0.0 ::               # All interfaces (dual-stack) - INSECURE, publicly accessible\n");
  (void)fprintf(desc, "  acds 0.0.0.0                  # All IPv4 interfaces\n");
  (void)fprintf(desc, "  acds ::                       # All IPv6 interfaces\n");
  (void)fprintf(desc, "  acds 192.168.1.100 ::1        # Specific IPv4 + localhost IPv6\n");
  (void)fprintf(desc, "  acds --port 9443              # Use port 9443 instead of default 27225\n\n");
  (void)fprintf(desc, "OPTIONS:\n");
  (void)fprintf(desc, USAGE_HELP_LINE);
  (void)fprintf(desc, USAGE_VERSION_LINE);
  (void)fprintf(desc, USAGE_PORT_ACDS_LINE);
  (void)fprintf(desc, USAGE_DATABASE_LINE);
  (void)fprintf(desc, USAGE_KEY_ACDS_LINE);
  (void)fprintf(desc, USAGE_LOG_FILE_LINE);
  (void)fprintf(desc, USAGE_LOG_LEVEL_LINE);
  (void)fprintf(desc, "\n");
  (void)fprintf(desc, "SECURITY OPTIONS:\n");
  (void)fprintf(desc, USAGE_INDENT "--require-server-identity " USAGE_INDENT
                                   "Require servers to provide signed Ed25519 identity when creating sessions\n");
  (void)fprintf(desc, USAGE_INDENT "--require-client-identity " USAGE_INDENT
                                   "Require clients to provide signed Ed25519 identity when joining sessions\n");
  (void)fprintf(desc, "\n");
  (void)fprintf(desc, "For more information: https://github.com/zfogg/ascii-chat\n");
}
