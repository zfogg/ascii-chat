
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

#include "image2ascii/ascii.h"
#include "options.h"
#include "config.h"
#include "common.h"
#include "util/ip.h"
#include "platform/system.h"
#include "platform/terminal.h"
#include "platform/password.h"
#include "version.h"
#include "crypto/crypto.h"

// Safely parse string to integer with validation
asciichat_error_t strtoint_safe(const char *str) {
  if (!str || *str == '\0') {
    return INT_MIN; // Error: NULL or empty string
  }

  char *endptr;
  long result = strtol(str, &endptr, 10);

  // Check for various error conditions:
  // 1. No conversion performed (endptr == str)
  // 2. Partial conversion (still characters left)
  // 3. Out of int range
  if (endptr == str || *endptr != '\0' || result > INT_MAX || result < INT_MIN) {
    return INT_MIN; // Error: invalid input
  }

  return (int)result;
}

// Detect default SSH key path for the current user
static asciichat_error_t detect_default_ssh_key(char *key_path, size_t path_size) {
  const char *home_dir = platform_getenv("HOME");
  if (!home_dir) {
    // Fallback for Windows
    home_dir = platform_getenv("USERPROFILE");
  }

  if (!home_dir) {
    return SET_ERRNO(ERROR_CONFIG, "Could not determine user home directory");
  }

  // Only support Ed25519 keys (modern, secure, fast)
  char full_path[PLATFORM_MAX_PATH_LENGTH];
  SAFE_SNPRINTF(full_path, sizeof(full_path), "%s/.ssh/id_ed25519", home_dir);

  // Check if the Ed25519 private key file exists
  struct stat st;
  if (stat(full_path, &st) == 0 && S_ISREG(st.st_mode)) {
    SAFE_SNPRINTF(key_path, path_size, "%s", full_path);
    log_debug("Found default SSH key: %s", full_path);
    return ASCIICHAT_OK;
  }

  (void)fprintf(stderr, "No Ed25519 SSH key found at %s\n", full_path);
  return SET_ERRNO(
      ERROR_CRYPTO_KEY,
      "Only Ed25519 keys are supported (modern, secure, fast). Generate a new key with: ssh-keygen -t ed25519");
}

ASCIICHAT_API unsigned short int opt_width = OPT_WIDTH_DEFAULT, opt_height = OPT_HEIGHT_DEFAULT;
ASCIICHAT_API bool auto_width = true, auto_height = true;

ASCIICHAT_API char opt_address[OPTIONS_BUFF_SIZE] = "localhost", opt_address6[OPTIONS_BUFF_SIZE] = "",
                   opt_port[OPTIONS_BUFF_SIZE] = "27224";

ASCIICHAT_API unsigned short int opt_webcam_index = 0;

ASCIICHAT_API bool opt_webcam_flip = true;

ASCIICHAT_API bool opt_test_pattern = false; // Use test pattern instead of real webcam

// Terminal color mode and capability options
ASCIICHAT_API terminal_color_mode_t opt_color_mode = COLOR_MODE_AUTO; // Auto-detect by default
ASCIICHAT_API render_mode_t opt_render_mode = RENDER_MODE_FOREGROUND; // Foreground by default
ASCIICHAT_API unsigned short int opt_show_capabilities = 0;           // Don't show capabilities by default
ASCIICHAT_API unsigned short int opt_force_utf8 = 0;                  // Don't force UTF-8 by default

ASCIICHAT_API unsigned short int opt_audio_enabled = 0;
ASCIICHAT_API int opt_audio_device = -1; // -1 means use default device

// Allow stretching/shrinking without preserving aspect ratio when set via -s/--stretch
ASCIICHAT_API unsigned short int opt_stretch = 0;

// Disable console logging when set via -q/--quiet (logs only to file)
ASCIICHAT_API unsigned short int opt_quiet = 0;

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

// Log file path for file logging (empty string means no file logging)
ASCIICHAT_API char opt_log_file[OPTIONS_BUFF_SIZE] = "";

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
static struct option client_options[] = {{"address", required_argument, NULL, 'a'},
                                         {"host", required_argument, NULL, 'H'},
                                         {"port", required_argument, NULL, 'p'},
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
                                         {"audio-device", required_argument, NULL, 1007},
                                         {"stretch", no_argument, NULL, 's'},
                                         {"quiet", no_argument, NULL, 'q'},
                                         {"snapshot", no_argument, NULL, 'S'},
                                         {"snapshot-delay", required_argument, NULL, 'D'},
                                         {"log-file", required_argument, NULL, 'L'},
                                         {"encrypt", no_argument, NULL, 'E'},
                                         {"key", required_argument, NULL, 'K'},
                                         {"password", optional_argument, NULL, 1009},
                                         {"keyfile", required_argument, NULL, 'F'},
                                         {"no-encrypt", no_argument, NULL, 1005},
                                         {"server-key", required_argument, NULL, 1006},
                                         {"config", required_argument, NULL, 1010},
                                         {"config-create", optional_argument, NULL, 1011},
                                         {"help", optional_argument, NULL, 'h'},
                                         {0, 0, 0, 0}};

// Server-only options
static struct option server_options[] = {{"address", required_argument, NULL, 'a'},
                                         {"address6", required_argument, NULL, 1012},
                                         {"port", required_argument, NULL, 'p'},
                                         {"palette", required_argument, NULL, 'P'},
                                         {"palette-chars", required_argument, NULL, 'C'},
                                         {"log-file", required_argument, NULL, 'L'},
                                         {"encrypt", no_argument, NULL, 'E'},
                                         {"key", required_argument, NULL, 'K'},
                                         {"password", optional_argument, NULL, 1009},
                                         {"keyfile", required_argument, NULL, 'F'},
                                         {"no-encrypt", no_argument, NULL, 1005},
                                         {"client-keys", required_argument, NULL, 1008},
                                         {"config", required_argument, NULL, 1010},
                                         {"config-create", optional_argument, NULL, 1011},
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
// Validation Helper Functions (shared between options.c and config.c)
// ============================================================================

/**
 * Validate port number (1-65535)
 * Returns 0 on success, non-zero on error
 */
int validate_port(const char *value_str, char *error_msg, size_t error_msg_size) {
  if (!value_str || strlen(value_str) == 0) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Port value is required");
    }
    return -1;
  }

  char *endptr;
  long port_num = strtol(value_str, &endptr, 10);
  if (*endptr != '\0' || value_str == endptr || port_num < 1 || port_num > 65535) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Invalid port value '%s'. Port must be a number between 1 and 65535.",
                    value_str);
    }
    return -1;
  }
  return 0;
}

/**
 * Validate positive integer
 * Returns parsed value on success, -1 on error
 */
int validate_positive_int(const char *value_str, char *error_msg, size_t error_msg_size) {
  if (!value_str || strlen(value_str) == 0) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Value is required");
    }
    return -1;
  }

  int val = strtoint_safe(value_str);
  if (val == INT_MIN || val <= 0) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Invalid value '%s'. Must be a positive integer.", value_str);
    }
    return -1;
  }
  return val;
}

/**
 * Validate non-negative integer
 * Returns parsed value on success, -1 on error
 */
int validate_non_negative_int(const char *value_str, char *error_msg, size_t error_msg_size) {
  if (!value_str || strlen(value_str) == 0) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Value is required");
    }
    return -1;
  }

  int val = strtoint_safe(value_str);
  if (val == INT_MIN || val < 0) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Invalid value '%s'. Must be a non-negative integer.", value_str);
    }
    return -1;
  }
  return val;
}

/**
 * Validate color mode string
 * Returns parsed color mode on success, -1 on error
 */
int validate_color_mode(const char *value_str, char *error_msg, size_t error_msg_size) {
  if (!value_str) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Color mode value is required");
    }
    return -1;
  }

  if (strcmp(value_str, "auto") == 0) {
    return COLOR_MODE_AUTO;
  }
  if (strcmp(value_str, "mono") == 0 || strcmp(value_str, "monochrome") == 0) {
    return COLOR_MODE_MONO;
  }
  if (strcmp(value_str, "16") == 0 || strcmp(value_str, "16color") == 0) {
    return COLOR_MODE_16_COLOR;
  }
  if (strcmp(value_str, "256") == 0 || strcmp(value_str, "256color") == 0) {
    return COLOR_MODE_256_COLOR;
  }
  if (strcmp(value_str, "truecolor") == 0 || strcmp(value_str, "24bit") == 0) {
    return COLOR_MODE_TRUECOLOR;
  }
  if (error_msg) {
    SAFE_SNPRINTF(error_msg, error_msg_size, "Invalid color mode '%s'. Valid modes: auto, mono, 16, 256, truecolor",
                  value_str);
  }
  return -1;
}

/**
 * Validate render mode string
 * Returns parsed render mode on success, -1 on error
 */
int validate_render_mode(const char *value_str, char *error_msg, size_t error_msg_size) {
  if (!value_str) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Render mode value is required");
    }
    return -1;
  }

  if (strcmp(value_str, "foreground") == 0 || strcmp(value_str, "fg") == 0) {
    return RENDER_MODE_FOREGROUND;
  }
  if (strcmp(value_str, "background") == 0 || strcmp(value_str, "bg") == 0) {
    return RENDER_MODE_BACKGROUND;
  }
  if (strcmp(value_str, "half-block") == 0 || strcmp(value_str, "halfblock") == 0) {
    return RENDER_MODE_HALF_BLOCK;
  }
  if (error_msg) {
    SAFE_SNPRINTF(error_msg, error_msg_size,
                  "Invalid render mode '%s'. Valid modes: foreground, background, half-block", value_str);
  }
  return -1;
}

/**
 * Validate palette type string
 * Returns parsed palette type on success, -1 on error
 */
int validate_palette(const char *value_str, char *error_msg, size_t error_msg_size) {
  if (!value_str) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Palette value is required");
    }
    return -1;
  }

  if (strcmp(value_str, "standard") == 0) {
    return PALETTE_STANDARD;
  } else if (strcmp(value_str, "blocks") == 0) {
    return PALETTE_BLOCKS;
  } else if (strcmp(value_str, "digital") == 0) {
    return PALETTE_DIGITAL;
  } else if (strcmp(value_str, "minimal") == 0) {
    return PALETTE_MINIMAL;
  } else if (strcmp(value_str, "cool") == 0) {
    return PALETTE_COOL;
  } else if (strcmp(value_str, "custom") == 0) {
    return PALETTE_CUSTOM;
  } else {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size,
                    "Invalid palette '%s'. Valid palettes: standard, blocks, digital, minimal, cool, custom",
                    value_str);
    }
    return -1;
  }
}

/**
 * Validate IP address or hostname
 * Returns 0 on success, -1 on error
 * Sets parsed_address on success (resolved if hostname)
 */
int validate_ip_address(const char *value_str, char *parsed_address, size_t address_size, bool is_client,
                        char *error_msg, size_t error_msg_size) {
  (void)is_client; // Parameter not used but kept for API consistency
  if (!value_str || strlen(value_str) == 0) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Address value is required");
    }
    return -1;
  }

  // Parse IPv6 address (remove brackets if present)
  char parsed_addr[OPTIONS_BUFF_SIZE];
  if (parse_ipv6_address(value_str, parsed_addr, sizeof(parsed_addr)) == 0) {
    value_str = parsed_addr;
  }

  // Check if it's a valid IPv4 address
  if (is_valid_ipv4(value_str)) {
    SAFE_SNPRINTF(parsed_address, address_size, "%s", value_str);
    return 0;
  }
  // Check if it's a valid IPv6 address
  if (is_valid_ipv6(value_str)) {
    SAFE_SNPRINTF(parsed_address, address_size, "%s", value_str);
    return 0;
  }
  // Check if it looks like an invalid IP (has dots but not valid IPv4 format)
  if (strchr(value_str, '.') != NULL) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size,
                    "Invalid IP address format '%s'. IPv4 addresses must have exactly 4 octets.", value_str);
    }
    return -1;
  }

  // Otherwise, try to resolve as hostname
  char resolved_ip[OPTIONS_BUFF_SIZE];
  if (platform_resolve_hostname_to_ipv4(value_str, resolved_ip, sizeof(resolved_ip)) == 0) {
    SAFE_SNPRINTF(parsed_address, address_size, "%s", resolved_ip);
    return 0;
  } else {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Failed to resolve hostname '%s' to IP address.", value_str);
    }
    return -1;
  }
}

/**
 * Validate float value (non-negative)
 * Returns parsed value on success, returns -1.0f on error (caller must check)
 */
float validate_float_non_negative(const char *value_str, char *error_msg, size_t error_msg_size) {
  if (!value_str || strlen(value_str) == 0) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Value is required");
    }
    return -1.0f;
  }

  char *endptr;
  float val = strtof(value_str, &endptr);
  if (*endptr != '\0' || value_str == endptr) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Invalid float value '%s'. Must be a number.", value_str);
    }
    return -1.0f;
  }
  if (val < 0.0f) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Value must be non-negative (got %.2f)", val);
    }
    return -1.0f;
  }
  return val;
}

/**
 * Validate FPS value (1-144)
 * Returns parsed value on success, -1 on error
 */
int validate_fps(const char *value_str, char *error_msg, size_t error_msg_size) {
  if (!value_str || strlen(value_str) == 0) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "FPS value is required");
    }
    return -1;
  }

  int fps_val = strtoint_safe(value_str);
  if (fps_val == INT_MIN || fps_val < 1 || fps_val > 144) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Invalid FPS value '%s'. FPS must be between 1 and 144.", value_str);
    }
    return -1;
  }
  return fps_val;
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
                                   bool is_client) {
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
  (void)fprintf(stderr, "%s: option '--%s' requires an argument\n", is_client ? "client" : "server", option_name);
  (void)fflush(stderr);
  return NULL; // Signal error to caller
}

asciichat_error_t options_init(int argc, char **argv, bool is_client) {
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
    SAFE_SNPRINTF(opt_log_file, OPTIONS_BUFF_SIZE, "%s%sascii-chat.%s.log", temp_dir,
#if defined(_WIN32) || defined(WIN32)
                  "\\",
#else
                  "/",
#endif
                  is_client ? "client" : "server");
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
  if (is_client) {
    // Client connects to localhost by default (IPv6-first with IPv4 fallback)
    SAFE_SNPRINTF(opt_address, OPTIONS_BUFF_SIZE, "localhost");
    opt_address6[0] = '\0'; // Client doesn't use opt_address6
  } else {
    // Server binds to 127.0.0.1 (IPv4) and ::1 (IPv6) by default
    SAFE_SNPRINTF(opt_address, OPTIONS_BUFF_SIZE, "127.0.0.1");
    SAFE_SNPRINTF(opt_address6, OPTIONS_BUFF_SIZE, "::1");
  }

  // Track config file path from --config option (if provided)
  const char *custom_config_path = NULL;

  // Pre-pass: Check for --config-create before parsing (it creates file and exits)
  for (int i = 1; i < argc; i++) {
    if (argv[i] == NULL)
      break;
    if (strncmp(argv[i], "--config-create=", 16) == 0) {
      // Format: --config-create=path
      const char *create_path = argv[i] + 16; // Skip "--config-create=" (16 characters including =)
      if (create_path[0] == '\0') {
        create_path = NULL;
      }
      (void)fprintf(stderr, "[DEBUG] options_init: Found --config-create=%s\n", create_path ? create_path : "(empty)");
      asciichat_error_t create_result = config_create_default(create_path);
      if (create_result != ASCIICHAT_OK) {
        (void)fprintf(stderr, "Failed to create config file: %s\n", asciichat_error_string(create_result));
        return create_result;
      }
      const char *final_path = create_path ? create_path : "default location";
      (void)fprintf(stdout, "Created default config file at %s\n", final_path);
      (void)fflush(stdout);
      return ASCIICHAT_OK; // Exit successfully
    } else if (strcmp(argv[i], "--config-create") == 0) {
      // Format: --config-create [path] (space-separated, path optional)
      // Check if next argument is a mode (server/client) or a path
      const char *create_path = NULL;
      if (i + 1 < argc && argv[i + 1] != NULL) {
        // If next arg is "server" or "client", it's the mode, not a path
        if (strcmp(argv[i + 1], "server") != 0 && strcmp(argv[i + 1], "client") != 0) {
          create_path = argv[i + 1];
        }
      }
      (void)fprintf(stderr, "[DEBUG] options_init: Found --config-create with path=%s\n",
                    create_path ? create_path : "(NULL - using default location)");
      asciichat_error_t create_result = config_create_default(create_path);
      if (create_result != ASCIICHAT_OK) {
        (void)fprintf(stderr, "Failed to create config file: %s\n", asciichat_error_string(create_result));
        return create_result;
      }
      const char *final_path = create_path ? create_path : "default location";
      (void)fprintf(stdout, "Created default config file at %s\n", final_path);
      (void)fflush(stdout);
      return ASCIICHAT_OK; // Exit successfully
    }
  }

  // Pre-pass: Check for --config option (must load before other options are parsed)
  for (int i = 1; i < argc; i++) {
    if (argv[i] == NULL)
      break;
    if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
      custom_config_path = argv[i + 1];
      break;
    } else if (strncmp(argv[i], "--config=", 9) == 0) {
      custom_config_path = argv[i] + 9;
      break;
    }
  }

  // Load configuration from TOML file (if it exists)
  // This happens BEFORE CLI parsing so CLI arguments can override config values
  // Use strict=true if custom path provided (errors are fatal), strict=false for default (non-fatal)
  bool strict_config = (custom_config_path != NULL);
  asciichat_error_t config_result = config_load_and_apply(is_client, custom_config_path, strict_config);
  if (config_result != ASCIICHAT_OK) {
    if (strict_config) {
      // Custom config file errors are fatal - show detailed error message
      const char *config_file_path = custom_config_path ? custom_config_path : "default location";

      // Get error context to retrieve the detailed message
      asciichat_error_context_t err_ctx;
      if (asciichat_has_errno(&err_ctx) && err_ctx.context_message && strlen(err_ctx.context_message) > 0) {
        // Use the detailed context message from SET_ERRNO
        (void)fprintf(stderr, "%s\n", err_ctx.context_message);
      } else {
        // Fallback to generic error message
        const char *error_msg = asciichat_error_string(config_result);
        (void)fprintf(stderr, "Failed to load config file '%s': %s (error code: %d)\n", config_file_path, error_msg,
                      config_result);
        (void)fprintf(stderr, "Please check that the file exists, is readable, and contains valid TOML syntax.\n");
      }
      return config_result;
    }
    // Config load errors are non-fatal for default location (logged as warnings)
    // Continue with defaults and CLI parsing
  }

  // Use different option sets for client vs server
  const char *optstring;
  struct option *options;

  if (is_client) {
    optstring = ":a:H:p:x:y:c:fM:P:C:AsqSD:L:EK:F:h"; // Leading ':' for error reporting
    options = client_options;
  } else {
    optstring = ":a:p:P:C:L:EK:F:h"; // Leading ':' for error reporting (removed A for audio)
    options = server_options;
  }

  // Pre-pass: Check for --help or --version first (they have priority over everything)
  // This ensures help/version are shown without triggering password prompts or other side effects
  for (int i = 1; i < argc; i++) {
    if (argv[i] == NULL) {
      break; // Stop if we hit a NULL element (safety check for tests with malformed argv)
    }
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      usage(stdout, is_client);
      (void)fflush(stdout);
      _exit(0);
    }
    if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
      const char *binary_name = is_client ? "ascii-chat client" : "ascii-chat server";
      printf("%s v%d.%d.%d-%s (%s, %s)\n", binary_name, ASCII_CHAT_VERSION_MAJOR, ASCII_CHAT_VERSION_MINOR,
             ASCII_CHAT_VERSION_PATCH, ASCII_CHAT_GIT_VERSION, ASCII_CHAT_BUILD_DATE, ASCII_CHAT_BUILD_TYPE);
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
      if (options[longindex].name) {
        // Skip --config and --config-create (already handled in pre-pass)
        if (strcmp(options[longindex].name, "config") == 0 || strcmp(options[longindex].name, "config-create") == 0) {
          break;
        }
      }
      break;

    case 'a': {
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "address", is_client);
      if (!value_str)
        return ERROR_USAGE;

      // Parse IPv6 address (remove brackets if present)
      char parsed_addr[OPTIONS_BUFF_SIZE];
      if (parse_ipv6_address(value_str, parsed_addr, sizeof(parsed_addr)) == 0) {
        value_str = parsed_addr;
      }

      // Check if it's a valid IPv4 or IPv6 address
      if (is_valid_ipv4(value_str) || is_valid_ipv6(value_str)) {
        SAFE_SNPRINTF(opt_address, OPTIONS_BUFF_SIZE, "%s", value_str);
      }
      // Check if it looks like an invalid IP (has dots but not valid IPv4 format)
      // This prevents trying to resolve malformed IPs like "192.168.1" as hostnames
      else if (strchr(value_str, '.') != NULL) {
        // Has dots but not valid IPv4 - reject immediately
        (void)fprintf(stderr, "Invalid IP address format '%s'.\n", value_str);
        (void)fprintf(stderr, "IPv4 addresses must have exactly 4 octets (e.g., 192.0.2.1).\n");
        (void)fprintf(stderr, "Supported formats:\n");
        (void)fprintf(stderr, "  IPv4: 192.0.2.1\n");
        (void)fprintf(stderr, "  IPv6: 2001:db8::1 or [2001:db8::1]\n");
        (void)fprintf(stderr, "  Hostname: example.com\n");
        return ERROR_USAGE;
      }
      // Otherwise, try to resolve as hostname
      else {
        // Try to resolve hostname to IPv4 first (for backward compatibility)
        char resolved_ip[OPTIONS_BUFF_SIZE];
        if (platform_resolve_hostname_to_ipv4(value_str, resolved_ip, sizeof(resolved_ip)) == 0) {
          SAFE_SNPRINTF(opt_address, OPTIONS_BUFF_SIZE, "%s", resolved_ip);
        } else {
          (void)fprintf(stderr, "Failed to resolve hostname '%s' to IP address.\n", value_str);
          (void)fprintf(stderr, "Check that the hostname is valid and your DNS is working.\n");
          (void)fprintf(stderr, "Supported formats:\n");
          (void)fprintf(stderr, "  IPv4: 192.0.2.1\n");
          (void)fprintf(stderr, "  IPv6: 2001:db8::1 or [2001:db8::1]\n");
          (void)fprintf(stderr, "  Hostname: example.com\n");
          return ERROR_USAGE;
        }
      }
      break;
    }

    case 1012: { // --address6 (server only)
      if (is_client) {
        (void)fprintf(stderr, "Error: --address6 is only available for server mode.\n");
        return ERROR_USAGE;
      }
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "address6", is_client);
      if (!value_str)
        return ERROR_USAGE;

      // Parse IPv6 address (remove brackets if present)
      char parsed_addr[OPTIONS_BUFF_SIZE];
      if (parse_ipv6_address(value_str, parsed_addr, sizeof(parsed_addr)) == 0) {
        value_str = parsed_addr;
      }

      // Check if it's a valid IPv6 address
      if (is_valid_ipv6(value_str)) {
        SAFE_SNPRINTF(opt_address6, OPTIONS_BUFF_SIZE, "%s", value_str);
      } else {
        (void)fprintf(stderr, "Error: Invalid IPv6 address '%s'.\n", value_str);
        return ERROR_USAGE;
      }
      break;
    }

    case 'H': { // --host (DNS lookup)
      char *hostname = get_required_argument(optarg, argbuf, sizeof(argbuf), "host", is_client);
      if (!hostname)
        return ERROR_USAGE;
      char resolved_ip[OPTIONS_BUFF_SIZE];
      if (platform_resolve_hostname_to_ipv4(hostname, resolved_ip, sizeof(resolved_ip)) != 0) {
        (void)fprintf(stderr, "Failed to resolve hostname '%s' to IPv4 address.\n", hostname);
        (void)fprintf(stderr, "Check that the hostname is valid and your DNS is working.\n");
        return ERROR_USAGE;
      }
      SAFE_SNPRINTF(opt_address, OPTIONS_BUFF_SIZE, "%s", resolved_ip);
      break;
    }

    case 'p': {
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "port", is_client);
      if (!value_str)
        return ERROR_USAGE;
      // Validate port is a number between 1 and 65535
      char *endptr;
      long port_num = strtol(value_str, &endptr, 10);
      if (*endptr != '\0' || value_str == endptr || port_num < 1 || port_num > 65535) {
        (void)fprintf(stderr, "Invalid port value '%s'. Port must be a number between 1 and 65535.\n", value_str);
        return ERROR_USAGE;
      }
      SAFE_SNPRINTF(opt_port, OPTIONS_BUFF_SIZE, "%s", value_str);
      break;
    }

    case 'x': {
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "width", is_client);
      if (!value_str)
        return ERROR_USAGE;
      int width_val = strtoint_safe(value_str);
      if (width_val == INT_MIN || width_val <= 0) {
        (void)fprintf(stderr, "Invalid width value '%s'. Width must be a positive integer.\n", value_str);
        return ERROR_USAGE;
      }
      opt_width = (unsigned short int)width_val;
      auto_width = false; // Mark as manually set
      break;
    }

    case 'y': {
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "height", is_client);
      if (!value_str)
        return ERROR_USAGE;
      int height_val = strtoint_safe(value_str);
      if (height_val == INT_MIN || height_val <= 0) {
        (void)fprintf(stderr, "Invalid height value '%s'. Height must be a positive integer.\n", value_str);
        return ERROR_USAGE;
      }
      opt_height = (unsigned short int)height_val;
      auto_height = false; // Mark as manually set
      break;
    }

    case 'c': {
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "webcam-index", is_client);
      if (!value_str)
        return ERROR_USAGE;
      int parsed_index = strtoint_safe(value_str);
      if (parsed_index == INT_MIN || parsed_index < 0) {
        (void)fprintf(stderr, "Invalid webcam index value '%s'. Webcam index must be a non-negative integer.\n",
                      value_str);
        return ERROR_USAGE;
      }
      opt_webcam_index = (unsigned short int)parsed_index;
      break;
    }

    case 'f': {
      // Webcam flip is now a binary flag - if present, toggle flip state
      opt_webcam_flip = !opt_webcam_flip;
      break;
    }

    case 1000: { // --color-mode
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "color-mode", is_client);
      if (!value_str)
        return ERROR_USAGE;
      if (strcmp(value_str, "auto") == 0) {
        opt_color_mode = COLOR_MODE_AUTO;
      } else if (strcmp(value_str, "mono") == 0 || strcmp(value_str, "monochrome") == 0) {
        opt_color_mode = COLOR_MODE_MONO;
      } else if (strcmp(value_str, "16") == 0 || strcmp(value_str, "16color") == 0) {
        opt_color_mode = COLOR_MODE_16_COLOR;
      } else if (strcmp(value_str, "256") == 0 || strcmp(value_str, "256color") == 0) {
        opt_color_mode = COLOR_MODE_256_COLOR;
      } else if (strcmp(value_str, "truecolor") == 0 || strcmp(value_str, "24bit") == 0) {
        opt_color_mode = COLOR_MODE_TRUECOLOR;
      } else {
        (void)fprintf(stderr, "Error: Invalid color mode '%s'. Valid modes: auto, mono, 16, 256, truecolor\n",
                      value_str);
        return ERROR_USAGE;
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
      if (!is_client) {
        (void)fprintf(stderr, "Error: --fps is a client-only option.\n");
        return ERROR_USAGE;
      }
      extern int g_max_fps; // From common.c
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "fps", is_client);
      if (!value_str)
        return ERROR_USAGE;
      int fps_val = strtoint_safe(value_str);
      if (fps_val == INT_MIN || fps_val < 1 || fps_val > 144) {
        (void)fprintf(stderr, "Invalid FPS value '%s'. FPS must be between 1 and 144.\n", value_str);
        return ERROR_USAGE;
      }
      g_max_fps = fps_val;
      break;
    }

    case 1004: { // --test-pattern (client only - use test pattern instead of webcam)
      if (!is_client) {
        (void)fprintf(stderr, "Error: --test-pattern is a client-only option.\n");
        return ERROR_USAGE;
      }
      opt_test_pattern = true;
      log_info("Using test pattern mode - webcam will not be opened");
      break;
    }

    case 'M': { // --render-mode
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "render-mode", is_client);
      if (!value_str)
        return ERROR_USAGE;
      if (strcmp(value_str, "foreground") == 0 || strcmp(value_str, "fg") == 0) {
        opt_render_mode = RENDER_MODE_FOREGROUND;
      } else if (strcmp(value_str, "background") == 0 || strcmp(value_str, "bg") == 0) {
        opt_render_mode = RENDER_MODE_BACKGROUND;
      } else if (strcmp(value_str, "half-block") == 0 || strcmp(value_str, "halfblock") == 0) {
        opt_render_mode = RENDER_MODE_HALF_BLOCK;
      } else {
        (void)fprintf(stderr, "Error: Invalid render mode '%s'. Valid modes: foreground, background, half-block\n",
                      value_str);
        return ERROR_USAGE;
      }
      break;
    }

    case 'P': { // --palette
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "palette", is_client);
      if (!value_str)
        return ERROR_USAGE;
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
        return ERROR_USAGE;
      }
      break;
    }

    case 'C': { // --palette-chars
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "palette-chars", is_client);
      if (!value_str)
        return ERROR_USAGE;
      if (strlen(value_str) >= sizeof(opt_palette_custom)) {
        (void)fprintf(stderr, "Invalid palette-chars: too long (%zu chars, max %zu)\n", strlen(value_str),
                      sizeof(opt_palette_custom) - 1);
        return ERROR_USAGE;
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

    case 1007: // --audio-device
      opt_audio_device = strtoint_safe(optarg);
      if (opt_audio_device < 0) {
        safe_fprintf(stderr, "Error: Invalid audio device index '%s'\n", optarg);
        return -1;
      }
      break;

    case 'q':
      opt_quiet = 1;
      break;

    case 'S':
      opt_snapshot_mode = 1;
      break;

    case 'D': {
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "snapshot-delay", is_client);
      if (!value_str)
        return ERROR_USAGE;
      char *endptr;
      opt_snapshot_delay = strtof(value_str, &endptr);
      if (*endptr != '\0' || value_str == endptr) {
        (void)fprintf(stderr, "Invalid snapshot delay value '%s'. Snapshot delay must be a number.\n", value_str);
        (void)fflush(stderr);
        return ERROR_USAGE;
      }
      if (opt_snapshot_delay < 0.0f) {
        (void)fprintf(stderr, "Snapshot delay must be non-negative (got %.2f)\n", (double)opt_snapshot_delay);
        (void)fflush(stderr);
        return ERROR_USAGE;
      }
      break;
    }

    case 'L': {
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "log-file", is_client);
      if (!value_str)
        return ERROR_USAGE;
      SAFE_SNPRINTF(opt_log_file, OPTIONS_BUFF_SIZE, "%s", value_str);
      break;
    }

    case 'E':
      opt_encrypt_enabled = 1;
      break;

    case 'K': {
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "key", is_client);
      if (!value_str)
        return ERROR_USAGE;

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
          return ERROR_USAGE;
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
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "keyfile", is_client);
      if (!value_str)
        return ERROR_USAGE;
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
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "server-key", is_client);
      if (!value_str)
        return ERROR_USAGE;
      SAFE_SNPRINTF(opt_server_key, OPTIONS_BUFF_SIZE, "%s", value_str);
      break;
    }

    case 1008: { // --client-keys (server only)
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "client-keys", is_client);
      if (!value_str)
        return ERROR_USAGE;
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
        if (platform_prompt_password("Enter password for encryption:", prompted_password, sizeof(prompted_password)) !=
            0) {
          (void)fprintf(stderr, "Error: Failed to read password\n");
          return ERROR_USAGE;
        }
        value_str = prompted_password;
        // Copy to argbuf so it persists beyond this scope
        SAFE_SNPRINTF(argbuf, sizeof(argbuf), "%s", prompted_password);
        value_str = argbuf;
        // Clear the prompted_password buffer for security
        memset(prompted_password, 0, sizeof(prompted_password));
      }

      // Validate password length requirements
      size_t password_len = strlen(value_str);
      if (password_len < MIN_PASSWORD_LENGTH) {
        (void)fprintf(stderr, "Error: Password too short (minimum %d characters, got %zu)\n", MIN_PASSWORD_LENGTH,
                      password_len);
        return ERROR_USAGE;
      }
      if (password_len > MAX_PASSWORD_LENGTH) {
        (void)fprintf(stderr, "Error: Password too long (maximum %d characters, got %zu)\n", MAX_PASSWORD_LENGTH,
                      password_len);
        return ERROR_USAGE;
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
              usage(stderr, is_client);
              return ERROR_USAGE;
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
        (void)fprintf(stderr, "%s: option '--%s' requires an argument\n", is_client ? "client" : "server", opt_name);
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
          (void)fprintf(stderr, "%s: option '--%s' requires an argument\n", is_client ? "client" : "server", long_name);
        } else {
          (void)fprintf(stderr, "%s: option '-%c' requires an argument\n", is_client ? "client" : "server", optopt);
        }
      }
      return ERROR_USAGE;

    case '?':
      // Handle unknown options - extract the actual option name from argv
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
            if (user_opt_len > 0 && user_opt_len < 256) {
              char unsupported_opt[256];
              SAFE_STRNCPY(unsupported_opt, user_opt, sizeof(unsupported_opt));
              unsupported_opt[user_opt_len] = '\0';
              option_name = unsupported_opt;
            } else {
              option_name = user_opt;
            }
          } else {
            option_name = user_opt;
          }
        } else if (user_input) {
          option_name = user_input;
        }
        safe_fprintf(stderr, "Unknown option '--%s'\n", option_name);
      } else {
        // Short option
        safe_fprintf(stderr, "Unknown option '-%c'\n", optopt);
      }
      usage(stderr, is_client);
      return ERROR_USAGE;

    case 'h':
      usage(stdout, is_client);
      (void)fflush(stdout);
      _exit(0);

    case 'v': {
      const char *binary_name = is_client ? "ascii-chat client" : "ascii-chat server";
      printf("%s v%d.%d.%d-%s (%s, %s)\n", binary_name, ASCII_CHAT_VERSION_MAJOR, ASCII_CHAT_VERSION_MINOR,
             ASCII_CHAT_VERSION_PATCH, ASCII_CHAT_GIT_VERSION, ASCII_CHAT_BUILD_DATE, ASCII_CHAT_BUILD_TYPE);
      (void)fflush(stdout);
      _exit(0);
    }

    default:
      abort();
    }
  }

  // After parsing command line options, update dimensions
  // First set any auto dimensions to terminal size, then apply full height logic
  update_dimensions_to_terminal_size();
  update_dimensions_for_full_height();

  return ASCIICHAT_OK;
}

#define USAGE_INDENT "        "

void usage_client(FILE *desc /* stdout|stderr*/) {
  (void)fprintf(desc, "ascii-chat - client options\n");
  (void)fprintf(desc, ASCII_CHAT_DESCRIPTION "\n\n");
  (void)fprintf(desc, USAGE_INDENT "-h --help                    " USAGE_INDENT "print this help\n");
  (void)fprintf(desc,
                USAGE_INDENT "-a --address ADDRESS         " USAGE_INDENT "server address (default: localhost)\n");
  (void)fprintf(desc, USAGE_INDENT "-H --host HOSTNAME           " USAGE_INDENT
                                   "hostname for DNS lookup (alternative to --address)\n");
  (void)fprintf(desc, USAGE_INDENT "-p --port PORT               " USAGE_INDENT "TCP port (default: 27224)\n");
  (void)fprintf(desc, USAGE_INDENT "-x --width WIDTH             " USAGE_INDENT "render width (default: [auto-set])\n");
  (void)fprintf(desc,
                USAGE_INDENT "-y --height HEIGHT           " USAGE_INDENT "render height (default: [auto-set])\n");
  (void)fprintf(desc, USAGE_INDENT "-c --webcam-index CAMERA     " USAGE_INDENT
                                   "webcam device index (0-based) (default: 0)\n");
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
                USAGE_INDENT "   --color-mode MODE         " USAGE_INDENT "color modes: auto, mono, 16, 256, truecolor "
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
  (void)fprintf(desc, USAGE_INDENT "-s --stretch                 " USAGE_INDENT "stretch or shrink video to fit "
                                   "(ignore aspect ratio) (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "-q --quiet                   " USAGE_INDENT
                                   "disable console logging (log only to file) (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "-S --snapshot                " USAGE_INDENT
                                   "capture single frame and exit (default: [unset])\n");
  (void)fprintf(
      desc, USAGE_INDENT "-D --snapshot-delay SECONDS  " USAGE_INDENT "delay SECONDS before snapshot (default: %.1f)\n",
      (double)SNAPSHOT_DELAY_DEFAULT);
  (void)fprintf(desc,
                USAGE_INDENT "-L --log-file FILE           " USAGE_INDENT "redirect logs to FILE (default: [unset])\n");
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

void usage_server(FILE *desc /* stdout|stderr*/) {
  (void)fprintf(desc, "ascii-chat - server options\n");
  (void)fprintf(desc, ASCII_CHAT_DESCRIPTION "\n\n");
  (void)fprintf(desc, USAGE_INDENT "-h --help            " USAGE_INDENT "print this help\n");
  (void)fprintf(desc,
                USAGE_INDENT "-a --address ADDRESS " USAGE_INDENT "IPv4 address to bind to (default: 127.0.0.1)\n");
  (void)fprintf(desc, USAGE_INDENT "    --address6 ADDR6 " USAGE_INDENT "IPv6 address to bind to (default: ::1)\n");
  (void)fprintf(desc, USAGE_INDENT "-p --port PORT       " USAGE_INDENT "TCP port to listen on (default: 27224)\n");
  (void)fprintf(desc, USAGE_INDENT "-P --palette PALETTE " USAGE_INDENT "ASCII character palette: "
                                   "standard, blocks, digital, minimal, cool, custom (default: standard)\n");
  (void)fprintf(desc, USAGE_INDENT "-C --palette-chars CHARS     "
                                   "Custom palette characters for --palette=custom (implies --palette=custom)\n");
  (void)fprintf(desc, USAGE_INDENT "-L --log-file FILE   " USAGE_INDENT "redirect logs to file (default: [unset])\n");
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
}

void usage(FILE *desc /* stdout|stderr*/, bool is_client) {
  if (is_client) {
    usage_client(desc);
  } else {
    usage_server(desc);
  }
}
