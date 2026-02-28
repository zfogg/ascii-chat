
/**
 * @file options.c
 * @ingroup options
 * @brief ⚙️ Main entry point for unified options parsing with mode detection
 *
 * **Purpose**: Implements `options_init()` - the unified entry point that:
 *
 * 1. **Detects mode** from command-line arguments (server, client, mirror, acds, etc.)
 * 2. **Parses binary-level options** (--help, --version, --log-file, etc.)
 * 3. **Routes to mode-specific parsing** using preset configurations
 * 4. **Loads configuration files** if --config specified
 * 5. **Validates** all options with cross-field checks
 * 6. **Publishes** options via RCU for lock-free thread-safe access
 *
 * **Architecture**:
 *
 * The parsing flow is:
 *
 * ```
 * options_init(argc, argv)
 *   ├─ Mode Detection
 *   │  └─ Checks for --help, --version, mode keywords, session strings
 *   ├─ Binary-Level Option Parsing
 *   │  └─ Handles: --help, --version, --log-file, --verbose, --quiet, etc.
 *   ├─ Get Mode-Specific Preset (unified)
 *   │  └─ Uses options_preset_unified() from presets.h
 *   ├─ Mode-Specific Option Parsing
 *   │  └─ Builder parses remaining args for the detected mode
 *   ├─ Configuration File Loading (if --config specified)
 *   │  └─ Loads and merges TOML config file
 *   ├─ Validation
 *   │  └─ Applies defaults, validates ranges, cross-field checks
 *   └─ RCU Publishing
 *      └─ Makes options available via options_get() and GET_OPTION()
 * ```
 *
 * **Key Components**:
 *
 * - `detect_mode_and_parse_binary_options()`: Mode detection and binary-level parsing
 * - `options_preset_unified()`: Preset config for all modes (from presets.h)
 * - `options_config_parse_args()`: Builder-based parsing of mode-specific options
 * - Configuration file loading (if --config specified)
 * - `options_state_init()` and `options_state_set()`: RCU publishing
 *
 * **Option Lifecycle** in `options_init()`:
 *
 * 1. Create default options via `options_t_new()`
 * 2. Detect mode from argv
 * 3. Parse binary-level options (may exit for --help, --version)
 * 4. Get mode-specific preset from registry
 * 5. Parse mode-specific options into options struct
 * 6. Load config file if specified
 * 7. Validate all options with defaults
 * 8. Initialize RCU state
 * 9. Publish options for lock-free access
 *
 * **Error Handling**:
 *
 * - Invalid options → ERROR_USAGE (usage already printed to stderr)
 * - File errors → ERROR_IO or ERROR_CONFIG
 * - Validation failures → ERROR_VALIDATION with context message
 * - Success → ASCIICHAT_OK
 *
 * **Thread Safety**:
 *
 * - `options_init()` must be called exactly once before worker threads
 * - After `options_init()` completes, options are read-only
 * - Access via lock-free `GET_OPTION()` macro from worker threads
 * - Optional updates via `options_set_*()` functions
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <ctype.h>
#include <sys/stat.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h> // RCU-based thread-safe options
#include <ascii-chat/options/common.h>
#include <ascii-chat/options/parsers.h>
#include <ascii-chat/options/validation.h>
#include <ascii-chat/options/manpage.h>
#include <ascii-chat/options/presets.h>
#include <ascii-chat/options/actions.h>
#include <ascii-chat/options/registry/mode_defaults.h> // Mode-aware defaults

#include <ascii-chat/options/config.h>
#include <ascii-chat/options/strings.h> // Enum/mode string conversions
#include <ascii-chat/options/schema.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/log/grep.h>
#include <ascii-chat/options/colorscheme.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/util.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/util/path.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/url.h>
#include <ascii-chat/util/utf8.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Global Action Flag Tracking
// ============================================================================

/** @brief Global flag tracking if an action was passed (--show-capabilities, etc) */
static bool g_action_flag = false;

/**
 * @brief Set the action flag to indicate an action was passed
 * @param action_present true if action flag was detected
 */
static void set_action_flag(bool action_present) {
  g_action_flag = action_present;
}

/**
 * @brief Check if an action flag was detected
 * @return true if an action flag was passed
 */
bool has_action_flag(void) {
  return g_action_flag;
}

// ============================================================================
// Mode Detection Helper
// ============================================================================

/**
 * @brief Detect mode from command-line arguments
 * @param argc Argument count
 * @param argv Argument vector
 * @param out_mode Detected mode (OUTPUT)
 * @param out_session_string Session string if ACDS mode (OUTPUT, can be NULL)
 * @param out_mode_index Index of mode in argv (OUTPUT)
 * @return ASCIICHAT_OK on success, ERROR_USAGE on error or early exit
 *
 * Detects mode using this priority:
 * 1. --help or --version → handled, may exit(0)
 * 2. First positional argument → matches against mode names
 * 3. Session string pattern (word-word-word) → client mode
 * 4. No mode → show help and exit(0)
 */
/**
 * @brief Check if an option is binary-level only and whether it takes an argument
 *
 * @param arg The argument string (e.g., "--verbose", "-L")
 * @param out_takes_arg OUTPUT: true if the option takes a required argument
 * @param out_takes_optional_arg OUTPUT: true if the option takes an optional argument
 * @return true if this is a binary-level option
 */
static bool is_binary_level_option_with_args(const char *arg, bool *out_takes_arg, bool *out_takes_optional_arg) {
  if (!arg) {
    SET_ERRNO(ERROR_INVALID_PARAM, "arg is null");
    return false;
  }

  if (out_takes_arg)
    *out_takes_arg = false;
  if (out_takes_optional_arg)
    *out_takes_optional_arg = false;

  // Extract option name (without leading dashes and without value after '=')
  const char *opt_name = arg;
  if (arg[0] == '-') {
    opt_name = arg + (arg[1] == '-' ? 2 : 1); // Skip - or --
  }

  // For --option=value format, stop at the '=' sign
  const char *equals = strchr(opt_name, '=');
  size_t opt_len = equals ? (size_t)(equals - opt_name) : strlen(opt_name);
  char opt_buffer[64];
  if (opt_len >= sizeof(opt_buffer)) {
    opt_len = sizeof(opt_buffer) - 1;
  }
  SAFE_STRNCPY(opt_buffer, opt_name, opt_len + 1);
  opt_name = opt_buffer;

  // Check option against binary-level options
  if (strcmp(opt_name, "help") == 0 || strcmp(arg, "-h") == 0) {
    return true;
  }
  if (strcmp(opt_name, "version") == 0 || strcmp(arg, "-v") == 0) {
    return true;
  }
  if (strcmp(opt_name, "verbose") == 0 || strcmp(arg, "-V") == 0) {
    if (out_takes_optional_arg)
      *out_takes_optional_arg = true;
    return true;
  }
  if (strcmp(opt_name, "quiet") == 0 || strcmp(arg, "-q") == 0) {
    return true;
  }
  if (strcmp(opt_name, "log-file") == 0 || strcmp(arg, "-L") == 0) {
    if (out_takes_arg)
      *out_takes_arg = true;
    return true;
  }
  if (strcmp(opt_name, "log-level") == 0) {
    if (out_takes_arg)
      *out_takes_arg = true;
    return true;
  }
  if (strcmp(opt_name, "config") == 0) {
    if (out_takes_arg)
      *out_takes_arg = true;
    return true;
  }
  if (strcmp(opt_name, "config-create") == 0) {
    if (out_takes_optional_arg)
      *out_takes_optional_arg = true;
    return true;
  }
  if (strcmp(opt_name, "color") == 0) {
    // --color takes an optional argument (auto/true/false)
    // Can be used as --color (defaults to true) or --color=auto or --color auto
    if (out_takes_optional_arg)
      *out_takes_optional_arg = true;
    return true;
  }
  if (strcmp(opt_name, "check-update") == 0) {
    return true;
  }
  if (strcmp(opt_name, "no-check-update") == 0) {
    return true;
  }
  if (strcmp(opt_name, "json") == 0) {
    return true;
  }
  if (strcmp(opt_name, "log-format-console") == 0) {
    return true;
  }

  return false;
}

/**
 * @brief Check if a CLI flag/option is mirror-mode specific
 *
 * Queries the mirror mode config to determine if a given option
 * is unique to mirror mode (not in server/client/discovery modes).
 * This allows mode detection via options like -f or -u without
 * explicitly specifying "mirror" keyword.
 *
 * @param opt_name Option name without dashes (e.g., "file" not "-f")
 * @param short_name Short option char (e.g., 'f' or '\0')
 * @return true if this option exists in mirror mode config
 */
static bool is_mirror_mode_option(const char *opt_name, char short_name) {
  // Get unified config
  options_config_t *config = options_preset_unified(NULL, NULL);
  if (!config) {
    return false;
  }

  // Check if this option exists and applies to mirror mode
  bool found = false;
  for (size_t i = 0; i < config->num_descriptors; i++) {
    const option_descriptor_t *desc = &config->descriptors[i];

    // Check by long name
    if (opt_name && desc->long_name && strcmp(opt_name, desc->long_name) == 0) {
      // Check if this option is EXCLUSIVE to mirror mode (not shared with other modes)
      if ((desc->mode_bitmask & (1 << MODE_MIRROR)) && (desc->mode_bitmask == (1 << MODE_MIRROR))) {
        found = true;
      }
      break;
    }

    // Check by short name
    if (short_name != '\0' && desc->short_name == short_name) {
      // Check if this option is EXCLUSIVE to mirror mode (not shared with other modes)
      if ((desc->mode_bitmask & (1 << MODE_MIRROR)) && (desc->mode_bitmask == (1 << MODE_MIRROR))) {
        found = true;
      }
      break;
    }
  }

  options_config_destroy(config);
  return found;
}

static asciichat_error_t options_detect_mode(int argc, char **argv, asciichat_mode_t *out_mode,
                                             char *out_session_string, int *out_mode_index) {
  if (out_mode == NULL || out_mode_index == NULL) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Output parameters must not be NULL");
  }

  *out_mode_index = -1;
  if (out_session_string) {
    out_session_string[0] = '\0';
  }

  // Check if argv[0] itself is a mode name (for test compatibility)
  // This handles the case where tests pass ["client", "-p", "80"] without a binary name
  const char *const mode_names_check[] = {"server", "client", "mirror", "discovery-service", NULL};
  const asciichat_mode_t mode_values_check[] = {MODE_SERVER, MODE_CLIENT, MODE_MIRROR, MODE_DISCOVERY_SERVICE};
  for (int i = 0; mode_names_check[i] != NULL; i++) {
    if (strcmp(argv[0], mode_names_check[i]) == 0) {
      log_dev("Mode detected from argv[0]: %s", argv[0]);
      *out_mode = mode_values_check[i];
      *out_mode_index = 0;
      return ASCIICHAT_OK;
    }
  }

  log_dev("argv[0] '%s' is not a mode name, looking for first non-option argument", argv[0]);

  // Find the first non-option argument (potential mode or session string)
  // Also detect mirror-specific options to infer mode if no positional arg found
  int first_positional_idx = -1;
  bool has_mirror_specific_option = false;
  for (int i = 1; i < argc; i++) {
    // Skip options and their arguments
    if (argv[i][0] == '-') {
      bool takes_arg = false;
      bool takes_optional_arg = false;

      // Check if this is a binary-level option
      if (is_binary_level_option_with_args(argv[i], &takes_arg, &takes_optional_arg)) {
        // For --option=value format, don't skip the next argument
        // (the value is part of the current argument)
        const char *equals = strchr(argv[i], '=');
        if (!equals) {
          // No equals sign, so value might be in next argument
          if (takes_arg && i + 1 < argc) {
            i++; // Skip required argument
          } else if (takes_optional_arg && i + 1 < argc && argv[i + 1][0] != '-') {
            // For optional arguments, skip if it doesn't look like a mode/session string
            const char *next_arg = argv[i + 1];
            bool looks_like_mode_or_session = false;

            // Check if it looks like a mode name
            const char *const mode_names[] = {"server", "client", "mirror", "discovery-service", NULL};
            for (int j = 0; mode_names[j] != NULL; j++) {
              if (strcmp(next_arg, mode_names[j]) == 0) {
                looks_like_mode_or_session = true;
                break;
              }
            }

            // Check if it looks like a session string (word-word-word)
            if (!looks_like_mode_or_session && is_session_string(next_arg)) {
              looks_like_mode_or_session = true;
            }

            // If it doesn't look like a mode/session, it's the optional argument for this option
            if (!looks_like_mode_or_session) {
              i++; // Skip optional argument
            }
          }
        }
      } else {
        // Extract option name (without leading dashes)
        const char *opt_name = argv[i];
        char short_name = '\0';

        if (opt_name[0] == '-') {
          if (opt_name[1] == '-') {
            // Long option: skip "--"
            opt_name = opt_name + 2;
          } else {
            // Short option: extract char and store
            short_name = opt_name[1];
            opt_name = opt_name + 1;
          }
        }

        // Check if this option is mirror-specific by querying mirror config
        if (is_mirror_mode_option(opt_name, short_name)) {
          has_mirror_specific_option = true;
          // Skip the argument if this option takes one
          if (i + 1 < argc && argv[i + 1][0] != '-') {
            i++; // Skip required argument
          }
        } else {
          // Other mode-specific option - skip argument if it takes one
          if (i + 1 < argc && argv[i + 1][0] != '-') {
            i++; // Skip potential argument
          }
        }
      }
      continue;
    }

    // Found a positional argument
    first_positional_idx = i;
    break;
  }

  // If no positional argument found, check for mirror-specific options
  if (first_positional_idx == -1) {
    if (has_mirror_specific_option) {
      // User specified mirror-specific option (-f or -u) without explicit mode keyword
      *out_mode = MODE_MIRROR;
      *out_mode_index = -1;
      return ASCIICHAT_OK;
    }
    // Otherwise default to discovery mode (start new session)
    *out_mode = MODE_DISCOVERY;
    *out_mode_index = -1;
    return ASCIICHAT_OK;
  }

  const char *positional = argv[first_positional_idx];

  // Try to match against known modes
  const char *const mode_names[] = {"server", "client", "mirror", "discovery-service", "discovery", "default", NULL};
  const asciichat_mode_t mode_values[] = {MODE_SERVER,  MODE_CLIENT, MODE_MIRROR, MODE_DISCOVERY_SERVICE,
                                          MODE_INVALID, MODE_INVALID};

  for (int i = 0; mode_names[i] != NULL; i++) {
    if (strcmp(positional, mode_names[i]) == 0) {
      if (mode_values[i] == MODE_INVALID) {
        return SET_ERRNO(ERROR_USAGE,
                         "The default mode cannot be specified explicitly. Just run 'ascii-chat' without a mode.");
      }
      *out_mode = mode_values[i];
      *out_mode_index = first_positional_idx;
      return ASCIICHAT_OK;
    }
  }

  // Not a known mode - check if it's a session string (word-word-word pattern)
  if (is_session_string(positional)) {
    *out_mode = MODE_DISCOVERY;
    *out_mode_index = first_positional_idx;
    if (out_session_string) {
      SAFE_STRNCPY(out_session_string, positional, SESSION_STRING_BUFFER_SIZE);
    }
    return ASCIICHAT_OK;
  }

  // Unknown positional argument - suggest closest match
  const char *suggestion = asciichat_suggest_mode(positional);
  if (suggestion) {
    return SET_ERRNO(ERROR_USAGE, "Unknown mode '%s'. Did you mean '%s'?", positional, suggestion);
  }
  return SET_ERRNO(ERROR_USAGE, "Unknown mode or invalid argument: %s", positional);
}

// ============================================================================
// Default Initialization
// ============================================================================

/**
 * @brief Create a new options_t struct with all defaults set
 *
 * Initializes an options_t struct with all fields set to their default values
 * from OPT_*_DEFAULT defines. This ensures consistent default initialization
 * across the codebase.
 */
/**
 * @brief Helper to save binary-level options before reset
 *
 * Binary-level options are parsed first (STAGE 1) and should never be reset by
 * subsequent stages. This helper extracts them so they can be preserved across
 * options_t_new() calls.
 */
typedef struct {
  log_level_t log_level;
  unsigned short int verbose_level;
  bool quiet;
  char log_file[OPTIONS_BUFF_SIZE];
  bool help;
  bool version;
  char config_file[OPTIONS_BUFF_SIZE];
  asciichat_mode_t detected_mode;
  int color;                    // Color setting (COLOR_SETTING_AUTO/TRUE/FALSE) - binary-level option parsed early
  bool json;                    // JSON logging format - binary-level option
  bool log_format_console_only; // Apply log template only to console - binary-level option
} binary_level_opts_t;

static inline binary_level_opts_t extract_binary_level(const options_t *opts) {
  binary_level_opts_t binary = {0};
  if (!opts)
    return binary;
  binary.log_level = opts->log_level;
  binary.verbose_level = opts->verbose_level;
  binary.quiet = opts->quiet;
  SAFE_STRNCPY(binary.log_file, opts->log_file, sizeof(binary.log_file));
  binary.help = opts->help;
  binary.version = opts->version;
  SAFE_STRNCPY(binary.config_file, opts->config_file, sizeof(binary.config_file));
  binary.detected_mode = opts->detected_mode;
  binary.color = opts->color;                                     // Save color setting (parsed in STAGE 1A)
  binary.json = opts->json;                                       // Save JSON logging flag (binary-level option)
  binary.log_format_console_only = opts->log_format_console_only; // Save log format console setting
  return binary;
}

static inline void restore_binary_level(options_t *opts, const binary_level_opts_t *binary) {
  if (!opts || !binary)
    return;
  opts->log_level = binary->log_level;
  opts->verbose_level = binary->verbose_level;
  opts->quiet = binary->quiet;
  SAFE_STRNCPY(opts->log_file, binary->log_file, sizeof(opts->log_file));
  opts->help = binary->help;
  opts->version = binary->version;
  SAFE_STRNCPY(opts->config_file, binary->config_file, sizeof(opts->config_file));
  opts->detected_mode = binary->detected_mode;
  opts->color = binary->color;                                     // Restore color setting (parsed in STAGE 1A)
  opts->json = binary->json;                                       // Restore JSON logging flag (binary-level option)
  opts->log_format_console_only = binary->log_format_console_only; // Restore log format console setting
}

options_t options_t_new(void) {
  options_t opts;

  // Zero-initialize all fields first
  memset(&opts, 0, sizeof(opts));

  // ============================================================================
  // GENERAL CATEGORY - General-purpose options
  // ============================================================================
  opts.help = OPT_HELP_DEFAULT;
  opts.version = OPT_VERSION_DEFAULT;
  opts.splash_screen = OPT_SPLASH_DEFAULT;
  opts.splash_screen_explicitly_set = OPT_SPLASH_SCREEN_EXPLICITLY_SET_DEFAULT;
  opts.status_screen = OPT_STATUS_SCREEN_DEFAULT;
  opts.status_screen_explicitly_set = OPT_STATUS_SCREEN_EXPLICITLY_SET_DEFAULT;
  opts.enable_keepawake = OPT_ENABLE_KEEPAWAKE_DEFAULT;
  opts.disable_keepawake = OPT_DISABLE_KEEPAWAKE_DEFAULT;
  opts.no_check_update = OPT_NO_CHECK_UPDATE_DEFAULT;

  // ============================================================================
  // LOGGING CATEGORY - Binary-level logging and output control options
  // ============================================================================
  // log_file is mode-dependent at startup and intentionally left empty here
  opts.quiet = OPT_QUIET_DEFAULT;
  opts.verbose_level = OPT_VERBOSE_LEVEL_DEFAULT;
  opts.log_level = OPT_LOG_LEVEL_DEFAULT;
  opts.grep_pattern[0] = '\0'; // Explicitly ensure grep_pattern is empty
  opts.json = OPT_JSON_DEFAULT;
  SAFE_STRNCPY(opts.color_scheme_name, OPT_COLOR_SCHEME_NAME_DEFAULT, sizeof(opts.color_scheme_name));
  // log_template and log_format are set by get_default_log_template()

  // ============================================================================
  // TERMINAL CATEGORY - Terminal display options
  // ============================================================================
  opts.width = OPT_WIDTH_DEFAULT;
  opts.height = OPT_HEIGHT_DEFAULT;
  opts.auto_width = OPT_AUTO_WIDTH_DEFAULT;
  opts.auto_height = OPT_AUTO_HEIGHT_DEFAULT;
  opts.color_mode = OPT_COLOR_MODE_DEFAULT;
  opts.show_capabilities = OPT_SHOW_CAPABILITIES_DEFAULT;
  opts.force_utf8 = OPT_FORCE_UTF8_DEFAULT;
  opts.strip_ansi = OPT_STRIP_ANSI_DEFAULT;
  opts.color = OPT_COLOR_DEFAULT;

  // ============================================================================
  // DISPLAY CATEGORY - Display and rendering options
  // ============================================================================
  opts.color_filter = OPT_COLOR_FILTER_DEFAULT;
  opts.render_mode = OPT_RENDER_MODE_DEFAULT;
  opts.palette_type = OPT_PALETTE_TYPE_DEFAULT;
  // palette_custom is already zeroed by memset
  opts.palette_custom_set = OPT_PALETTE_CUSTOM_SET_DEFAULT;
  opts.stretch = OPT_STRETCH_DEFAULT;
  opts.fps = OPT_FPS_DEFAULT;
  opts.snapshot_mode = OPT_SNAPSHOT_MODE_DEFAULT;
  opts.snapshot_delay = OPT_SNAPSHOT_DELAY_DEFAULT;
  opts.matrix_rain = OPT_MATRIX_RAIN_DEFAULT;
  opts.flip_x = OPT_FLIP_X_DEFAULT;
  opts.flip_y = OPT_FLIP_Y_DEFAULT;

  // ============================================================================
  // WEBCAM CATEGORY - Webcam and capture device options
  // ============================================================================
  opts.webcam_index = OPT_WEBCAM_INDEX_DEFAULT;
  opts.test_pattern = OPT_TEST_PATTERN_DEFAULT;
  opts.no_audio_mixer = OPT_NO_AUDIO_MIXER_DEFAULT;

  // ============================================================================
  // MEDIA CATEGORY - Media file streaming and playback options
  // ============================================================================
  // media_file is already zeroed by memset
  // media_url is already zeroed by memset
  opts.media_loop = OPT_MEDIA_LOOP_DEFAULT;
  opts.pause = OPT_PAUSE_DEFAULT;
  opts.media_from_stdin = OPT_MEDIA_FROM_STDIN_DEFAULT;
  opts.media_seek_timestamp = OPT_MEDIA_SEEK_TIMESTAMP_DEFAULT;
  // yt_dlp_options is already zeroed by memset

  // Render-to-file options (Unix only)
#ifndef _WIN32
  // render_file is already zeroed by memset
  opts.render_theme = OPT_RENDER_THEME_DEFAULT;
  // render_font is already zeroed by memset
  opts.render_font_size = OPT_RENDER_FONT_SIZE_DEFAULT;
#endif

  // ============================================================================
  // NETWORK CATEGORY - Network connectivity and protocol options
  // ============================================================================
  SAFE_STRNCPY(opts.address, OPT_ADDRESS_DEFAULT, sizeof(opts.address));
  SAFE_STRNCPY(opts.address6, OPT_ADDRESS6_DEFAULT, sizeof(opts.address6));
  opts.port = OPT_PORT_INT_DEFAULT;
  opts.websocket_port = OPT_WEBSOCKET_PORT_SERVER_DEFAULT;
  opts.max_clients = OPT_MAX_CLIENTS_DEFAULT;
  opts.reconnect_attempts = OPT_RECONNECT_ATTEMPTS_DEFAULT;
  opts.compression_level = OPT_COMPRESSION_LEVEL_DEFAULT;
  opts.no_compress = OPT_NO_COMPRESS_DEFAULT;
  // Discovery Service (ACDS) Options
  opts.discovery = OPT_ACDS_DEFAULT;
  opts.discovery_port = OPT_ACDS_PORT_INT_DEFAULT;
  SAFE_STRNCPY(opts.discovery_server, OPT_ENDPOINT_DISCOVERY_SERVICE, sizeof(opts.discovery_server));
  opts.discovery_expose_ip = OPT_ACDS_EXPOSE_IP_DEFAULT;
  opts.discovery_insecure = OPT_ACDS_INSECURE_DEFAULT;
  opts.require_server_identity = OPT_REQUIRE_SERVER_IDENTITY_DEFAULT;
  opts.require_client_identity = OPT_REQUIRE_CLIENT_IDENTITY_DEFAULT;
  // discovery_service_key is already zeroed by memset
  opts.discovery_database_path[0] = '\0'; // Explicitly ensure discovery_database_path is empty
  // LAN Discovery & WebRTC Options
  opts.lan_discovery = OPT_LAN_DISCOVERY_DEFAULT;
  opts.no_mdns_advertise = OPT_NO_MDNS_ADVERTISE_DEFAULT;
  opts.enable_upnp = OPT_ENABLE_UPNP_DEFAULT;
  // WebRTC Mode & Strategy Options
  opts.webrtc = OPT_WEBRTC_DEFAULT;
  opts.prefer_webrtc = OPT_PREFER_WEBRTC_DEFAULT;
  opts.no_webrtc = OPT_NO_WEBRTC_DEFAULT;
  opts.webrtc_skip_stun = OPT_WEBRTC_SKIP_STUN_DEFAULT;
  opts.webrtc_disable_turn = OPT_WEBRTC_DISABLE_TURN_DEFAULT;
  opts.webrtc_skip_host = OPT_WEBRTC_SKIP_HOST_DEFAULT;
  opts.webrtc_ice_timeout_ms = OPT_WEBRTC_ICE_TIMEOUT_MS_DEFAULT;
  opts.webrtc_reconnect_attempts = OPT_WEBRTC_RECONNECT_ATTEMPTS_DEFAULT;
  SAFE_STRNCPY(opts.stun_servers, OPT_STUN_SERVERS_DEFAULT, sizeof(opts.stun_servers));
  SAFE_STRNCPY(opts.turn_servers, OPT_TURN_SERVERS_DEFAULT, sizeof(opts.turn_servers));
  SAFE_STRNCPY(opts.turn_username, OPT_TURN_USERNAME_DEFAULT, sizeof(opts.turn_username));
  SAFE_STRNCPY(opts.turn_credential, OPT_TURN_CREDENTIAL_DEFAULT, sizeof(opts.turn_credential));
  // turn_secret is already zeroed by memset

  // ============================================================================
  // AUDIO CATEGORY - Audio capture, playback and processing options
  // ============================================================================
  opts.audio_enabled = OPT_AUDIO_ENABLED_DEFAULT;
  opts.audio_source = OPT_AUDIO_SOURCE_DEFAULT;
  opts.microphone_index = OPT_MICROPHONE_INDEX_DEFAULT;
  opts.speakers_index = OPT_SPEAKERS_INDEX_DEFAULT;
  opts.microphone_sensitivity = OPT_MICROPHONE_SENSITIVITY_DEFAULT;
  opts.speakers_volume = OPT_SPEAKERS_VOLUME_DEFAULT;
  opts.audio_no_playback = OPT_AUDIO_NO_PLAYBACK_DEFAULT;
  opts.audio_analysis_enabled = OPT_AUDIO_ANALYSIS_ENABLED_DEFAULT;
  opts.encode_audio = OPT_ENCODE_AUDIO_DEFAULT;

  // ============================================================================
  // SECURITY CATEGORY - Encryption and authentication options
  // ============================================================================
  opts.encrypt_enabled = OPT_ENCRYPT_ENABLED_DEFAULT;
  // encrypt_key is already zeroed by memset
  // password is already zeroed by memset
  // encrypt_keyfile is already zeroed by memset
  opts.no_encrypt = OPT_NO_ENCRYPT_DEFAULT;
  opts.no_auth = OPT_NO_AUTH_DEFAULT;
  // server_key is already zeroed by memset
  // client_keys is already zeroed by memset
  // identity_keys array is already zeroed by memset
  opts.num_identity_keys = 0;
  opts.require_server_verify = OPT_REQUIRE_SERVER_VERIFY_DEFAULT;
  opts.require_client_verify = OPT_REQUIRE_CLIENT_VERIFY_DEFAULT;

  // ============================================================================
  // CONFIGURATION CATEGORY - Application configuration options
  // ============================================================================
  // config_file is already zeroed by memset

  // ============================================================================
  // DATABASE CATEGORY - Database and persistent storage options
  // ============================================================================
  // discovery_database_path is already zeroed by memset (set above in NETWORK)

  // ============================================================================
  // Internal/System Fields
  // ============================================================================
  // session_string is already zeroed by memset
  // detected_mode is already zeroed by memset

  return opts;
}

/**
 * @brief Create new options struct, preserving binary-level fields from source
 *
 * This version of options_t_new() extracts binary-level options from a source
 * struct before performing a full reset, then restores them after. This allows
 * binary-level options (--quiet, --verbose, --log-level, etc.) to persist through
 * multiple reset cycles without manual save/restore code.
 *
 * @param source The options struct to extract binary-level fields from (NULL-safe)
 * @return New options_t with defaults and binary-level fields preserved
 */
options_t options_t_new_preserve_binary(const options_t *source) {
  // Extract binary-level options before reset
  binary_level_opts_t binary = extract_binary_level(source);

  // Create fresh options with all defaults
  options_t opts = options_t_new();

  // Restore binary-level options (overwrite defaults)
  restore_binary_level(&opts, &binary);

  return opts;
}

static char *options_get_log_filepath(asciichat_mode_t detected_mode, options_t opts) {
  // Use static buffers to avoid SAFE_MALLOC and malloc before memory tracking is initialized
  static char log_filename_buf[256];
  static char default_log_path_buf[PLATFORM_MAX_PATH_LENGTH];
  static char tmp_dir_buf[PLATFORM_MAX_PATH_LENGTH];
  // Use a smaller static buffer to avoid WASM memory issues
  static char result_buf[512]; // Reduced size for WASM compatibility

  // Determine log filename based on mode
  const char *log_filename_literal;
  switch (detected_mode) {
  case MODE_SERVER:
    log_filename_literal = "server.log";
    break;
  case MODE_CLIENT:
    log_filename_literal = "client.log";
    break;
  case MODE_MIRROR:
    log_filename_literal = "mirror.log";
    break;
  case MODE_DISCOVERY_SERVICE:
    log_filename_literal = "acds.log";
    break;
  case MODE_DISCOVERY:
    log_filename_literal = "discovery.log";
    break;
  default:
    log_filename_literal = "ascii-chat.log";
    break;
  }
  SAFE_STRNCPY(log_filename_buf, log_filename_literal, sizeof(log_filename_buf) - 1);
  log_filename_buf[sizeof(log_filename_buf) - 1] = '\0';

  char *log_dir = get_log_dir();
  if (log_dir) {
    // Build full log file path: log_dir + separator + log_filename
    safe_snprintf(default_log_path_buf, PLATFORM_MAX_PATH_LENGTH, "%s%s%s", log_dir, PATH_SEPARATOR_STR,
                  log_filename_buf);

    // Validate and normalize the path
    char *normalized_default_log = NULL;
    if (path_validate_user_path(default_log_path_buf, PATH_ROLE_LOG_FILE, &normalized_default_log) == ASCIICHAT_OK) {
      SAFE_SNPRINTF(opts.log_file, OPTIONS_BUFF_SIZE, "%s", normalized_default_log);
      SAFE_FREE(normalized_default_log);
    } else {
      // Validation failed - use the path as-is (validation may fail in debug builds)
      SAFE_SNPRINTF(opts.log_file, OPTIONS_BUFF_SIZE, "%s", default_log_path_buf);
    }

    // log_dir comes from find_project_root() which uses malloc() to avoid debug_malloc recursion
    // So it must be freed with free(), not SAFE_FREE()
    free(log_dir);

    // Copy to static result buffer
    SAFE_STRNCPY(result_buf, default_log_path_buf, sizeof(result_buf) - 1);
    result_buf[sizeof(result_buf) - 1] = '\0';
    return result_buf;

  } else {
    SAFE_FREE(log_dir);
    if (platform_get_temp_dir(tmp_dir_buf, PLATFORM_MAX_PATH_LENGTH)) {
      safe_snprintf(default_log_path_buf, PLATFORM_MAX_PATH_LENGTH, "%s%s%s", tmp_dir_buf, PATH_SEPARATOR_STR,
                    "ascii-chat.log");
      SAFE_STRNCPY(result_buf, default_log_path_buf, sizeof(result_buf) - 1);
      result_buf[sizeof(result_buf) - 1] = '\0';
      return result_buf;
    } else {
      // Fallback to just the filename
      SAFE_STRNCPY(result_buf, log_filename_buf, sizeof(result_buf) - 1);
      result_buf[sizeof(result_buf) - 1] = '\0';
      return result_buf;
    }
  }
}

// ============================================================================
// Binary-Level Boolean Option Parser
// ============================================================================

// Parses binary-level boolean options with support for multiple formats:
// - --long-name → sets field to true
// - --long-name=true/false/yes/no/1/0/on/off → parses value
// - -X (short form) → sets field to true
// - -X=value (short form with value) → parses value
static bool parse_binary_bool_arg(const char *arg, bool *field, const char *long_name, char short_name) {
  if (!arg || !field || !long_name)
    return false;

  // Check long form: --long-name
  char long_form[256];
  snprintf(long_form, sizeof(long_form), "--%s", long_name);
  if (strcmp(arg, long_form) == 0) {
    *field = true;
    return true;
  }

  // Check long form with =value: --long-name=...
  char long_form_eq[256];
  snprintf(long_form_eq, sizeof(long_form_eq), "--%s=", long_name);
  if (strncmp(arg, long_form_eq, strlen(long_form_eq)) == 0) {
    const char *value = arg + strlen(long_form_eq);
    if (strcasecmp(value, "true") == 0 || strcasecmp(value, "yes") == 0 || strcasecmp(value, "1") == 0 ||
        strcasecmp(value, "on") == 0) {
      *field = true;
      return true;
    } else if (strcasecmp(value, "false") == 0 || strcasecmp(value, "no") == 0 || strcasecmp(value, "0") == 0 ||
               strcasecmp(value, "off") == 0) {
      *field = false;
      return true;
    }
    // If invalid value, still consume it (builder will validate later)
    return true;
  }

  // Check short form: -X (if defined)
  if (short_name != '\0') {
    char short_form[3];
    snprintf(short_form, sizeof(short_form), "-%c", short_name);
    if (strcmp(arg, short_form) == 0) {
      *field = true;
      return true;
    }
  }

  return false;
}

// ============================================================================
// Main Option Parser Entry Point
// ============================================================================

asciichat_error_t options_init(int argc, char **argv) {
  // NOTE: --grep filter is initialized in main.c BEFORE any logging starts
  // This allows ALL logs (including from shared_init) to be filtered
  // Validate arguments (safety check for tests)
  if (argc < 0 || argc > 128) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid argc: %d", argc);
  }
  if (argv == NULL) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "argv is NULL");
  }
  // Validate all argv elements are non-NULL up to argc
  for (int i = 0; i < argc; i++) {
    if (argv[i] == NULL) {
      return SET_ERRNO(ERROR_INVALID_PARAM, "argv[%d] is NULL (argc=%d)", i, argc);
    }
  }
  // Initialize RCU options system (must be done before any threads start)
  // This must happen FIRST, before any other initialization
  asciichat_error_t rcu_init_result = options_state_init();
  if (rcu_init_result != ASCIICHAT_OK) {
    return rcu_init_result;
  }

  // ========================================================================
  // STAGE 1: Mode Detection and Binary-Level Option Handling
  // ========================================================================

  // Check for binary-level actions FIRST (before mode detection)
  // These actions may take arguments, so we need to check them before mode detection
  bool show_version = false;
  bool create_config = false;
  bool create_manpage = false;
  bool has_action = false; // Track if any action flag is present
  const char *config_create_path = NULL;
  const char *manpage_create_path = NULL;

  // ========================================================================
  // STAGE 1A: Quick scan for action flags FIRST (they bypass mode detection)
  // ========================================================================
  // Quick scan for action flags (they may have arguments)
  // This must happen BEFORE logging initialization so we can suppress logs before shared_init()
  // Also scan for --quiet / -q so we can suppress logging from the start
  // Also scan for --color early so it affects help output colors
  bool user_quiet = false;
  int parsed_color_setting = COLOR_SETTING_AUTO; // Store parsed color value until opts is created
  bool color_setting_found = false;
  bool check_update_flag_seen = false;
  bool no_check_update_flag_seen = false;

  // FIRST: Scan entire argv for --help (special case - works before OR after mode)
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      // Scan backwards to find if a mode was specified before --help
      asciichat_mode_t help_mode = MODE_DISCOVERY; // Default to discovery mode (binary-level help)
      for (int j = i - 1; j >= 1; j--) {
        if (argv[j][0] != '-') {
          if (strcmp(argv[j], "server") == 0) {
            help_mode = MODE_SERVER;
          } else if (strcmp(argv[j], "client") == 0) {
            help_mode = MODE_CLIENT;
          } else if (strcmp(argv[j], "mirror") == 0) {
            help_mode = MODE_MIRROR;
          } else if (strcmp(argv[j], "discovery-service") == 0) {
            help_mode = MODE_DISCOVERY_SERVICE;
          } else if (strcmp(argv[j], "discovery") == 0) {
            help_mode = MODE_DISCOVERY;
          }
          break; // Found first non-flag argument
        }
      }

      // Show help for the detected mode (or binary-level if no mode)
      usage(stdout, help_mode);
      fflush(NULL);
      _Exit(0);
    }
  }

  // THEN: Scan for other binary-level actions (stops at mode name)
  for (int i = 1; i < argc; i++) {
    // Stop scanning at mode name - binary-level options must come before the mode
    if (argv[i][0] != '-') {
      bool is_mode =
          (strcmp(argv[i], "server") == 0 || strcmp(argv[i], "client") == 0 || strcmp(argv[i], "mirror") == 0 ||
           strcmp(argv[i], "discovery") == 0 || strcmp(argv[i], "discovery-service") == 0);
      if (is_mode) {
        break; // Stop processing at mode name
      }
    }
    if (argv[i][0] == '-') {
      // Handle --quiet, -q, --quiet=value formats
      bool temp_quiet = false;
      if (parse_binary_bool_arg(argv[i], &temp_quiet, "quiet", 'q')) {
        if (temp_quiet) {
          user_quiet = true;
        }
      }
      // Validate --log-level and --log-file require arguments
      if (strcmp(argv[i], "--log-level") == 0) {
        if (i + 1 >= argc || argv[i + 1][0] == '-') {
          log_plain_stderr("Error: --log-level requires a value (dev, debug, info, warn, error, fatal)");
          return ERROR_USAGE;
        }
        i++; // Skip the argument in this loop
      }
      if (strcmp(argv[i], "-L") == 0 || strcmp(argv[i], "--log-file") == 0) {
        if (i + 1 >= argc || argv[i + 1][0] == '-') {
          log_plain_stderr("Error: %s requires a file path", argv[i]);
          return ERROR_USAGE;
        }
        i++; // Skip the argument in this loop
      }
      // --help is handled in the first scan loop above (works before OR after mode)
      // Parse --color early so it affects help output colors
      if (strcmp(argv[i], "--color") == 0) {
        if (i + 1 < argc && argv[i + 1][0] != '-') {
          // Check if next arg is a mode name (not a color value)
          const char *next_arg = argv[i + 1];
          bool is_mode =
              (strcmp(next_arg, "server") == 0 || strcmp(next_arg, "client") == 0 || strcmp(next_arg, "mirror") == 0 ||
               strcmp(next_arg, "discovery") == 0 || strcmp(next_arg, "discovery-service") == 0);

          if (is_mode) {
            // Next arg is a mode, not a color value - default to true
            parsed_color_setting = COLOR_SETTING_TRUE;
            color_setting_found = true;
          } else {
            // Try to parse as color value
            char *error_msg = NULL;
            if (parse_color_setting(next_arg, &parsed_color_setting, &error_msg)) {
              color_setting_found = true;
              i++; // Skip the setting argument
            } else {
              if (error_msg) {
                log_error("Error parsing --color: %s", error_msg);
                free(error_msg);
              }
            }
          }
        } else {
          // --color without argument defaults to true (enable colors)
          parsed_color_setting = COLOR_SETTING_TRUE;
          color_setting_found = true;
        }
      }
      // Check for --color=value format
      if (strncmp(argv[i], "--color=", 8) == 0) {
        const char *value = argv[i] + 8;
        char *error_msg = NULL;
        if (parse_color_setting(value, &parsed_color_setting, &error_msg)) {
          color_setting_found = true;
        } else {
          if (error_msg) {
            log_error("Error parsing --color: %s", error_msg);
            free(error_msg);
          }
        }
      }
      if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
        show_version = true;
        has_action = true;
        break;
      }
      if (strcmp(argv[i], "--check-update") == 0) {
        check_update_flag_seen = true;
        if (no_check_update_flag_seen) {
          log_plain_stderr("Error: Cannot specify both --check-update and --no-check-update");
          return ERROR_USAGE;
        }
        has_action = true;
        action_check_update_immediate();
        // action_check_update_immediate() calls _Exit(), so we don't reach here
        break;
      }
      if (strcmp(argv[i], "--no-check-update") == 0) {
        no_check_update_flag_seen = true;
        if (check_update_flag_seen) {
          log_plain_stderr("Error: Cannot specify both --check-update and --no-check-update");
          return ERROR_USAGE;
        }
        // Flag will be parsed normally later
      }
      if (strcmp(argv[i], "--config-create") == 0) {
        create_config = true;
        has_action = true;
        // Check for optional [FILE] argument
        if (i + 1 < argc && argv[i + 1][0] != '-') {
          config_create_path = argv[i + 1];
          i++; // Consume the file path argument
        }
        break;
      }
      if (strcmp(argv[i], "--man-page-create") == 0) {
        create_manpage = true;
        has_action = true;
        // Check for optional [FILE] argument
        if (i + 1 < argc && argv[i + 1][0] != '-') {
          manpage_create_path = argv[i + 1];
          i++; // Consume the file path argument
        }
        break;
      }
      if (strcmp(argv[i], "--completions") == 0) {
        has_action = true;
        // Handle --completions: generate shell completion scripts
        if (i + 1 < argc && argv[i + 1][0] != '-') {
          const char *shell_name = argv[i + 1];
          i++; // Consume shell name

          // Check for optional output file
          const char *output_file = NULL;
          if (i + 1 < argc && argv[i + 1][0] != '-') {
            output_file = argv[i + 1];
            i++; // Consume output file
          }

          action_completions(shell_name, output_file);
          // action_completions() calls _Exit(), so we don't reach here
        } else {
          log_plain_stderr("Error: --completions requires shell name (bash, fish, zsh, powershell)");
          return ERROR_USAGE;
        }
        break; // Unreachable, but for clarity
      }
      if (strcmp(argv[i], "--list-webcams") == 0) {
        has_action = true;
        action_list_webcams();
        // action_list_webcams() calls _Exit(), so we don't reach here
        break;
      }
      if (strcmp(argv[i], "--list-microphones") == 0) {
        has_action = true;
        action_list_microphones();
        // action_list_microphones() calls _Exit(), so we don't reach here
        break;
      }
      if (strcmp(argv[i], "--list-speakers") == 0) {
        has_action = true;
        action_list_speakers();
        // action_list_speakers() calls _Exit(), so we don't reach here
        break;
      }
      // Check for --show-capabilities (binary-level action)
      if (strcmp(argv[i], "--show-capabilities") == 0) {
        has_action = true;
        action_show_capabilities_immediate();
        // action_show_capabilities_immediate() calls _Exit(), so we don't reach here
        break;
      }
    }
  }
  // Store action flag globally for use during cleanup
  set_action_flag(has_action);
  // ========================================================================
  // STAGE 1B: DO MODE DETECTION EARLY (needed for log_init)
  // ========================================================================
  asciichat_mode_t detected_mode = MODE_DISCOVERY; // Default mode
  char detected_session_string[SESSION_STRING_BUFFER_SIZE] = {0};
  int mode_index = -1;

  asciichat_error_t mode_detect_result =
      options_detect_mode(argc, argv, &detected_mode, detected_session_string, &mode_index);
  if (mode_detect_result != ASCIICHAT_OK) {
    return mode_detect_result;
  }

  // VALIDATE: Binary-level options must appear BEFORE the mode
  // Check if any binary-level options appear after the mode position
  if (mode_index > 0) {
    for (int i = mode_index + 1; i < argc; i++) {
      if (argv[i][0] == '-') {
        bool takes_arg = false;
        bool takes_optional_arg = false;

        if (is_binary_level_option_with_args(argv[i], &takes_arg, &takes_optional_arg)) {
          return SET_ERRNO(ERROR_USAGE, "Binary-level option '%s' must appear before the mode '%s', not after it",
                           argv[i], argv[mode_index]);
        }
      }
    }
  }
  // ========================================================================
  // STAGE 1C: Initialize logging EARLY (before any log_dev calls)
  // ========================================================================
  // Create local options struct and initialize with defaults
  options_t opts = options_t_new(); // Initialize with all defaults
  opts.detected_mode = detected_mode;
  char *log_filename = options_get_log_filepath(detected_mode, opts);
  SAFE_SNPRINTF(opts.log_file, OPTIONS_BUFF_SIZE, "%s", log_filename);
  // Note: log_init() is called earlier in asciichat_shared_init() and will be
  // reconfigured with the actual log level and file in main.c after options are fully parsed
  // NOTE: --color detection now happens in src/main.c BEFORE asciichat_shared_init()
  // This ensures g_color_flag_passed and g_color_flag_value are set before any logging.
  //
  // NOTE: Timer system and shared subsystems are initialized by src/main.c
  // via asciichat_shared_init() BEFORE options_init() is called.
  // This allows options_init() to use properly configured logging.

  // If an action flag is detected OR user passed --quiet, silence logs for clean output
  if (user_quiet || has_action) {
    log_set_terminal_output(false); // Suppress console logging for clean action output
  }
  // Apply parsed color setting from STAGE 1A
  if (color_setting_found) {
    opts.color = parsed_color_setting;
  } else {
    opts.color = OPT_COLOR_DEFAULT; // Apply default
  }

  // If we found version/config-create/create-manpage, handle them immediately (before mode detection)
  if (show_version || create_config || create_manpage) {
    if (show_version) {
      opts.version = true;
      options_state_set(&opts);
      return ASCIICHAT_OK;
    }
    else if (create_config) {
      // Build the schema first so config_create_default can generate options from it
      const options_config_t *unified_config = options_preset_unified(NULL, NULL);
      if (unified_config) {
        asciichat_error_t schema_build_result = config_schema_build_from_configs(&unified_config, 1);
        if (schema_build_result != ASCIICHAT_OK) {
          // Schema build failed, but continue anyway
          (void)schema_build_result;
        }
        options_config_destroy(unified_config);
      }

      // Call action handler which handles all output and prompts properly
      action_create_config(config_create_path);
      // action_create_config() calls _Exit(), so we don't reach here
    }
    else if (create_manpage) {
      // Call action handler which handles all output and prompts properly
      action_create_manpage(manpage_create_path);
      // action_create_manpage() calls _Exit(), so we don't reach here
    }
  }

  // Mode detection and logging already initialized early in STAGE 1B/1C above
  // (moved earlier to ensure logging is available before any log_dev() calls)

  // Check for binary-level options that can appear before or after mode
  // Search entire argv to find --quiet, --log-file, --log-level, -V, etc.
  // These are documented as binary-level options that can appear anywhere
  for (int i = 1; i < argc; i++) {
    if (argv[i][0] == '-') {
      // Handle -V and --verbose (stackable verbosity)
      if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--verbose") == 0) {
        // Check if next argument is a number (optional argument)
        if (i + 1 < argc && argv[i + 1][0] != '-') {
          // Try to parse as integer count
          char *endptr;
          long value = strtol(argv[i + 1], &endptr, 10);
          if (*endptr == '\0' && value >= 0 && value <= 100) {
            opts.verbose_level = (unsigned short int)value;
            i++; // Skip next argument
            continue;
          }
        }
        // No valid number, just increment
        opts.verbose_level++;
      }
      // Handle binary-level boolean options using the abstraction function
      // -q/--quiet, --json, --log-format-console all use the same parser
      if (parse_binary_bool_arg(argv[i], &opts.quiet, "quiet", 'q')) {
        continue;
      }
      if (parse_binary_bool_arg(argv[i], &opts.json, "json", '\0')) {
        continue;
      }
      if (parse_binary_bool_arg(argv[i], &opts.log_format_console_only, "log-format-console", '\0')) {
        continue;
      }
      // Handle --log-level LEVEL (set log threshold)
      if (strcmp(argv[i], "--log-level") == 0) {
        if (i + 1 >= argc) {
          log_plain_stderr("Error: --log-level requires a value (dev, debug, info, warn, error, fatal)");
          return ERROR_USAGE;
        }
        if (argv[i + 1][0] == '-') {
          log_plain_stderr("Error: --log-level requires a value (dev, debug, info, warn, error, fatal)");
          return ERROR_USAGE;
        }
        char *error_msg = NULL;
        if (parse_log_level(argv[i + 1], &opts.log_level, &error_msg)) {
          i++; // Skip the level argument
        } else {
          if (error_msg) {
            log_plain_stderr("Error: %s", error_msg);
            free(error_msg);
          } else {
            log_plain_stderr("Error: invalid log level value: %s", argv[i + 1]);
          }
          return ERROR_USAGE;
        }
      }
      // Handle -L and --log-file FILE (set log file path)
      if ((strcmp(argv[i], "-L") == 0 || strcmp(argv[i], "--log-file") == 0)) {
        if (i + 1 >= argc) {
          log_plain_stderr("Error: %s requires a file path", argv[i]);
          return ERROR_USAGE;
        }
        if (argv[i + 1][0] == '-') {
          log_plain_stderr("Error: %s requires a file path", argv[i]);
          return ERROR_USAGE;
        }
        SAFE_STRNCPY(opts.log_file, argv[i + 1], sizeof(opts.log_file));
        i++; // Skip the file argument
      }
      // NOTE: --color is now parsed in STAGE 1A (early, before help processing)
      // to ensure help output colors are applied correctly
    }
  }

  if (show_version) {
    // Show binary-level version from src/main.c
    opts.version = true;
    options_state_set(&opts);
    return ASCIICHAT_OK;
  }

  if (create_config) {
    // Handle --config-create: create default config file and exit
    // Use provided path or default to user config location
    char config_path[PLATFORM_MAX_PATH_LENGTH];
    if (config_create_path) {
      SAFE_STRNCPY(config_path, config_create_path, sizeof(config_path));
    } else {
      // Use default config path: ~/.ascii-chat/config.toml
      char *config_dir = get_config_dir();
      if (!config_dir) {
        log_error("Error: Failed to determine default config directory");
        return ERROR_CONFIG;
      }
      safe_snprintf(config_path, sizeof(config_path), "%sconfig.toml", config_dir);
      SAFE_FREE(config_dir);
    }

    // Create config with default options
    asciichat_error_t result = config_create_default(config_path);
    if (result != ASCIICHAT_OK) {
      asciichat_error_context_t err_ctx;
      if (HAS_ERRNO(&err_ctx)) {
        log_error("Error creating config: %s", err_ctx.context_message);
      } else {
        log_error("Error: Failed to create config file at %s", config_path);
      }
      return result;
    }

    log_plain("Created default config file at: %s", config_path);
    return ASCIICHAT_OK; // Return successfully after creating config
  }

  if (create_manpage) {
    // Handle --create-man-page-template: generate merged man page template
    // The .1.in file is the existing template to read from (not the output)
    const char *existing_template_path = "share/man/man1/ascii-chat.1.in";
    options_config_t *config = options_preset_unified(NULL, NULL);
    if (!config) {
      log_error("Error: Failed to get binary options config");
      return ERROR_MEMORY;
    }

    // Generate merged man page from embedded or filesystem resources
    // (existing_template_path and manpage_content_file parameters are no longer supported)
    asciichat_error_t err = options_config_generate_manpage_merged(config, "ascii-chat", NULL, existing_template_path,
                                                                   "Video chat in your terminal");

    options_config_destroy(config);

    if (err != ASCIICHAT_OK) {
      asciichat_error_context_t err_ctx;
      if (HAS_ERRNO(&err_ctx)) {
        log_error("%s", err_ctx.context_message);
      } else {
        log_error("Error: Failed to generate man page");
      }
      return err;
    }

    log_plain("Generated man page: %s", existing_template_path);
    return ASCIICHAT_OK; // Return successfully after generating man page
  }

  // ========================================================================
  // STAGE 2: Build argv for mode-specific parsing
  // ========================================================================
  // If mode was found, build argv with only arguments after the mode
  // If mode_index == -1, filter out binary-level options from all arguments
  int mode_argc = argc;
  char **mode_argv = (char **)argv;
  char **allocated_mode_argv = NULL; // Track if we need to free mode_argv

  if (mode_index == -1) {
    // No explicit mode - filter binary-level options from entire argv
    int max_mode_argc = argc; // Worst case: no binary opts skipped

    if (max_mode_argc > 256) {
      return SET_ERRNO(ERROR_INVALID_PARAM, "Too many arguments: %d", max_mode_argc);
    }

    char **new_mode_argv = SAFE_MALLOC((size_t)(max_mode_argc + 1) * sizeof(char *), char **);
    if (!new_mode_argv) {
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate mode_argv");
    }
    allocated_mode_argv = new_mode_argv;

    new_mode_argv[0] = argv[0]; // Copy program name

    // Copy all args except binary-level options
    int new_argv_idx = 1;
    for (int i = 1; i < argc; i++) {
      bool takes_arg = false;
      bool takes_optional_arg = false;

      // Check if this is a binary-level option
      if (is_binary_level_option_with_args(argv[i], &takes_arg, &takes_optional_arg)) {
        // Skip argument if needed
        if (takes_arg && i + 1 < argc) {
          i++; // Skip required argument
        } else if (takes_optional_arg && i + 1 < argc && argv[i + 1][0] != '-') {
          i++; // Skip optional argument
        }
        continue;
      }
      // Not a binary option, copy to mode_argv
      new_mode_argv[new_argv_idx++] = argv[i];
    }

    mode_argc = new_argv_idx;
    new_mode_argv[mode_argc] = NULL;
    mode_argv = new_mode_argv;
  } else if (mode_index != -1) {
    // Mode found at position mode_index
    // Build new argv: [program_name, args_before_mode (no binary opts)..., args_after_mode...]
    // Binary-level options are not passed to mode-specific parsers

    int args_after_mode = argc - mode_index - 1;
    // We'll calculate final argc after skipping binary options
    int max_mode_argc = 1 + (mode_index - 1) + args_after_mode; // Worst case: no binary opts skipped

    if (max_mode_argc > 256) {
      return SET_ERRNO(ERROR_INVALID_PARAM, "Too many arguments: %d", max_mode_argc);
    }

    // Allocate max_mode_argc+1 to accommodate NULL terminator
    char **new_mode_argv = SAFE_MALLOC((size_t)(max_mode_argc + 1) * sizeof(char *), char **);
    if (!new_mode_argv) {
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate mode_argv");
    }
    allocated_mode_argv = new_mode_argv; // Track allocation for cleanup

    // Copy: [program_name, args_before_mode (excluding binary opts)..., args_after_mode...]
    if (mode_index == 0) {
      // Mode is at argv[0], use "ascii-chat" as program name
      new_mode_argv[0] = "ascii-chat";
    } else {
      new_mode_argv[0] = argv[0];
    }

    // Copy args before mode, skipping binary-level options
    int new_argv_idx = 1;
    for (int i = 1; i < mode_index; i++) {
      bool takes_arg = false;
      bool takes_optional_arg = false;

      // Check if this is a binary-level option
      if (is_binary_level_option_with_args(argv[i], &takes_arg, &takes_optional_arg)) {
        // Skip argument if needed
        if (takes_arg && i + 1 < mode_index) {
          i++; // Skip required argument
        } else if (takes_optional_arg && i + 1 < mode_index && argv[i + 1][0] != '-') {
          i++; // Skip optional argument
        }
        continue;
      }
      // Not a binary option, copy to mode_argv
      new_mode_argv[new_argv_idx++] = argv[i];
    }
    // Copy args after mode, filtering out any binary-level options (they shouldn't appear here)
    int args_after_mode_idx = 0;
    for (int i = mode_index + 1; i < argc; i++) {
      bool takes_arg = false;
      bool takes_optional_arg = false;

      // Check if this is a binary-level option (shouldn't appear after mode)
      if (is_binary_level_option_with_args(argv[i], &takes_arg, &takes_optional_arg)) {
        // Skip binary-level option and its argument if needed
        if (takes_arg && i + 1 < argc) {
          i++; // Skip required argument
        } else if (takes_optional_arg && i + 1 < argc && argv[i + 1][0] != '-') {
          i++; // Skip optional argument (note: removed isdigit check for consistency)
        }
        continue; // Skip this option
      }
      // Not a binary option, copy to mode_argv
      new_mode_argv[new_argv_idx + args_after_mode_idx++] = argv[i];
    }
    // Calculate actual argc (program + filtered args before + filtered args after)
    mode_argc = new_argv_idx + args_after_mode_idx;
    new_mode_argv[mode_argc] = NULL;

    mode_argv = new_mode_argv;
  }

  // ========================================================================
  // STAGE 3: Set Mode-Specific Defaults
  // ========================================================================
  // Initialize all defaults using options_t_new_preserve_binary() to keep binary-level
  // options (--quiet, --verbose, --log-level, etc.) from being reset
  opts = options_t_new_preserve_binary(&opts);
  // If a session string was detected during mode detection, restore it after options reset
  // This ensures discovery mode knows which session to join
  if (detected_session_string[0] != '\0') {
    SAFE_STRNCPY(opts.session_string, detected_session_string, sizeof(opts.session_string));
    log_info("options_init: Detected session string from argv: '%s'", detected_session_string);
  }

  // Encryption options default to disabled/empty
  opts.no_encrypt = 0;
  opts.encrypt_key[0] = '\0';
  opts.password[0] = '\0';
  opts.encrypt_keyfile[0] = '\0';
  opts.server_key[0] = '\0';
  opts.client_keys[0] = '\0';
  opts.palette_custom[0] = '\0';

  // Set different default addresses for different modes
  if (detected_mode == MODE_CLIENT || detected_mode == MODE_MIRROR || detected_mode == MODE_DISCOVERY) {
    // Client/Mirror/Discovery: connects to localhost by default (discovery uses ACDS to find server)
    SAFE_SNPRINTF(opts.address, OPTIONS_BUFF_SIZE, "localhost");
    opts.address6[0] = '\0'; // Client doesn't use address6
  } else if (detected_mode == MODE_SERVER) {
    // Server: binds to 127.0.0.1 (IPv4) and ::1 (IPv6) by default
    SAFE_SNPRINTF(opts.address, OPTIONS_BUFF_SIZE, "127.0.0.1");
    // address6 is now a positional argument, not an option
    opts.address6[0] = '\0';
  } else if (detected_mode == MODE_DISCOVERY_SERVICE) {
    // ACDS: binds to all interfaces by default
    SAFE_SNPRINTF(opts.address, OPTIONS_BUFF_SIZE, "0.0.0.0");
    opts.address6[0] = '\0';
  }

  // ========================================================================
  // STAGE 4: Build Dynamic Schema from Unified Options Config
  // ========================================================================
  // Build the config schema dynamically from the unified config
  // This generates TOML keys, CLI flags, categories, and types from builder data
  const options_config_t *unified_config = options_preset_unified(NULL, NULL);
  if (unified_config) {
    asciichat_error_t schema_build_result = config_schema_build_from_configs(&unified_config, 1);
    if (schema_build_result != ASCIICHAT_OK) {
      // Schema build failed, but continue with static schema as fallback
      (void)schema_build_result;
    } else {
    }
    options_config_destroy(unified_config);
  } else {
  }

  // ========================================================================
  // STAGE 5: Load Configuration Files
  // ========================================================================
  // Extract binary-level options BEFORE config loading (config may reset them)
  binary_level_opts_t binary_before_config = extract_binary_level(&opts);
  // Publish options to RCU before config loading so that config logs get proper colors
  // from the parsed --color setting (e.g., --color=true)
  asciichat_error_t config_publish_result = options_state_set(&opts);
  if (config_publish_result != ASCIICHAT_OK) {
  } else {
  }
  // Load config files - now uses detected_mode directly for bitmask validation
  // Save flip_x and flip_y as they should not be reset by config files
  // Also save encryption settings - they should only be controlled via CLI, not config file
  bool saved_flip_x_from_config = opts.flip_x;
  bool saved_flip_y_from_config = opts.flip_y;
  bool saved_encrypt_enabled = opts.encrypt_enabled;
  asciichat_error_t config_result = config_load_system_and_user(detected_mode, false, &opts);
  (void)config_result; // Continue with defaults and CLI parsing regardless of result
  // Restore binary-level options (don't let config override command-line options)
  restore_binary_level(&opts, &binary_before_config);

  // Restore flip_x and flip_y - config shouldn't override the defaults
  opts.flip_x = saved_flip_x_from_config;
  opts.flip_y = saved_flip_y_from_config;

  // Restore encrypt_enabled - it should only be set via CLI, not config file
  // The config can set key/password which auto-enables encryption, but the encrypt_enabled
  // flag itself should stay at its default unless the user explicitly passes --encrypt
  opts.encrypt_enabled = saved_encrypt_enabled;

  // ========================================================================
  // STAGE 6: Parse Command-Line Arguments (Unified)
  // ========================================================================
  // Extract binary-level options BEFORE applying unified defaults
  // (which might override them from config files)
  binary_level_opts_t binary_before_defaults = extract_binary_level(&opts);
  asciichat_mode_t mode_saved_for_parsing = detected_mode; // CRITICAL: Save before defaults reset
  // Get unified config
  options_config_t *config = options_preset_unified(NULL, NULL);
  if (!config) {
    SAFE_FREE(allocated_mode_argv);
    return SET_ERRNO(ERROR_CONFIG, "Failed to create options configuration");
  }
  int remaining_argc;
  char **remaining_argv;

  // Save flip_x and flip_y before applying defaults (should not be reset by defaults)
  bool saved_flip_x = opts.flip_x;
  bool saved_flip_y = opts.flip_y;
  // Apply defaults from unified config
  asciichat_error_t defaults_result = options_config_set_defaults(config, &opts);
  if (defaults_result != ASCIICHAT_OK) {
    options_config_destroy(config);
    SAFE_FREE(allocated_mode_argv);
    return defaults_result;
  }
  // Restore binary-level options (they should never be overridden by defaults)
  restore_binary_level(&opts, &binary_before_defaults);

  // Restore flip_x and flip_y - they should keep the values from options_t_new()
  // unless explicitly set by the user (but defaults shouldn't override them)
  opts.flip_x = saved_flip_x;
  opts.flip_y = saved_flip_y;

  // Restore detected_mode before parsing so mode validation works.
  opts.detected_mode = mode_saved_for_parsing;

  // Save flip_x and flip_y before parsing - they should not be reset by the parser
  bool saved_flip_x_for_parse = opts.flip_x;
  bool saved_flip_y_for_parse = opts.flip_y;
  // Note: json is already saved via extract_binary_level/restore_binary_level mechanism
  // Parse mode-specific arguments
  option_mode_bitmask_t mode_bitmask = (1 << mode_saved_for_parsing);
  asciichat_error_t result =
      options_config_parse(config, mode_argc, mode_argv, &opts, mode_bitmask, &remaining_argc, &remaining_argv);
  // Restore flip_x and flip_y - they should keep their values unless explicitly overridden
  opts.flip_x = saved_flip_x_for_parse;
  opts.flip_y = saved_flip_y_for_parse;
  // json is already restored via the call to options_state_set which calls restore_binary_level
  if (result != ASCIICHAT_OK) {
    options_config_destroy(config);
    SAFE_FREE(allocated_mode_argv);
    // Convert ERROR_CONFIG to ERROR_USAGE for command-line parsing errors
    if (result == ERROR_CONFIG) {
      return ERROR_USAGE;
    }
    return result;
  }

  // ========================================================================
  // STAGE 6.5: Publish Parsed Options Early
  // ========================================================================
  // Publish options to RCU as soon as they're parsed
  // This ensures GET_OPTION() works during cleanup even if validation fails
  log_debug("Publishing parsed options to RCU before validation");
  asciichat_error_t early_publish = options_state_set(&opts);
  if (early_publish != ASCIICHAT_OK) {
    log_error("Failed to publish parsed options to RCU state early");
    options_config_destroy(config);
    SAFE_FREE(allocated_mode_argv);
    return early_publish;
  }
  log_debug("Successfully published options to RCU");

  // Auto-enable custom palette if palette-chars was set
  if (opts.palette_custom[0] != '\0') {
    // palette-chars was set - always use PALETTE_CUSTOM (overrides any explicit --palette setting)
    opts.palette_type = PALETTE_CUSTOM;
    opts.palette_custom_set = true;
    log_debug("Set PALETTE_CUSTOM because --palette-chars was provided");

    // Validate palette characters for UTF-8 correctness
    if (!utf8_is_valid(opts.palette_custom)) {
      log_error("Error: --palette-chars contains invalid UTF-8 sequences");
      options_config_destroy(config);
      SAFE_FREE(allocated_mode_argv);
      return option_error_invalid();
    }

    // Check if palette contains non-ASCII characters
    bool has_non_ascii = !utf8_is_ascii_only(opts.palette_custom);
    if (has_non_ascii) {
      // Non-ASCII characters require UTF-8 support
      // Check if UTF-8 is explicitly disabled or unavailable
      bool utf8_disabled = (opts.force_utf8 == COLOR_SETTING_FALSE);
      bool utf8_auto_unavailable = (opts.force_utf8 == COLOR_SETTING_AUTO && !terminal_supports_utf8());

      if (utf8_disabled) {
        log_error("Error: --palette-chars contains non-ASCII characters but --utf8=false was specified");
        log_error("       Remove --utf8=false or use ASCII-only palette characters");
        options_config_destroy(config);
        SAFE_FREE(allocated_mode_argv);
        return option_error_invalid();
      }

      if (utf8_auto_unavailable) {
        log_error("Error: --palette-chars contains non-ASCII characters but terminal does not support UTF-8");
        log_error("       Use --utf8=true to force UTF-8 mode or use ASCII-only palette characters");
        options_config_destroy(config);
        SAFE_FREE(allocated_mode_argv);
        return option_error_invalid();
      }
    }
  }

  // Auto-enable encryption if key was provided
  if (opts.encrypt_key[0] != '\0') {
    // Validate key file exists (skip for remote/virtual keys)
    if (!is_remote_key_path(opts.encrypt_key)) {
      struct stat st;
      if (stat(opts.encrypt_key, &st) != 0) {
        log_error("Key file not found: %s", opts.encrypt_key);
        options_config_destroy(config);
        SAFE_FREE(allocated_mode_argv);
        return SET_ERRNO(ERROR_CRYPTO_KEY, "Key file not found: %s", opts.encrypt_key);
      }
      if ((st.st_mode & S_IFMT) != S_IFREG) {
        log_error("Key path is not a regular file: %s", opts.encrypt_key);
        options_config_destroy(config);
        SAFE_FREE(allocated_mode_argv);
        return SET_ERRNO(ERROR_CRYPTO_KEY, "Key path is not a regular file: %s", opts.encrypt_key);
      }
    }
    opts.encrypt_enabled = 1;
    log_debug("Auto-enabled encryption because --key was provided");
  }

  // Color filter validation and auto-enable
  if (opts.color_filter != COLOR_FILTER_NONE) {
    // Color filter requires color to be enabled
    if (opts.color == COLOR_SETTING_FALSE) {
      log_error("Error: --color-filter cannot be used with --color=false");
      options_config_destroy(config);
      SAFE_FREE(allocated_mode_argv);
      return option_error_invalid();
    }
    // Auto-enable color when color filter is specified
    opts.color = COLOR_SETTING_TRUE;
    log_debug("Auto-enabled color because --color-filter was provided");
  }

  // Detect if splash or status_screen were explicitly set on command line
  for (int i = 0; i < mode_argc; i++) {
    if (mode_argv[i] &&
        (strcmp(mode_argv[i], "--splash-screen") == 0 || strncmp(mode_argv[i], "--splash-screen=", 16) == 0)) {
      opts.splash_screen = true;
      opts.splash_screen_explicitly_set = true;
    }
    if (mode_argv[i] &&
        (strcmp(mode_argv[i], "--no-splash-screen") == 0 || strncmp(mode_argv[i], "--no-splash-screen=", 19) == 0)) {
      opts.splash_screen = false;
      opts.splash_screen_explicitly_set = true;
    }
    if (mode_argv[i] &&
        (strcmp(mode_argv[i], "--status-screen") == 0 || strncmp(mode_argv[i], "--status-screen=", 16) == 0)) {
      opts.status_screen = true;
      opts.status_screen_explicitly_set = true;
    }
    if (mode_argv[i] &&
        (strcmp(mode_argv[i], "--no-status-screen") == 0 || strncmp(mode_argv[i], "--no-status-screen=", 19) == 0)) {
      opts.status_screen = false;
      opts.status_screen_explicitly_set = true;
    }
#ifndef NDEBUG
    if (mode_argv[i] &&
        (strcmp(mode_argv[i], "--sync-state") == 0 || strncmp(mode_argv[i], "--sync-state=", 13) == 0)) {
      opts.debug_sync_state_time_explicit = true;
      if (strcmp(mode_argv[i], "--sync-state") == 0) {
        if (i + 1 < mode_argc) {
          char *endptr;
          double val = strtod(mode_argv[i + 1], &endptr);
          if (endptr != mode_argv[i + 1] && val > 0.0) {
            opts.debug_sync_state_time = val;
            i++;
          }
        }
      } else {
        const char *value_str = mode_argv[i] + 13;
        if (value_str[0] != '\0') {
          char *endptr;
          double val = strtod(value_str, &endptr);
          if (endptr != value_str && val > 0.0) {
            opts.debug_sync_state_time = val;
          }
        }
      }
    }
    if (mode_argv[i] && (strcmp(mode_argv[i], "--backtrace") == 0 || strncmp(mode_argv[i], "--backtrace=", 12) == 0)) {
      opts.debug_backtrace_time_explicit = true;
      if (strcmp(mode_argv[i], "--backtrace") == 0) {
        if (i + 1 < mode_argc) {
          char *endptr;
          double val = strtod(mode_argv[i + 1], &endptr);
          if (endptr != mode_argv[i + 1] && val > 0.0) {
            opts.debug_backtrace_time = val;
            i++;
          }
        }
      } else {
        const char *value_str = mode_argv[i] + 12;
        if (value_str[0] != '\0') {
          char *endptr;
          double val = strtod(value_str, &endptr);
          if (endptr != value_str && val > 0.0) {
            opts.debug_backtrace_time = val;
          }
        }
      }
    }
    if (mode_argv[i] &&
        (strcmp(mode_argv[i], "--memory-report") == 0 || strncmp(mode_argv[i], "--memory-report=", 16) == 0)) {
      opts.debug_memory_report_interval_explicit = true;
      if (strcmp(mode_argv[i], "--memory-report") == 0) {
        if (i + 1 < mode_argc) {
          char *endptr;
          double val = strtod(mode_argv[i + 1], &endptr);
          if (endptr != mode_argv[i + 1] && val > 0.0) {
            opts.debug_memory_report_interval = val;
            i++;
          }
        }
      } else {
        const char *value_str = mode_argv[i] + 16;
        if (value_str[0] != '\0') {
          char *endptr;
          double val = strtod(value_str, &endptr);
          if (endptr != value_str && val > 0.0) {
            opts.debug_memory_report_interval = val;
          }
        }
      }
    }
#endif
  }

  // Auto-disable splash when grep is used (since it's one-time startup screen)
  // UNLESS it was explicitly set by the user
  // Status screen is now compatible with --grep since we support auto-loading patterns
  bool grep_was_provided = false;
  for (int i = 0; i < mode_argc; i++) {
    if (mode_argv[i] && (strcmp(mode_argv[i], "--grep") == 0 || strncmp(mode_argv[i], "--grep=", 7) == 0)) {
      grep_was_provided = true;
      break;
    }
  }

  if (grep_was_provided) {
    if (!opts.splash_screen_explicitly_set) {
      opts.splash_screen = false;
      log_debug("Auto-disabled splash because --grep was provided");
    }
  }

  // Auto-disable splash and status screens when terminal is non-interactive or CLAUDECODE is set
  // This ensures clean output when running under LLM automation or non-interactive shells
  bool is_terminal_interactive = terminal_is_interactive();
  bool is_claudecode_set = platform_getenv("CLAUDECODE") != NULL;
  bool should_auto_disable_screens = !is_terminal_interactive || is_claudecode_set;

  if (should_auto_disable_screens) {
    if (!opts.splash_screen_explicitly_set) {
      opts.splash_screen = false;
      log_debug("Auto-disabled splash (non-interactive terminal or CLAUDECODE set)");
    }
    if (!opts.status_screen_explicitly_set) {
      opts.status_screen = false;
      log_debug("Auto-disabled status screen (non-interactive terminal or CLAUDECODE set)");
    }
  }

  // Validate all string options contain valid UTF-8
  // This prevents crashes and corruption from invalid UTF-8 in any option
  const char *string_fields[][2] = {{"address", opts.address},
                                    {"address6", opts.address6},
                                    {"encrypt_key", opts.encrypt_key},
                                    {"encrypt_keyfile", opts.encrypt_keyfile},
                                    {"server_key", opts.server_key},
                                    {"client_keys", opts.client_keys},
                                    {"discovery_server", opts.discovery_server},
                                    {"discovery_service_key", opts.discovery_service_key},
                                    {"discovery_database_path", opts.discovery_database_path},
                                    {"log_file", opts.log_file},
                                    {"media_file", opts.media_file},
                                    {"palette_custom", opts.palette_custom},
                                    {"stun_servers", opts.stun_servers},
                                    {"turn_servers", opts.turn_servers},
                                    {"turn_username", opts.turn_username},
                                    {"turn_credential", opts.turn_credential},
                                    {"turn_secret", opts.turn_secret},
                                    {"session_string", opts.session_string},
                                    {NULL, NULL}};

  for (int i = 0; string_fields[i][0] != NULL; i++) {
    const char *field_name = string_fields[i][0];
    const char *field_value = string_fields[i][1];

    // Skip empty strings
    if (!field_value || field_value[0] == '\0') {
      continue;
    }

    // Validate UTF-8
    if (!utf8_is_valid(field_value)) {
      log_error("Error: Option --%s contains invalid UTF-8 sequences", field_name);
      log_error("       Value: %s", field_value);
      options_config_destroy(config);
      SAFE_FREE(allocated_mode_argv);
      return option_error_invalid();
    }
  }
  // Validate options
  result = validate_options_and_report(config, &opts);
  if (result != ASCIICHAT_OK) {
    options_config_destroy(config);
    SAFE_FREE(allocated_mode_argv);
    return result;
  }
  // Check for unexpected remaining arguments
  if (remaining_argc > 0) {
    log_error("Error: Unexpected arguments after options:");
    for (int i = 0; i < remaining_argc; i++) {
      log_error("  %s", remaining_argv[i]);
    }
    options_config_destroy(config);
    SAFE_FREE(allocated_mode_argv);
    return option_error_invalid();
  }

  // Mode-specific post-processing
  // Apply mode-specific defaults (port, websocket-port)
  apply_mode_specific_defaults(&opts);
  log_dev("Applied mode-specific defaults: port=%d, websocket_port=%d", opts.port, opts.websocket_port);

  if (detected_mode == MODE_DISCOVERY_SERVICE) {
    // Set default paths if not specified
    if (opts.discovery_database_path[0] == '\0') {
      // Database: Try system-wide location first, fall back to user directories
      // Preference: /usr/local/var/ascii-chat/ > ~/.local/share/ascii-chat/ > ~/.config/ascii-chat/
      char *db_dir = get_discovery_database_dir();
      if (!db_dir) {
        options_config_destroy(config);
        SAFE_FREE(allocated_mode_argv);
        return SET_ERRNO(ERROR_CONFIG, "Failed to get database directory (tried system and user locations)");
      }
      safe_snprintf(opts.discovery_database_path, sizeof(opts.discovery_database_path), "%sdiscovery.db", db_dir);
      SAFE_FREE(db_dir);
    }
  }

  options_config_destroy(config);

  // ========================================================================
  // STAGE 7: Post-Processing & Validation
  // ========================================================================

  // Collect multiple --key flags for multi-key support (server/ACDS only)
  // This enables servers to load both SSH and GPG keys and select the right one
  // during handshake based on what the client expects
  if (detected_mode == MODE_SERVER || detected_mode == MODE_DISCOVERY_SERVICE) {
    int num_keys = options_collect_identity_keys(&opts, argc, argv);
    if (num_keys < 0) {
      SAFE_FREE(allocated_mode_argv);
      return SET_ERRNO(ERROR_INVALID_PARAM, "Failed to collect identity keys");
    }
    // num_keys == 0 is OK (no --key flags provided)
  }

  // After parsing command line options, update dimensions
  // First set any auto dimensions to terminal size, then apply full height logic

  // Clear auto flags if dimensions were explicitly parsed from options
  // This must happen before update_dimensions_to_terminal_size to prevent auto-detected
  // values from overwriting user-specified dimensions
  // Note: width=0 and height=0 are special values meaning "auto-detect", so don't clear auto flags for those
  if (opts.width != OPT_WIDTH_DEFAULT && opts.width != 0) {
    opts.auto_width = false;
  }
  if (opts.height != OPT_HEIGHT_DEFAULT && opts.height != 0) {
    opts.auto_height = false;
  }
  update_dimensions_to_terminal_size(&opts);
  update_dimensions_for_full_height(&opts);
  // Apply verbose level to log threshold
  // Each -V decreases the log level by 1 (showing more verbose output)
  // Minimum level is LOG_DEV (0)
  if (opts.verbose_level > 0) {
    log_level_t current_level = log_get_level();
    int new_level = (int)current_level - (int)opts.verbose_level;
    if (new_level < LOG_DEV) {
      new_level = LOG_DEV;
    }
    log_set_level((log_level_t)new_level);
  }

  // Check WEBCAM_DISABLED environment variable to enable test pattern mode
  // Useful for CI/CD and testing environments without a physical webcam
  const char *webcam_disabled = SAFE_GETENV("WEBCAM_DISABLED");
  if (webcam_disabled &&
      (strcmp(webcam_disabled, "1") == 0 || platform_strcasecmp(webcam_disabled, "true") == 0 ||
       platform_strcasecmp(webcam_disabled, "yes") == 0 || platform_strcasecmp(webcam_disabled, "on") == 0)) {
    opts.test_pattern = true;
  }

  // Apply --no-compress interaction with audio encoding
  if (opts.no_compress) {
    opts.encode_audio = false;
    log_debug("--no-compress set: disabling audio encoding");
  }

  // Set media_from_stdin flag if media_file is "-"
  if (opts.media_file[0] != '\0' && strcmp(opts.media_file, "-") == 0) {
    opts.media_from_stdin = true;
    log_debug("Media file set to stdin");
  }

  // Validate --seek option
  if (opts.media_seek_timestamp > 0.0) {
    // Can't seek stdin
    if (opts.media_from_stdin) {
      log_error("--seek cannot be used with stdin (--file -)");
      SAFE_FREE(allocated_mode_argv);
      return ERROR_INVALID_PARAM;
    }

    // Require --file or --url
    if (opts.media_file[0] == '\0' && opts.media_url[0] == '\0') {
      log_error("--seek requires --file or --url");
      SAFE_FREE(allocated_mode_argv);
      return ERROR_INVALID_PARAM;
    }
  }

  // Validate --pause option
  if (opts.pause) {
    // Require --file or --url (not webcam, not test pattern)
    if (opts.media_file[0] == '\0' && opts.media_url[0] == '\0') {
      log_error("--pause requires --file or --url");
      SAFE_FREE(allocated_mode_argv);
      return ERROR_INVALID_PARAM;
    }
  }

  // Validate --url option
  if (opts.media_url[0] != '\0') {
    // URL must be a valid HTTP(S) URL (YouTube URLs are HTTPS URLs)
    if (!url_is_valid(opts.media_url)) {
      log_error("--url must be a valid HTTP(S) URL: %s", opts.media_url);
      SAFE_FREE(allocated_mode_argv);
      return ERROR_INVALID_PARAM;
    }

    // Normalize bare URLs by prepending http:// if not present
    if (!strstr(opts.media_url, "://")) {
      char normalized_url[2048];
      int result = snprintf(normalized_url, sizeof(normalized_url), "http://%s", opts.media_url);
      if (result > 0 && result < (int)sizeof(normalized_url)) {
        SAFE_STRNCPY(opts.media_url, normalized_url, sizeof(opts.media_url));
      } else {
        log_error("Failed to normalize URL (too long): %s", opts.media_url);
        SAFE_FREE(allocated_mode_argv);
        return ERROR_INVALID_PARAM;
      }
    }
  }

  // ========================================================================
  // STAGE 7: Publish to RCU
  // ========================================================================

  // Save the quiet flag before publishing (RCU will be cleaned up before memory report runs)
#if defined(DEBUG_MEMORY) && !defined(USE_MIMALLOC_DEBUG) && !defined(NDEBUG)
  bool quiet_for_memory_report = opts.quiet;
#endif

  // Publish parsed options to RCU state (replaces options_state_populate_from_globals)
  // This makes the options visible to all threads via lock-free reads
  asciichat_error_t publish_result = options_state_set(&opts);
  if (publish_result != ASCIICHAT_OK) {
    log_error("Failed to publish parsed options to RCU state: %d", publish_result);
    SAFE_FREE(allocated_mode_argv);
    return publish_result;
  }

  // Now update debug memory quiet mode with the saved quiet value
#if defined(DEBUG_MEMORY) && !defined(USE_MIMALLOC_DEBUG) && !defined(NDEBUG)
  debug_memory_set_quiet_mode(quiet_for_memory_report);
#endif

  // ========================================================================
  // Apply color scheme to logging
  // ========================================================================
  // Now that options are parsed, set and apply the selected color scheme to logging
  if (opts.color_scheme_name[0] != '\0') {
    asciichat_error_t scheme_result = colorscheme_set_active_scheme(opts.color_scheme_name);
    if (scheme_result == ASCIICHAT_OK) {
      const color_scheme_t *scheme = colorscheme_get_active_scheme();
      if (scheme) {
        log_set_color_scheme(scheme);
        log_debug("Color scheme applied: %s", opts.color_scheme_name);
      }
    } else {
      log_warn("Failed to apply color scheme: %s", opts.color_scheme_name);
    }
  }

  // ========================================================================
  // STAGE 8: Execute deferred actions (after all options parsed and published)
  // ========================================================================
  // Deferred actions (--list-webcams, --list-microphones, --list-speakers, --show-capabilities)
  // are executed here after all options are fully parsed and published via RCU.
  // This ensures action output reflects the final parsed state (e.g., final dimensions
  // for --show-capabilities).
  // Re-enable terminal output so deferred actions can print their results.
  // It was disabled earlier (STAGE 1C) to suppress log noise during option parsing.
  if (has_action) {
    log_set_terminal_output(true);
  }
  actions_execute_deferred();
  SAFE_FREE(allocated_mode_argv);
  return ASCIICHAT_OK;
}
