/**
 * @file common.c
 * @ingroup options
 * @brief Common utilities and helpers for option parsing
 *
 * Shared helper functions, validators, and global variables used by all
 * option parsing modules (client.c, server.c, mirror.c).
 */

#include <ascii-chat/options/common.h>

#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/options/levenshtein.h>
#include <ascii-chat/options/validation.h>
#include <ascii-chat/options/builder.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/stat.h>
#include <ascii-chat/util/parsing.h>
#include <ascii-chat/util/password.h>
#include <ascii-chat/util/path.h>
#include <ascii-chat/util/string.h>

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

// Helper to format all available modes for an option (used in error messages)
// Returns comma-separated list like "server, client, mirror" or "global options"
const char *format_available_modes(option_mode_bitmask_t mode_bitmask) {
  static char buffer[256];
  buffer[0] = '\0';

  bool first = true;

  // Check if it's a global/binary option
  if (mode_bitmask & OPTION_MODE_BINARY) {
    SAFE_SNPRINTF(buffer, sizeof(buffer), "global options");
    return buffer;
  }

  // Build comma-separated list of modes (ordered: default, client, server, mirror, discovery-service)
  if (mode_bitmask & OPTION_MODE_DISCOVERY) {
    safe_snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "%sdefault", first ? "" : ", ");
    first = false;
  }
  if (mode_bitmask & OPTION_MODE_CLIENT) {
    safe_snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "%sclient", first ? "" : ", ");
    first = false;
  }
  if (mode_bitmask & OPTION_MODE_SERVER) {
    safe_snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "%sserver", first ? "" : ", ");
    first = false;
  }
  if (mode_bitmask & OPTION_MODE_MIRROR) {
    safe_snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "%smirror", first ? "" : ", ");
    first = false;
  }
  if (mode_bitmask & OPTION_MODE_DISCOVERY_SVC) {
    safe_snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "%sdiscovery-service", first ? "" : ", ");
    first = false;
  }

  // Fallback if no modes matched
  if (buffer[0] == '\0') {
    SAFE_SNPRINTF(buffer, sizeof(buffer), "unknown mode");
  }

  return buffer;
}

// Find similar option across all modes and suggest it with mode information
// Returns formatted suggestion string with mode info, or NULL if no good match
const char *find_similar_option_with_mode(const char *unknown_opt, const options_config_t *config,
                                          option_mode_bitmask_t current_mode_bitmask) {
  if (!unknown_opt || !config) {
    return NULL;
  }

  // Extract the option name without dashes
  const char *opt_name = unknown_opt;
  if (strncmp(opt_name, "--", 2) == 0) {
    opt_name += 2;
  } else if (strncmp(opt_name, "-", 1) == 0) {
    opt_name += 1;
  } else {
    return NULL; // Not an option format
  }

  const option_descriptor_t *best_match = NULL;
  size_t best_distance = SIZE_MAX;

  // Search through all descriptors
  for (size_t i = 0; i < config->num_descriptors; i++) {
    const option_descriptor_t *desc = &config->descriptors[i];
    if (!desc->long_name)
      continue;

    // Calculate distance to the long name
    size_t dist = levenshtein(opt_name, desc->long_name);
    if (dist < best_distance) {
      best_distance = dist;
      best_match = desc;
    }
  }

  // Only suggest if the distance is within our threshold
  if (best_distance > LEVENSHTEIN_SUGGESTION_THRESHOLD || !best_match) {
    return NULL;
  }

  // Check if the option is not available in current mode
  bool available_in_current_mode = (best_match->mode_bitmask & current_mode_bitmask) != 0;

  static char suggestion[256];
  if (available_in_current_mode) {
    // Option exists but user typed it wrong - just suggest the correct spelling
    safe_snprintf(suggestion, sizeof(suggestion), "Did you mean '--%s'?", best_match->long_name);
  } else {
    // Option exists but in a different mode - show all available modes
    const char *modes_str = format_available_modes(best_match->mode_bitmask);
    safe_snprintf(suggestion, sizeof(suggestion), "Did you mean '--%s' (available in modes: %s)?",
                  best_match->long_name, modes_str);
  }

  return suggestion;
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
    log_error("Invalid %s value '%s'. %s must be a positive integer.", param_name, value_str, param_name);
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
    log_error("Invalid port value '%s'. Port must be a number between 1 and 65535.", value_str);
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
    log_error("Invalid FPS value '%s'. FPS must be between 1 and 144.", value_str);
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

  char error_msg[BUFFER_SIZE_SMALL];
  int parsed_index = validate_opt_device_index(value_str, error_msg, sizeof(error_msg));
  if (parsed_index == INT_MIN) {
    log_error("Invalid webcam index: %s", error_msg);
    return false;
  }
  // Webcam index doesn't support -1 (default), must be >= 0
  if (parsed_index < 0) {
    log_error("Invalid webcam index '%s'. Webcam index must be a non-negative integer.", value_str);
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

  log_error("No Ed25519 SSH key found at %s", full_path);
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
  (void)0;
  const char *mode_name = (mode == MODE_SERVER ? "server" : (mode == MODE_MIRROR ? "mirror" : "client"));
  log_error("%s: option '--%s' requires an argument", mode_name, option_name);
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
// Removed: All opt_* global variables moved to RCU options_t struct
// Access these via GET_OPTION(field_name) for thread-safe lock-free reads
// Modify via options_set_*() functions for thread-safe updates
// ============================================================================

// These flags are used during parsing, not part of RCU options
ASCIICHAT_API bool auto_width = true, auto_height = true;

// Track if --port was explicitly set via command-line flag (for mutual exclusion validation)
bool port_explicitly_set_via_flag = false;

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

  if (strcmp(value_str, "auto") == 0 || strcmp(value_str, "a") == 0) {
    opts->color_mode = COLOR_MODE_AUTO;
  } else if (strcmp(value_str, "none") == 0 || strcmp(value_str, "mono") == 0) {
    opts->color_mode = COLOR_MODE_NONE;
  } else if (strcmp(value_str, "16") == 0 || strcmp(value_str, "16color") == 0 || strcmp(value_str, "ansi") == 0) {
    opts->color_mode = COLOR_MODE_16_COLOR;
  } else if (strcmp(value_str, "256") == 0 || strcmp(value_str, "256color") == 0) {
    opts->color_mode = COLOR_MODE_256_COLOR;
  } else if (strcmp(value_str, "truecolor") == 0 || strcmp(value_str, "24bit") == 0 || strcmp(value_str, "tc") == 0 ||
             strcmp(value_str, "rgb") == 0 || strcmp(value_str, "true") == 0) {
    opts->color_mode = COLOR_MODE_TRUECOLOR;
  } else {
    log_error("Invalid color mode '%s'. Valid modes: auto, none, 16, 256, truecolor", value_str);
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
    log_error("Invalid render mode '%s'. Valid modes: foreground, background, half-block", value_str);
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
    log_error("Invalid palette '%s'. Valid palettes: standard, blocks, digital, minimal, cool, custom", value_str);
    return ERROR_INVALID_PARAM;
  }

  return ASCIICHAT_OK;
}

asciichat_error_t parse_palette_chars_option(const char *value_str, options_t *opts) {
  if (!value_str || !opts) {
    return ERROR_INVALID_PARAM;
  }

  if (strlen(value_str) >= sizeof(opts->palette_custom)) {
    log_error("Invalid palette-chars: too long (%zu chars, max %zu)", strlen(value_str),
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
    log_error("Invalid snapshot delay '%s'. Must be a non-negative number.", value_str);
    return ERROR_INVALID_PARAM;
  }

  opts->snapshot_delay = delay;

  return ASCIICHAT_OK;
}

asciichat_error_t parse_log_level_option(const char *value_str, options_t *opts) {
  if (!opts) {
    return ERROR_INVALID_PARAM;
  }

  char error_msg[BUFFER_SIZE_SMALL];
  int log_level = validate_opt_log_level(value_str, error_msg, sizeof(error_msg));

  if (log_level == -1) {
    log_error("%s", error_msg);
    return ERROR_INVALID_PARAM;
  }

  opts->log_level = (log_level_t)log_level;

  return ASCIICHAT_OK;
}

// ============================================================================
// Option Formatting Utilities
// ============================================================================

const char *options_get_type_placeholder(option_type_t type) {
  switch (type) {
  case OPTION_TYPE_INT:
    return "INTEGER";
  case OPTION_TYPE_DOUBLE:
    return "NUMBER";
  case OPTION_TYPE_STRING:
    return "STRING";
  case OPTION_TYPE_CALLBACK:
    return "VALUE";
  case OPTION_TYPE_BOOL:
    return "BOOLEAN";
  case OPTION_TYPE_ACTION:
  default:
    return "";
  }
}

int options_format_default_value(option_type_t type, const void *default_value, char *buf, size_t bufsize) {
  if (!default_value || !buf || bufsize == 0) {
    return 0;
  }

  switch (type) {
  case OPTION_TYPE_BOOL:
    return safe_snprintf(buf, bufsize, "%s", *(const bool *)default_value ? "true" : "false");
  case OPTION_TYPE_INT: {
    int int_val = 0;
    memcpy(&int_val, default_value, sizeof(int));
    return safe_snprintf(buf, bufsize, "%d", int_val);
  }
  case OPTION_TYPE_STRING:
    return safe_snprintf(buf, bufsize, "%s", *(const char *const *)default_value);
  case OPTION_TYPE_DOUBLE: {
    double double_val = 0.0;
    memcpy(&double_val, default_value, sizeof(double));
    return safe_snprintf(buf, bufsize, "%.2f", double_val);
  }
  default:
    // OPTION_TYPE_CALLBACK and OPTION_TYPE_ACTION don't have defaults to display
    return 0;
  }
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
    log_dev("Terminal size detected: %ux%u (auto_width=%d, auto_height=%d)", term_width, term_height, opts->auto_width,
            opts->auto_height);
    if (opts->auto_width) {
      opts->width = term_width;
      log_debug("Auto-width: set width to %u", opts->width);
    }
    if (opts->auto_height) {
      opts->height = term_height;
      log_debug("Auto-height: set height to %u", opts->height);
    }
    log_debug("Final dimensions: %ux%u", opts->width, opts->height);
  } else {
    // Terminal detection failed - keep the default values set in options_init()
    log_warn("TERMINAL_DETECT_FAIL: Could not detect terminal size, using defaults: %ux%u", opts->width, opts->height);
  }
}

// ============================================================================
// Generic Usage Function (Unified Implementation)
// ============================================================================

/**
 * @brief Mode metadata for usage display
 */
typedef struct {
  asciichat_mode_t mode;    ///< Application mode
  const char *program_name; ///< Program name shown in usage
  const char *description;  ///< One-line description of mode
} mode_metadata_t;

static const mode_metadata_t mode_info[] = {
    {MODE_SERVER, "ascii-chat server", "host a server mixing video and audio for ascii-chat clients"},
    {MODE_CLIENT, "ascii-chat client", "connect to an ascii-chat server"},
    {MODE_MIRROR, "ascii-chat mirror", "use the webcam or files or urls without network connections"},
    {MODE_DISCOVERY_SERVICE, "ascii-chat discovery-service", "secure p2p session signalling"},
    {MODE_DISCOVERY, "💻📸 ascii-chat 🔡💬", "Video chat in your terminal"},
};

void usage(FILE *desc, asciichat_mode_t mode) {
  if (!desc) {
    return;
  }

  // Find mode metadata
  const mode_metadata_t *metadata = NULL;
  for (size_t i = 0; i < sizeof(mode_info) / sizeof(mode_info[0]); i++) {
    if (mode_info[i].mode == mode) {
      metadata = &mode_info[i];
      break;
    }
  }

  if (!metadata) {
    (void)fprintf(desc, "error: Unknown mode\n");
    return;
  }

  // Get unified config
  const options_config_t *config = options_preset_unified(metadata->program_name, metadata->description);
  if (!config) {
    (void)fprintf(desc, "Error: Failed to create options config\n");
    return;
  }

  options_print_help_for_mode(config, mode, metadata->program_name, metadata->description, desc);
  options_config_destroy(config);
}

// ============================================================================
// Options Validation Helper
// ============================================================================

asciichat_error_t validate_options_and_report(const void *config, const void *opts) {
  if (!config || !opts) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Config or options is NULL");
  }

  char *error_message = NULL;
  // Cast opaque config pointer to actual type
  const options_config_t *config_typed = (const options_config_t *)config;
  asciichat_error_t result = options_config_validate(config_typed, opts, &error_message);
  if (result != ASCIICHAT_OK) {
    if (error_message) {
      log_error("%s", error_message);
      free(error_message);
    }
  }
  return result;
}

// ============================================================================
// Print Project Links
// ============================================================================

void print_project_links(FILE *desc) {
  if (!desc) {
    return;
  }

  (void)fprintf(desc, "🔗 %s\n", colored_string(LOG_COLOR_GREY, "https://ascii-chat.com"));
  (void)fprintf(desc, "🔗 %s\n", colored_string(LOG_COLOR_GREY, "https://github.com/zfogg/ascii-chat"));
}
