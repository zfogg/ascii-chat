/**
 * @file server.c
 * @ingroup options
 * @brief Server mode option parsing and help text
 *
 * Server-specific command-line argument parsing with support for:
 * - Dual-stack binding (0-2 IPv4/IPv6 addresses)
 * - Client authentication and access control
 * - Connection limits
 * - Network compression
 * - Audio mixer control
 */

#include "options/server.h"
#include "options/common.h"

#include "asciichat_errno.h"
#include "common.h"
#include "log/logging.h"
#include "options/options.h"
#include "options/validation.h"
#include "util/ip.h"
#include "util/parsing.h"
#include "util/password.h"
#include "video/ascii.h"

#ifdef _WIN32
#include "platform/windows/getopt.h"
#else
#include <getopt.h>
#endif

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Server Options Array
// ============================================================================

static struct option server_options[] = {{"port", required_argument, NULL, 'p'},
                                         {"palette", required_argument, NULL, 'P'},
                                         {"palette-chars", required_argument, NULL, 'C'},
                                         {"encrypt", no_argument, NULL, 'E'},
                                         {"key", required_argument, NULL, 'K'},
                                         {"password", optional_argument, NULL, 1009},
                                         {"keyfile", required_argument, NULL, 'F'},
                                         {"no-encrypt", no_argument, NULL, 1005},
                                         {"client-keys", required_argument, NULL, 1008},
                                         {"compression-level", required_argument, NULL, 1019},
                                         {"no-compress", no_argument, NULL, 1022},
                                         {"encode-audio", no_argument, NULL, 1023},
                                         {"no-encode-audio", no_argument, NULL, 1024},
                                         {"max-clients", required_argument, NULL, 1021},
                                         {"no-audio-mixer", no_argument, NULL, 1026},
                                         {"help", optional_argument, NULL, 'h'},
                                         {0, 0, 0, 0}};

// ============================================================================
// Server Option Parsing
// ============================================================================

asciichat_error_t parse_server_options(int argc, char **argv) {
  const char *optstring = ":p:P:C:EK:F:h";

  // Pre-pass: Check for --help first
  for (int i = 1; i < argc; i++) {
    if (argv[i] == NULL) {
      break;
    }
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      usage_server(stdout);
      (void)fflush(stdout);
      _exit(0);
    }
  }

  // Main parsing loop
  int longindex = 0;
  while (1) {
    longindex = 0;
    int c = getopt_long(argc, argv, optstring, server_options, &longindex);
    if (c == -1)
      break;

    char argbuf[1024];
    switch (c) {
    case 0:
      // Long-only options
      break;

    case 'p': {
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "port", MODE_SERVER);
      if (!value_str)
        return option_error_invalid();
      uint16_t port_num;
      if (!validate_port_opt(value_str, &port_num))
        return option_error_invalid();
      SAFE_SNPRINTF(opt_port, OPTIONS_BUFF_SIZE, "%s", value_str);
      break;
    }

    case 'P': { // --palette
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "palette", MODE_SERVER);
      if (!value_str)
        return option_error_invalid();
      if (strcmp(value_str, "standard") == 0) {
        opt_palette_type = PALETTE_STANDARD;
      } else if (strcmp(value_str, "blocks") == 0) {
        opt_palette_type = PALETTE_BLOCKS;
      } else if (strcmp(value_str, "digital") == 0) {
        opt_palette_type = PALETTE_DIGITAL;
      } else if (strcmp(value_str, "minimal") == 0) {
        opt_palette_type = PALETTE_MINIMAL;
      } else if (strcmp(value_str, "cool") == 0) {
        opt_palette_type = PALETTE_COOL;
      } else if (strcmp(value_str, "custom") == 0) {
        opt_palette_type = PALETTE_CUSTOM;
      } else {
        (void)fprintf(stderr,
                      "Invalid palette '%s'. Valid palettes: standard, blocks, digital, minimal, cool, custom\n",
                      value_str);
        return option_error_invalid();
      }
      break;
    }

    case 'C': { // --palette-chars
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "palette-chars", MODE_SERVER);
      if (!value_str)
        return option_error_invalid();
      if (strlen(value_str) >= sizeof(opt_palette_custom)) {
        (void)fprintf(stderr, "Invalid palette-chars: too long (%zu chars, max %zu)\n", strlen(value_str),
                      sizeof(opt_palette_custom) - 1);
        return option_error_invalid();
      }
      SAFE_STRNCPY(opt_palette_custom, value_str, sizeof(opt_palette_custom));
      opt_palette_custom[sizeof(opt_palette_custom) - 1] = '\0';
      opt_palette_custom_set = true;
      opt_palette_type = PALETTE_CUSTOM;
      break;
    }

    case 'E': // --encrypt
      opt_encrypt_enabled = 1;
      break;

    case 'K': { // --key
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "key", MODE_SERVER);
      if (!value_str)
        return option_error_invalid();
      SAFE_SNPRINTF(opt_encrypt_key, OPTIONS_BUFF_SIZE, "%s", value_str);
      opt_encrypt_enabled = 1;
      break;
    }

    case 1009: { // --password
      if (optarg) {
        SAFE_SNPRINTF(opt_password, OPTIONS_BUFF_SIZE, "%s", optarg);
      } else {
        char *pw = read_password_from_stdin("Enter encryption password: ");
        if (!pw) {
          (void)fprintf(stderr, "Failed to read password\n");
          return option_error_invalid();
        }
        SAFE_SNPRINTF(opt_password, OPTIONS_BUFF_SIZE, "%s", pw);
        SAFE_FREE(pw);
      }
      opt_encrypt_enabled = 1;
      break;
    }

    case 'F': { // --keyfile
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "keyfile", MODE_SERVER);
      if (!value_str)
        return option_error_invalid();
      SAFE_SNPRINTF(opt_encrypt_keyfile, OPTIONS_BUFF_SIZE, "%s", value_str);
      opt_encrypt_enabled = 1;
      break;
    }

    case 1005: // --no-encrypt
      opt_no_encrypt = 1;
      break;

    case 1008: { // --client-keys
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "client-keys", MODE_SERVER);
      if (!value_str)
        return option_error_invalid();
      SAFE_SNPRINTF(opt_client_keys, OPTIONS_BUFF_SIZE, "%s", value_str);
      break;
    }

    case 1019: { // --compression-level
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "compression-level", MODE_SERVER);
      if (!value_str)
        return option_error_invalid();
      int level = strtoint_safe(value_str);
      if (level == INT_MIN || level < 1 || level > 9) {
        (void)fprintf(stderr, "Invalid compression level '%s'. Must be 1-9.\n", value_str);
        return option_error_invalid();
      }
      opt_compression_level = level;
      break;
    }

    case 1022: // --no-compress
      opt_no_compress = true;
      break;

    case 1023: // --encode-audio
      opt_encode_audio = true;
      break;

    case 1024: // --no-encode-audio
      opt_encode_audio = false;
      break;

    case 1021: { // --max-clients
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "max-clients", MODE_SERVER);
      if (!value_str)
        return option_error_invalid();

      char error_msg[256];
      int max_clients = validate_opt_max_clients(value_str, error_msg, sizeof(error_msg));
      if (max_clients == INT_MIN) {
        (void)fprintf(stderr, "Invalid max-clients: %s\n", error_msg);
        return option_error_invalid();
      }
      opt_max_clients = max_clients;
      break;
    }

    case 1026: // --no-audio-mixer
      opt_no_audio_mixer = true;
      log_info("Audio mixer disabled - will send silence instead of mixing");
      break;

    case 'h': // --help
      usage_server(stdout);
      (void)fflush(stdout);
      _exit(0);

    case ':': {
      const char *option_name = server_options[longindex].name;
      (void)fprintf(stderr, "server: option '--%s' requires an argument\n", option_name);
      return option_error_invalid();
    }

    case '?':
    default: {
      const char *unknown = argv[optind - 1];
      if (unknown && unknown[0] == '-' && unknown[1] == '-') {
        const char *suggestion = find_similar_option(unknown + 2, server_options);
        if (suggestion) {
          (void)fprintf(stderr, "server: unknown option '%s'. Did you mean '--%s'?\n", unknown, suggestion);
        } else {
          (void)fprintf(stderr, "server: unknown option '%s'\n", unknown);
        }
      } else {
        (void)fprintf(stderr, "server: invalid option\n");
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
        (void)fprintf(stderr, "Already have: %s\n", opt_address);
        (void)fprintf(stderr, "Cannot add: %s\n", addr_arg);
        return option_error_invalid();
      }
      SAFE_SNPRINTF(opt_address, OPTIONS_BUFF_SIZE, "%s", addr_arg);
      has_ipv4 = true;
      num_addresses++;
    } else if (is_valid_ipv6(addr_arg)) {
      if (has_ipv6) {
        (void)fprintf(stderr, "Error: Cannot specify multiple IPv6 addresses.\n");
        (void)fprintf(stderr, "Already have: %s\n", opt_address6);
        (void)fprintf(stderr, "Cannot add: %s\n", addr_arg);
        return option_error_invalid();
      }
      SAFE_SNPRINTF(opt_address6, OPTIONS_BUFF_SIZE, "%s", addr_arg);
      has_ipv6 = true;
      num_addresses++;
    } else {
      (void)fprintf(stderr, "Error: Invalid IP address '%s'.\n", addr_arg);
      (void)fprintf(stderr, "Server bind addresses must be valid IPv4 or IPv6 addresses.\n");
      (void)fprintf(stderr, "Examples:\n");
      (void)fprintf(stderr, "  ascii-chat server 0.0.0.0\n");
      (void)fprintf(stderr, "  ascii-chat server ::1\n");
      (void)fprintf(stderr, "  ascii-chat server 0.0.0.0 ::1\n");
      return option_error_invalid();
    }

    optind++;
  }

  return ASCIICHAT_OK;
}

// ============================================================================
// Server Usage Text
// ============================================================================

#define USAGE_INDENT "        "

void usage_server(FILE *desc) {
  (void)fprintf(desc, "ascii-chat - server options\n\n");
  (void)fprintf(desc, "USAGE:\n");
  (void)fprintf(desc, "  ascii-chat server [address1] [address2] [options...]\n\n");
  (void)fprintf(desc, "BIND ADDRESSES (Positional Arguments):\n");
  (void)fprintf(desc, "  0 arguments: Bind to 127.0.0.1 and ::1 (localhost dual-stack)\n");
  (void)fprintf(desc, "  1 argument:  Bind only to this IPv4 OR IPv6 address\n");
  (void)fprintf(desc, "  2 arguments: Bind to both (must be one IPv4 AND one IPv6, order-independent)\n\n");
  (void)fprintf(desc, "EXAMPLES:\n");
  (void)fprintf(desc, "  ascii-chat server                    # Localhost only (127.0.0.1 + ::1)\n");
  (void)fprintf(desc, "  ascii-chat server 0.0.0.0            # All IPv4 interfaces\n");
  (void)fprintf(desc, "  ascii-chat server ::                 # All IPv6 interfaces\n");
  (void)fprintf(desc, "  ascii-chat server 0.0.0.0 ::         # All interfaces (dual-stack)\n");
  (void)fprintf(desc, "  ascii-chat server 192.168.1.100 ::1  # Specific IPv4 + localhost IPv6\n\n");
  (void)fprintf(desc, "OPTIONS:\n");
  (void)fprintf(desc, USAGE_INDENT "-h --help            " USAGE_INDENT "print this help\n");
  (void)fprintf(desc, USAGE_INDENT "-p --port PORT       " USAGE_INDENT "TCP port to listen on (default: 27224)\n");
  (void)fprintf(desc,
                USAGE_INDENT "   --max-clients N   " USAGE_INDENT "maximum simultaneous clients (1-9, default: 9)\n");
  (void)fprintf(desc, USAGE_INDENT "-P --palette PALETTE " USAGE_INDENT "ASCII character palette: "
                                   "standard, blocks, digital, minimal, cool, custom (default: standard)\n");
  (void)fprintf(desc, USAGE_INDENT "-C --palette-chars CHARS     " USAGE_INDENT
                                   "Custom palette characters for --palette=custom (implies --palette=custom)\n");
  (void)fprintf(desc,
                USAGE_INDENT "-E --encrypt         " USAGE_INDENT "enable packet encryption (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT
                "-K --key KEY         " USAGE_INDENT
                "SSH/GPG key file for authentication: /path/to/key, gpg:keyid, github:user, gitlab:user, or 'ssh' "
                "(implies --encrypt) (default: [unset])\n");
  (void)fprintf(
      desc, USAGE_INDENT
      "   --password [PASS] " USAGE_INDENT
      "password for connection encryption (prompts if not provided) (implies --encrypt) (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "-F --keyfile FILE    " USAGE_INDENT "read encryption key from file "
                                   "(implies --encrypt) (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "   --no-encrypt      " USAGE_INDENT "disable encryption (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "   --client-keys KEYS" USAGE_INDENT
                                   "allowed client public keys (comma-separated, supports github:user, "
                                   "gitlab:user, gpg:keyid, or SSH pubkey) (default: [unset])\n");
  (void)fprintf(desc,
                USAGE_INDENT "   --compression-level N " USAGE_INDENT "zstd compression level 1-9 (default: 1)\n");
  (void)fprintf(desc,
                USAGE_INDENT "   --no-compress     " USAGE_INDENT "disable frame compression (default: [unset])\n");
  (void)fprintf(desc,
                USAGE_INDENT "   --encode-audio    " USAGE_INDENT "enable Opus audio encoding (default: enabled)\n");
  (void)fprintf(desc, USAGE_INDENT "   --no-encode-audio " USAGE_INDENT "disable Opus audio encoding\n");
  (void)fprintf(desc, USAGE_INDENT "   --no-audio-mixer  " USAGE_INDENT
                                   "disable audio mixer - send silence (debug mode only)\n");
}
