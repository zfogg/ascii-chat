
/**
 * @file options.c
 * @ingroup options
 * @brief ⚙️ Unified command-line argument parser with built-in mode detection
 *
 * Main entry point for option parsing. Detects mode from command-line arguments,
 * parses binary-level options, then dispatches to mode-specific parsers
 * (client, server, mirror, acds) with common initialization and post-processing.
 */

#include "options/options.h"
#include "options/rcu.h" // RCU-based thread-safe options
#include "options/common.h"
#include "options/client.h"
#include "options/server.h"
#include "options/mirror.h"
#include "options/discovery_server.h"
#include "options/discovery.h"
#include "options/validation.h"

#include "options/config.h"
#include "asciichat_errno.h"
#include "common.h"
#include "log/logging.h"
#include "platform/system.h"
#include "platform/terminal.h"
#include "platform/util.h"
#include "util/path.h"
#include "network/mdns/discovery.h"

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
  const char *const mode_names_check[] = {"server", "client", "mirror", "discovery-server", NULL};
  const asciichat_mode_t mode_values_check[] = {MODE_SERVER, MODE_CLIENT, MODE_MIRROR, MODE_DISCOVERY_SERVER};
  for (int i = 0; mode_names_check[i] != NULL; i++) {
    if (strcmp(argv[0], mode_names_check[i]) == 0) {
      *out_mode = mode_values_check[i];
      *out_mode_index = 0;
      return ASCIICHAT_OK;
    }
  }

  // Find the first non-option argument (potential mode or session string)
  int first_positional_idx = -1;
  for (int i = 1; i < argc; i++) {
    // Skip options and their arguments
    if (argv[i][0] == '-') {
      // Check if this option takes an argument
      if ((strcmp(argv[i], "--log-file") == 0 || strcmp(argv[i], "-L") == 0 || strcmp(argv[i], "--log-level") == 0 ||
           strcmp(argv[i], "--config") == 0) &&
          i + 1 < argc) {
        i++; // Skip the argument
      }
      continue;
    }

    // Found a positional argument
    first_positional_idx = i;
    break;
  }

  // If no positional argument found, use discovery mode (start new session)
  if (first_positional_idx == -1) {
    *out_mode = MODE_DISCOVERY;
    *out_mode_index = -1;
    return ASCIICHAT_OK;
  }

  const char *positional = argv[first_positional_idx];

  // Try to match against known modes
  const char *const mode_names[] = {"server", "client", "mirror", "discovery-server", NULL};
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

  asciichat_mode_t detected_mode = MODE_SERVER; // Default mode
  char detected_session_string[64] = {0};
  int mode_index = -1;

  // First, detect the mode from command-line arguments
  asciichat_error_t mode_detect_result =
      options_detect_mode(argc, argv, &detected_mode, detected_session_string, &mode_index);
  if (mode_detect_result != ASCIICHAT_OK) {
    return mode_detect_result;
  }

  opts.detected_mode = detected_mode;

  // Check for binary-level --help, --version, or --config-create
  // These only trigger if:
  // 1. No mode was detected (mode_index == -1), OR
  // 2. The option appears BEFORE the mode in argv
  bool show_help = false;
  bool show_version = false;
  bool create_config = false;
  const char *config_create_path = NULL;

  int search_limit = (mode_index == -1) ? argc : mode_index;
  for (int i = 1; i < search_limit; i++) {
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
        // Check if next argument is a path (not a flag)
        if (i + 1 < argc && argv[i + 1][0] != '-') {
          config_create_path = argv[i + 1];
        }
        break;
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

  // ========================================================================
  // STAGE 2: Build argv for mode-specific parsing
  // ========================================================================

  // If mode was found, build argv with only arguments after the mode
  // If mode_index == -1, we use all arguments (they become mode-specific args)
  int mode_argc = argc;
  char **mode_argv = (char **)argv;

  if (mode_index != -1) {
    // Mode found at position mode_index
    // Build new argv: [program_name, args_before_mode..., args_after_mode...]
    // This preserves binary-level options like --log-file that appear before mode

    // Special case: if mode_index == 0, argv[0] is the mode name (test compatibility)
    // Use "ascii-chat" as the binary name
    int args_before_mode = (mode_index == 0) ? 0 : (mode_index - 1);
    int args_after_mode = argc - mode_index - 1;
    mode_argc = 1 + args_before_mode + args_after_mode;

    if (mode_argc > 256) {
      return SET_ERRNO(ERROR_INVALID_PARAM, "Too many arguments: %d", mode_argc);
    }

    // Allocate mode_argc+1 to accommodate NULL terminator
    char **new_mode_argv = SAFE_MALLOC((size_t)(mode_argc + 1) * sizeof(char *), char **);
    if (!new_mode_argv) {
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate mode_argv");
    }

    // Copy: [program_name, args_before_mode..., args_after_mode...]
    if (mode_index == 0) {
      // Mode is at argv[0], use "ascii-chat" as program name
      new_mode_argv[0] = "ascii-chat";
    } else {
      new_mode_argv[0] = argv[0];
    }
    for (int i = 0; i < args_before_mode; i++) {
      new_mode_argv[1 + i] = argv[1 + i]; // argv[1] to argv[mode_index-1]
    }
    for (int i = 0; i < args_after_mode; i++) {
      new_mode_argv[1 + args_before_mode + i] = argv[mode_index + 1 + i];
    }
    new_mode_argv[mode_argc] = NULL;

    mode_argv = new_mode_argv;
  }

  // ========================================================================
  // STAGE 3: Set Mode-Specific Defaults
  // ========================================================================

  // Set default dimensions (fallback if terminal size detection fails)
  opts.width = OPT_WIDTH_DEFAULT;
  opts.height = OPT_HEIGHT_DEFAULT;
  opts.auto_width = true;
  opts.auto_height = true;

  // Set default port
  SAFE_SNPRINTF(opts.port, OPTIONS_BUFF_SIZE, "%s", OPT_PORT_DEFAULT);

  // Set other non-zero defaults (using macros from options.h)
  opts.webcam_flip = OPT_WEBCAM_FLIP_DEFAULT;
  opts.color_mode = OPT_COLOR_MODE_DEFAULT;
  opts.render_mode = OPT_RENDER_MODE_DEFAULT;
  opts.encode_audio = OPT_ENCODE_AUDIO_DEFAULT;
  opts.compression_level = OPT_COMPRESSION_LEVEL_DEFAULT;
  opts.max_clients = OPT_MAX_CLIENTS_DEFAULT;
  opts.microphone_index = OPT_MICROPHONE_INDEX_DEFAULT;
  opts.speakers_index = OPT_SPEAKERS_INDEX_DEFAULT;
  opts.reconnect_attempts = OPT_RECONNECT_ATTEMPTS_DEFAULT;

  // Set default log file paths based on build type
  // Release: $tmpdir/ascii-chat/MODE.log (e.g., /tmp/ascii-chat/server.log)
  // Debug: MODE.log in current working directory (e.g., ./server.log)
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
  // STAGE 4: Load Configuration Files
  // ========================================================================

  // Discovery mode is client-like (uses terminal display, webcam, etc.)
  bool is_client_or_mirror = (detected_mode == MODE_CLIENT || detected_mode == MODE_MIRROR ||
                              detected_mode == MODE_DISCOVERY);
  asciichat_error_t config_result = config_load_system_and_user(is_client_or_mirror, NULL, false, &opts);
  (void)config_result; // Continue with defaults and CLI parsing regardless of result

  // ========================================================================
  // STAGE 5: Parse Command-Line Arguments (Mode-Specific)
  // ========================================================================

  asciichat_error_t result = ASCIICHAT_OK;
  switch (detected_mode) {
  case MODE_SERVER:
    result = parse_server_options(mode_argc, mode_argv, &opts);
    break;
  case MODE_CLIENT:
    result = parse_client_options(mode_argc, mode_argv, &opts);
    break;
  case MODE_MIRROR:
    result = parse_mirror_options(mode_argc, mode_argv, &opts);
    break;
  case MODE_DISCOVERY_SERVER:
    result = parse_discovery_server_options(mode_argc, mode_argv, &opts);
    break;
  case MODE_DISCOVERY:
    result = parse_discovery_options(mode_argc, mode_argv, &opts);
    break;
  default:
    result = SET_ERRNO(ERROR_INVALID_PARAM, "Invalid detected mode: %d", detected_mode);
  }

  // Free the temporary mode_argv if we allocated it
  if (mode_argv != (char **)argv) {
    SAFE_FREE(mode_argv);
  }

  if (result != ASCIICHAT_OK) {
    return result;
  }

  // Set session string if it was detected (ACDS mode via session string pattern)
  if (detected_session_string[0] != '\0') {
    SAFE_STRNCPY(opts.session_string, detected_session_string, sizeof(opts.session_string));
  }

  // ========================================================================
  // STAGE 6: Post-Processing & Validation
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
