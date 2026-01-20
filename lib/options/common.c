/**
 * @file common.c
 * @ingroup options
 * @brief Common utilities and helpers for option parsing
 *
 * Shared helper functions, validators, and global variables used by all
 * option parsing modules (client.c, server.c, mirror.c).
 */

#include "options/common.h"

#include "asciichat_errno.h"
#include "common.h"
#include "log/logging.h"
#include "options/levenshtein.h"
#include "options/validation.h"
#include "platform/terminal.h"
#include "util/parsing.h"
#include "util/password.h"
#include "util/path.h"

#ifdef _WIN32
#include <io.h>
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#else
#include <unistd.h>
#endif

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// ============================================================================
// Helper Functions
// ============================================================================

// Find the most similar option name to an unknown option
// Returns the best matching option name, or NULL if no good match found
// Note: Uses LEVENSHTEIN_SUGGESTION_THRESHOLD from levenshtein.h
const char *find_similar_option(const char *unknown_opt, const struct option *options) {
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
char *get_required_argument(const char *opt_value, char *buffer, size_t buffer_size, const char *option_name,
                            asciichat_mode_t mode);

// Validate and retrieve required argument for an option
char *validate_required_argument(const char *optarg, char *argbuf, size_t argbuf_size, const char *option_name,
                                 asciichat_mode_t mode) {
  char *value = get_required_argument(optarg, argbuf, argbuf_size, option_name, mode);
  if (!value) {
    (void)option_error_invalid();
  }
  return value;
}

// Validate a positive integer value (internal option parsing helper)
bool validate_positive_int_opt(const char *value_str, int *out_value, const char *param_name) {
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
bool validate_port_opt(const char *value_str, uint16_t *out_port) {
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
bool validate_fps_opt(const char *value_str, int *out_fps) {
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
bool validate_webcam_index(const char *value_str, unsigned short int *out_index) {
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
asciichat_error_t detect_default_ssh_key(char *key_path, size_t path_size) {
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

// ============================================================================
// Argument Processing Helpers
// ============================================================================

// Helper function to strip equals sign from optarg if present
char *strip_equals_prefix(const char *opt_value, char *buffer, size_t buffer_size) {
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
char *get_required_argument(const char *opt_value, char *buffer, size_t buffer_size, const char *option_name,
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

// Read password from stdin with prompt (returns allocated string or NULL on error)
char *read_password_from_stdin(const char *prompt) {
  char *password_buf = SAFE_MALLOC(PASSWORD_MAX_LEN, char *);
  if (!password_buf) {
    return NULL;
  }

  if (prompt_password_simple(prompt, password_buf, PASSWORD_MAX_LEN) != 0) {
    SAFE_FREE(password_buf);
    return NULL;
  }

  return password_buf; // Caller must free
}

// ============================================================================
// Global Variable Definitions
// ============================================================================

ASCIICHAT_API unsigned short int opt_width = OPT_WIDTH_DEFAULT, opt_height = OPT_HEIGHT_DEFAULT;
ASCIICHAT_API bool auto_width = true, auto_height = true;

// Track if --port was explicitly set via command-line flag (for mutual exclusion validation)
bool port_explicitly_set_via_flag = false;

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

// Color channel lookup tables (used by image.c for palette precomputation)
// Size must match ASCII_LUMINANCE_LEVELS (256) from ascii.h
unsigned short int RED[256];
unsigned short int GREEN[256];
unsigned short int BLUE[256];
unsigned short int GRAY[256];

// ============================================================================
// Shared Option Parsers (Client + Mirror Common Options)
// ============================================================================

asciichat_error_t parse_color_mode_option(const char *value_str, options_t *opts) {
  if (!value_str || !opts) {
    return ERROR_INVALID_PARAM;
  }

  if (strcmp(value_str, "auto") == 0) {
    opts->color_mode = COLOR_MODE_AUTO;
  } else if (strcmp(value_str, "none") == 0 || strcmp(value_str, "mono") == 0) {
    opts->color_mode = COLOR_MODE_NONE;
  } else if (strcmp(value_str, "16") == 0 || strcmp(value_str, "16color") == 0) {
    opts->color_mode = COLOR_MODE_16_COLOR;
  } else if (strcmp(value_str, "256") == 0 || strcmp(value_str, "256color") == 0) {
    opts->color_mode = COLOR_MODE_256_COLOR;
  } else if (strcmp(value_str, "truecolor") == 0 || strcmp(value_str, "24bit") == 0) {
    opts->color_mode = COLOR_MODE_TRUECOLOR;
  } else {
    (void)fprintf(stderr, "Error: Invalid color mode '%s'. Valid modes: auto, none, 16, 256, truecolor\n", value_str);
    return ERROR_INVALID_PARAM;
  }

  return ASCIICHAT_OK;
}

asciichat_error_t parse_render_mode_option(const char *value_str, options_t *opts) {
  if (!value_str || !opts) {
    return ERROR_INVALID_PARAM;
  }

  if (strcmp(value_str, "foreground") == 0 || strcmp(value_str, "fg") == 0) {
    opts->render_mode = RENDER_MODE_FOREGROUND;
  } else if (strcmp(value_str, "background") == 0 || strcmp(value_str, "bg") == 0) {
    opts->render_mode = RENDER_MODE_BACKGROUND;
  } else if (strcmp(value_str, "half-block") == 0 || strcmp(value_str, "halfblock") == 0) {
    opts->render_mode = RENDER_MODE_HALF_BLOCK;
  } else {
    (void)fprintf(stderr, "Error: Invalid render mode '%s'. Valid modes: foreground, background, half-block\n",
                  value_str);
    return ERROR_INVALID_PARAM;
  }

  return ASCIICHAT_OK;
}

asciichat_error_t parse_palette_option(const char *value_str, options_t *opts) {
  if (!value_str || !opts) {
    return ERROR_INVALID_PARAM;
  }

  if (strcmp(value_str, "standard") == 0) {
    opts->palette_type = PALETTE_STANDARD;
  } else if (strcmp(value_str, "blocks") == 0) {
    opts->palette_type = PALETTE_BLOCKS;
  } else if (strcmp(value_str, "digital") == 0) {
    opts->palette_type = PALETTE_DIGITAL;
  } else if (strcmp(value_str, "minimal") == 0) {
    opts->palette_type = PALETTE_MINIMAL;
  } else if (strcmp(value_str, "cool") == 0) {
    opts->palette_type = PALETTE_COOL;
  } else if (strcmp(value_str, "custom") == 0) {
    opts->palette_type = PALETTE_CUSTOM;
  } else {
    (void)fprintf(stderr, "Invalid palette '%s'. Valid palettes: standard, blocks, digital, minimal, cool, custom\n",
                  value_str);
    return ERROR_INVALID_PARAM;
  }

  return ASCIICHAT_OK;
}

asciichat_error_t parse_palette_chars_option(const char *value_str, options_t *opts) {
  if (!value_str || !opts) {
    return ERROR_INVALID_PARAM;
  }

  if (strlen(value_str) >= sizeof(opts->palette_custom)) {
    (void)fprintf(stderr, "Invalid palette-chars: too long (%zu chars, max %zu)\n", strlen(value_str),
                  sizeof(opts->palette_custom) - 1);
    return ERROR_INVALID_PARAM;
  }

  SAFE_STRNCPY(opts->palette_custom, value_str, sizeof(opts->palette_custom));
  opts->palette_custom[sizeof(opts->palette_custom) - 1] = '\0';
  opts->palette_custom_set = true;
  opts->palette_type = PALETTE_CUSTOM;

  return ASCIICHAT_OK;
}

asciichat_error_t parse_width_option(const char *value_str, options_t *opts) {
  if (!opts) {
    return ERROR_INVALID_PARAM;
  }

  int width_val;
  if (!validate_positive_int_opt(value_str, &width_val, "width")) {
    return ERROR_INVALID_PARAM;
  }

  opts->width = width_val;
  opts->auto_width = false;

  return ASCIICHAT_OK;
}

asciichat_error_t parse_height_option(const char *value_str, options_t *opts) {
  if (!opts) {
    return ERROR_INVALID_PARAM;
  }

  int height_val;
  if (!validate_positive_int_opt(value_str, &height_val, "height")) {
    return ERROR_INVALID_PARAM;
  }

  opts->height = height_val;
  opts->auto_height = false;

  return ASCIICHAT_OK;
}

asciichat_error_t parse_webcam_index_option(const char *value_str, options_t *opts) {
  if (!opts) {
    return ERROR_INVALID_PARAM;
  }

  unsigned short int index_val;
  if (!validate_webcam_index(value_str, &index_val)) {
    return ERROR_INVALID_PARAM;
  }

  opts->webcam_index = index_val;

  return ASCIICHAT_OK;
}

asciichat_error_t parse_snapshot_delay_option(const char *value_str, options_t *opts) {
  if (!value_str || !opts) {
    return ERROR_INVALID_PARAM;
  }

  char *endptr;
  float delay = strtof(value_str, &endptr);
  if (endptr == value_str || *endptr != '\0' || delay < 0.0f) {
    (void)fprintf(stderr, "Invalid snapshot delay '%s'. Must be a non-negative number.\n", value_str);
    return ERROR_INVALID_PARAM;
  }

  opts->snapshot_delay = delay;

  return ASCIICHAT_OK;
}

asciichat_error_t parse_log_level_option(const char *value_str, options_t *opts) {
  if (!opts) {
    return ERROR_INVALID_PARAM;
  }

  char error_msg[256];
  int log_level = validate_opt_log_level(value_str, error_msg, sizeof(error_msg));

  if (log_level == -1) {
    (void)fprintf(stderr, "Error: %s\n", error_msg);
    return ERROR_INVALID_PARAM;
  }

  opts->log_level = (log_level_t)log_level;

  return ASCIICHAT_OK;
}

// ============================================================================
// Terminal Dimension Utilities
// ============================================================================

void update_dimensions_for_full_height(options_t *opts) {
  if (!opts) {
    return;
  }

  unsigned short int term_width, term_height;

  // Note: Logging is not available during options_init, so we can't use log_debug here
  asciichat_error_t result = get_terminal_size(&term_width, &term_height);
  if (result == ASCIICHAT_OK) {
    // If both dimensions are auto, set height to terminal height and let
    // aspect_ratio calculate width
    if (opts->auto_height && opts->auto_width) {
      opts->height = term_height;
      opts->width = term_width; // Also set width when both are auto
    }
    // If only height is auto, use full terminal height
    else if (opts->auto_height) {
      opts->height = term_height;
    }
    // If only width is auto, use full terminal width
    else if (opts->auto_width) {
      opts->width = term_width;
    }
  } else {
    // Terminal size detection failed, but we can still continue with defaults
  }
}

void update_dimensions_to_terminal_size(options_t *opts) {
  if (!opts) {
    return;
  }

  unsigned short int term_width, term_height;
  // Get current terminal size (get_terminal_size already handles ioctl first, then $COLUMNS/$LINES fallback)
  asciichat_error_t terminal_result = get_terminal_size(&term_width, &term_height);
  if (terminal_result == ASCIICHAT_OK) {
    // Use INFO level so this is visible without -v flag (important for debugging dimension issues)
    log_info("Terminal size detected: %ux%u (auto_width=%d, auto_height=%d)", term_width, term_height, opts->auto_width,
             opts->auto_height);
    if (opts->auto_width) {
      opts->width = term_width;
      log_info("Auto-width: set width to %u", opts->width);
    }
    if (opts->auto_height) {
      opts->height = term_height;
      log_info("Auto-height: set height to %u", opts->height);
    }
    log_info("Final dimensions: %ux%u", opts->width, opts->height);
  } else {
    // Terminal detection failed - keep the default values set in options_init()
    log_warn("TERMINAL_DETECT_FAIL: Could not detect terminal size, using defaults: %ux%u", opts->width, opts->height);
  }
}

// ============================================================================
// Generic Usage Function (Dispatches to Mode-Specific Functions)
// ============================================================================

void usage(FILE *desc, asciichat_mode_t mode) {
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
  case MODE_DISCOVERY_SERVER:
    usage_acds(desc);
    break;
  default:
    (void)fprintf(desc, "Unknown mode\n");
    break;
  }
}
