
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
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h> // RCU-based thread-safe options
#include <ascii-chat/options/common.h>
#include <ascii-chat/options/parsers.h>
#include <ascii-chat/options/validation.h>
#include <ascii-chat/options/manpage.h>
#include <ascii-chat/options/presets.h>
#include <ascii-chat/options/actions.h>

#include <ascii-chat/options/config.h>
#include <ascii-chat/options/schema.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/options/colorscheme.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/util.h>
#include <ascii-chat/util/path.h>
#include <ascii-chat/util/time.h>

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
  strncpy(opt_buffer, opt_name, opt_len);
  opt_buffer[opt_len] = '\0';
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
    // --color takes a required argument (auto/true/false)
    if (out_takes_arg)
      *out_takes_arg = true;
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
  const options_config_t *config = options_preset_unified(NULL, NULL);
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
      log_debug("Mode detected from argv[0]: %s", argv[0]);
      *out_mode = mode_values_check[i];
      *out_mode_index = 0;
      return ASCIICHAT_OK;
    }
  }

  log_debug("argv[0] '%s' is not a mode name, looking for first non-option argument", argv[0]);

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
  const char *const mode_names[] = {"server", "client", "mirror", "discovery-service", "discovery", NULL};
  const asciichat_mode_t mode_values[] = {MODE_SERVER, MODE_CLIENT, MODE_MIRROR, MODE_DISCOVERY_SERVICE, MODE_INVALID};

  for (int i = 0; mode_names[i] != NULL; i++) {
    if (strcmp(positional, mode_names[i]) == 0) {
      if (mode_values[i] == MODE_INVALID) {
        return SET_ERRNO(ERROR_USAGE, "'discovery' is the default mode and cannot be specified explicitly.");
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

  // Unknown positional argument
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
  int color; // Color setting (COLOR_SETTING_AUTO/TRUE/FALSE) - binary-level option parsed early
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
  binary.color = opts->color; // Save color setting (parsed in STAGE 1A)
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
  opts->color = binary->color; // Restore color setting (parsed in STAGE 1A)
}

options_t options_t_new(void) {
  options_t opts;

  // Zero-initialize all fields first
  memset(&opts, 0, sizeof(opts));

  // ============================================================================
  // LOGGING
  // ============================================================================
  // log_file is mode-dependent at startup and intentionally left empty here
  opts.log_level = OPT_LOG_LEVEL_DEFAULT;
  opts.verbose_level = OPT_VERBOSE_LEVEL_DEFAULT;
  opts.quiet = OPT_QUIET_DEFAULT;

  // ============================================================================
  // TERMINAL
  // ============================================================================
  opts.width = OPT_WIDTH_DEFAULT;
  opts.height = OPT_HEIGHT_DEFAULT;
  opts.auto_width = OPT_AUTO_WIDTH_DEFAULT;
  opts.auto_height = OPT_AUTO_HEIGHT_DEFAULT;
  opts.color = OPT_COLOR_DEFAULT;
  SAFE_STRNCPY(opts.color_scheme_name, OPT_COLOR_SCHEME_NAME_DEFAULT, sizeof(opts.color_scheme_name));

  // ============================================================================
  // WEBCAM
  // ============================================================================
  opts.webcam_index = OPT_WEBCAM_INDEX_DEFAULT;
  opts.webcam_flip = OPT_WEBCAM_FLIP_DEFAULT;
  opts.test_pattern = OPT_TEST_PATTERN_DEFAULT;
  opts.no_audio_mixer = OPT_NO_AUDIO_MIXER_DEFAULT;

  // ============================================================================
  // DISPLAY
  // ============================================================================
  opts.color_mode = OPT_COLOR_MODE_DEFAULT;
  opts.color_filter = OPT_COLOR_FILTER_DEFAULT;
  opts.render_mode = OPT_RENDER_MODE_DEFAULT;
  opts.palette_type = OPT_PALETTE_TYPE_DEFAULT;
  // palette_custom is already zeroed by memset
  opts.palette_custom_set = OPT_PALETTE_CUSTOM_SET_DEFAULT;
  opts.show_capabilities = OPT_SHOW_CAPABILITIES_DEFAULT;
  opts.force_utf8 = OPT_FORCE_UTF8_DEFAULT;
  opts.stretch = OPT_STRETCH_DEFAULT;
  opts.strip_ansi = OPT_STRIP_ANSI_DEFAULT;
  opts.fps = OPT_FPS_DEFAULT;
  opts.splash = OPT_SPLASH_DEFAULT;
  opts.status_screen = OPT_STATUS_SCREEN_DEFAULT;

  // ============================================================================
  // SNAPSHOT
  // ============================================================================
  opts.snapshot_mode = OPT_SNAPSHOT_MODE_DEFAULT;
  opts.snapshot_delay = SNAPSHOT_DELAY_DEFAULT;

  // ============================================================================
  // PERFORMANCE
  // ============================================================================
  opts.compression_level = OPT_COMPRESSION_LEVEL_DEFAULT;
  opts.no_compress = OPT_NO_COMPRESS_DEFAULT;
  opts.encode_audio = OPT_ENCODE_AUDIO_DEFAULT;

  // ============================================================================
  // SECURITY
  // ============================================================================
  opts.encrypt_enabled = OPT_ENCRYPT_ENABLED_DEFAULT;
  // encrypt_key is already zeroed by memset
  // password is already zeroed by memset
  // encrypt_keyfile is already zeroed by memset
  opts.no_encrypt = OPT_NO_ENCRYPT_DEFAULT;
  // server_key is already zeroed by memset
  // client_keys is already zeroed by memset
  opts.discovery_insecure = OPT_ACDS_INSECURE_DEFAULT;
  // discovery_service_key is already zeroed by memset
  // identity_keys array is already zeroed by memset
  opts.num_identity_keys = 0;
  opts.require_server_identity = OPT_REQUIRE_SERVER_IDENTITY_DEFAULT;
  opts.require_client_identity = OPT_REQUIRE_CLIENT_IDENTITY_DEFAULT;
  opts.require_server_verify = OPT_REQUIRE_SERVER_VERIFY_DEFAULT;
  opts.require_client_verify = OPT_REQUIRE_CLIENT_VERIFY_DEFAULT;

  // ============================================================================
  // NETWORK
  // ============================================================================
  opts.port = OPT_PORT_INT_DEFAULT;
  opts.max_clients = OPT_MAX_CLIENTS_DEFAULT;
  opts.reconnect_attempts = OPT_RECONNECT_ATTEMPTS_DEFAULT;
  opts.lan_discovery = OPT_LAN_DISCOVERY_DEFAULT;
  opts.no_mdns_advertise = OPT_NO_MDNS_ADVERTISE_DEFAULT;
  opts.webrtc = OPT_WEBRTC_DEFAULT;
  opts.no_webrtc = OPT_NO_WEBRTC_DEFAULT;
  opts.prefer_webrtc = OPT_PREFER_WEBRTC_DEFAULT;
  opts.webrtc_skip_stun = OPT_WEBRTC_SKIP_STUN_DEFAULT;
  opts.webrtc_disable_turn = OPT_WEBRTC_DISABLE_TURN_DEFAULT;
  SAFE_STRNCPY(opts.stun_servers, OPT_STUN_SERVERS_DEFAULT, sizeof(opts.stun_servers));
  SAFE_STRNCPY(opts.turn_servers, OPT_TURN_SERVERS_DEFAULT, sizeof(opts.turn_servers));
  SAFE_STRNCPY(opts.turn_username, OPT_TURN_USERNAME_DEFAULT, sizeof(opts.turn_username));
  SAFE_STRNCPY(opts.turn_credential, OPT_TURN_CREDENTIAL_DEFAULT, sizeof(opts.turn_credential));
  // turn_secret is already zeroed by memset
  SAFE_STRNCPY(opts.discovery_server, OPT_ENDPOINT_DISCOVERY_SERVICE, sizeof(opts.discovery_server));
  opts.discovery_port = OPT_ACDS_PORT_INT_DEFAULT;
  opts.discovery_expose_ip = OPT_ACDS_EXPOSE_IP_DEFAULT;
  opts.enable_upnp = OPT_ENABLE_UPNP_DEFAULT;
  opts.discovery = OPT_ACDS_DEFAULT;

  // ============================================================================
  // MEDIA
  // ============================================================================
  // media_file is already zeroed by memset
  // media_url is already zeroed by memset
  opts.media_loop = OPT_MEDIA_LOOP_DEFAULT;
  opts.pause = OPT_PAUSE_DEFAULT;
  opts.media_from_stdin = OPT_MEDIA_FROM_STDIN_DEFAULT;
  opts.media_seek_timestamp = OPT_MEDIA_SEEK_TIMESTAMP_DEFAULT;
  // cookies_from_browser is already zeroed by memset
  opts.no_cookies_from_browser = OPT_NO_COOKIES_FROM_BROWSER_DEFAULT;

  // ============================================================================
  // AUDIO
  // ============================================================================
  opts.audio_enabled = OPT_AUDIO_ENABLED_DEFAULT;
  opts.audio_source = OPT_AUDIO_SOURCE_DEFAULT;
  opts.microphone_index = OPT_MICROPHONE_INDEX_DEFAULT;
  opts.speakers_index = OPT_SPEAKERS_INDEX_DEFAULT;
  opts.microphone_sensitivity = OPT_MICROPHONE_SENSITIVITY_DEFAULT;
  opts.speakers_volume = OPT_SPEAKERS_VOLUME_DEFAULT;
  opts.audio_no_playback = OPT_AUDIO_NO_PLAYBACK_DEFAULT;
  opts.audio_analysis_enabled = OPT_AUDIO_ANALYSIS_ENABLED_DEFAULT;

  // ============================================================================
  // DATABASE (discovery-service only)
  // ============================================================================
  // discovery_database_path is already zeroed by memset

  // ============================================================================
  // Internal/Non-Displayed Fields
  // ============================================================================
  opts.help = OPT_HELP_DEFAULT;
  opts.version = OPT_VERSION_DEFAULT;
  // config_file is already zeroed by memset
  SAFE_STRNCPY(opts.address, OPT_ADDRESS_DEFAULT, sizeof(opts.address));
  SAFE_STRNCPY(opts.address6, OPT_ADDRESS6_DEFAULT, sizeof(opts.address6));
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
  // Determine log filename based on mode
  const char *log_filename = SAFE_MALLOC(PLATFORM_MAX_PATH_LENGTH, char *);
  switch (detected_mode) {
  case MODE_SERVER:
    strcpy((char *)log_filename, "server.log");
    break;
  case MODE_CLIENT:
    strcpy((char *)log_filename, "client.log");
    break;
  case MODE_MIRROR:
    strcpy((char *)log_filename, "mirror.log");
    break;
  case MODE_DISCOVERY_SERVICE:
    strcpy((char *)log_filename, "acds.log");
    break;
  case MODE_DISCOVERY:
    strcpy((char *)log_filename, "discovery.log");
    break;
  default:
    strcpy((char *)log_filename, "ascii-chat.log");
    break;
  }

  char *log_dir = get_log_dir();

  if (log_dir) {
    // Build full log file path: log_dir + separator + log_filename
    char *default_log_path = SAFE_MALLOC(PLATFORM_MAX_PATH_LENGTH, char *);
    safe_snprintf(default_log_path, PLATFORM_MAX_PATH_LENGTH, "%s%s%s", log_dir, PATH_SEPARATOR_STR, log_filename);

    // Validate and normalize the path
    char *normalized_default_log = NULL;
    if (path_validate_user_path(default_log_path, PATH_ROLE_LOG_FILE, &normalized_default_log) == ASCIICHAT_OK) {
      SAFE_SNPRINTF(opts.log_file, OPTIONS_BUFF_SIZE, "%s", normalized_default_log);
      SAFE_FREE(normalized_default_log);
    } else {
      // Validation failed - use the path as-is (validation may fail in debug builds)
      SAFE_SNPRINTF(opts.log_file, OPTIONS_BUFF_SIZE, "%s", default_log_path);
    }

    SAFE_FREE(log_filename);
    SAFE_FREE(log_dir);
    return default_log_path;

  } else {
    SAFE_FREE(log_filename);
    SAFE_FREE(log_dir);
    char *tmp_dir = SAFE_MALLOC(PLATFORM_MAX_PATH_LENGTH, char *);
    if (platform_get_temp_dir(tmp_dir, PLATFORM_MAX_PATH_LENGTH)) {
      char *default_log_path = SAFE_MALLOC(PLATFORM_MAX_PATH_LENGTH, char *);
      safe_snprintf(default_log_path, sizeof(default_log_path), "%s%s%s", tmp_dir, PATH_SEPARATOR_STR,
                    "ascii-chat.log");
      SAFE_FREE(tmp_dir);
      return default_log_path;

    } else {
      SAFE_FREE(tmp_dir);
      return log_filename;
    }
  }
}

// ============================================================================
// Main Option Parser Entry Point
// ============================================================================

asciichat_error_t options_init(int argc, char **argv) {
  log_debug("options_init called with argc=%d, argv[0]=%s, argv[1]=%s", argc, argc > 0 ? argv[0] : "NULL",
            argc > 1 ? argv[1] : "NULL");

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
  bool show_help = false;
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
  for (int i = 1; i < argc; i++) {
    if (argv[i][0] == '-') {
      if (strcmp(argv[i], "--quiet") == 0 || strcmp(argv[i], "-q") == 0) {
        user_quiet = true;
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
      if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
        show_help = true;
        has_action = true;
        // Don't break here - continue scanning for other quick actions in case there are multiple
        // (though --help is usually the only one passed)
      }
      // Parse --color early so it affects help output colors
      if (strcmp(argv[i], "--color") == 0) {
        if (i + 1 < argc && argv[i + 1][0] != '-') {
          char *error_msg = NULL;
          if (parse_color_setting(argv[i + 1], &parsed_color_setting, &error_msg)) {
            color_setting_found = true;
            i++; // Skip the setting argument
          } else {
            if (error_msg) {
              log_error("Error parsing --color: %s", error_msg);
              free(error_msg);
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
      if (strcmp(argv[i], "--config-create") == 0) {
        create_config = true;
        has_action = true;
        if (i + 1 < argc && argv[i + 1][0] != '-') {
          config_create_path = argv[i + 1];
        }
        break;
      }
      if (strcmp(argv[i], "--man-page-create") == 0) {
        create_manpage = true;
        has_action = true;
        if (i + 1 < argc && argv[i + 1][0] != '-') {
          manpage_create_path = argv[i + 1];
          // Consume the argument
          i++;
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
          // action_completions() calls exit(), so we don't reach here
        } else {
          log_plain_stderr("Error: --completions requires shell name (bash, fish, zsh, powershell)");
          return ERROR_USAGE;
        }
        break; // Unreachable, but for clarity
      }
      if (strcmp(argv[i], "--list-webcams") == 0) {
        has_action = true;
        action_list_webcams();
        // action_list_webcams() calls exit(), so we don't reach here
        break;
      }
      if (strcmp(argv[i], "--list-microphones") == 0) {
        has_action = true;
        action_list_microphones();
        // action_list_microphones() calls exit(), so we don't reach here
        break;
      }
      if (strcmp(argv[i], "--list-speakers") == 0) {
        has_action = true;
        action_list_speakers();
        // action_list_speakers() calls exit(), so we don't reach here
        break;
      }
      // Check for other action flags that are parsed by the builder
      // Note: --show-capabilities is only for client/mirror modes (parsed by builder)
      if (strcmp(argv[i], "--show-capabilities") == 0) {
        has_action = true;
      }
    }
  }

  // Store action flag globally for use during cleanup
  set_action_flag(has_action);

  // ========================================================================
  // EARLY: Check if --color was in argv and set global flags for color detection
  // ========================================================================
  // This must happen VERY EARLY, even before logging init, because the builder
  // will execute help actions which call colored_string(), and those need to know
  // whether --color was passed. We can't wait until after builder parsing.
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--color") == 0) {
      g_color_flag_passed = true;
      g_color_flag_value = true;
      break;
    }
  }

  // NOTE: Timer system and shared subsystems are initialized by src/main.c
  // via asciichat_shared_init() BEFORE options_init() is called.
  // This allows options_init() to use properly configured logging.

  // If an action flag is detected OR user passed --quiet, silence logs for clean output
  if (user_quiet || has_action) {
    log_set_terminal_output(false); // Suppress console logging for clean action output
  }

  // Create local options struct and initialize with defaults
  options_t opts = options_t_new(); // Initialize with all defaults

  // Set log file default for config file generation
  SAFE_SNPRINTF(opts.log_file, OPTIONS_BUFF_SIZE, "ascii-chat.log");

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
    if (create_config) {
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
      // action_create_config() calls exit(), so we don't reach here
    }
    if (create_manpage) {
      // Call action handler which handles all output and prompts properly
      action_create_manpage(manpage_create_path);
      // action_create_manpage() calls exit(), so we don't reach here
    }
  }

  // ========================================================================
  // STAGE 1B: DO MODE DETECTION (after non-mode actions, before --help)
  // ========================================================================
  // This ensures that --help for an invalid mode like "discovery" properly fails

  asciichat_mode_t detected_mode = MODE_DISCOVERY; // Default mode
  char detected_session_string[SESSION_STRING_BUFFER_SIZE] = {0};
  int mode_index = -1;

  asciichat_error_t mode_detect_result =
      options_detect_mode(argc, argv, &detected_mode, detected_session_string, &mode_index);
  if (mode_detect_result != ASCIICHAT_OK) {
    return mode_detect_result;
  }

  // ========================================================================
  // STAGE 1C: Process detected mode and set log file early
  // ========================================================================
  // Set log file based on mode IMMEDIATELY (even before --help check)
  // so all modes use their proper log files
  opts.detected_mode = detected_mode;
  char *log_filename = options_get_log_filepath(detected_mode, opts);
  SAFE_SNPRINTF(opts.log_file, OPTIONS_BUFF_SIZE, "%s", log_filename);
  // Force stderr when stdout is not a TTY (piping or redirecting output)
  bool is_tty = platform_isatty(STDOUT_FILENO);
  log_init(opts.log_file, GET_OPTION(log_level), !is_tty, false);
  log_debug("Initialized mode-specific logging for mode %d: %s", detected_mode, opts.log_file);
  SAFE_FREE(log_filename);

  // If --help was found in STAGE 1A, handle it with the detected mode
  if (show_help) {
    opts.help = true;
    options_state_set(&opts);
    return ASCIICHAT_OK;
  }

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
      // Handle -q and --quiet (disable console logging)
      if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
        opts.quiet = true;
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
    const options_config_t *config = options_preset_unified(NULL, NULL);
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
  // If mode_index == -1, we use all arguments (they become mode-specific args)
  int mode_argc = argc;
  char **mode_argv = (char **)argv;
  char **allocated_mode_argv = NULL; // Track if we need to free mode_argv

  if (mode_index != -1) {
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
        } else if (takes_optional_arg && i + 1 < mode_index && argv[i + 1][0] != '-' &&
                   isdigit((unsigned char)argv[i + 1][0])) {
          i++; // Skip optional numeric argument
        }
        continue;
      }
      // Not a binary option, copy to mode_argv
      new_mode_argv[new_argv_idx++] = argv[i];
    }
    for (int i = 0; i < args_after_mode; i++) {
      new_mode_argv[new_argv_idx + i] = argv[mode_index + 1 + i];
    }
    // Calculate actual argc (program + filtered args before + args after)
    mode_argc = new_argv_idx + args_after_mode;
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
    }
    options_config_destroy(unified_config);
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
    log_warn("Failed to publish options before config loading (non-fatal)");
  }

  // Load config files - now uses detected_mode directly for bitmask validation
  // Save webcam_flip as it should not be reset by config files
  // Also save encryption settings - they should only be controlled via CLI, not config file
  bool saved_webcam_flip_from_config = opts.webcam_flip;
  bool saved_encrypt_enabled = opts.encrypt_enabled;
  asciichat_error_t config_result = config_load_system_and_user(detected_mode, false, &opts);
  (void)config_result; // Continue with defaults and CLI parsing regardless of result

  // Restore binary-level options (don't let config override command-line options)
  restore_binary_level(&opts, &binary_before_config);

  // Restore webcam_flip - config shouldn't override the default
  opts.webcam_flip = saved_webcam_flip_from_config;

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
  const options_config_t *config = options_preset_unified(NULL, NULL);
  if (!config) {
    SAFE_FREE(allocated_mode_argv);
    return SET_ERRNO(ERROR_CONFIG, "Failed to create options configuration");
  }

  int remaining_argc;
  char **remaining_argv;

  // Save webcam_flip before applying defaults (should not be reset by defaults)
  bool saved_webcam_flip = opts.webcam_flip;

  // Apply defaults from unified config
  asciichat_error_t defaults_result = options_config_set_defaults(config, &opts);
  if (defaults_result != ASCIICHAT_OK) {
    options_config_destroy(config);
    SAFE_FREE(allocated_mode_argv);
    return defaults_result;
  }

  // Restore binary-level options (they should never be overridden by defaults)
  restore_binary_level(&opts, &binary_before_defaults);

  // Restore webcam_flip - it should keep the value from options_t_new()
  // unless explicitly set by the user (but defaults shouldn't override it)
  opts.webcam_flip = saved_webcam_flip;

  // CRITICAL: RESTORE detected_mode BEFORE parsing so mode validation works
  opts.detected_mode = mode_saved_for_parsing;

  // Save webcam_flip before parsing - it should not be reset by the parser
  bool saved_webcam_flip_for_parse = opts.webcam_flip;

  // Parse mode-specific arguments
  option_mode_bitmask_t mode_bitmask = (1 << mode_saved_for_parsing);
  asciichat_error_t result =
      options_config_parse(config, mode_argc, mode_argv, &opts, mode_bitmask, &remaining_argc, &remaining_argv);

  // Restore webcam_flip - it should keep the default value unless explicitly overridden
  opts.webcam_flip = saved_webcam_flip_for_parse;
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
  }

  // Auto-enable encryption if key was provided
  if (opts.encrypt_key[0] != '\0') {
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
  if (detected_mode == MODE_DISCOVERY_SERVICE) {
    // Set default paths if not specified
    if (opts.discovery_database_path[0] == '\0') {
      char *config_dir = get_config_dir();
      if (!config_dir) {
        options_config_destroy(config);
        SAFE_FREE(allocated_mode_argv);
        return SET_ERRNO(ERROR_CONFIG, "Failed to get config directory for database path");
      }
      safe_snprintf(opts.discovery_database_path, sizeof(opts.discovery_database_path), "%sdiscovery.db", config_dir);
      SAFE_FREE(config_dir);
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
  actions_execute_deferred();

  SAFE_FREE(allocated_mode_argv);
  return ASCIICHAT_OK;
}
