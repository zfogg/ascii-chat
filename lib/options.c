#include "aspect_ratio.h"
#include "platform/system.h"
#ifdef _WIN32
#include "platform/windows/getopt.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <getopt.h>
#include <netdb.h>
#include <arpa/inet.h>
#endif
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

#include "image2ascii/ascii.h"
#include "options.h"
#include "common.h"
#include "platform/system.h"
#include "platform/terminal.h"
#include "version.h"

// Safely parse string to integer with validation
int strtoint_safe(const char *str) {
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
static int detect_default_ssh_key(char *key_path, size_t path_size) {
  const char *home_dir = platform_getenv("HOME");
  if (!home_dir) {
    // Fallback for Windows
    home_dir = platform_getenv("USERPROFILE");
  }

  if (!home_dir) {
    (void)fprintf(stderr, "Could not determine user home directory\n");
    return -1;
  }

  // Only support Ed25519 keys (modern, secure, fast)
  char full_path[1024];
  SAFE_SNPRINTF(full_path, sizeof(full_path), "%s/.ssh/id_ed25519", home_dir);

  // Check if the Ed25519 private key file exists
  struct stat st;
  if (stat(full_path, &st) == 0 && S_ISREG(st.st_mode)) {
    SAFE_SNPRINTF(key_path, path_size, "%s", full_path);
    log_debug("Found default SSH key: %s", full_path);
    return 0;
  }

  (void)fprintf(stderr, "No Ed25519 SSH key found at %s\n", full_path);
  (void)fprintf(stderr, "Only Ed25519 keys are supported (modern, secure, fast)\n");
  (void)fprintf(stderr, "Generate a new key with: ssh-keygen -t ed25519\n");
  return -1;
}

unsigned short int opt_width = OPT_WIDTH_DEFAULT, opt_height = OPT_HEIGHT_DEFAULT;
bool auto_width = true, auto_height = true;

char opt_address[OPTIONS_BUFF_SIZE] = "127.0.0.1", opt_port[OPTIONS_BUFF_SIZE] = "27224";

unsigned short int opt_webcam_index = 0;

bool opt_webcam_flip = true;

bool opt_test_pattern = false; // Use test pattern instead of real webcam

// Terminal color mode and capability options
terminal_color_mode_t opt_color_mode = COLOR_MODE_AUTO; // Auto-detect by default
render_mode_t opt_render_mode = RENDER_MODE_FOREGROUND; // Foreground by default
unsigned short int opt_show_capabilities = 0;           // Don't show capabilities by default
unsigned short int opt_force_utf8 = 0;                  // Don't force UTF-8 by default

unsigned short int opt_audio_enabled = 0;

// Allow stretching/shrinking without preserving aspect ratio when set via -s/--stretch
unsigned short int opt_stretch = 0;

// Disable console logging when set via -q/--quiet (logs only to file)
unsigned short int opt_quiet = 0;

// Enable snapshot mode when set via --snapshot (client only - capture one frame and exit)
unsigned short int opt_snapshot_mode = 0;

// Snapshot delay in seconds (float) - default 3.0 for webcam warmup
#if defined(__APPLE__)
// their macbook webcams shows pure black first then fade up into a real color image over a few seconds
#define SNAPSHOT_DELAY_DEFAULT 4.0f
#else
#define SNAPSHOT_DELAY_DEFAULT 3.0f
#endif
float opt_snapshot_delay = SNAPSHOT_DELAY_DEFAULT;

// Log file path for file logging (empty string means no file logging)
char opt_log_file[OPTIONS_BUFF_SIZE] = "";

// Encryption options
unsigned short int opt_encrypt_enabled = 0;       // Enable AES encryption via --encrypt
char opt_encrypt_key[OPTIONS_BUFF_SIZE] = "";     // SSH/GPG key file from --key (file-based only)
char opt_password[OPTIONS_BUFF_SIZE] = "";        // Password string from --password
char opt_encrypt_keyfile[OPTIONS_BUFF_SIZE] = ""; // Key file path from --keyfile

// New crypto options (Phase 2)
unsigned short int opt_no_encrypt = 0;        // Disable encryption (opt-out)
char opt_server_key[OPTIONS_BUFF_SIZE] = "";  // Expected server public key (client only)
char opt_client_keys[OPTIONS_BUFF_SIZE] = ""; // Allowed client keys (server only)

// Palette options
palette_type_t opt_palette_type = PALETTE_STANDARD; // Default to standard palette
char opt_palette_custom[256] = "";                  // Custom palette characters
bool opt_palette_custom_set = false;                // True if custom palette was set

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
                                         {"stretch", no_argument, NULL, 's'},
                                         {"quiet", no_argument, NULL, 'q'},
                                         {"snapshot", no_argument, NULL, 'S'},
                                         {"snapshot-delay", required_argument, NULL, 'D'},
                                         {"log-file", required_argument, NULL, 'L'},
                                         {"encrypt", no_argument, NULL, 'E'},
                                         {"key", required_argument, NULL, 'K'},
                                         {"password", required_argument, NULL, 1009},
                                         {"keyfile", required_argument, NULL, 'F'},
                                         {"no-encrypt", no_argument, NULL, 1005},
                                         {"server-key", required_argument, NULL, 1006},
                                         {"version", no_argument, NULL, 'v'},
                                         {"help", optional_argument, NULL, 'h'},
                                         {0, 0, 0, 0}};

// Server-only options
static struct option server_options[] = {{"address", required_argument, NULL, 'a'},
                                         {"port", required_argument, NULL, 'p'},
                                         {"palette", required_argument, NULL, 'P'},
                                         {"palette-chars", required_argument, NULL, 'C'},
                                         {"audio", no_argument, NULL, 'A'},
                                         {"log-file", required_argument, NULL, 'L'},
                                         {"encrypt", no_argument, NULL, 'E'},
                                         {"key", required_argument, NULL, 'K'},
                                         {"password", required_argument, NULL, 1009},
                                         {"keyfile", required_argument, NULL, 'F'},
                                         {"no-encrypt", no_argument, NULL, 1005},
                                         {"client-keys", required_argument, NULL, 1008},
                                         {"version", no_argument, NULL, 'v'},
                                         {"help", optional_argument, NULL, 'h'},
                                         {0, 0, 0, 0}};

// Terminal size detection functions moved to terminal_detect.c

void update_dimensions_for_full_height(void) {
  unsigned short int term_width, term_height;

  if (get_terminal_size(&term_width, &term_height) == 0) {
    log_debug("Terminal size detected: %dx%d, auto_width=%d, auto_height=%d", term_width, term_height, (int)auto_width,
              (int)auto_height);
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
  }
}

void update_dimensions_to_terminal_size(void) {
  unsigned short int term_width, term_height;
  // Get current terminal size (get_terminal_size already handles ioctl first, then $COLUMNS/$LINES fallback)
  int terminal_result = get_terminal_size(&term_width, &term_height);
  if (terminal_result == 0) {
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

// Helper function to strip equals sign from optarg if present
static char *strip_equals_prefix(const char *optarg, char *buffer, size_t buffer_size) {
  if (!optarg)
    return NULL;

  SAFE_SNPRINTF(buffer, buffer_size, "%s", optarg);
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
static char *get_required_argument(const char *optarg, char *buffer, size_t buffer_size, const char *option_name,
                                   bool is_client) {
  // Check if optarg is NULL or empty
  if (!optarg || strlen(optarg) == 0) {
    goto error;
  }

  // Check if getopt_long returned the option name itself as the argument
  // This happens when a long option requiring an argument is at the end of argv
  if (optarg && option_name && strcmp(optarg, option_name) == 0) {
    goto error;
  }

  // Process the argument normally
  char *value_str = strip_equals_prefix(optarg, buffer, buffer_size);
  if (!value_str) {
    goto error;
  }

  return value_str;

error:
  (void)fprintf(stderr, "%s: option '--%s' requires an argument\n", is_client ? "client" : "server", option_name);
  (void)fflush(stderr);
  return NULL; // Signal error to caller
}

// Helper function to validate IPv4 address format
static int is_valid_ipv4(const char *ip) {
  if (!ip)
    return 0;

  // Check for leading or trailing dots
  if (ip[0] == '.' || ip[strlen(ip) - 1] == '.')
    return 0;

  // Check for consecutive dots
  for (size_t i = 0; i < strlen(ip) - 1; i++) {
    if (ip[i] == '.' && ip[i + 1] == '.')
      return 0;
  }

  // int octets[4];
  int count = 0;
  char temp[15 + 1]; // Maximum IPv4 length is 15 characters + null terminator

  // Copy to temp buffer to avoid modifying original
  if (strlen(ip) >= sizeof(temp))
    return 0;
  SAFE_STRNCPY(temp, ip, sizeof(temp));
  temp[sizeof(temp) - 1] = '\0';

  char *saveptr;
  char *token = platform_strtok_r(temp, ".", &saveptr);
  while (token != NULL && count < 4) {
    char *endptr;
    long octet = strtol(token, &endptr, 10);

    // Check if conversion was successful and entire token was consumed
    if (*endptr != '\0' || token == endptr)
      return 0;

    // Check octet range (0-255)
    if (octet < 0 || octet > 255)
      return 0;

    // octets[count] = (int)octet;
    token = platform_strtok_r(NULL, ".", &saveptr);
    count++; // Increment count for each valid octet
  }

  // Must have exactly 4 octets and no remaining tokens
  return (count == 4 && token == NULL);
}

int options_init(int argc, char **argv, bool is_client) {
  // Parse arguments first, then update dimensions (moved below)

  // Set different default addresses for client vs server
  if (is_client) {
    // Client connects to localhost by default
    SAFE_SNPRINTF(opt_address, OPTIONS_BUFF_SIZE, "127.0.0.1");
  } else {
    // Server binds to all interfaces by default
    SAFE_SNPRINTF(opt_address, OPTIONS_BUFF_SIZE, "0.0.0.0");
  }

  // Use different option sets for client vs server
  const char *optstring;
  struct option *options;

  if (is_client) {
    optstring = ":a:H:p:x:y:c:fM:P:C:AsqSD:L:EK:F:hv"; // Leading ':' for error reporting
    options = client_options;
  } else {
    optstring = ":a:p:P:C:AL:EK:F:hv"; // Leading ':' for error reporting
    options = server_options;
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
      break;

    case 'a': {
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "address", is_client);
      if (!value_str)
        return ASCIICHAT_ERROR_USAGE;
      if (!is_valid_ipv4(value_str)) {
        // Try to resolve hostname to IPv4
        char resolved_ip[OPTIONS_BUFF_SIZE];
        if (platform_resolve_hostname_to_ipv4(value_str, resolved_ip, sizeof(resolved_ip)) != 0) {
          (void)fprintf(stderr, "Failed to resolve hostname '%s' to IPv4 address.\n", value_str);
          (void)fprintf(stderr, "Check that the hostname is valid and your DNS is working.\n");
          return ASCIICHAT_ERROR_USAGE;
        }
        SAFE_SNPRINTF(opt_address, OPTIONS_BUFF_SIZE, "%s", resolved_ip);
      } else {
        SAFE_SNPRINTF(opt_address, OPTIONS_BUFF_SIZE, "%s", value_str);
      }
      break;
    }

    case 'H': { // --host (DNS lookup)
      char *hostname = get_required_argument(optarg, argbuf, sizeof(argbuf), "host", is_client);
      if (!hostname)
        return ASCIICHAT_ERROR_USAGE;
      char resolved_ip[OPTIONS_BUFF_SIZE];
      if (platform_resolve_hostname_to_ipv4(hostname, resolved_ip, sizeof(resolved_ip)) != 0) {
        (void)fprintf(stderr, "Failed to resolve hostname '%s' to IPv4 address.\n", hostname);
        (void)fprintf(stderr, "Check that the hostname is valid and your DNS is working.\n");
        return ASCIICHAT_ERROR_USAGE;
      }
      SAFE_SNPRINTF(opt_address, OPTIONS_BUFF_SIZE, "%s", resolved_ip);
      break;
    }

    case 'p': {
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "port", is_client);
      if (!value_str)
        return ASCIICHAT_ERROR_USAGE;
      // Validate port is a number between 1 and 65535
      char *endptr;
      long port_num = strtol(value_str, &endptr, 10);
      if (*endptr != '\0' || value_str == endptr || port_num < 1 || port_num > 65535) {
        (void)fprintf(stderr, "Invalid port value '%s'. Port must be a number between 1 and 65535.\n", value_str);
        return ASCIICHAT_ERROR_USAGE;
      }
      SAFE_SNPRINTF(opt_port, OPTIONS_BUFF_SIZE, "%s", value_str);
      break;
    }

    case 'x': {
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "width", is_client);
      if (!value_str)
        return ASCIICHAT_ERROR_USAGE;
      int width_val = strtoint_safe(value_str);
      if (width_val == INT_MIN || width_val <= 0) {
        (void)fprintf(stderr, "Invalid width value '%s'. Width must be a positive integer.\n", value_str);
        return ASCIICHAT_ERROR_USAGE;
      }
      opt_width = (unsigned short int)width_val;
      auto_width = false; // Mark as manually set
      break;
    }

    case 'y': {
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "height", is_client);
      if (!value_str)
        return ASCIICHAT_ERROR_USAGE;
      int height_val = strtoint_safe(value_str);
      if (height_val == INT_MIN || height_val <= 0) {
        (void)fprintf(stderr, "Invalid height value '%s'. Height must be a positive integer.\n", value_str);
        return ASCIICHAT_ERROR_USAGE;
      }
      opt_height = (unsigned short int)height_val;
      auto_height = false; // Mark as manually set
      break;
    }

    case 'c': {
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "webcam-index", is_client);
      if (!value_str)
        return ASCIICHAT_ERROR_USAGE;
      int parsed_index = strtoint_safe(value_str);
      if (parsed_index == INT_MIN || parsed_index < 0) {
        (void)fprintf(stderr, "Invalid webcam index value '%s'. Webcam index must be a non-negative integer.\n",
                      value_str);
        return ASCIICHAT_ERROR_USAGE;
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
        return ASCIICHAT_ERROR_USAGE;
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
        return ASCIICHAT_ERROR_USAGE;
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
        return ASCIICHAT_ERROR_USAGE;
      }
      extern int g_max_fps; // From common.c
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "fps", is_client);
      if (!value_str)
        return ASCIICHAT_ERROR_USAGE;
      int fps_val = strtoint_safe(value_str);
      if (fps_val == INT_MIN || fps_val < 1 || fps_val > 144) {
        (void)fprintf(stderr, "Invalid FPS value '%s'. FPS must be between 1 and 144.\n", value_str);
        return ASCIICHAT_ERROR_USAGE;
      }
      g_max_fps = fps_val;
      break;
    }

    case 1004: { // --test-pattern (client only - use test pattern instead of webcam)
      if (!is_client) {
        (void)fprintf(stderr, "Error: --test-pattern is a client-only option.\n");
        return ASCIICHAT_ERROR_USAGE;
      }
      opt_test_pattern = true;
      log_info("Using test pattern mode - webcam will not be opened");
      break;
    }

    case 'M': { // --render-mode
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "render-mode", is_client);
      if (!value_str)
        return ASCIICHAT_ERROR_USAGE;
      if (strcmp(value_str, "foreground") == 0 || strcmp(value_str, "fg") == 0) {
        opt_render_mode = RENDER_MODE_FOREGROUND;
      } else if (strcmp(value_str, "background") == 0 || strcmp(value_str, "bg") == 0) {
        opt_render_mode = RENDER_MODE_BACKGROUND;
      } else if (strcmp(value_str, "half-block") == 0 || strcmp(value_str, "halfblock") == 0) {
        opt_render_mode = RENDER_MODE_HALF_BLOCK;
      } else {
        (void)fprintf(stderr, "Error: Invalid render mode '%s'. Valid modes: foreground, background, half-block\n",
                      value_str);
        return ASCIICHAT_ERROR_USAGE;
      }
      break;
    }

    case 'P': { // --palette
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "palette", is_client);
      if (!value_str)
        return ASCIICHAT_ERROR_USAGE;
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
        return ASCIICHAT_ERROR_USAGE;
      }
      break;
    }

    case 'C': { // --palette-chars
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "palette-chars", is_client);
      if (!value_str)
        return ASCIICHAT_ERROR_USAGE;
      if (strlen(value_str) >= sizeof(opt_palette_custom)) {
        (void)fprintf(stderr, "Invalid palette-chars: too long (%zu chars, max %zu)\n", strlen(value_str),
                      sizeof(opt_palette_custom) - 1);
        return ASCIICHAT_ERROR_USAGE;
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

    case 'q':
      opt_quiet = 1;
      break;

    case 'S':
      opt_snapshot_mode = 1;
      break;

    case 'D': {
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "snapshot-delay", is_client);
      if (!value_str)
        return ASCIICHAT_ERROR_USAGE;
      char *endptr;
      opt_snapshot_delay = strtof(value_str, &endptr);
      if (*endptr != '\0' || value_str == endptr) {
        (void)fprintf(stderr, "Invalid snapshot delay value '%s'. Snapshot delay must be a number.\n", value_str);
        (void)fflush(stderr);
        return ASCIICHAT_ERROR_USAGE;
      }
      if (opt_snapshot_delay < 0.0f) {
        (void)fprintf(stderr, "Snapshot delay must be non-negative (got %.2f)\n", opt_snapshot_delay);
        (void)fflush(stderr);
        return ASCIICHAT_ERROR_USAGE;
      }
      break;
    }

    case 'L': {
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "log-file", is_client);
      if (!value_str)
        return ASCIICHAT_ERROR_USAGE;
      SAFE_SNPRINTF(opt_log_file, OPTIONS_BUFF_SIZE, "%s", value_str);
      break;
    }

    case 'E':
      opt_encrypt_enabled = 1;
      break;

    case 'K': {
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "key", is_client);
      if (!value_str)
        return ASCIICHAT_ERROR_USAGE;

      // --key is for file-based authentication only (SSH keys, GPG keys, GitHub/GitLab)
      // For password-based encryption, use --password instead

      // Check if it's a GPG key (gpg:keyid format)
      if (strncmp(value_str, "gpg:", 4) == 0) {
        SAFE_SNPRINTF(opt_encrypt_key, OPTIONS_BUFF_SIZE, "%s", value_str);
        opt_encrypt_enabled = 1;
      }
      // Check if it's a GitHub key (github:username format)
      else if (strncmp(value_str, "github:", 7) == 0) {
        SAFE_SNPRINTF(opt_encrypt_key, OPTIONS_BUFF_SIZE, "%s", value_str);
        opt_encrypt_enabled = 1;
      }
      // Check if it's a GitLab key (gitlab:username format)
      else if (strncmp(value_str, "gitlab:", 7) == 0) {
        SAFE_SNPRINTF(opt_encrypt_key, OPTIONS_BUFF_SIZE, "%s", value_str);
        opt_encrypt_enabled = 1;
      }
      // Check if it's "ssh" or "ssh:" to auto-detect SSH key
      else if (strcmp(value_str, "ssh") == 0 || strcmp(value_str, "ssh:") == 0) {
        char default_key[OPTIONS_BUFF_SIZE];
        if (detect_default_ssh_key(default_key, sizeof(default_key)) == 0) {
          SAFE_SNPRINTF(opt_encrypt_key, OPTIONS_BUFF_SIZE, "%s", default_key);
          opt_encrypt_enabled = 1;
        } else {
          (void)fprintf(stderr, "No Ed25519 SSH key found for auto-detection\n");
          (void)fprintf(stderr, "Please specify a key with --key /path/to/key\n");
          (void)fprintf(stderr, "Or generate a new key with: ssh-keygen -t ed25519\n");
          return ASCIICHAT_ERROR_USAGE;
        }
      }
      // Otherwise, treat as a file path - will be validated later for existence/permissions
      else {
        // Treat as SSH key file path - will be validated later for existence/permissions
        SAFE_SNPRINTF(opt_encrypt_key, OPTIONS_BUFF_SIZE, "%s", value_str);
        opt_encrypt_enabled = 1;
      }
      break;
    }

    case 'F': {
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "keyfile", is_client);
      if (!value_str)
        return ASCIICHAT_ERROR_USAGE;
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
        return ASCIICHAT_ERROR_USAGE;
      SAFE_SNPRINTF(opt_server_key, OPTIONS_BUFF_SIZE, "%s", value_str);
      break;
    }

    case 1008: { // --client-keys (server only)
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "client-keys", is_client);
      if (!value_str)
        return ASCIICHAT_ERROR_USAGE;
      SAFE_SNPRINTF(opt_client_keys, OPTIONS_BUFF_SIZE, "%s", value_str);
      break;
    }

    case 1009: { // --password (password-based encryption)
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "password", is_client);
      if (!value_str)
        return ASCIICHAT_ERROR_USAGE;
      SAFE_SNPRINTF(opt_password, OPTIONS_BUFF_SIZE, "%s", value_str);
      opt_encrypt_enabled = 1; // Auto-enable encryption when password provided
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
              snprintf(abbreviated_opt, sizeof(abbreviated_opt), "%.*s", (int)user_opt_len, user_opt);
              fprintf(stderr, "Unknown option '--%s'\n", abbreviated_opt);
              usage(stderr, is_client);
              return ASCIICHAT_ERROR_USAGE;
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
              size_t len = eq - opt_name;
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
      return ASCIICHAT_ERROR_USAGE;

    case '?':
      (void)fprintf(stderr, "Unknown option %c\n", optopt);
      usage(stderr, is_client);
      return ASCIICHAT_ERROR_USAGE;

    case 'h':
      usage(stdout, is_client);
      exit(0);

    case 'v': {
      const char *binary_name = is_client ? "ascii-chat-client" : "ascii-chat-server";
      printf("%s v%d.%d.%d-%s (%s)\n", binary_name, ASCII_CHAT_VERSION_MAJOR, ASCII_CHAT_VERSION_MINOR,
             ASCII_CHAT_VERSION_PATCH, ASCII_CHAT_GIT_VERSION, ASCII_CHAT_BUILD_TYPE);
      exit(0);
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

#define USAGE_INDENT "    "

void usage_client(FILE *desc /* stdout|stderr*/) {
  (void)fprintf(desc, "ascii-chat - client options\n");
  (void)fprintf(desc, USAGE_INDENT "-h --help                    " USAGE_INDENT "print this help\n");
  (void)fprintf(desc, USAGE_INDENT "-v --version                 " USAGE_INDENT "show version information\n");
  (void)fprintf(desc, USAGE_INDENT "-a --address ADDRESS         " USAGE_INDENT "IPv4 address (default: 127.0.0.1)\n");
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
  (void)fprintf(desc, USAGE_INDENT "   --utf8                    " USAGE_INDENT "force enable UTF-8/Unicode support\n");
  (void)fprintf(desc, USAGE_INDENT "-M --render-mode MODE        " USAGE_INDENT "Rendering modes: "
                                   "foreground, background, half-block (default: foreground)\n");
  (void)fprintf(desc, USAGE_INDENT "-P --palette PALETTE         " USAGE_INDENT "ASCII character palette: "
                                   "standard, blocks, digital, minimal, cool, custom (default: standard)\n");
  (void)fprintf(desc, USAGE_INDENT "-C --palette-chars CHARS     " USAGE_INDENT
                                   "Custom palette characters for --palette=custom "
                                   "(implies --palette=custom)\n");
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
      SNAPSHOT_DELAY_DEFAULT);
  (void)fprintf(desc,
                USAGE_INDENT "-L --log-file FILE           " USAGE_INDENT "redirect logs to FILE (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "-E --encrypt                 " USAGE_INDENT
                                   "enable packet encryption (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "-K --key KEY                  " USAGE_INDENT
                                   "SSH/GPG key file for authentication: /path/to/key, gpg:keyid, github:user, "
                                   "gitlab:user, or 'ssh' for auto-detect "
                                   "(implies --encrypt) (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "   --password PASS            " USAGE_INDENT
                                   "password for connection encryption (implies --encrypt) (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "-F --keyfile FILE            " USAGE_INDENT "read encryption key from FILE "
                                   "(implies --encrypt) (default: [unset])\n");
  (void)fprintf(desc,
                USAGE_INDENT "   --no-encrypt               " USAGE_INDENT "disable encryption (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "   --server-key KEY           " USAGE_INDENT
                                   "expected server public key for verification (default: [unset])\n");
}

void usage_server(FILE *desc /* stdout|stderr*/) {
  (void)fprintf(desc, "ascii-chat - server options\n");
  (void)fprintf(desc, USAGE_INDENT "-h --help            " USAGE_INDENT "print this help\n");
  (void)fprintf(desc, USAGE_INDENT "-v --version         " USAGE_INDENT "show version information\n");
  (void)fprintf(desc, USAGE_INDENT "-a --address ADDRESS " USAGE_INDENT "IPv4 address to bind to (default: 0.0.0.0)\n");
  (void)fprintf(desc, USAGE_INDENT "-p --port PORT       " USAGE_INDENT "TCP port to listen on (default: 27224)\n");
  (void)fprintf(desc, USAGE_INDENT "-P --palette PALETTE " USAGE_INDENT "ASCII character palette: "
                                   "standard, blocks, digital, minimal, cool, custom (default: standard)\n");
  (void)fprintf(desc,
                USAGE_INDENT "-C --palette-chars CHARS " USAGE_INDENT "Custom palette characters for --palette=custom "
                             "(implies --palette=custom)\n");
  (void)fprintf(desc, USAGE_INDENT "-A --audio           " USAGE_INDENT
                                   "enable audio streaming to clients (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "-L --log-file FILE   " USAGE_INDENT "redirect logs to file (default: [unset])\n");
  (void)fprintf(desc,
                USAGE_INDENT "-E --encrypt         " USAGE_INDENT "enable packet encryption (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT
                "-K --key KEY         " USAGE_INDENT
                "SSH/GPG key file for authentication: /path/to/key, gpg:keyid, github:user, gitlab:user, or 'ssh' "
                "(implies --encrypt) (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "   --password PASS   " USAGE_INDENT
                                   "password for connection encryption (implies --encrypt) (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "-F --keyfile FILE    " USAGE_INDENT "read encryption key from file "
                                   "(implies --encrypt) (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "   --no-encrypt       " USAGE_INDENT "disable encryption (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "   --client-keys FILE  " USAGE_INDENT
                                   "allowed client keys file for authentication (default: [unset])\n");
}

void usage(FILE *desc /* stdout|stderr*/, bool is_client) {
  if (is_client) {
    usage_client(desc);
  } else {
    usage_server(desc);
  }
}
