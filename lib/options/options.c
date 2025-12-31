
/**
 * @file options.c
 * @ingroup options
 * @brief ⚙️ Command-line argument parser with validation and configuration merging
 */

#include "platform/system.h"
#include "asciichat_errno.h"
#ifdef _WIN32
#include "platform/windows/getopt.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#else
#include <getopt.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "video/ascii.h"
#include "options/options.h"
#include "options/config.h"
#include "common.h"
#include "log/logging.h"
#include "video/webcam/webcam.h"
#include "audio/audio.h"
#include "util/ip.h"
#include "util/path.h"
#include "util/parsing.h"
#include "platform/system.h"
#include "platform/terminal.h"
#include "util/password.h"
#include "platform/util.h"
#include "version.h"
#include "crypto/crypto.h"
#include "options/levenshtein.h"
#include "options/validation.h"

// Find the most similar option name to an unknown option
// Returns the best matching option name, or NULL if no good match found
// Note: Uses LEVENSHTEIN_SUGGESTION_THRESHOLD from levenshtein.h
static const char *find_similar_option(const char *unknown_opt, const struct option *options) {
  if (!unknown_opt || !options) {
    return NULL;
  }

  const char *best_match = NULL;
  size_t best_distance = SIZE_MAX;

  for (int i = 0; options[i].name != NULL; i++) {
    size_t dist = levenshtein(unknown_opt, options[i].name);
    if (dist < best_distance) {
      best_distance = dist;
      best_match = options[i].name;
    }
  }

  // Only suggest if the distance is within our threshold
  if (best_distance <= LEVENSHTEIN_SUGGESTION_THRESHOLD) {
    return best_match;
  }

  return NULL;
}

// Safely parse string to integer with validation
int strtoint_safe(const char *str) {
  if (!str || *str == '\0') {
    return INT_MIN; // Error: NULL or empty string
  }

  int32_t result = 0;
  // Use safe parsing utility with full int32 range validation
  if (parse_int32(str, &result, INT_MIN, INT_MAX) != ASCIICHAT_OK) {
    return INT_MIN; // Error: invalid input or out of range
  }

  return (int)result;
}
// Forward declaration for get_required_argument (defined later in file)
static char *get_required_argument(const char *opt_value, char *buffer, size_t buffer_size, const char *option_name,
                                   asciichat_mode_t mode);

// Standard option parsing error return
static inline asciichat_error_t option_error_invalid(void) {
  return ERROR_INVALID_PARAM;
}

// Validate and retrieve required argument for an option
static char *validate_required_argument(const char *optarg, char *argbuf, size_t argbuf_size, const char *option_name,
                                        asciichat_mode_t mode) {
  char *value = get_required_argument(optarg, argbuf, argbuf_size, option_name, mode);
  if (!value) {
    (void)option_error_invalid();
  }
  return value;
}

// Validate a positive integer value (internal option parsing helper)
static bool validate_positive_int_opt(const char *value_str, int *out_value, const char *param_name) {
  if (!value_str || !out_value) {
    return false;
  }

  int val = strtoint_safe(value_str);
  if (val == INT_MIN || val <= 0) {
    (void)fprintf(stderr, "Invalid %s value '%s'. %s must be a positive integer.\n", param_name, value_str, param_name);
    return false;
  }

  *out_value = val;
  return true;
}

// Validate port number (1-65535) (internal option parsing helper)
static bool validate_port_opt(const char *value_str, uint16_t *out_port) {
  if (!value_str || !out_port) {
    return false;
  }

  // Use safe integer parsing with range validation
  if (parse_port(value_str, out_port) != ASCIICHAT_OK) {
    (void)fprintf(stderr, "Invalid port value '%s'. Port must be a number between 1 and 65535.\n", value_str);
    return false;
  }

  return true;
}

// Validate FPS value (1-144) (internal option parsing helper)
static bool validate_fps_opt(const char *value_str, int *out_fps) {
  if (!value_str || !out_fps) {
    return false;
  }

  int fps_val = strtoint_safe(value_str);
  if (fps_val == INT_MIN || fps_val < 1 || fps_val > 144) {
    (void)fprintf(stderr, "Invalid FPS value '%s'. FPS must be between 1 and 144.\n", value_str);
    return false;
  }

  *out_fps = fps_val;
  return true;
}

// Validate webcam index using the common device index validator
static bool validate_webcam_index(const char *value_str, unsigned short int *out_index) {
  if (!value_str || !out_index) {
    return false;
  }

  char error_msg[256];
  int parsed_index = validate_opt_device_index(value_str, error_msg, sizeof(error_msg));
  if (parsed_index == INT_MIN) {
    (void)fprintf(stderr, "Invalid webcam index: %s\n", error_msg);
    return false;
  }
  // Webcam index doesn't support -1 (default), must be >= 0
  if (parsed_index < 0) {
    (void)fprintf(stderr, "Invalid webcam index '%s'. Webcam index must be a non-negative integer.\n", value_str);
    return false;
  }

  *out_index = (unsigned short int)parsed_index;
  return true;
}

// Detect default SSH key path for the current user
static asciichat_error_t detect_default_ssh_key(char *key_path, size_t path_size) {
  // Use expand_path utility to resolve ~/.ssh/id_ed25519
  char *full_path = expand_path("~/.ssh/id_ed25519");
  if (!full_path) {
    return SET_ERRNO(ERROR_CONFIG, "Could not expand SSH key path");
  }

  // Check if the Ed25519 private key file exists
  struct stat st;
  bool found = (stat(full_path, &st) == 0 && S_ISREG(st.st_mode));

  if (found) {
    SAFE_SNPRINTF(key_path, path_size, "%s", full_path);
    log_debug("Found default SSH key: %s", full_path);
    SAFE_FREE(full_path);
    return ASCIICHAT_OK;
  }

  (void)fprintf(stderr, "No Ed25519 SSH key found at %s\n", full_path);
  SAFE_FREE(full_path);
  return SET_ERRNO(
      ERROR_CRYPTO_KEY,
      "Only Ed25519 keys are supported (modern, secure, fast). Generate a new key with: ssh-keygen -t ed25519");
}

ASCIICHAT_API unsigned short int opt_width = OPT_WIDTH_DEFAULT, opt_height = OPT_HEIGHT_DEFAULT;
ASCIICHAT_API bool auto_width = true, auto_height = true;

// Track if --port was explicitly set via command-line flag (for mutual exclusion validation)
static bool port_explicitly_set_via_flag = false;

ASCIICHAT_API char opt_address[OPTIONS_BUFF_SIZE] = "localhost", opt_address6[OPTIONS_BUFF_SIZE] = "",
                   opt_port[OPTIONS_BUFF_SIZE] = "27224";

// Server options
ASCIICHAT_API int opt_max_clients = 10; // Maximum concurrent clients (min 1, max 32)

// Network performance options
ASCIICHAT_API int opt_compression_level = 1; // zstd compression level (min 1, max 9, default 1)
ASCIICHAT_API bool opt_no_compress = false;  // Disable compression entirely (default: false)
ASCIICHAT_API bool opt_encode_audio = true;  // Enable Opus audio encoding (default: true)

// Client reconnection options
ASCIICHAT_API int opt_reconnect_attempts = -1; // Number of reconnection attempts (0=off, -1=unlimited/auto)

ASCIICHAT_API unsigned short int opt_webcam_index = 0;

ASCIICHAT_API bool opt_webcam_flip = true;

ASCIICHAT_API bool opt_test_pattern = false;   // Use test pattern instead of real webcam
ASCIICHAT_API bool opt_no_audio_mixer = false; // Disable audio mixer (debug only)

// Terminal color mode and capability options
ASCIICHAT_API terminal_color_mode_t opt_color_mode = COLOR_MODE_AUTO; // Auto-detect by default
ASCIICHAT_API render_mode_t opt_render_mode = RENDER_MODE_FOREGROUND; // Foreground by default
ASCIICHAT_API unsigned short int opt_show_capabilities = 0;           // Don't show capabilities by default
ASCIICHAT_API unsigned short int opt_force_utf8 = 0;                  // Don't force UTF-8 by default

ASCIICHAT_API unsigned short int opt_audio_enabled = 0;
ASCIICHAT_API int opt_microphone_index = -1; // -1 means use default microphone
ASCIICHAT_API int opt_speakers_index = -1;   // -1 means use default speakers
ASCIICHAT_API unsigned short int opt_audio_analysis_enabled = 0;
ASCIICHAT_API unsigned short int opt_audio_no_playback = 0; // Disable speaker playback for debugging

// Allow stretching/shrinking without preserving aspect ratio when set via -s/--stretch
ASCIICHAT_API unsigned short int opt_stretch = 0;

// Disable console logging when set via -q/--quiet (logs only to file)
ASCIICHAT_API unsigned short int opt_quiet = 0;

// Verbose logging level - each -V increases verbosity (decreases log level threshold)
ASCIICHAT_API unsigned short int opt_verbose_level = 0;

// Enable snapshot mode when set via --snapshot (client only - capture one frame and exit)
ASCIICHAT_API unsigned short int opt_snapshot_mode = 0;

// Snapshot delay in seconds (float) - default 3.0 for webcam warmup
#if defined(__APPLE__)
// their macbook webcams shows pure black first then fade up into a real color image over a few seconds
#define SNAPSHOT_DELAY_DEFAULT 4.0f
#else
#define SNAPSHOT_DELAY_DEFAULT 3.0f
#endif
ASCIICHAT_API float opt_snapshot_delay = SNAPSHOT_DELAY_DEFAULT;

// Strip ANSI escape sequences from output
ASCIICHAT_API unsigned short int opt_strip_ansi = 0;

// Log file path for file logging (empty string means no file logging)
ASCIICHAT_API char opt_log_file[OPTIONS_BUFF_SIZE] = "";

// Log level for console and file output
#ifdef NDEBUG
ASCIICHAT_API log_level_t opt_log_level = LOG_INFO;
#else
ASCIICHAT_API log_level_t opt_log_level = LOG_DEBUG;
#endif

// Encryption options
ASCIICHAT_API unsigned short int opt_encrypt_enabled = 0;       // Enable AES encryption via --encrypt
ASCIICHAT_API char opt_encrypt_key[OPTIONS_BUFF_SIZE] = "";     // SSH/GPG key file from --key (file-based only)
ASCIICHAT_API char opt_password[OPTIONS_BUFF_SIZE] = "";        // Password string from --password
ASCIICHAT_API char opt_encrypt_keyfile[OPTIONS_BUFF_SIZE] = ""; // Key file path from --keyfile

// New crypto options (Phase 2)
ASCIICHAT_API unsigned short int opt_no_encrypt = 0;        // Disable encryption (opt-out)
ASCIICHAT_API char opt_server_key[OPTIONS_BUFF_SIZE] = "";  // Expected server public key (client only)
ASCIICHAT_API char opt_client_keys[OPTIONS_BUFF_SIZE] = ""; // Allowed client keys (server only)

// Palette options
ASCIICHAT_API palette_type_t opt_palette_type = PALETTE_STANDARD; // Default to standard palette
ASCIICHAT_API char opt_palette_custom[256] = "";                  // Custom palette characters
ASCIICHAT_API bool opt_palette_custom_set = false;                // True if custom palette was set

// Default weights; must add up to 1.0
const float weight_red = 0.2989f;
const float weight_green = 0.5866f;
const float weight_blue = 0.1145f;

/*
Analysis of Your Current Palette
Your palette " ...',;:clodxkO0KXNWM" represents luminance from dark to light:
    (spaces) = darkest/black areas
    ...,' = very dark details
    ;:cl = mid-dark tones
    odxk = medium tones
    O0KX = bright areas
    NWM = brightest/white areas
*/
// ASCII palette for image-to-text conversion
char ascii_palette[] = "   ...',;:clodxkO0KXNWM";

unsigned short int RED[ASCII_LUMINANCE_LEVELS], GREEN[ASCII_LUMINANCE_LEVELS], BLUE[ASCII_LUMINANCE_LEVELS],
    GRAY[ASCII_LUMINANCE_LEVELS];

// Client-only options
// Note: -a/--address and -H/--host are replaced by positional argument [address][:port]
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
                                         {"quiet", no_argument, NULL, 'q'},
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

// Mirror mode options (local webcam viewing without network)
static struct option mirror_options[] = {{"width", required_argument, NULL, 'x'},
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
                                         {"stretch", no_argument, NULL, 's'},
                                         {"quiet", no_argument, NULL, 'q'},
                                         {"snapshot", no_argument, NULL, 'S'},
                                         {"snapshot-delay", required_argument, NULL, 'D'},
                                         {"strip-ansi", no_argument, NULL, 1017},
                                         {"list-webcams", no_argument, NULL, 1013},
                                         {"help", optional_argument, NULL, 'h'},
                                         {0, 0, 0, 0}};

// Server-only options
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

// Terminal size detection functions moved to terminal_detect.c

void update_dimensions_for_full_height(void) {
  unsigned short int term_width, term_height;

  // Note: Logging is not available during options_init, so we can't use log_debug here
  asciichat_error_t result = get_terminal_size(&term_width, &term_height);
  if (result == ASCIICHAT_OK) {
    // If both dimensions are auto, set height to terminal height and let
    // aspect_ratio calculate width
    if (auto_height && auto_width) {
      opt_height = term_height;
      opt_width = term_width; // Also set width when both are auto
    }
    // If only height is auto, use full terminal height
    else if (auto_height) {
      opt_height = term_height;
    }
    // If only width is auto, use full terminal width
    else if (auto_width) {
      opt_width = term_width;
    }
  } else {
    // Terminal size detection failed, but we can still continue with defaults
  }
}

void update_dimensions_to_terminal_size(void) {
  unsigned short int term_width, term_height;
  // Get current terminal size (get_terminal_size already handles ioctl first, then $COLUMNS/$LINES fallback)
  asciichat_error_t terminal_result = get_terminal_size(&term_width, &term_height);
  if (terminal_result == ASCIICHAT_OK) {
    if (auto_width) {
      opt_width = term_width;
    }
    if (auto_height) {
      opt_height = term_height;
    }
    log_debug("After update_dimensions_to_terminal_size: opt_width=%d, opt_height=%d", opt_width, opt_height);
  } else {
    log_debug("Failed to get terminal size in update_dimensions_to_terminal_size");
  }
}

// ============================================================================
// Helper Functions (internal to options.c)
// ============================================================================

// Helper function to strip equals sign from optarg if present
static char *strip_equals_prefix(const char *opt_value, char *buffer, size_t buffer_size) {
  if (!opt_value)
    return NULL;

  SAFE_SNPRINTF(buffer, buffer_size, "%s", opt_value);
  char *value_str = buffer;
  if (value_str[0] == '=') {
    value_str++; // Skip the equals sign
  }

  // Return NULL for empty strings (treat as missing argument)
  if (strlen(value_str) == 0) {
    return NULL;
  }

  return value_str;
}

// Helper function to handle required arguments with consistent error messages
// Returns NULL on error (caller should check and return error code)
static char *get_required_argument(const char *opt_value, char *buffer, size_t buffer_size, const char *option_name,
                                   asciichat_mode_t mode) {
  // Check if opt_value is NULL or empty
  if (!opt_value || strlen(opt_value) == 0) {
    goto error;
  }

  // Check if getopt_long returned the option name itself as the argument
  // This happens when a long option requiring an argument is at the end of argv
  if (opt_value && option_name && strcmp(opt_value, option_name) == 0) {
    goto error;
  }

  // Process the argument normally
  char *value_str = strip_equals_prefix(opt_value, buffer, buffer_size);
  if (!value_str) {
    goto error;
  }

  return value_str;

error:
  (void)fprintf(stderr, "%s: option '--%s' requires an argument\n",
                mode == MODE_SERVER ? "server" : (mode == MODE_MIRROR ? "mirror" : "client"), option_name);
  (void)fflush(stderr);
  return NULL; // Signal error to caller
}

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

  // Use different option sets for server, client, and mirror modes
  const char *optstring;
  struct option *options;

  switch (mode) {
  case MODE_SERVER:
    optstring = ":a:p:P:C:EK:F:h"; // Leading ':' for error reporting
    options = server_options;
    break;
  case MODE_CLIENT:
    optstring = ":a:H:p:x:y:c:fM:P:C:AsqSD:EK:F:h"; // Leading ':' for error reporting
    options = client_options;
    break;
  case MODE_MIRROR:
    optstring = ":x:y:c:fM:P:C:sqSD:h"; // Leading ':' for error reporting
    options = mirror_options;
    break;
  default:
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid mode: %d", mode);
  }

  // Pre-pass: Check for --help first (it has priority over everything)
  // This ensures help is shown without triggering password prompts or other side effects
  // Note: --version is handled at binary level in src/main.c, not here
  for (int i = 1; i < argc; i++) {
    if (argv[i] == NULL) {
      break; // Stop if we hit a NULL element (safety check for tests with malformed argv)
    }
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      usage(stdout, mode);
      (void)fflush(stdout);
      _exit(0);
    }
  }

  int longindex = 0; // Move outside loop so ':' case can access it
  while (1) {
    longindex = 0;
    int c = getopt_long(argc, argv, optstring, options, &longindex);
    if (c == -1)
      break;

    char argbuf[1024];
    switch (c) {
    case 0:
      // Handle long-only options that return 0
      // if (options[longindex].name) {
      //}
      break;

    case 'p': {
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "port", mode);
      if (!value_str)
        return option_error_invalid();
      uint16_t port_num;
      if (!validate_port_opt(value_str, &port_num))
        return option_error_invalid();
      SAFE_SNPRINTF(opt_port, OPTIONS_BUFF_SIZE, "%s", value_str);
      port_explicitly_set_via_flag = true; // Track that --port was used
      break;
    }

    case 'x': {
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "width", mode);
      if (!value_str)
        return option_error_invalid();
      int width_val;
      if (!validate_positive_int_opt(value_str, &width_val, "width"))
        return option_error_invalid();
      opt_width = (unsigned short int)width_val;
      auto_width = false; // Mark as manually set
      break;
    }

    case 'y': {
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "height", mode);
      if (!value_str)
        return option_error_invalid();
      int height_val;
      if (!validate_positive_int_opt(value_str, &height_val, "height"))
        return option_error_invalid();
      opt_height = (unsigned short int)height_val;
      auto_height = false; // Mark as manually set
      break;
    }

    case 'c': {
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "webcam-index", mode);
      if (!value_str)
        return option_error_invalid();
      unsigned short int index_val;
      if (!validate_webcam_index(value_str, &index_val))
        return option_error_invalid();
      opt_webcam_index = index_val;
      break;
    }

    case 'f': {
      // Webcam flip is now a binary flag - if present, toggle flip state
      opt_webcam_flip = !opt_webcam_flip;
      break;
    }

    case 1000: { // --color-mode
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "color-mode", mode);
      if (!value_str)
        return option_error_invalid();
      if (strcmp(value_str, "auto") == 0) {
        opt_color_mode = COLOR_MODE_AUTO;
      } else if (strcmp(value_str, "none") == 0) {
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

    case 1003: { // --fps (client only - sets client's desired frame rate)
      if (mode == MODE_SERVER) {
        (void)fprintf(stderr, "Error: --fps is a client-only option.\n");
        return option_error_invalid();
      }
      extern int g_max_fps; // From common.c
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "fps", mode);
      if (!value_str)
        return option_error_invalid();
      int fps_val;
      if (!validate_fps_opt(value_str, &fps_val))
        return option_error_invalid();
      g_max_fps = fps_val;
      break;
    }

    case 1004: { // --test-pattern (client only - use test pattern instead of webcam)
      if (mode == MODE_SERVER) {
        (void)fprintf(stderr, "Error: --test-pattern is a client-only option.\n");
        return option_error_invalid();
      }
      opt_test_pattern = true;
      log_info("Using test pattern mode - webcam will not be opened");
      break;
    }

    case 1026: { // --no-audio-mixer (server only - disable audio mixer for debugging)
      if (mode == MODE_CLIENT || mode == MODE_MIRROR) {
        (void)fprintf(stderr, "Error: --no-audio-mixer is a server-only option.\n");
        return option_error_invalid();
      }
      opt_no_audio_mixer = true;
      log_info("Audio mixer disabled - will send silence instead of mixing");
      break;
    }

    case 1013: { // --list-webcams (client only - list available webcam devices and exit)
      if (mode == MODE_SERVER) {
        (void)fprintf(stderr, "Error: --list-webcams is a client-only option.\n");
        return option_error_invalid();
      }
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

    case 1014: { // --list-microphones (client only - list available audio input devices and exit)
      if (mode == MODE_SERVER) {
        (void)fprintf(stderr, "Error: --list-microphones is a client-only option.\n");
        return option_error_invalid();
      }
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

    case 1015: { // --list-speakers (client only - list available audio output devices and exit)
      if (mode == MODE_SERVER) {
        (void)fprintf(stderr, "Error: --list-speakers is a client-only option.\n");
        return option_error_invalid();
      }
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
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "render-mode", mode);
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
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "palette", mode);
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
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "palette-chars", mode);
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
      opt_palette_type = PALETTE_CUSTOM; // Automatically set to custom
      break;
    }

    case 's':
      opt_stretch = 1;
      break;

    case 'A':
      opt_audio_enabled = 1;
      break;

    case 1028: { // --microphone-index
      char error_msg[256];
      int index = validate_opt_device_index(optarg, error_msg, sizeof(error_msg));
      if (index == INT_MIN) {
        safe_fprintf(stderr, "Error: Invalid microphone index: %s\n", error_msg);
        return -1;
      }
      opt_microphone_index = index;
      break;
    }

    case 1029: { // --speakers-index
      char error_msg[256];
      int index = validate_opt_device_index(optarg, error_msg, sizeof(error_msg));
      if (index == INT_MIN) {
        safe_fprintf(stderr, "Error: Invalid speakers index: %s\n", error_msg);
        return -1;
      }
      opt_speakers_index = index;
      break;
    }

    case 1025: // --audio-analysis
      opt_audio_analysis_enabled = 1;
      break;

    case 1027: // --no-audio-playback
      opt_audio_no_playback = 1;
      break;

    case 'q':
      opt_quiet = 1;
      break;

    case 'V':
      opt_verbose_level++;
      break;

    case 'S':
      opt_snapshot_mode = 1;
      break;

    case 1017: // --strip-ansi (client only - remove ANSI escape sequences from output)
      opt_strip_ansi = 1;
      break;

    case 'D': {
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "snapshot-delay", mode);
      if (!value_str)
        return option_error_invalid();
      char *endptr;
      opt_snapshot_delay = strtof(value_str, &endptr);
      if (*endptr != '\0' || value_str == endptr) {
        (void)fprintf(stderr, "Invalid snapshot delay value '%s'. Snapshot delay must be a number.\n", value_str);
        (void)fflush(stderr);
        return option_error_invalid();
      }
      if (opt_snapshot_delay < 0.0f) {
        (void)fprintf(stderr, "Snapshot delay must be non-negative (got %.2f)\n", (double)opt_snapshot_delay);
        (void)fflush(stderr);
        return option_error_invalid();
      }
      break;
    }

    case 'L': {
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "log-file", mode);
      if (!value_str)
        return option_error_invalid();
      char *normalized_log = NULL;
      asciichat_error_t log_result = path_validate_user_path(value_str, PATH_ROLE_LOG_FILE, &normalized_log);
      if (log_result != ASCIICHAT_OK) {
        SAFE_FREE(normalized_log);
        fprintf(stderr, "Invalid log file path: %s\n", value_str);
        return option_error_invalid();
      }
      SAFE_SNPRINTF(opt_log_file, OPTIONS_BUFF_SIZE, "%s", normalized_log);
      SAFE_FREE(normalized_log);
      break;
    }

    case 1018: { // --log-level
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "log-level", mode);
      if (!value_str)
        return option_error_invalid();
      int log_level = validate_opt_log_level(value_str, NULL, 0);
      if (log_level < 0) {
        (void)fprintf(stderr, "Invalid log level '%s'. Valid levels: dev, debug, info, warn, error, fatal\n",
                      value_str);
        return option_error_invalid();
      }
      opt_log_level = (log_level_t)log_level;
      break;
    }

    case 1019: { // --compression-level
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "compression-level", mode);
      if (!value_str)
        return option_error_invalid();

      char error_msg[256];
      int level = validate_opt_compression_level(value_str, error_msg, sizeof(error_msg));
      if (level < 0) {
        (void)fprintf(stderr, "%s\n", error_msg);
        (void)fprintf(stderr, "  Level 1: Fastest compression (best for real-time)\n");
        (void)fprintf(stderr, "  Level 3: Balanced speed/ratio\n");
        (void)fprintf(stderr, "  Level 9: Best compression (for limited bandwidth)\n");
        return option_error_invalid();
      }

      opt_compression_level = (int)level;
      break;
    }

    case 1020: { // --reconnect (client only)
      if (mode == MODE_SERVER) {
        (void)fprintf(stderr, "Warning: --reconnect is ignored in server mode\n");
        break;
      }

      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "reconnect", mode);
      if (!value_str)
        return option_error_invalid();

      char error_msg[256];
      int attempts = validate_opt_reconnect(value_str, error_msg, sizeof(error_msg));
      if (attempts == INT_MIN) {
        (void)fprintf(stderr, "%s\n", error_msg);
        (void)fprintf(stderr, "  Use 'off' for no reconnection\n");
        (void)fprintf(stderr, "  Use 'auto' for unlimited reconnection\n");
        return option_error_invalid();
      }
      opt_reconnect_attempts = attempts;
      break;
    }

    case 1021: { // --max-clients (server only)
      if (mode == MODE_CLIENT || mode == MODE_MIRROR) {
        (void)fprintf(stderr, "Warning: --max-clients is ignored in client mode\n");
        break;
      }

      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "max-clients", mode);
      if (!value_str)
        return option_error_invalid();

      char error_msg[256];
      int max_clients = validate_opt_max_clients(value_str, error_msg, sizeof(error_msg));
      if (max_clients < 0) {
        (void)fprintf(stderr, "%s\n", error_msg);
        return option_error_invalid();
      }

      opt_max_clients = max_clients;
      break;
    }

    case 1022: // --no-compress
      opt_no_compress = true;
      break;

    case 1023: // --encode-audio
      opt_encode_audio = true;
      encode_audio_explicitly_set = true;
      break;

    case 1024: // --no-encode-audio
      opt_encode_audio = false;
      encode_audio_explicitly_set = true;
      break;

    case 'E':
      opt_encrypt_enabled = 1;
      break;

    case 'K': {
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "key", mode);
      if (!value_str)
        return option_error_invalid();

      // --key is for file-based authentication only (SSH keys, GPG keys, GitHub/GitLab)
      // For password-based encryption, use --password instead

      // Check if it's "ssh" or "ssh:" to auto-detect SSH key
      if (strcmp(value_str, "ssh") == 0 || strcmp(value_str, "ssh:") == 0) {
        char default_key[OPTIONS_BUFF_SIZE];
        if (detect_default_ssh_key(default_key, sizeof(default_key)) == ASCIICHAT_OK) {
          SAFE_SNPRINTF(opt_encrypt_key, OPTIONS_BUFF_SIZE, "%s", default_key);
          opt_encrypt_enabled = 1;
        } else {
          (void)fprintf(stderr, "No Ed25519 SSH key found for auto-detection\n");
          (void)fprintf(stderr, "Please specify a key with --key /path/to/key\n");
          (void)fprintf(stderr, "Or generate a new key with: ssh-keygen -t ed25519\n");
          return option_error_invalid();
        }
      }
      // Otherwise, treat as GPG key (gpg:keyid), GitHub key (github:username),
      // GitLab key (gitlab:username), or file path - will be validated later
      else {
        SAFE_SNPRINTF(opt_encrypt_key, OPTIONS_BUFF_SIZE, "%s", value_str);
        opt_encrypt_enabled = 1;
      }
      break;
    }

    case 'F': {
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "keyfile", mode);
      if (!value_str)
        return option_error_invalid();
      SAFE_SNPRINTF(opt_encrypt_keyfile, OPTIONS_BUFF_SIZE, "%s", value_str);
      opt_encrypt_enabled = 1; // Auto-enable encryption when keyfile provided
      break;
    }

    case 1005: { // --no-encrypt (disable encryption)
      opt_no_encrypt = 1;
      opt_encrypt_enabled = 0; // Disable encryption
      break;
    }

    case 1006: { // --server-key (client only)
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "server-key", mode);
      if (!value_str)
        return option_error_invalid();
      SAFE_SNPRINTF(opt_server_key, OPTIONS_BUFF_SIZE, "%s", value_str);
      break;
    }

    case 1008: { // --client-keys (server only)
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "client-keys", mode);
      if (!value_str)
        return option_error_invalid();
      SAFE_SNPRINTF(opt_client_keys, OPTIONS_BUFF_SIZE, "%s", value_str);
      break;
    }

    case 1009: { // --password (password-based encryption)
      char *value_str = NULL;

      // Check if password was provided as argument
      if (optarg && strlen(optarg) > 0) {
        // Password provided with --password=value format
        value_str = strip_equals_prefix(optarg, argbuf, sizeof(argbuf));
      }
      // Check if next argument exists and doesn't start with '-' (space-separated format)
      else if (optind < argc && argv[optind] && argv[optind][0] != '-') {
        // Password provided with --password value format (space-separated)
        SAFE_SNPRINTF(argbuf, sizeof(argbuf), "%s", argv[optind]);
        value_str = argbuf;
        optind++; // Consume this argument
      }

      // If no password argument provided, prompt the user
      if (!value_str) {
        char prompted_password[OPTIONS_BUFF_SIZE];
        if (prompt_password("Enter password for encryption:", prompted_password, sizeof(prompted_password)) != 0) {
          (void)fprintf(stderr, "Error: Failed to read password\n");
          return option_error_invalid();
        }
        value_str = prompted_password;
        // Copy to argbuf so it persists beyond this scope
        SAFE_SNPRINTF(argbuf, sizeof(argbuf), "%s", prompted_password);
        value_str = argbuf;
        // Clear the prompted_password buffer for security
        memset(prompted_password, 0, sizeof(prompted_password));
      }

      // Validate password using the common validator
      char error_msg[256];
      if (validate_opt_password(value_str, error_msg, sizeof(error_msg)) < 0) {
        (void)fprintf(stderr, "Error: %s\n", error_msg);
        return option_error_invalid();
      }

      SAFE_SNPRINTF(opt_password, OPTIONS_BUFF_SIZE, "%s", value_str);
      opt_encrypt_enabled = 1; // Auto-enable encryption when password provided

      // Clear the temporary buffer for security
      memset(argbuf, 0, sizeof(argbuf));
      break;
    }

    case ':':
      // Missing argument for option
      if (optopt == 0 || optopt > 127) {
        // Long option - check if it was abbreviated first
        if (optind > 0 && optind <= argc && argv && argv[optind - 1]) {
          const char *user_input = argv[optind - 1];
          if (user_input && strlen(user_input) > 2 && strncmp(user_input, "--", 2) == 0) {
            // Extract the option name from user input (skip "--")
            const char *user_opt = user_input + 2;
            // Handle --option=value format
            const char *eq_pos = strchr(user_opt, '=');
            size_t user_opt_len = eq_pos ? (size_t)(eq_pos - user_opt) : strlen(user_opt);

            // Find which option was matched by searching for a prefix match
            const char *matched_option = NULL;
            for (int i = 0; options[i].name != NULL; i++) {
              const char *opt_name = options[i].name;
              size_t opt_len = strlen(opt_name);
              // Check if user's input is a prefix of this option name
              if (opt_len > user_opt_len && strncmp(user_opt, opt_name, user_opt_len) == 0) {
                matched_option = opt_name;
                break;
              }
            }

            // If we found a match and it's not exact, treat as unknown option
            if (matched_option && strlen(matched_option) != user_opt_len) {
              char abbreviated_opt[256];
              safe_snprintf(abbreviated_opt, sizeof(abbreviated_opt), "%.*s", (int)user_opt_len, user_opt);
              safe_fprintf(stderr, "Unknown option '--%s'\n", abbreviated_opt);
              usage(stderr, mode);
              return option_error_invalid();
            }
          }
        }

        // If we get here, it's a valid option name but missing argument
        const char *opt_name = "unknown";
        if (optind > 0 && optind <= argc && argv && argv[optind - 1]) {
          const char *arg = argv[optind - 1];
          if (arg && strlen(arg) > 2 && strncmp(arg, "--", 2) == 0) {
            // Simple approach: just skip the "--" prefix
            opt_name = arg + 2;
            // If there's an equals sign, we need to handle it safely
            const char *eq = strchr(opt_name, '=');
            if (eq && eq > opt_name) {
              // Use a static buffer to avoid stack issues
              static char safe_buf[256];
              size_t len = (size_t)(eq - opt_name);
              if (len > 0 && len < sizeof(safe_buf) - 1) {
                SAFE_STRNCPY(safe_buf, opt_name, sizeof(safe_buf));
                safe_buf[len] = '\0';
                opt_name = safe_buf;
              }
            }
          }
        }
        (void)fprintf(stderr, "%s: option '--%s' requires an argument\n",
                      mode == MODE_SERVER ? "server" : (mode == MODE_MIRROR ? "mirror" : "client"), opt_name);
      } else {
        // Short option - try to find the corresponding long option name
        const char *long_name = NULL;
        for (int i = 0; options[i].name != NULL; i++) {
          if (options[i].val == optopt) {
            long_name = options[i].name;
            break;
          }
        }
        if (long_name) {
          (void)fprintf(stderr, "%s: option '--%s' requires an argument\n",
                        mode == MODE_SERVER ? "server" : (mode == MODE_MIRROR ? "mirror" : "client"), long_name);
        } else {
          (void)fprintf(stderr, "%s: option '-%c' requires an argument\n",
                        mode == MODE_SERVER ? "server" : (mode == MODE_MIRROR ? "mirror" : "client"), optopt);
        }
      }
      return option_error_invalid();

    case '?': {
      // Handle unknown options - extract the actual option name from argv
      // Use a buffer that persists for the suggestion lookup
      char unknown_opt_buf[256] = {0};

      if (optopt == 0 || optopt > 127) {
        // Long option - extract from argv
        const char *user_input = NULL;
        if (optind > 0 && optind <= argc && argv && argv[optind - 1]) {
          user_input = argv[optind - 1];
        }

        // Extract option name for long options
        const char *option_name = "[unknown option name] (this is invalid and an error. this should never be printed)";
        if (user_input && strlen(user_input) > 2 && strncmp(user_input, "--", 2) == 0) {
          const char *user_opt = user_input + 2;
          // Handle --option=value format
          const char *eq_pos = strchr(user_opt, '=');
          if (eq_pos) {
            size_t user_opt_len = (size_t)(eq_pos - user_opt);
            if (user_opt_len > 0 && user_opt_len < sizeof(unknown_opt_buf)) {
              SAFE_STRNCPY(unknown_opt_buf, user_opt, sizeof(unknown_opt_buf));
              unknown_opt_buf[user_opt_len] = '\0';
              option_name = unknown_opt_buf;
            } else {
              option_name = user_opt;
            }
          } else {
            option_name = user_opt;
            SAFE_STRNCPY(unknown_opt_buf, user_opt, sizeof(unknown_opt_buf));
          }
        } else if (user_input) {
          option_name = user_input;
        }
        safe_fprintf(stderr, "Unknown option '--%s'\n", option_name);

        // Try to find a similar option and suggest it
        const char *suggestion = find_similar_option(unknown_opt_buf[0] ? unknown_opt_buf : option_name, options);
        if (suggestion) {
          safe_fprintf(stderr, "Did you mean '--%s'?\n", suggestion);
        }
      } else {
        // Short option - no suggestions for single character options
        safe_fprintf(stderr, "Unknown option '-%c'\n", optopt);
      }
#ifndef NDEBUG
      // Only print full usage in debug builds - release builds just show the error
      usage(stderr, mode);
#endif
      return option_error_invalid();
    }

    case 'h':
      usage(stdout, mode);
      (void)fflush(stdout);
      _exit(0);

    default:
      abort();
    }
  }

  // Server mode: Parse positional arguments for bind addresses (0-2 addresses)
  // - 0 args: bind to defaults (127.0.0.1 and ::1)
  // - 1 arg: bind to this address (IPv4 or IPv6)
  // - 2 args: bind to both (must be 1 IPv4 and 1 IPv6)
  if (mode == MODE_SERVER) {
    int num_addresses = 0;
    bool has_ipv4 = false;
    bool has_ipv6 = false;

    // Parse up to 2 positional arguments
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

      optind++; // Consume this argument
    }
  }

  // Client mode: Parse positional argument for address[:port]
  // This replaces the old -a/--address and -H/--host options
  // Note: Mirror mode does NOT accept positional arguments
  if (mode == MODE_CLIENT) {
    // Check if there's a remaining positional argument after options
    if (optind < argc && argv[optind] != NULL) {
      const char *addr_arg = argv[optind];

      // Skip if it looks like an option (starts with -)
      if (addr_arg[0] != '-') {
        char parsed_address[OPTIONS_BUFF_SIZE];
        uint16_t parsed_port = 0;

        // Parse address with optional port
        if (parse_address_with_optional_port(addr_arg, parsed_address, sizeof(parsed_address), &parsed_port, 27224) !=
            0) {
          (void)fprintf(stderr, "Error: Invalid address format '%s'.\n", addr_arg);
          (void)fprintf(stderr, "Supported formats:\n");
          (void)fprintf(stderr, "  hostname          (e.g., localhost, example.com)\n");
          (void)fprintf(stderr, "  hostname:port     (e.g., example.com:8080)\n");
          (void)fprintf(stderr, "  IPv4              (e.g., 192.168.1.1)\n");
          (void)fprintf(stderr, "  IPv4:port         (e.g., 192.168.1.1:8080)\n");
          (void)fprintf(stderr, "  IPv6              (e.g., ::1, 2001:db8::1)\n");
          (void)fprintf(stderr, "  [IPv6]:port       (e.g., [::1]:8080)\n");
          return option_error_invalid();
        }

        // Store parsed address
        // For hostname, try to resolve it to IP
        if (is_valid_ipv4(parsed_address) || is_valid_ipv6(parsed_address)) {
          SAFE_SNPRINTF(opt_address, OPTIONS_BUFF_SIZE, "%s", parsed_address);
        } else {
          // Try to resolve hostname to IP address
          char resolved_ip[OPTIONS_BUFF_SIZE];
          if (platform_resolve_hostname_to_ipv4(parsed_address, resolved_ip, sizeof(resolved_ip)) == 0) {
            SAFE_SNPRINTF(opt_address, OPTIONS_BUFF_SIZE, "%s", resolved_ip);
          } else {
            // DNS resolution failed - print error and exit
            (void)fprintf(stderr, "Error: Failed to resolve hostname '%s' to IP address.\n", parsed_address);
            (void)fprintf(stderr, "Check that the hostname is correct and DNS is working.\n");
            return option_error_invalid();
          }
        }

        // Check for mutual exclusion: cannot specify port in both positional arg and --port flag
        if (parsed_port != 27224 && port_explicitly_set_via_flag) {
          (void)fprintf(stderr, "Error: Cannot specify port in both the positional argument and --port flag.\n");
          return option_error_invalid();
        }

        // Store parsed port (only if not already set via --port flag)
        SAFE_SNPRINTF(opt_port, OPTIONS_BUFF_SIZE, "%u", parsed_port);

        optind++; // Consume this argument
      }
    }
  }

  // Validate no extra positional arguments (mirror mode only - client/server handle their own)
  if (mode == MODE_MIRROR && optind < argc) {
    (void)fprintf(stderr, "Unexpected argument: '%s'\n", argv[optind]);
    return option_error_invalid();
  }

  // Validate no extra positional arguments for client/server beyond what they parsed
  if ((mode == MODE_CLIENT || mode == MODE_SERVER) && optind < argc) {
    (void)fprintf(stderr, "Unexpected argument: '%s'\n", argv[optind]);
    return option_error_invalid();
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

#define USAGE_INDENT "        "

void usage_client(FILE *desc /* stdout|stderr*/) {
  (void)fprintf(desc, "ascii-chat - client options\n");
  (void)fprintf(desc, ASCII_CHAT_DESCRIPTION "\n\n");
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
  (void)fprintf(desc, USAGE_INDENT "-q --quiet                   " USAGE_INDENT
                                   "disable console logging (log only to file) (default: [unset])\n");
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

void usage_mirror(FILE *desc /* stdout|stderr*/) {
  (void)fprintf(desc, "ascii-chat - mirror options\n");
  (void)fprintf(desc, ASCII_CHAT_DESCRIPTION "\n\n");
  (void)fprintf(desc, "USAGE:\n");
  (void)fprintf(desc, "  ascii-chat mirror [options...]\n\n");
  (void)fprintf(desc, "DESCRIPTION:\n");
  (void)fprintf(desc, "  View your local webcam as ASCII art without connecting to a server.\n\n");
  (void)fprintf(desc, "OPTIONS:\n");
  (void)fprintf(desc, USAGE_INDENT "-h --help                    " USAGE_INDENT "print this help\n");
  (void)fprintf(desc, USAGE_INDENT "-x --width WIDTH             " USAGE_INDENT "render width (default: [auto-set])\n");
  (void)fprintf(desc,
                USAGE_INDENT "-y --height HEIGHT           " USAGE_INDENT "render height (default: [auto-set])\n");
  (void)fprintf(desc, USAGE_INDENT "-c --webcam-index CAMERA     " USAGE_INDENT
                                   "webcam device index (0-based) (default: 0)\n");
  (void)fprintf(desc,
                USAGE_INDENT "   --list-webcams            " USAGE_INDENT "list available webcam devices and exit\n");
  (void)fprintf(desc, USAGE_INDENT "-f --webcam-flip             " USAGE_INDENT "toggle horizontal flip of webcam "
                                   "image (default: flipped)\n");
  (void)fprintf(desc, USAGE_INDENT "   --test-pattern            " USAGE_INDENT "use test pattern instead of webcam "
                                   "(for testing)\n");
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
  (void)fprintf(desc, USAGE_INDENT "-s --stretch                 " USAGE_INDENT "stretch or shrink video to fit "
                                   "(ignore aspect ratio) (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "-q --quiet                   " USAGE_INDENT
                                   "disable console logging (log only to file) (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "-S --snapshot                " USAGE_INDENT
                                   "capture single frame and exit (default: [unset])\n");
  (void)fprintf(
      desc, USAGE_INDENT "-D --snapshot-delay SECONDS  " USAGE_INDENT "delay SECONDS before snapshot (default: %.1f)\n",
      (double)SNAPSHOT_DELAY_DEFAULT);
  (void)fprintf(desc, USAGE_INDENT "   --strip-ansi              " USAGE_INDENT
                                   "remove all ANSI escape codes from output (default: [unset])\n");
}

void usage_server(FILE *desc /* stdout|stderr*/) {
  (void)fprintf(desc, "ascii-chat - server options\n");
  (void)fprintf(desc, ASCII_CHAT_DESCRIPTION "\n\n");
  (void)fprintf(desc, USAGE_INDENT "-h --help            " USAGE_INDENT "print this help\n");
  (void)fprintf(desc, USAGE_INDENT "-p --port PORT       " USAGE_INDENT "TCP port to listen on (default: 27224)\n");
  (void)fprintf(desc, USAGE_INDENT "-P --palette PALETTE " USAGE_INDENT "ASCII character palette: "
                                   "standard, blocks, digital, minimal, cool, custom (default: standard)\n");
  (void)fprintf(desc, USAGE_INDENT "-C --palette-chars CHARS     "
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
  (void)fprintf(desc, USAGE_INDENT "   --client-keys FILE" USAGE_INDENT
                                   "allowed client keys file for authentication (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "   --no-audio-mixer  " USAGE_INDENT
                                   "disable audio mixer - send silence (debug mode only)\n");
}

void usage(FILE *desc /* stdout|stderr*/, asciichat_mode_t mode) {
  switch (mode) {
  case MODE_SERVER:
    usage_server(desc);
    break;
  case MODE_CLIENT:
    usage_client(desc);
    break;
  case MODE_MIRROR:
    usage_mirror(desc);
    break;
  default:
    (void)fprintf(desc, "Error: Unknown mode\n");
    break;
  }
}

// trigger ci 1
