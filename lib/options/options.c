
/**
 * @file options.c
 * @ingroup options
 * @brief ⚙️ Unified command-line argument parser with built-in mode detection
 *
 * Main entry point for option parsing. Detects mode from command-line arguments,
 * parses binary-level options, then dispatches to mode-specific parsers
 * (client, server, mirror, acds) with common initialization and post-processing.
 */

#include <ctype.h>
#include "options/options.h"
#include "options/rcu.h" // RCU-based thread-safe options
#include "options/common.h"
#include "options/parsers.h"
#include "options/validation.h"
#include "options/manpage.h"
#include "options/presets.h"
#include "network/mdns/discovery.h"

#include "options/config.h"
#include "options/schema.h"
#include "asciichat_errno.h"
#include "common.h"
#include "log/logging.h"
#include "platform/system.h"
#include "platform/terminal.h"
#include "platform/util.h"
#include "util/path.h"
#include "network/mdns/discovery.h"
#include "version.h"

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  if (!arg)
    return false;

  if (out_takes_arg)
    *out_takes_arg = false;
  if (out_takes_optional_arg)
    *out_takes_optional_arg = false;

  // Extract option name (without leading dashes)
  const char *opt_name = arg;
  if (arg[0] == '-') {
    opt_name = arg + (arg[1] == '-' ? 2 : 1); // Skip - or --
  }

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
      // Check if this option applies to mirror mode
      if (desc->mode_bitmask & (1 << MODE_MIRROR)) {
        found = true;
      }
      break;
    }

    // Check by short name
    if (short_name != '\0' && desc->short_name == short_name) {
      // Check if this option applies to mirror mode
      if (desc->mode_bitmask & (1 << MODE_MIRROR)) {
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
  const asciichat_mode_t mode_values_check[] = {MODE_SERVER, MODE_CLIENT, MODE_MIRROR, MODE_DISCOVERY_SERVER};
  for (int i = 0; mode_names_check[i] != NULL; i++) {
    if (strcmp(argv[0], mode_names_check[i]) == 0) {
      *out_mode = mode_values_check[i];
      *out_mode_index = 0;
      return ASCIICHAT_OK;
    }
  }

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
        // Skip the argument if needed
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
  const char *const mode_names[] = {"server", "client", "mirror", "discovery-service", NULL};
  const asciichat_mode_t mode_values[] = {MODE_SERVER, MODE_CLIENT, MODE_MIRROR, MODE_DISCOVERY_SERVER};

  for (int i = 0; mode_names[i] != NULL; i++) {
    if (strcmp(positional, mode_names[i]) == 0) {
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
      SAFE_STRNCPY(out_session_string, positional, 64);
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
options_t options_t_new(void) {
  options_t opts;

  // Zero-initialize all fields first
  memset(&opts, 0, sizeof(opts));

  // ============================================================================
  // Terminal Dimensions
  // ============================================================================
  opts.width = OPT_WIDTH_DEFAULT;
  opts.height = OPT_HEIGHT_DEFAULT;
  opts.auto_width = OPT_AUTO_WIDTH_DEFAULT;
  opts.auto_height = OPT_AUTO_HEIGHT_DEFAULT;

  // ============================================================================
  // Network Options
  // ============================================================================
  SAFE_SNPRINTF(opts.port, OPTIONS_BUFF_SIZE, "%s", OPT_PORT_DEFAULT);
  SAFE_STRNCPY(opts.address, OPT_ADDRESS_DEFAULT, sizeof(opts.address));
  SAFE_STRNCPY(opts.address6, OPT_ADDRESS6_DEFAULT, sizeof(opts.address6));

  // ============================================================================
  // Server Options
  // ============================================================================
  opts.max_clients = OPT_MAX_CLIENTS_DEFAULT;
  opts.compression_level = OPT_COMPRESSION_LEVEL_DEFAULT;
  opts.fps = OPT_FPS_DEFAULT;

  // ============================================================================
  // Client Options
  // ============================================================================
  opts.webcam_index = OPT_WEBCAM_INDEX_DEFAULT;
  opts.microphone_index = OPT_MICROPHONE_INDEX_DEFAULT;
  opts.speakers_index = OPT_SPEAKERS_INDEX_DEFAULT;
  opts.reconnect_attempts = OPT_RECONNECT_ATTEMPTS_DEFAULT;
  opts.snapshot_delay = SNAPSHOT_DELAY_DEFAULT;

  // ============================================================================
  // Display Options
  // ============================================================================
  opts.color_mode = OPT_COLOR_MODE_DEFAULT;
  opts.render_mode = OPT_RENDER_MODE_DEFAULT;
  opts.palette_type = PALETTE_STANDARD; // Default palette (no OPT_*_DEFAULT for this)
  opts.show_capabilities = OPT_SHOW_CAPABILITIES_DEFAULT;
  opts.force_utf8 = OPT_FORCE_UTF8_DEFAULT;
  opts.stretch = OPT_STRETCH_DEFAULT;
  opts.strip_ansi = OPT_STRIP_ANSI_DEFAULT;

  // ============================================================================
  // Audio Options
  // ============================================================================
  opts.audio_enabled = OPT_AUDIO_ENABLED_DEFAULT;
  opts.encode_audio = OPT_ENCODE_AUDIO_DEFAULT;
  opts.microphone_sensitivity = OPT_MICROPHONE_SENSITIVITY_DEFAULT;
  opts.speakers_volume = OPT_SPEAKERS_VOLUME_DEFAULT;
  opts.audio_analysis_enabled = OPT_AUDIO_ANALYSIS_ENABLED_DEFAULT;
  opts.audio_no_playback = OPT_AUDIO_NO_PLAYBACK_DEFAULT;

  // ============================================================================
  // Webcam Options
  // ============================================================================
  opts.webcam_flip = OPT_WEBCAM_FLIP_DEFAULT;
  opts.test_pattern = OPT_TEST_PATTERN_DEFAULT;

  // ============================================================================
  // Output Options
  // ============================================================================
  opts.quiet = OPT_QUIET_DEFAULT;
  opts.snapshot_mode = OPT_SNAPSHOT_MODE_DEFAULT;

  // ============================================================================
  // Encryption Options
  // ============================================================================
  opts.encrypt_enabled = OPT_ENCRYPT_ENABLED_DEFAULT;
  opts.no_encrypt = OPT_NO_ENCRYPT_DEFAULT;

  // ============================================================================
  // WebRTC Options
  // ============================================================================
  opts.webrtc = OPT_WEBRTC_DEFAULT;
  opts.prefer_webrtc = OPT_PREFER_WEBRTC_DEFAULT;
  opts.no_webrtc = OPT_NO_WEBRTC_DEFAULT;
  opts.webrtc_skip_stun = OPT_WEBRTC_SKIP_STUN_DEFAULT;
  opts.webrtc_disable_turn = OPT_WEBRTC_DISABLE_TURN_DEFAULT;

  // ============================================================================
  // ACDS/Discovery Options
  // ============================================================================
  opts.discovery = OPT_ACDS_DEFAULT;
  opts.discovery_expose_ip = OPT_ACDS_EXPOSE_IP_DEFAULT;
  opts.discovery_insecure = OPT_ACDS_INSECURE_DEFAULT;
  opts.enable_upnp = OPT_ENABLE_UPNP_DEFAULT;
  opts.lan_discovery = OPT_LAN_DISCOVERY_DEFAULT;
  SAFE_STRNCPY(opts.stun_servers, OPT_STUN_SERVERS_DEFAULT, sizeof(opts.stun_servers));
  SAFE_STRNCPY(opts.turn_servers, OPT_TURN_SERVERS_DEFAULT, sizeof(opts.turn_servers));
  SAFE_STRNCPY(opts.turn_username, OPT_TURN_USERNAME_DEFAULT, sizeof(opts.turn_username));
  SAFE_STRNCPY(opts.turn_credential, OPT_TURN_CREDENTIAL_DEFAULT, sizeof(opts.turn_credential));

  // ============================================================================
  // Other Options
  // ============================================================================
  opts.no_compress = OPT_NO_COMPRESS_DEFAULT;
  opts.media_loop = OPT_MEDIA_LOOP_DEFAULT;

  return opts;
}

// ============================================================================
// Main Option Parser Entry Point
// ============================================================================

asciichat_error_t options_init(int argc, char **argv) {
  // Validate arguments (safety check for tests)
  if (argc < 0 || argc > 1000) {
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
  asciichat_error_t rcu_init_result = options_state_init();
  if (rcu_init_result != ASCIICHAT_OK) {
    return rcu_init_result;
  }

  // Create local options struct and initialize with defaults
  options_t opts = {0}; // Zero-initialize all fields

  // ========================================================================
  // STAGE 1: Mode Detection and Binary-Level Option Handling
  // ========================================================================

  // Check for binary-level actions FIRST (before mode detection)
  // These actions may take arguments, so we need to check them before mode detection
  bool show_help = false;
  bool show_version = false;
  bool create_config = false;
  bool create_manpage = false;
  const char *config_create_path = NULL;
  const char *manpage_template_file = NULL; // Template file path (.1.in)
  const char *manpage_content_file = NULL;  // Content file path (.1.content)

  // Quick scan for action flags (they may have arguments)
  for (int i = 1; i < argc; i++) {
    if (argv[i][0] == '-') {
      if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
        show_help = true;
        break;
      }
      if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
        show_version = true;
        break;
      }
      if (strcmp(argv[i], "--config-create") == 0) {
        create_config = true;
        if (i + 1 < argc && argv[i + 1][0] != '-') {
          config_create_path = argv[i + 1];
        }
        break;
      }
      if (strcmp(argv[i], "--create-man-page") == 0) {
        create_manpage = true;
        // First arg: template file path (.1.in)
        if (i + 1 < argc && argv[i + 1][0] != '-') {
          manpage_template_file = argv[i + 1];
          i++; // Skip this arg
          // Second arg: content file path (.1.content)
          if (i + 1 < argc && argv[i + 1][0] != '-') {
            manpage_content_file = argv[i + 1];
            i++; // Skip this arg
          }
        }
        break;
      }
    }
  }

  // If we found an action, skip mode detection and handle it immediately
  if (show_help || show_version || create_config || create_manpage) {
    // Handle actions (they will exit)
    if (show_help) {
      opts.help = true;
      options_state_set(&opts);
      return ASCIICHAT_OK;
    }
    if (show_version) {
      opts.version = true;
      options_state_set(&opts);
      return ASCIICHAT_OK;
    }
    if (create_config) {
      // Handle --config-create: create default config file and exit
      char config_path[PLATFORM_MAX_PATH_LENGTH];
      if (config_create_path) {
        SAFE_STRNCPY(config_path, config_create_path, sizeof(config_path));
      } else {
        char *config_dir = get_config_dir();
        if (!config_dir) {
          fprintf(stderr, "Error: Failed to determine default config directory\n");
          return ERROR_CONFIG;
        }
        snprintf(config_path, sizeof(config_path), "%sconfig.toml", config_dir);
        SAFE_FREE(config_dir);
      }
      asciichat_error_t result = config_create_default(config_path, &opts);
      if (result != ASCIICHAT_OK) {
        asciichat_error_context_t err_ctx;
        if (HAS_ERRNO(&err_ctx)) {
          fprintf(stderr, "Error creating config: %s\n", err_ctx.context_message);
        } else {
          fprintf(stderr, "Error: Failed to create config file at %s\n", config_path);
        }
        return result;
      }
      printf("Created default config file at: %s\n", config_path);
      exit(0);
    }
    if (create_manpage) {
      // Handle --create-man-page: generate merged man page template to stdout
      // First arg: template file path (.1.in), second arg: content file path (.1.content)
      const char *template_path = manpage_template_file;
      if (!template_path) {
        // Default template file path
        template_path = "share/man/man1/ascii-chat.1.in";
      }
      const char *content_file = manpage_content_file;
      if (!content_file) {
        // Default content file path
        content_file = "share/man/man1/ascii-chat.1.content";
      }
      const options_config_t *config = options_preset_unified(NULL, NULL);
      if (!config) {
        fprintf(stderr, "Error: Failed to get binary options config\n");
        return ERROR_MEMORY;
      }
      // Pass NULL as output_path to write merged result to stdout
      asciichat_error_t err = options_config_generate_manpage_merged(
          config, "ascii-chat", NULL, NULL, "Video chat in your terminal", template_path, content_file);
      options_config_destroy(config);
      if (err != ASCIICHAT_OK) {
        asciichat_error_context_t err_ctx;
        if (HAS_ERRNO(&err_ctx)) {
          fprintf(stderr, "Error: %s\n", err_ctx.context_message);
        } else {
          fprintf(stderr, "Error: Failed to generate man page template\n");
        }
        return err;
      }
      exit(0);
    }
  }

  // Now do mode detection (only if no action was found)
  asciichat_mode_t detected_mode = MODE_DISCOVERY; // Default mode
  char detected_session_string[64] = {0};
  int mode_index = -1;

  asciichat_error_t mode_detect_result =
      options_detect_mode(argc, argv, &detected_mode, detected_session_string, &mode_index);
  if (mode_detect_result != ASCIICHAT_OK) {
    return mode_detect_result;
  }

  opts.detected_mode = detected_mode;

  // Check for binary-level options that can appear before or after mode
  int search_limit = (mode_index == -1) ? argc : mode_index;
  bool binary_level_log_file_set = false;  // Track if user explicitly set --log-file
  bool binary_level_log_level_set = false; // Track if user explicitly set --log-level
  for (int i = 1; i < search_limit; i++) {
    if (argv[i][0] == '-') {
      // Handle -V and --verbose (stackable verbosity)
      if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--verbose") == 0) {
        // Check if next argument is a number (optional argument)
        if (i + 1 < search_limit && argv[i + 1][0] != '-') {
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
        if (i + 1 < argc && argv[i + 1][0] != '-') {
          char *error_msg = NULL;
          if (parse_log_level(argv[i + 1], &opts.log_level, &error_msg)) {
            binary_level_log_level_set = true;
            i++; // Skip the level argument
          } else {
            if (error_msg) {
              fprintf(stderr, "Error parsing --log-level: %s\n", error_msg);
              free(error_msg);
            }
          }
        }
      }
      // Handle -L and --log-file FILE (set log file path)
      if ((strcmp(argv[i], "-L") == 0 || strcmp(argv[i], "--log-file") == 0)) {
        if (i + 1 < argc && argv[i + 1][0] != '-') {
          SAFE_STRNCPY(opts.log_file, argv[i + 1], sizeof(opts.log_file));
          binary_level_log_file_set = true;
          i++; // Skip the file argument
        }
      }
    }
  }

  if (show_help) {
    // Show binary-level help from src/main.c
    opts.help = true;
    options_state_set(&opts);
    return ASCIICHAT_OK;
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
        fprintf(stderr, "Error: Failed to determine default config directory\n");
        return ERROR_CONFIG;
      }
      snprintf(config_path, sizeof(config_path), "%sconfig.toml", config_dir);
      SAFE_FREE(config_dir);
    }

    // Create config with default options
    asciichat_error_t result = config_create_default(config_path, &opts);
    if (result != ASCIICHAT_OK) {
      asciichat_error_context_t err_ctx;
      if (HAS_ERRNO(&err_ctx)) {
        fprintf(stderr, "Error creating config: %s\n", err_ctx.context_message);
      } else {
        fprintf(stderr, "Error: Failed to create config file at %s\n", config_path);
      }
      return result;
    }

    printf("Created default config file at: %s\n", config_path);
    exit(0); // Exit successfully after creating config
  }

  if (create_manpage) {
    // Handle --create-man-page-template: generate merged man page template
    // The .1.in file is the existing template to read from (not the output)
    const char *existing_template_path = "share/man/man1/ascii-chat.1.in";
    const options_config_t *config = options_preset_unified(NULL, NULL);
    if (!config) {
      fprintf(stderr, "Error: Failed to get binary options config\n");
      return ERROR_MEMORY;
    }

    // Use content file if provided (debug builds only), otherwise default to share/man/man1/ascii-chat.1.content
    const char *content_file = manpage_content_file;
    if (!content_file) {
      content_file = "share/man/man1/ascii-chat.1.content";
    }
    // Write merged result to the template file (overwrites existing template)
    asciichat_error_t err =
        options_config_generate_manpage_merged(config, "ascii-chat", NULL, existing_template_path,
                                               "Video chat in your terminal", existing_template_path, content_file);

    options_config_destroy(config);

    if (err != ASCIICHAT_OK) {
      asciichat_error_context_t err_ctx;
      if (HAS_ERRNO(&err_ctx)) {
        fprintf(stderr, "Error: %s\n", err_ctx.context_message);
      } else {
        fprintf(stderr, "Error: Failed to generate man page template\n");
      }
      return err;
    }

    printf("Generated merged man page template: %s\n", existing_template_path);
    printf("Review AUTO sections - manual edits will be lost on regeneration.\n");

    exit(0); // Exit successfully after generating template
  }

  // ========================================================================
  // STAGE 2: Build argv for mode-specific parsing
  // ========================================================================

  // If mode was found, build argv with only arguments after the mode
  // If mode_index == -1, we use all arguments (they become mode-specific args)
  int mode_argc = argc;
  char **mode_argv = (char **)argv;

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

  // Save detected_mode before creating new opts struct
  asciichat_mode_t saved_detected_mode = opts.detected_mode;

  // Initialize all defaults using options_t_new()
  opts = options_t_new();

  // Restore detected_mode (was zeroed by options_t_new)
  opts.detected_mode = saved_detected_mode;

  // Set default log file paths based on build type
  // Release: $tmpdir/ascii-chat/MODE.log (e.g., /tmp/ascii-chat/server.log)
  // Debug: MODE.log in current working directory (e.g., ./server.log)
  // SKIP this if user already set --log-file or -L at binary level
  if (!binary_level_log_file_set) {
    char *log_dir = get_log_dir();
    if (log_dir) {
      // Determine log filename based on mode
      const char *log_filename;
      switch (detected_mode) {
      case MODE_SERVER:
        log_filename = "server.log";
        break;
      case MODE_CLIENT:
        log_filename = "client.log";
        break;
      case MODE_MIRROR:
        log_filename = "mirror.log";
        break;
      case MODE_DISCOVERY_SERVER:
        log_filename = "acds.log";
        break;
      case MODE_DISCOVERY:
        log_filename = "discovery.log";
        break;
      default:
        log_filename = "ascii-chat.log";
        break;
      }

      // Build full log file path: log_dir + separator + log_filename
      char default_log_path[PLATFORM_MAX_PATH_LENGTH];
      safe_snprintf(default_log_path, sizeof(default_log_path), "%s%s%s", log_dir, PATH_SEPARATOR_STR, log_filename);

      // Validate and normalize the path
      char *normalized_default_log = NULL;
      if (path_validate_user_path(default_log_path, PATH_ROLE_LOG_FILE, &normalized_default_log) == ASCIICHAT_OK) {
        SAFE_SNPRINTF(opts.log_file, OPTIONS_BUFF_SIZE, "%s", normalized_default_log);
        SAFE_FREE(normalized_default_log);
      } else {
        // Validation failed - use the path as-is (validation may fail in debug builds)
        SAFE_SNPRINTF(opts.log_file, OPTIONS_BUFF_SIZE, "%s", default_log_path);
      }

      SAFE_FREE(log_dir);
    } else {
      // Fallback if get_log_dir() fails - use simple filename in CWD
      const char *log_filename;
      switch (detected_mode) {
      case MODE_SERVER:
        log_filename = "server.log";
        break;
      case MODE_CLIENT:
        log_filename = "client.log";
        break;
      case MODE_MIRROR:
        log_filename = "mirror.log";
        break;
      case MODE_DISCOVERY_SERVER:
        log_filename = "acds.log";
        break;
      case MODE_DISCOVERY:
        log_filename = "discovery.log";
        break;
      default:
        log_filename = "ascii-chat.log";
        break;
      }
      SAFE_SNPRINTF(opts.log_file, OPTIONS_BUFF_SIZE, "%s", log_filename);
    }
  } // Close: if (!binary_level_log_file_set)

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
  } else if (detected_mode == MODE_DISCOVERY_SERVER) {
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

  // Extract --config value from argv before loading config files
  const char *config_path_to_load = NULL;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
      config_path_to_load = argv[i + 1];
      break;
    }
  }

  // Load config files - now uses detected_mode directly for bitmask validation
  asciichat_error_t config_result = config_load_system_and_user(detected_mode, config_path_to_load, false, &opts);
  (void)config_result; // Continue with defaults and CLI parsing regardless of result

  // ========================================================================
  // STAGE 6: Parse Command-Line Arguments (Unified)
  // ========================================================================

  // SAVE binary-level parsed values before unified defaults overwrite them
  log_level_t saved_log_level = opts.log_level;
  char saved_log_file[OPTIONS_BUFF_SIZE];
  SAFE_STRNCPY(saved_log_file, opts.log_file, sizeof(saved_log_file));

  // Get unified config
  const options_config_t *config = options_preset_unified(NULL, NULL);
  if (!config) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to create options configuration");
  }

  int remaining_argc;
  char **remaining_argv;

  // Apply defaults from unified config
  asciichat_error_t defaults_result = options_config_set_defaults(config, &opts);
  if (defaults_result != ASCIICHAT_OK) {
    options_config_destroy(config);
    return defaults_result;
  }

  // Parse mode-specific arguments
  asciichat_error_t result =
      options_config_parse(config, mode_argc, mode_argv, &opts, &remaining_argc, &remaining_argv);
  if (result != ASCIICHAT_OK) {
    options_config_destroy(config);
    return result;
  }

  // Validate options
  result = validate_options_and_report(config, &opts);
  if (result != ASCIICHAT_OK) {
    options_config_destroy(config);
    return result;
  }

  // Check for unexpected remaining arguments
  if (remaining_argc > 0) {
    (void)fprintf(stderr, "Error: Unexpected arguments after options:\n");
    for (int i = 0; i < remaining_argc; i++) {
      (void)fprintf(stderr, "  %s\n", remaining_argv[i]);
    }
    options_config_destroy(config);
    return option_error_invalid();
  }

  // Mode-specific post-processing
  if (detected_mode == MODE_DISCOVERY_SERVER) {
    // Set default paths if not specified
    if (opts.discovery_database_path[0] == '\0') {
      char *config_dir = get_config_dir();
      if (!config_dir) {
        options_config_destroy(config);
        return SET_ERRNO(ERROR_CONFIG, "Failed to get config directory for database path");
      }
      snprintf(opts.discovery_database_path, sizeof(opts.discovery_database_path), "%sdiscovery.db", config_dir);
      SAFE_FREE(config_dir);
    }

    if (opts.discovery_key_path[0] == '\0') {
      char *config_dir = get_config_dir();
      if (!config_dir) {
        options_config_destroy(config);
        return SET_ERRNO(ERROR_CONFIG, "Failed to get config directory for identity key path");
      }
      snprintf(opts.discovery_key_path, sizeof(opts.discovery_key_path), "%sdiscovery_identity", config_dir);
      SAFE_FREE(config_dir);
    }
  }

  // Restore binary-level parsed values
  if (binary_level_log_level_set) {
    opts.log_level = saved_log_level;
  }
  if (binary_level_log_file_set) {
    SAFE_STRNCPY(opts.log_file, saved_log_file, sizeof(opts.log_file));
  }

  options_config_destroy(config);

  // ========================================================================
  // STAGE 7: Post-Processing & Validation
  // ========================================================================

  // Collect multiple --key flags for multi-key support (server/ACDS only)
  // This enables servers to load both SSH and GPG keys and select the right one
  // during handshake based on what the client expects
  if (detected_mode == MODE_SERVER || detected_mode == MODE_DISCOVERY_SERVER) {
    int num_keys = options_collect_identity_keys(&opts, argc, argv);
    if (num_keys < 0) {
      return SET_ERRNO(ERROR_INVALID_PARAM, "Failed to collect identity keys");
    }
    // num_keys == 0 is OK (no --key flags provided)
  }

  // After parsing command line options, update dimensions
  // First set any auto dimensions to terminal size, then apply full height logic
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
      return ERROR_INVALID_PARAM;
    }

    // Require --file or --url
    if (opts.media_file[0] == '\0' && opts.media_url[0] == '\0') {
      log_error("--seek requires --file or --url");
      return ERROR_INVALID_PARAM;
    }
  }

  // ========================================================================
  // STAGE 7: Publish to RCU
  // ========================================================================

  // Publish parsed options to RCU state (replaces options_state_populate_from_globals)
  // This makes the options visible to all threads via lock-free reads
  asciichat_error_t publish_result = options_state_set(&opts);
  if (publish_result != ASCIICHAT_OK) {
    log_error("Failed to publish parsed options to RCU state");
    return publish_result;
  }

  return ASCIICHAT_OK;
}
