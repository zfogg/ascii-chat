/**
 * @file client.c
 * @ingroup options
 * @brief Client mode option parsing and help text
 *
 * Client-specific command-line argument parsing with support for:
 * - Server connection (positional address[:port])
 * - Webcam configuration
 * - Audio capture/playback
 * - Display settings
 * - Cryptographic authentication
 * - Network compression
 * - Reconnection behavior
 */

#include "options/client.h"
#include "options/common.h"

#include "asciichat_errno.h"
#include "audio/audio.h"
#include "common.h"
#include "crypto/crypto.h"
#include "log/logging.h"
#include "options/levenshtein.h"
#include "options/options.h"
#include "options/validation.h"
#include "platform/system.h"
#include "util/ip.h"
#include "util/parsing.h"
#include "util/password.h"
#include "video/ascii.h"
#include "video/webcam/webcam.h"

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
// Client Options Array
// ============================================================================

static struct option client_options[] = {{"port", required_argument, NULL, 'p'},
                                         {"width", required_argument, NULL, 'x'},
                                         {"height", required_argument, NULL, 'y'},
                                         {"webcam-index", required_argument, NULL, 'c'},
                                         {"webcam-flip", no_argument, NULL, 'f'},
                                         {"test-pattern", no_argument, NULL, 1004},
                                         {"fps", required_argument, NULL, 1003},
                                         {"color-mode", required_argument, NULL, 1000},
                                         {"show-capabilities", no_argument, NULL, 1001},
                                         {"utf8", no_argument, NULL, 1002},
                                         {"render-mode", required_argument, NULL, 'M'},
                                         {"palette", required_argument, NULL, 'P'},
                                         {"palette-chars", required_argument, NULL, 'C'},
                                         {"audio", no_argument, NULL, 'A'},
                                         {"microphone-index", required_argument, NULL, 1028},
                                         {"speakers-index", required_argument, NULL, 1029},
                                         {"audio-analysis", no_argument, NULL, 1025},
                                         {"no-audio-playback", no_argument, NULL, 1027},
                                         {"stretch", no_argument, NULL, 's'},
                                         {"snapshot", no_argument, NULL, 'S'},
                                         {"snapshot-delay", required_argument, NULL, 'D'},
                                         {"strip-ansi", no_argument, NULL, 1017},
                                         {"encrypt", no_argument, NULL, 'E'},
                                         {"key", required_argument, NULL, 'K'},
                                         {"password", optional_argument, NULL, 1009},
                                         {"keyfile", required_argument, NULL, 'F'},
                                         {"no-encrypt", no_argument, NULL, 1005},
                                         {"server-key", required_argument, NULL, 1006},
                                         {"list-webcams", no_argument, NULL, 1013},
                                         {"list-microphones", no_argument, NULL, 1014},
                                         {"list-speakers", no_argument, NULL, 1015},
                                         {"compression-level", required_argument, NULL, 1019},
                                         {"no-compress", no_argument, NULL, 1022},
                                         {"encode-audio", no_argument, NULL, 1023},
                                         {"no-encode-audio", no_argument, NULL, 1024},
                                         {"reconnect", required_argument, NULL, 1020},
                                         {"help", optional_argument, NULL, 'h'},
                                         {0, 0, 0, 0}};

// ============================================================================
// Client Option Parsing
// ============================================================================

asciichat_error_t parse_client_options(int argc, char **argv) {
  const char *optstring = ":p:x:y:c:fM:P:C:AsSD:EK:F:h";

  // Pre-pass: Check for --help first
  for (int i = 1; i < argc; i++) {
    if (argv[i] == NULL) {
      break;
    }
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      usage_client(stdout);
      (void)fflush(stdout);
      _exit(0);
    }
  }

  // Main parsing loop
  int longindex = 0;
  while (1) {
    longindex = 0;
    int c = getopt_long(argc, argv, optstring, client_options, &longindex);
    if (c == -1)
      break;

    char argbuf[1024];
    switch (c) {
    case 0:
      // Long-only options
      break;

    case 'p': {
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "port", MODE_CLIENT);
      if (!value_str)
        return option_error_invalid();
      uint16_t port_num;
      if (!validate_port_opt(value_str, &port_num))
        return option_error_invalid();
      SAFE_SNPRINTF(opt_port, OPTIONS_BUFF_SIZE, "%s", value_str);
      extern bool port_explicitly_set_via_flag;
      port_explicitly_set_via_flag = true;
      break;
    }

    case 'x': {
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "width", MODE_CLIENT);
      if (!value_str)
        return option_error_invalid();
      int width_val;
      if (!validate_positive_int_opt(value_str, &width_val, "width"))
        return option_error_invalid();
      opt_width = (unsigned short int)width_val;
      extern bool auto_width;
      auto_width = false;
      break;
    }

    case 'y': {
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "height", MODE_CLIENT);
      if (!value_str)
        return option_error_invalid();
      int height_val;
      if (!validate_positive_int_opt(value_str, &height_val, "height"))
        return option_error_invalid();
      opt_height = (unsigned short int)height_val;
      extern bool auto_height;
      auto_height = false;
      break;
    }

    case 'c': {
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "webcam-index", MODE_CLIENT);
      if (!value_str)
        return option_error_invalid();
      unsigned short int index_val;
      if (!validate_webcam_index(value_str, &index_val))
        return option_error_invalid();
      opt_webcam_index = index_val;
      break;
    }

    case 'f':
      opt_webcam_flip = !opt_webcam_flip;
      break;

    case 1000: { // --color-mode
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "color-mode", MODE_CLIENT);
      if (!value_str)
        return option_error_invalid();
      if (strcmp(value_str, "auto") == 0) {
        opt_color_mode = COLOR_MODE_AUTO;
      } else if (strcmp(value_str, "none") == 0 || strcmp(value_str, "mono") == 0) {
        opt_color_mode = COLOR_MODE_NONE;
      } else if (strcmp(value_str, "16") == 0 || strcmp(value_str, "16color") == 0) {
        opt_color_mode = COLOR_MODE_16_COLOR;
      } else if (strcmp(value_str, "256") == 0 || strcmp(value_str, "256color") == 0) {
        opt_color_mode = COLOR_MODE_256_COLOR;
      } else if (strcmp(value_str, "truecolor") == 0 || strcmp(value_str, "24bit") == 0) {
        opt_color_mode = COLOR_MODE_TRUECOLOR;
      } else {
        (void)fprintf(stderr, "Error: Invalid color mode '%s'. Valid modes: auto, none, 16, 256, truecolor\n",
                      value_str);
        return option_error_invalid();
      }
      break;
    }

    case 1001: // --show-capabilities
      opt_show_capabilities = 1;
      break;

    case 1002: // --utf8
      opt_force_utf8 = 1;
      break;

    case 1003: { // --fps
      extern int g_max_fps;
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "fps", MODE_CLIENT);
      if (!value_str)
        return option_error_invalid();
      int fps_val;
      if (!validate_fps_opt(value_str, &fps_val))
        return option_error_invalid();
      g_max_fps = fps_val;
      break;
    }

    case 1004: // --test-pattern
      opt_test_pattern = true;
      log_info("Using test pattern mode - webcam will not be opened");
      break;

    case 1013: { // --list-webcams
      webcam_device_info_t *devices = NULL;
      unsigned int device_count = 0;
      asciichat_error_t list_result = webcam_list_devices(&devices, &device_count);
      if (list_result != ASCIICHAT_OK) {
        (void)fprintf(stderr, "Error: Failed to enumerate webcam devices\n");
        _exit(1);
      }
      if (device_count == 0) {
        (void)fprintf(stdout, "No webcam devices found.\n");
      } else {
        (void)fprintf(stdout, "Available webcam devices:\n");
        for (unsigned int i = 0; i < device_count; i++) {
          (void)fprintf(stdout, "  %u: %s\n", devices[i].index, devices[i].name);
        }
      }
      webcam_free_device_list(devices);
      (void)fflush(stdout);
      _exit(0);
    }

    case 1014: { // --list-microphones
      audio_device_info_t *devices = NULL;
      unsigned int device_count = 0;
      asciichat_error_t list_result = audio_list_input_devices(&devices, &device_count);
      if (list_result != ASCIICHAT_OK) {
        (void)fprintf(stderr, "Error: Failed to enumerate audio input devices\n");
        _exit(1);
      }
      if (device_count == 0) {
        (void)fprintf(stdout, "No audio input devices (microphones) found.\n");
      } else {
        (void)fprintf(stdout, "Available audio input devices (microphones):\n");
        for (unsigned int i = 0; i < device_count; i++) {
          (void)fprintf(stdout, "  %d: %s (%d ch, %.0f Hz)%s\n", devices[i].index, devices[i].name,
                        devices[i].max_input_channels, devices[i].default_sample_rate,
                        devices[i].is_default_input ? " [DEFAULT]" : "");
        }
      }
      audio_free_device_list(devices);
      (void)fflush(stdout);
      _exit(0);
    }

    case 1015: { // --list-speakers
      audio_device_info_t *devices = NULL;
      unsigned int device_count = 0;
      asciichat_error_t list_result = audio_list_output_devices(&devices, &device_count);
      if (list_result != ASCIICHAT_OK) {
        (void)fprintf(stderr, "Error: Failed to enumerate audio output devices\n");
        _exit(1);
      }
      if (device_count == 0) {
        (void)fprintf(stdout, "No audio output devices (speakers) found.\n");
      } else {
        (void)fprintf(stdout, "Available audio output devices (speakers):\n");
        for (unsigned int i = 0; i < device_count; i++) {
          (void)fprintf(stdout, "  %d: %s (%d ch, %.0f Hz)%s\n", devices[i].index, devices[i].name,
                        devices[i].max_output_channels, devices[i].default_sample_rate,
                        devices[i].is_default_output ? " [DEFAULT]" : "");
        }
      }
      audio_free_device_list(devices);
      (void)fflush(stdout);
      _exit(0);
    }

    case 'M': { // --render-mode
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "render-mode", MODE_CLIENT);
      if (!value_str)
        return option_error_invalid();
      if (strcmp(value_str, "foreground") == 0 || strcmp(value_str, "fg") == 0) {
        opt_render_mode = RENDER_MODE_FOREGROUND;
      } else if (strcmp(value_str, "background") == 0 || strcmp(value_str, "bg") == 0) {
        opt_render_mode = RENDER_MODE_BACKGROUND;
      } else if (strcmp(value_str, "half-block") == 0 || strcmp(value_str, "halfblock") == 0) {
        opt_render_mode = RENDER_MODE_HALF_BLOCK;
      } else {
        (void)fprintf(stderr, "Error: Invalid render mode '%s'. Valid modes: foreground, background, half-block\n",
                      value_str);
        return option_error_invalid();
      }
      break;
    }

    case 'P': { // --palette
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "palette", MODE_CLIENT);
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
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "palette-chars", MODE_CLIENT);
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

    case 'A': // --audio
      opt_audio_enabled = 1;
      break;

    case 1028: { // --microphone-index
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "microphone-index", MODE_CLIENT);
      if (!value_str)
        return option_error_invalid();
      char error_msg[256];
      int mic_index = validate_opt_device_index(value_str, error_msg, sizeof(error_msg));
      if (mic_index == INT_MIN) {
        (void)fprintf(stderr, "Invalid microphone index: %s\n", error_msg);
        return option_error_invalid();
      }
      opt_microphone_index = mic_index;
      break;
    }

    case 1029: { // --speakers-index
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "speakers-index", MODE_CLIENT);
      if (!value_str)
        return option_error_invalid();
      char error_msg[256];
      int speaker_index = validate_opt_device_index(value_str, error_msg, sizeof(error_msg));
      if (speaker_index == INT_MIN) {
        (void)fprintf(stderr, "Invalid speakers index: %s\n", error_msg);
        return option_error_invalid();
      }
      opt_speakers_index = speaker_index;
      break;
    }

    case 1025: // --audio-analysis
      opt_audio_analysis_enabled = 1;
      break;

    case 1027: // --no-audio-playback
      opt_audio_no_playback = 1;
      break;

    case 's': // --stretch
      opt_stretch = 1;
      break;

    case 'S': // --snapshot
      opt_snapshot_mode = 1;
      break;

    case 'D': { // --snapshot-delay
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "snapshot-delay", MODE_CLIENT);
      if (!value_str)
        return option_error_invalid();
      char *endptr;
      float delay = strtof(value_str, &endptr);
      if (endptr == value_str || *endptr != '\0' || delay < 0.0f) {
        (void)fprintf(stderr, "Invalid snapshot delay '%s'. Must be a non-negative number.\n", value_str);
        return option_error_invalid();
      }
      opt_snapshot_delay = delay;
      break;
    }

    case 1017: // --strip-ansi
      opt_strip_ansi = 1;
      break;

    case 'E': // --encrypt
      opt_encrypt_enabled = 1;
      break;

    case 'K': { // --key
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "key", MODE_CLIENT);
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
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "keyfile", MODE_CLIENT);
      if (!value_str)
        return option_error_invalid();
      SAFE_SNPRINTF(opt_encrypt_keyfile, OPTIONS_BUFF_SIZE, "%s", value_str);
      opt_encrypt_enabled = 1;
      break;
    }

    case 1005: // --no-encrypt
      opt_no_encrypt = 1;
      break;

    case 1006: { // --server-key
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "server-key", MODE_CLIENT);
      if (!value_str)
        return option_error_invalid();
      SAFE_SNPRINTF(opt_server_key, OPTIONS_BUFF_SIZE, "%s", value_str);
      break;
    }

    case 1019: { // --compression-level
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "compression-level", MODE_CLIENT);
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

    case 1020: { // --reconnect
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "reconnect", MODE_CLIENT);
      if (!value_str)
        return option_error_invalid();
      if (strcmp(value_str, "off") == 0 || strcmp(value_str, "0") == 0) {
        opt_reconnect_attempts = 0;
      } else if (strcmp(value_str, "auto") == 0 || strcmp(value_str, "-1") == 0) {
        opt_reconnect_attempts = -1;
      } else {
        int attempts = strtoint_safe(value_str);
        if (attempts == INT_MIN || attempts < 0 || attempts > 999) {
          (void)fprintf(stderr, "Invalid reconnect value '%s'. Use 'off', 'auto', or 1-999.\n", value_str);
          return option_error_invalid();
        }
        opt_reconnect_attempts = attempts;
      }
      break;
    }

    case 'h': // --help
      usage_client(stdout);
      (void)fflush(stdout);
      _exit(0);

    case ':': {
      const char *option_name = client_options[longindex].name;
      (void)fprintf(stderr, "client: option '--%s' requires an argument\n", option_name);
      return option_error_invalid();
    }

    case '?':
    default: {
      const char *unknown = argv[optind - 1];
      if (unknown && unknown[0] == '-' && unknown[1] == '-') {
        const char *suggestion = find_similar_option(unknown + 2, client_options);
        if (suggestion) {
          (void)fprintf(stderr, "client: unknown option '%s'. Did you mean '--%s'?\n", unknown, suggestion);
        } else {
          (void)fprintf(stderr, "client: unknown option '%s'\n", unknown);
        }
      } else {
        (void)fprintf(stderr, "client: invalid option\n");
      }
      return option_error_invalid();
    }
    }
  }

  // Parse positional argument: [address][:port]
  if (optind < argc && argv[optind] != NULL && argv[optind][0] != '-') {
    const char *address_arg = argv[optind];
    optind++;

    // Check for port in address (format: address:port or [ipv6]:port)
    const char *colon = strrchr(address_arg, ':');
    bool has_port_in_address = false;

    if (colon != NULL) {
      // Check if this is IPv6 with port [::1]:port or plain hostname:port
      if (address_arg[0] == '[') {
        // IPv6 with brackets: [address]:port
        const char *closing_bracket = strchr(address_arg, ']');
        if (closing_bracket && closing_bracket < colon) {
          has_port_in_address = true;
          // Extract address
          size_t addr_len = (size_t)(closing_bracket - address_arg - 1);
          if (addr_len >= OPTIONS_BUFF_SIZE) {
            (void)fprintf(stderr, "Error: IPv6 address too long\n");
            return option_error_invalid();
          }
          SAFE_SNPRINTF(opt_address, OPTIONS_BUFF_SIZE, "%.*s", (int)addr_len, address_arg + 1);
          // Extract port
          const char *port_str = colon + 1;
          uint16_t port_num;
          if (!validate_port_opt(port_str, &port_num)) {
            return option_error_invalid();
          }
          SAFE_SNPRINTF(opt_port, OPTIONS_BUFF_SIZE, "%s", port_str);
        }
      } else {
        // Check if it's IPv6 without brackets (no port allowed)
        // or hostname/IPv4:port
        size_t colon_count = 0;
        for (const char *p = address_arg; *p; p++) {
          if (*p == ':')
            colon_count++;
        }

        if (colon_count == 1) {
          // Likely hostname:port or IPv4:port
          has_port_in_address = true;
          size_t addr_len = (size_t)(colon - address_arg);
          if (addr_len >= OPTIONS_BUFF_SIZE) {
            (void)fprintf(stderr, "Error: Address too long\n");
            return option_error_invalid();
          }
          SAFE_SNPRINTF(opt_address, OPTIONS_BUFF_SIZE, "%.*s", (int)addr_len, address_arg);
          // Extract port
          const char *port_str = colon + 1;
          uint16_t port_num;
          if (!validate_port_opt(port_str, &port_num)) {
            return option_error_invalid();
          }
          SAFE_SNPRINTF(opt_port, OPTIONS_BUFF_SIZE, "%s", port_str);
        } else {
          // Multiple colons - likely bare IPv6 address
          SAFE_SNPRINTF(opt_address, OPTIONS_BUFF_SIZE, "%s", address_arg);
        }
      }
    } else {
      // No colon - just an address
      SAFE_SNPRINTF(opt_address, OPTIONS_BUFF_SIZE, "%s", address_arg);
    }

    // Validate addresses that contain dots as potential IPv4 addresses
    // If it has a dot, it's either a valid IPv4 or a hostname with domain (e.g., example.com)
    // Check if it looks like an IPv4 attempt (starts with digit and has dots)
    bool has_dot = strchr(opt_address, '.') != NULL;
    bool starts_with_digit = opt_address[0] >= '0' && opt_address[0] <= '9';

    if (has_dot && starts_with_digit) {
      // Looks like an IPv4 attempt - validate strictly
      if (!is_valid_ipv4(opt_address)) {
        (void)fprintf(stderr, "Error: Invalid IPv4 address '%s'.\n", opt_address);
        (void)fprintf(stderr, "IPv4 addresses must have exactly 4 octets (0-255) separated by dots.\n");
        (void)fprintf(stderr, "Examples: 127.0.0.1, 192.168.1.1\n");
        (void)fprintf(stderr, "For hostnames, use letters: example.com, localhost\n");
        return option_error_invalid();
      }
    }

    // Check for port conflict
    extern bool port_explicitly_set_via_flag;
    if (has_port_in_address && port_explicitly_set_via_flag) {
      (void)fprintf(stderr,
                    "Error: Cannot specify port in both positional argument (%s) and --port flag\n"
                    "Use either 'ascii-chat client %s' OR 'ascii-chat client %.*s --port <port>'\n",
                    address_arg, address_arg, (int)(colon - address_arg), address_arg);
      return option_error_invalid();
    }
  }

  return ASCIICHAT_OK;
}

// ============================================================================
// Client Usage Text
// ============================================================================

#define USAGE_INDENT "        "

void usage_client(FILE *desc) {
  (void)fprintf(desc, "ascii-chat - client options\n\n");
  (void)fprintf(desc, "USAGE:\n");
  (void)fprintf(desc, "  ascii-chat client [address][:port] [options...]\n\n");
  (void)fprintf(desc, "ADDRESS FORMATS:\n");
  (void)fprintf(desc, "  (none)                     connect to localhost:27224\n");
  (void)fprintf(desc, "  hostname                   connect to hostname:27224\n");
  (void)fprintf(desc, "  hostname:port              connect to hostname:port\n");
  (void)fprintf(desc, "  192.168.1.1                connect to IPv4:27224\n");
  (void)fprintf(desc, "  192.168.1.1:8080           connect to IPv4:port\n");
  (void)fprintf(desc, "  ::1                        connect to IPv6:27224\n");
  (void)fprintf(desc, "  [::1]:8080                 connect to IPv6:port (brackets required with port)\n\n");
  (void)fprintf(desc, "OPTIONS:\n");
  (void)fprintf(desc, USAGE_INDENT "-h --help                    " USAGE_INDENT "print this help\n");
  (void)fprintf(desc, USAGE_INDENT "-p --port PORT               " USAGE_INDENT
                                   "override port from address (default: 27224)\n");
  (void)fprintf(desc, USAGE_INDENT "   --reconnect VALUE         " USAGE_INDENT
                                   "reconnection behavior: off, auto, or 1-999 (default: auto)\n");
  (void)fprintf(desc, USAGE_INDENT "-x --width WIDTH             " USAGE_INDENT "render width (default: [auto-set])\n");
  (void)fprintf(desc,
                USAGE_INDENT "-y --height HEIGHT           " USAGE_INDENT "render height (default: [auto-set])\n");
  (void)fprintf(desc, USAGE_INDENT "-c --webcam-index CAMERA     " USAGE_INDENT
                                   "webcam device index (0-based) (default: 0)\n");
  (void)fprintf(desc,
                USAGE_INDENT "   --list-webcams            " USAGE_INDENT "list available webcam devices and exit\n");
  (void)fprintf(desc, USAGE_INDENT "   --list-microphones        " USAGE_INDENT
                                   "list available audio input devices and exit\n");
  (void)fprintf(desc, USAGE_INDENT "   --list-speakers           " USAGE_INDENT
                                   "list available audio output devices and exit\n");
  (void)fprintf(desc, USAGE_INDENT "   --microphone-index INDEX  " USAGE_INDENT
                                   "microphone device index (-1 for default) (default: -1)\n");
  (void)fprintf(desc, USAGE_INDENT "   --speakers-index INDEX    " USAGE_INDENT
                                   "speakers device index (-1 for default) (default: -1)\n");
  (void)fprintf(desc, USAGE_INDENT "-f --webcam-flip             " USAGE_INDENT "toggle horizontal flip of webcam "
                                   "image (default: flipped)\n");
  (void)fprintf(desc, USAGE_INDENT "   --test-pattern            " USAGE_INDENT "use test pattern instead of webcam "
                                   "(for testing multiple clients)\n");
  (void)fprintf(desc, USAGE_INDENT "   --fps FPS                 " USAGE_INDENT "desired frame rate 1-144 "
#ifdef _WIN32
                                   "(default: 30 for Windows)\n");
#else
                                   "(default: 60 for Unix)\n");
#endif
  (void)fprintf(desc,
                USAGE_INDENT "   --color-mode MODE         " USAGE_INDENT "color modes: auto, none, 16, 256, truecolor "
                             "(default: auto)\n");
  (void)fprintf(desc, USAGE_INDENT "   --show-capabilities       " USAGE_INDENT
                                   "show detected terminal capabilities and exit\n");
  (void)fprintf(desc, USAGE_INDENT "   --utf8                    " USAGE_INDENT
                                   "force enable UTF-8/Unicode support (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "-M --render-mode MODE        " USAGE_INDENT "Rendering modes: "
                                   "foreground, background, half-block (default: foreground)\n");
  (void)fprintf(desc, USAGE_INDENT "-P --palette PALETTE         " USAGE_INDENT "ASCII character palette: "
                                   "standard, blocks, digital, minimal, cool, custom (default: standard)\n");
  (void)fprintf(desc, USAGE_INDENT "-C --palette-chars CHARS     " USAGE_INDENT
                                   "Custom palette characters (implies --palette=custom) (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "-A --audio                   " USAGE_INDENT
                                   "enable audio capture and playback (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "   --audio-analysis          " USAGE_INDENT
                                   "track and report audio quality metrics (with --audio) (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT
                "   --no-audio-playback       " USAGE_INDENT
                "disable speaker playback but keep recording received audio (debug mode) (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "-s --stretch                 " USAGE_INDENT "stretch or shrink video to fit "
                                   "(ignore aspect ratio) (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "-S --snapshot                " USAGE_INDENT
                                   "capture single frame and exit (default: [unset])\n");
  (void)fprintf(
      desc, USAGE_INDENT "-D --snapshot-delay SECONDS  " USAGE_INDENT "delay SECONDS before snapshot (default: %.1f)\n",
      (double)SNAPSHOT_DELAY_DEFAULT);
  (void)fprintf(desc, USAGE_INDENT "   --strip-ansi              " USAGE_INDENT
                                   "remove all ANSI escape codes from output (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "-E --encrypt                 " USAGE_INDENT
                                   "enable packet encryption (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "-K --key KEY                  " USAGE_INDENT
                                   "SSH/GPG key file for authentication: /path/to/key, gpg:keyid, github:user, "
                                   "gitlab:user, or 'ssh' for auto-detect "
                                   "(implies --encrypt) (default: [unset])\n");
  (void)fprintf(
      desc, USAGE_INDENT
      "   --password [PASS]          " USAGE_INDENT
      "password for connection encryption (prompts if not provided) (implies --encrypt) (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "-F --keyfile FILE            " USAGE_INDENT "read encryption key from FILE "
                                   "(implies --encrypt) (default: [unset])\n");
  (void)fprintf(desc,
                USAGE_INDENT "   --no-encrypt               " USAGE_INDENT "disable encryption (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "   --server-key KEY           " USAGE_INDENT
                                   "expected server public key for verification (default: [unset])\n");
}
