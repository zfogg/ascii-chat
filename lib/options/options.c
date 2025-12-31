
/**
 * @file options.c
 * @ingroup options
 * @brief ⚙️ Command-line argument parser dispatcher and initialization
 *
 * Main entry point for option parsing. Dispatches to mode-specific parsers
 * (client, server, mirror) and handles common initialization and post-processing.
 */

#include "options/options.h"
#include "options/common.h"
#include "options/client.h"
#include "options/server.h"
#include "options/mirror.h"

#include "options/config.h"
#include "asciichat_errno.h"
#include "common.h"
#include "log/logging.h"
#include "platform/system.h"
#include "platform/terminal.h"
#include "platform/util.h"
#include "util/path.h"

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Main Option Parser Entry Point
// ============================================================================

asciichat_error_t options_init(int argc, char **argv, asciichat_mode_t mode) {
  // Track whether audio encoding flags were explicitly set
  bool encode_audio_explicitly_set = false;

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

  // Initialize global variables at runtime (Windows DLL workaround)
  // Static initializers don't work reliably in Windows DLLs created from OBJECT files
  // so we must initialize them explicitly here
  SAFE_SNPRINTF(opt_port, OPTIONS_BUFF_SIZE, "27224");

// Set default log file paths for Release builds
#ifdef NDEBUG
  char temp_dir[256];
  if (platform_get_temp_dir(temp_dir, sizeof(temp_dir))) {
    char default_log_path[PLATFORM_MAX_PATH_LENGTH];
    safe_snprintf(default_log_path, sizeof(default_log_path), "%s%sascii-chat.%s.log", temp_dir,
#if defined(_WIN32) || defined(WIN32)
                  "\\",
#else
                  "/",
#endif
                  mode == MODE_SERVER ? "server" : (mode == MODE_MIRROR ? "mirror" : "client"));

    char *normalized_default_log = NULL;
    if (path_validate_user_path(default_log_path, PATH_ROLE_LOG_FILE, &normalized_default_log) == ASCIICHAT_OK) {
      SAFE_SNPRINTF(opt_log_file, OPTIONS_BUFF_SIZE, "%s", normalized_default_log);
      SAFE_FREE(normalized_default_log);
    } else {
      SAFE_SNPRINTF(opt_log_file, OPTIONS_BUFF_SIZE, "%s", default_log_path);
    }
  } else {
    // Fallback if platform_get_temp_dir fails
    SAFE_SNPRINTF(opt_log_file, OPTIONS_BUFF_SIZE, "ascii-chat.log");
  }
#else
  // Debug builds: No default log file (empty string)
  opt_log_file[0] = '\0';
#endif

  opt_no_encrypt = 0;
  opt_encrypt_key[0] = '\0';
  opt_password[0] = '\0';
  opt_encrypt_keyfile[0] = '\0';
  opt_server_key[0] = '\0';
  opt_client_keys[0] = '\0';
  opt_palette_custom[0] = '\0';

  // Set different default addresses for client vs server (before config load)
  if (mode == MODE_CLIENT || mode == MODE_MIRROR) {
    // Client connects to localhost by default (IPv6-first with IPv4 fallback)
    SAFE_SNPRINTF(opt_address, OPTIONS_BUFF_SIZE, "localhost");
    opt_address6[0] = '\0'; // Client doesn't use opt_address6
  } else {
    // Server binds to 127.0.0.1 (IPv4) and ::1 (IPv6) by default
    SAFE_SNPRINTF(opt_address, OPTIONS_BUFF_SIZE, "127.0.0.1");
    SAFE_SNPRINTF(opt_address6, OPTIONS_BUFF_SIZE, "::1");
  }

  // Load configuration from TOML files (if they exist)
  // This loads system config first (${INSTALL_PREFIX}/etc/ascii-chat/config.toml),
  // then user config at default location. User config overrides system config.
  // This happens BEFORE CLI parsing so CLI arguments can override config values.
  // Config load errors are non-fatal for default location (logged as warnings)
  bool is_client_or_mirror = (mode == MODE_CLIENT || mode == MODE_MIRROR);
  asciichat_error_t config_result = config_load_system_and_user(is_client_or_mirror, NULL, false);
  (void)config_result; // Continue with defaults and CLI parsing regardless of result

  // Dispatch to mode-specific option parser
  asciichat_error_t result = ASCIICHAT_OK;
  switch (mode) {
  case MODE_SERVER:
    result = parse_server_options(argc, argv);
    break;
  case MODE_CLIENT:
    result = parse_client_options(argc, argv);
    break;
  case MODE_MIRROR:
    result = parse_mirror_options(argc, argv);
    break;
  default:
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid mode: %d", mode);
  }

  if (result != ASCIICHAT_OK) {
    return result;
  }

  // After parsing command line options, update dimensions
  // First set any auto dimensions to terminal size, then apply full height logic
  update_dimensions_to_terminal_size();
  update_dimensions_for_full_height();

  // Apply verbose level to log threshold
  // Each -V decreases the log level by 1 (showing more verbose output)
  // Minimum level is LOG_DEV (0)
  if (opt_verbose_level > 0) {
    log_level_t current_level = log_get_level();
    int new_level = (int)current_level - (int)opt_verbose_level;
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
    opt_test_pattern = true;
  }

  // Apply --no-compress interaction with audio encoding:
  // If --no-compress is set AND audio encoding was NOT explicitly set via flags,
  // disable audio encoding by default
  if (opt_no_compress && !encode_audio_explicitly_set) {
    opt_encode_audio = false;
    log_debug("--no-compress set without explicit audio encoding flag: disabling audio encoding");
  }

  return ASCIICHAT_OK;
}
