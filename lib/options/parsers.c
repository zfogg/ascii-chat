/**
 * @file parsers.c
 * @brief Custom option parsers implementation
 * @ingroup options
 */

#include "parsers.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stddef.h>
#include "common.h"
#include "options/options.h"

// Helper function to convert string to lowercase in-place (non-destructive)
static void to_lower(const char *src, char *dst, size_t max_len) {
  size_t i = 0;
  while (src[i] && i < max_len - 1) {
    dst[i] = (char)tolower((unsigned char)src[i]);
    i++;
  }
  dst[i] = '\0';
}

/**
 * @brief Validate if a string matches session string format
 *
 * Session strings must:
 * - Have length 1-47 characters
 * - Not start or end with hyphen
 * - Have exactly 2 hyphens (3 words)
 * - Only contain lowercase letters and hyphens
 * - No consecutive hyphens
 *
 * Examples: "swift-river-mountain", "quiet-forest-peak"
 *
 * @param str String to validate
 * @return true if valid session string format, false otherwise
 */
static bool is_session_string(const char *str) {
  if (!str) {
    return false;
  }

  size_t len = strlen(str);
  if (len == 0 || len > 47) {
    return false;
  }

  // Must not start or end with hyphen
  if (str[0] == '-' || str[len - 1] == '-') {
    return false;
  }

  // Count hyphens and validate characters
  int hyphen_count = 0;
  for (size_t i = 0; i < len; i++) {
    char c = str[i];
    if (c == '-') {
      hyphen_count++;
      // No consecutive hyphens
      if (i > 0 && str[i - 1] == '-') {
        return false;
      }
    } else if (!islower(c)) {
      // Only lowercase letters and hyphens allowed
      return false;
    }
  }

  // Must have exactly 2 hyphens (3 words)
  return hyphen_count == 2;
}

bool parse_color_mode(const char *arg, void *dest, char **error_msg) {
  if (!arg || !dest) {
    if (error_msg) {
      *error_msg = strdup("Internal error: NULL argument or destination");
    }
    return false;
  }

  terminal_color_mode_t *color_mode = (terminal_color_mode_t *)dest;
  char lower[32];
  to_lower(arg, lower, sizeof(lower));

  // Auto-detect
  if (strcmp(lower, "auto") == 0 || strcmp(lower, "a") == 0) {
    *color_mode = TERM_COLOR_AUTO;
    return true;
  }

  // Monochrome/None
  if (strcmp(lower, "none") == 0 || strcmp(lower, "mono") == 0 || strcmp(lower, "monochrome") == 0 ||
      strcmp(lower, "0") == 0) {
    *color_mode = TERM_COLOR_NONE;
    return true;
  }

  // 16-color
  if (strcmp(lower, "16") == 0 || strcmp(lower, "16color") == 0 || strcmp(lower, "ansi") == 0 ||
      strcmp(lower, "1") == 0) {
    *color_mode = TERM_COLOR_16;
    return true;
  }

  // 256-color
  if (strcmp(lower, "256") == 0 || strcmp(lower, "256color") == 0 || strcmp(lower, "2") == 0) {
    *color_mode = TERM_COLOR_256;
    return true;
  }

  // Truecolor
  if (strcmp(lower, "truecolor") == 0 || strcmp(lower, "true") == 0 || strcmp(lower, "tc") == 0 ||
      strcmp(lower, "rgb") == 0 || strcmp(lower, "24bit") == 0 || strcmp(lower, "3") == 0) {
    *color_mode = TERM_COLOR_TRUECOLOR;
    return true;
  }

  // Invalid value
  if (error_msg) {
    char *msg = SAFE_MALLOC(256, char *);
    if (msg) {
      snprintf(msg, 256, "Invalid color mode '%s'. Valid values: auto, none, 16, 256, truecolor", arg);
      *error_msg = msg;
    }
  }
  return false;
}

bool parse_render_mode(const char *arg, void *dest, char **error_msg) {
  if (!arg || !dest) {
    if (error_msg) {
      *error_msg = strdup("Internal error: NULL argument or destination");
    }
    return false;
  }

  render_mode_t *render_mode = (render_mode_t *)dest;
  char lower[32];
  to_lower(arg, lower, sizeof(lower));

  // Foreground mode
  if (strcmp(lower, "foreground") == 0 || strcmp(lower, "fg") == 0 || strcmp(lower, "0") == 0) {
    *render_mode = RENDER_MODE_FOREGROUND;
    return true;
  }

  // Background mode
  if (strcmp(lower, "background") == 0 || strcmp(lower, "bg") == 0 || strcmp(lower, "1") == 0) {
    *render_mode = RENDER_MODE_BACKGROUND;
    return true;
  }

  // Half-block mode
  if (strcmp(lower, "half-block") == 0 || strcmp(lower, "half") == 0 || strcmp(lower, "hb") == 0 ||
      strcmp(lower, "2") == 0) {
    *render_mode = RENDER_MODE_HALF_BLOCK;
    return true;
  }

  // Invalid value
  if (error_msg) {
    char *msg = SAFE_MALLOC(256, char *);
    if (msg) {
      snprintf(msg, 256, "Invalid render mode '%s'. Valid values: foreground, background, half-block", arg);
      *error_msg = msg;
    }
  }
  return false;
}

bool parse_palette_type(const char *arg, void *dest, char **error_msg) {
  if (!arg || !dest) {
    if (error_msg) {
      *error_msg = strdup("Internal error: NULL argument or destination");
    }
    return false;
  }

  palette_type_t *palette_type = (palette_type_t *)dest;
  char lower[32];
  to_lower(arg, lower, sizeof(lower));

  // Standard palette
  if (strcmp(lower, "standard") == 0 || strcmp(lower, "std") == 0 || strcmp(lower, "0") == 0) {
    *palette_type = PALETTE_STANDARD;
    return true;
  }

  // Blocks palette
  if (strcmp(lower, "blocks") == 0 || strcmp(lower, "block") == 0 || strcmp(lower, "1") == 0) {
    *palette_type = PALETTE_BLOCKS;
    return true;
  }

  // Digital palette
  if (strcmp(lower, "digital") == 0 || strcmp(lower, "dig") == 0 || strcmp(lower, "2") == 0) {
    *palette_type = PALETTE_DIGITAL;
    return true;
  }

  // Minimal palette
  if (strcmp(lower, "minimal") == 0 || strcmp(lower, "min") == 0 || strcmp(lower, "3") == 0) {
    *palette_type = PALETTE_MINIMAL;
    return true;
  }

  // Cool palette
  if (strcmp(lower, "cool") == 0 || strcmp(lower, "4") == 0) {
    *palette_type = PALETTE_COOL;
    return true;
  }

  // Custom palette
  if (strcmp(lower, "custom") == 0 || strcmp(lower, "5") == 0) {
    *palette_type = PALETTE_CUSTOM;
    return true;
  }

  // Invalid value
  if (error_msg) {
    char *msg = SAFE_MALLOC(256, char *);
    if (msg) {
      snprintf(msg, 256, "Invalid palette type '%s'. Valid values: standard, blocks, digital, minimal, cool, custom",
               arg);
      *error_msg = msg;
    }
  }
  return false;
}

bool parse_log_level(const char *arg, void *dest, char **error_msg) {
  if (!arg || !dest) {
    if (error_msg) {
      *error_msg = strdup("Internal error: NULL argument or destination");
    }
    return false;
  }

  log_level_t *log_level = (log_level_t *)dest;
  char lower[32];
  to_lower(arg, lower, sizeof(lower));

  // Development level
  if (strcmp(lower, "dev") == 0 || strcmp(lower, "development") == 0 || strcmp(lower, "0") == 0) {
    *log_level = LOG_DEV;
    return true;
  }

  // Debug level
  if (strcmp(lower, "debug") == 0 || strcmp(lower, "dbg") == 0 || strcmp(lower, "1") == 0) {
    *log_level = LOG_DEBUG;
    return true;
  }

  // Info level
  if (strcmp(lower, "info") == 0 || strcmp(lower, "information") == 0 || strcmp(lower, "2") == 0) {
    *log_level = LOG_INFO;
    return true;
  }

  // Warning level
  if (strcmp(lower, "warn") == 0 || strcmp(lower, "warning") == 0 || strcmp(lower, "3") == 0) {
    *log_level = LOG_WARN;
    return true;
  }

  // Error level
  if (strcmp(lower, "error") == 0 || strcmp(lower, "err") == 0 || strcmp(lower, "4") == 0) {
    *log_level = LOG_ERROR;
    return true;
  }

  // Fatal level
  if (strcmp(lower, "fatal") == 0 || strcmp(lower, "5") == 0) {
    *log_level = LOG_FATAL;
    return true;
  }

  // Invalid value
  if (error_msg) {
    char *msg = SAFE_MALLOC(256, char *);
    if (msg) {
      snprintf(msg, 256, "Invalid log level '%s'. Valid values: dev, debug, info, warn, error, fatal", arg);
      *error_msg = msg;
    }
  }
  return false;
}

// ============================================================================
// Positional Argument Parsers
// ============================================================================

#include "util/ip.h"
#include <string.h>

/**
 * @brief Parse server bind address positional argument
 *
 * Implements the server bind address parsing logic from server.c.
 * Can consume 1 argument per call, handling IPv4 or IPv6 bind addresses.
 * The positional arg system will call this multiple times for multiple args.
 */
int parse_server_bind_address(const char *arg, void *config, char **remaining, int num_remaining, char **error_msg) {
  if (!arg || !config) {
    if (error_msg) {
      *error_msg = strdup("Internal error: NULL argument or config");
    }
    return -1;
  }

  // Assume config struct has address and address6 fields (OPTIONS_BUFF_SIZE each)
  // This is a simplified version that assumes standard options_state layout
  char *address = (char *)config + offsetof(struct options_state, address);
  char *address6 = (char *)config + offsetof(struct options_state, address6);

  int consumed = 0;

  // Parse first argument (IPv4 or IPv6)
  char parsed_addr[OPTIONS_BUFF_SIZE];
  const char *addr_to_check = arg;
  if (parse_ipv6_address(arg, parsed_addr, sizeof(parsed_addr)) == 0) {
    addr_to_check = parsed_addr;
  }

  // Check if it's IPv4 or IPv6
  if (is_valid_ipv4(addr_to_check)) {
    // Check if we already have a non-default IPv4 address
    // Allow overwriting defaults (127.0.0.1, localhost, 0.0.0.0)
    if (address[0] != '\0' && strcmp(address, "127.0.0.1") != 0 && strcmp(address, "localhost") != 0 &&
        strcmp(address, "0.0.0.0") != 0) {
      if (error_msg) {
        char *msg = SAFE_MALLOC(256, char *);
        if (msg) {
          snprintf(msg, 256,
                   "Cannot specify multiple IPv4 addresses.\n"
                   "Already have: %s\n"
                   "Cannot add: %s",
                   address, addr_to_check);
          *error_msg = msg;
        }
      }
      return -1;
    }
    SAFE_SNPRINTF(address, OPTIONS_BUFF_SIZE, "%s", addr_to_check);
    consumed = 1;
  } else if (is_valid_ipv6(addr_to_check)) {
    // Check if we already have a non-default IPv6 address
    // Allow overwriting default (::1)
    if (address6[0] != '\0' && strcmp(address6, "::1") != 0) {
      if (error_msg) {
        char *msg = SAFE_MALLOC(256, char *);
        if (msg) {
          snprintf(msg, 256,
                   "Cannot specify multiple IPv6 addresses.\n"
                   "Already have: %s\n"
                   "Cannot add: %s",
                   address6, addr_to_check);
          *error_msg = msg;
        }
      }
      return -1;
    }
    SAFE_SNPRINTF(address6, OPTIONS_BUFF_SIZE, "%s", addr_to_check);
    consumed = 1;
  } else {
    if (error_msg) {
      char *msg = SAFE_MALLOC(512, char *);
      if (msg) {
        snprintf(msg, 512,
                 "Invalid IP address '%s'.\n"
                 "Server bind addresses must be valid IPv4 or IPv6 addresses.\n"
                 "Examples:\n"
                 "  ascii-chat server 0.0.0.0\n"
                 "  ascii-chat server ::1\n"
                 "  ascii-chat server 0.0.0.0 ::1",
                 arg);
        *error_msg = msg;
      }
    }
    return -1;
  }

  // Try to parse second address if available
  if (remaining && num_remaining > 0 && remaining[0]) {
    const char *second_arg = remaining[0];
    memset(parsed_addr, 0, sizeof(parsed_addr));
    addr_to_check = second_arg;
    if (parse_ipv6_address(second_arg, parsed_addr, sizeof(parsed_addr)) == 0) {
      addr_to_check = parsed_addr;
    }

    if (is_valid_ipv4(addr_to_check)) {
      // Second is IPv4
      if (address[0] != '\0' && strcmp(address, "127.0.0.1") != 0 && strcmp(address, "localhost") != 0 &&
          strcmp(address, "0.0.0.0") != 0) {
        // Already have an IPv4, can't add another
        return consumed;
      }
      if (is_valid_ipv4(arg)) {
        // First was also IPv4, can't have two IPv4s
        return consumed;
      }
      // First was IPv6, second is IPv4 - accept both
      SAFE_SNPRINTF(address, OPTIONS_BUFF_SIZE, "%s", addr_to_check);
      consumed = 2;
    } else if (is_valid_ipv6(addr_to_check)) {
      // Second is IPv6
      if (address6[0] != '\0' && strcmp(address6, "::1") != 0) {
        // Already have an IPv6, can't add another
        return consumed;
      }
      if (is_valid_ipv6(arg)) {
        // First was also IPv6, can't have two IPv6s
        return consumed;
      }
      // First was IPv4, second is IPv6 - accept both
      SAFE_SNPRINTF(address6, OPTIONS_BUFF_SIZE, "%s", addr_to_check);
      consumed = 2;
    }
  }

  return consumed;
}

/**
 * @brief Parse client address positional argument
 *
 * Implements the client address parsing logic from client.c.
 * Parses [address][:port] with complex IPv6 handling.
 */
int parse_client_address(const char *arg, void *config, char **remaining, int num_remaining, char **error_msg) {
  (void)remaining;
  (void)num_remaining;

  if (!arg || !config) {
    if (error_msg) {
      *error_msg = strdup("Internal error: NULL argument or config");
    }
    return -1;
  }

  // Check if this is a session string (format: adjective-noun-noun)
  // Session strings have exactly 2 hyphens, only lowercase letters, length 1-47
  if (is_session_string(arg)) {
    // This is a session string, not a server address
    char *session_string = (char *)config + offsetof(struct options_state, session_string);
    SAFE_SNPRINTF(session_string, 64, "%s", arg);
    log_debug("Detected session string: %s", arg);
    return 1; // Consumed 1 arg
  }

  // Not a session string, parse as server address
  // Access address and port fields from options_state struct
  char *address = (char *)config + offsetof(struct options_state, address);
  char *port = (char *)config + offsetof(struct options_state, port);

  // Check for port in address (format: address:port or [ipv6]:port)
  const char *colon = strrchr(arg, ':');

  if (colon != NULL) {
    // Check if this is IPv6 with port [::1]:port or plain hostname:port
    if (arg[0] == '[') {
      // IPv6 with brackets: [address]:port
      const char *closing_bracket = strchr(arg, ']');
      if (closing_bracket && closing_bracket < colon) {
        // Extract address (remove brackets)
        size_t addr_len = (size_t)(closing_bracket - arg - 1);
        if (addr_len >= OPTIONS_BUFF_SIZE) {
          if (error_msg) {
            *error_msg = strdup("IPv6 address too long");
          }
          return -1;
        }
        SAFE_SNPRINTF(address, OPTIONS_BUFF_SIZE, "%.*s", (int)addr_len, arg + 1);

        // Extract and validate port
        const char *port_str = colon + 1;
        char *endptr;
        long port_num = strtol(port_str, &endptr, 10);
        if (*endptr != '\0' || port_num < 1 || port_num > 65535) {
          if (error_msg) {
            char *msg = SAFE_MALLOC(256, char *);
            if (msg) {
              snprintf(msg, 256, "Invalid port number '%s'. Must be 1-65535.", port_str);
              *error_msg = msg;
            }
          }
          return -1;
        }
        SAFE_SNPRINTF(port, OPTIONS_BUFF_SIZE, "%s", port_str);
      }
    } else {
      // Check if it's IPv6 without brackets (no port allowed)
      // or hostname/IPv4:port
      size_t colon_count = 0;
      for (const char *p = arg; *p; p++) {
        if (*p == ':')
          colon_count++;
      }

      if (colon_count == 1) {
        // Likely hostname:port or IPv4:port
        size_t addr_len = (size_t)(colon - arg);
        if (addr_len >= OPTIONS_BUFF_SIZE) {
          if (error_msg) {
            *error_msg = strdup("Address too long");
          }
          return -1;
        }
        SAFE_SNPRINTF(address, OPTIONS_BUFF_SIZE, "%.*s", (int)addr_len, arg);

        // Extract and validate port
        const char *port_str = colon + 1;
        char *endptr;
        long port_num = strtol(port_str, &endptr, 10);
        if (*endptr != '\0' || port_num < 1 || port_num > 65535) {
          if (error_msg) {
            char *msg = SAFE_MALLOC(256, char *);
            if (msg) {
              snprintf(msg, 256, "Invalid port number '%s'. Must be 1-65535.", port_str);
              *error_msg = msg;
            }
          }
          return -1;
        }
        SAFE_SNPRINTF(port, OPTIONS_BUFF_SIZE, "%s", port_str);
      } else {
        // Multiple colons - likely bare IPv6 address
        SAFE_SNPRINTF(address, OPTIONS_BUFF_SIZE, "%s", arg);
      }
    }
  } else {
    // No colon - just an address
    SAFE_SNPRINTF(address, OPTIONS_BUFF_SIZE, "%s", arg);
  }

  // Validate addresses that contain dots as potential IPv4 addresses
  // If it has a dot, it's either a valid IPv4 or a hostname with domain
  bool has_dot = strchr(address, '.') != NULL;
  bool starts_with_digit = address[0] >= '0' && address[0] <= '9';

  if (has_dot && starts_with_digit) {
    // Looks like an IPv4 attempt - validate strictly
    if (!is_valid_ipv4(address)) {
      if (error_msg) {
        char *msg = SAFE_MALLOC(512, char *);
        if (msg) {
          snprintf(msg, 512,
                   "Invalid IPv4 address '%s'.\n"
                   "IPv4 addresses must have exactly 4 octets (0-255) separated by dots.\n"
                   "Examples: 127.0.0.1, 192.168.1.1\n"
                   "For hostnames, use letters: example.com, localhost",
                   address);
          *error_msg = msg;
        }
      }
      return -1;
    }
  }

  // Note: Port conflict checking would require additional state
  // (checking if --port flag was used). For now, this is a simplified version.
  // Full implementation would need to track whether port was set via flag.

  return 1; // Consumed 1 arg
}

// ============================================================================
// Palette Characters Parser
// ============================================================================

bool parse_palette_chars(const char *arg, void *dest, char **error_msg) {
  if (!arg || !dest) {
    if (error_msg) {
      *error_msg = strdup("Internal error: NULL argument or destination");
    }
    return false;
  }

  // The dest pointer points to the palette_custom field in options_t
  // We need to get the full options_t struct to call parse_palette_chars_option
  // Since we only have the field pointer, we need to handle this directly

  char *palette_custom = (char *)dest;

  if (strlen(arg) >= 256) {
    if (error_msg) {
      char *msg = SAFE_MALLOC(256, char *);
      if (msg) {
        snprintf(msg, 256, "Invalid palette-chars: too long (%zu chars, max 255)", strlen(arg));
        *error_msg = msg;
      }
    }
    return false;
  }

  // Copy the palette characters
  SAFE_STRNCPY(palette_custom, arg, 256);
  palette_custom[255] = '\0';

  // Also set the palette type to custom
  // Note: This is a simplification - ideally we'd have access to the full options_t struct
  // to set palette_custom_set and palette_type, but the callback interface doesn't provide that.
  // The palette_type should be handled separately or via a dependency.

  return true;
}
