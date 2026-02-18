/**
 * @file parsers.c
 * @brief Custom option parsers implementation
 * @ingroup options
 */

#include <ascii-chat/options/parsers.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stddef.h>
#include <ascii-chat/common.h>
#include <ascii-chat/platform/util.h>
#include <ascii-chat/options/builder.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/strings.h>    // For fuzzy matching suggestions
#include <ascii-chat/discovery/strings.h>  // For is_session_string() validation
#include <ascii-chat/util/parsing.h>       // For parse_port() validation
#include <ascii-chat/util/path.h>          // For path_validate_user_path()
#include <ascii-chat/util/pcre2.h>         // For centralized PCRE2 singleton
#include <ascii-chat/video/color_filter.h> // For color_filter_from_cli_name()
#include <pcre2.h>

// Helper function to convert string to lowercase in-place (non-destructive)
static void to_lower(const char *src, char *dst, size_t max_len) {
  size_t i = 0;
  while (src[i] && i < max_len - 1) {
    dst[i] = (char)tolower((unsigned char)src[i]);
    i++;
  }
  dst[i] = '\0';
}

// ═══════════════════════════════════════════════════════════════════════════
// PCRE2 REGEX-BASED SETTING PARSER
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief PCRE2 regex validator for enum setting parsing
 *
 * Validates setting strings like "auto", "true", "false" with case-insensitive
 * matching. Uses centralized PCRE2 singleton for thread-safe compilation.
 */

static const char *SETTING_PATTERN = "^(auto|a|0|true|yes|1|on|enabled|enable|false|no|-1|off|disabled|disable)$";

static pcre2_singleton_t *g_setting_regex = NULL;

/**
 * Get compiled setting regex (lazy initialization)
 * Returns NULL if compilation failed
 */
static pcre2_code *setting_regex_get(void) {
  if (g_setting_regex == NULL) {
    g_setting_regex = asciichat_pcre2_singleton_compile(SETTING_PATTERN, 0);
  }
  return asciichat_pcre2_singleton_get_code(g_setting_regex);
}

/**
 * @brief Lookup table for setting string-to-enum mapping
 */
typedef struct {
  const char *match; ///< Setting string (lowercased)
  int enum_value;    ///< Corresponding enum value
} setting_map_entry_t;

/**
 * @brief Generic setting parser using PCRE2 singleton and lookup table
 *
 * Validates string against regex and maps to enum value via lookup table.
 * Falls back to linear search if PCRE2 unavailable.
 *
 * @param arg Input setting string (e.g., "auto", "TRUE", "1")
 * @param dest Pointer to int destination for enum value
 * @param lookup_table Lookup table mapping strings to enum values
 * @param error_msg Error message destination (can be NULL)
 * @return true if valid setting, false otherwise
 */
static bool parse_setting_generic(const char *arg, void *dest, const setting_map_entry_t *lookup_table,
                                  char **error_msg) {
  if (!dest || !lookup_table) {
    if (error_msg) {
      *error_msg = platform_strdup("Internal error: NULL destination or lookup table");
    }
    return false;
  }

  int *result = (int *)dest;

  // Handle optional argument - default to first entry in table (usually "auto")
  if (!arg || arg[0] == '\0') {
    *result = lookup_table[0].enum_value;
    return true;
  }

  // Convert to lowercase
  char lower[32];
  to_lower(arg, lower, sizeof(lower));

  // Get compiled regex (lazy initialization)
  pcre2_code *regex = setting_regex_get();
  if (!regex) {
    if (error_msg) {
      *error_msg = platform_strdup("Internal error: PCRE2 regex not available");
    }
    return false;
  }

  pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(regex, NULL);
  if (!match_data) {
    if (error_msg) {
      *error_msg = platform_strdup("Internal error: Failed to allocate match data");
    }
    return false;
  }

  int rc = pcre2_jit_match(regex, (PCRE2_SPTR8)lower, strlen(lower), 0, 0, match_data, NULL);
  pcre2_match_data_free(match_data);

  if (rc < 0) {
    // No match - invalid setting
    if (error_msg) {
      *error_msg = platform_strdup("Invalid setting value");
    }
    return false;
  }

  // Linear search through lookup table to get enum value
  for (int i = 0; lookup_table[i].match != NULL; i++) {
    if (strcmp(lower, lookup_table[i].match) == 0) {
      *result = lookup_table[i].enum_value;
      return true;
    }
  }

  // Should not reach here if regex validated correctly
  if (error_msg) {
    *error_msg = platform_strdup("Internal error: Regex matched but lookup failed");
  }
  return false;
}

// NOTE: is_session_string() is now imported from lib/discovery/strings.h
// and provides enhanced validation against actual wordlists via hashtable lookup.
// See that module for the full implementation.

// Lookup table for color setting strings
static const setting_map_entry_t g_color_setting_map[] = {
    {"auto", COLOR_SETTING_AUTO},     {"a", COLOR_SETTING_AUTO},
    {"0", COLOR_SETTING_AUTO},        {"true", COLOR_SETTING_TRUE},
    {"yes", COLOR_SETTING_TRUE},      {"1", COLOR_SETTING_TRUE},
    {"on", COLOR_SETTING_TRUE},       {"enabled", COLOR_SETTING_TRUE},
    {"enable", COLOR_SETTING_TRUE},   {"false", COLOR_SETTING_FALSE},
    {"no", COLOR_SETTING_FALSE},      {"-1", COLOR_SETTING_FALSE},
    {"off", COLOR_SETTING_FALSE},     {"disabled", COLOR_SETTING_FALSE},
    {"disable", COLOR_SETTING_FALSE}, {NULL, 0} // Sentinel
};

bool parse_color_setting(const char *arg, void *dest, char **error_msg) {
  if (!dest) {
    if (error_msg) {
      *error_msg = platform_strdup("Internal error: NULL destination");
    }
    return false;
  }

  // Use generic parser with color setting lookup table
  // Default to TRUE if no arg provided
  if (!arg || arg[0] == '\0') {
    int *color_setting = (int *)dest;
    *color_setting = COLOR_SETTING_TRUE;
    return true;
  }

  return parse_setting_generic(arg, dest, g_color_setting_map, error_msg);
}

// Lookup table for UTF-8 setting strings (identical to color setting for boolean patterns)
static const setting_map_entry_t g_utf8_setting_map[] = {
    {"auto", UTF8_SETTING_AUTO},     {"a", UTF8_SETTING_AUTO},
    {"0", UTF8_SETTING_AUTO},        {"true", UTF8_SETTING_TRUE},
    {"yes", UTF8_SETTING_TRUE},      {"1", UTF8_SETTING_TRUE},
    {"on", UTF8_SETTING_TRUE},       {"enabled", UTF8_SETTING_TRUE},
    {"enable", UTF8_SETTING_TRUE},   {"false", UTF8_SETTING_FALSE},
    {"no", UTF8_SETTING_FALSE},      {"-1", UTF8_SETTING_FALSE},
    {"off", UTF8_SETTING_FALSE},     {"disabled", UTF8_SETTING_FALSE},
    {"disable", UTF8_SETTING_FALSE}, {NULL, 0} // Sentinel
};

bool parse_utf8_setting(const char *arg, void *dest, char **error_msg) {
  if (!dest) {
    if (error_msg) {
      *error_msg = platform_strdup("Internal error: NULL destination");
    }
    return false;
  }

  // Use generic parser with UTF-8 setting lookup table
  // Default to TRUE if no arg provided
  if (!arg || arg[0] == '\0') {
    int *utf8_setting = (int *)dest;
    *utf8_setting = UTF8_SETTING_TRUE;
    return true;
  }

  return parse_setting_generic(arg, dest, g_utf8_setting_map, error_msg);
}

bool parse_color_mode(const char *arg, void *dest, char **error_msg) {
  if (!arg || !dest) {
    if (error_msg) {
      *error_msg = platform_strdup("Internal error: NULL argument or destination");
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

  // Invalid value - suggest closest match
  if (error_msg) {
    char msg[256];
    const char *suggestion = asciichat_suggest_enum_value("color-mode", arg);
    if (suggestion) {
      safe_snprintf(msg, sizeof(msg), "Invalid color mode '%s'. Did you mean '%s'?", arg, suggestion);
    } else {
      safe_snprintf(msg, sizeof(msg), "Invalid color mode '%s'. Valid values: auto, none, 16, 256, truecolor", arg);
    }
    *error_msg = platform_strdup(msg);
  }
  return false;
}

bool parse_color_filter(const char *arg, void *dest, char **error_msg) {
  if (!arg || !dest) {
    if (error_msg) {
      *error_msg = platform_strdup("Internal error: NULL argument or destination");
    }
    return false;
  }

  color_filter_t *color_filter = (color_filter_t *)dest;
  char lower[32];
  to_lower(arg, lower, sizeof(lower));

  // Try to match against all known color filters
  *color_filter = color_filter_from_cli_name(lower);
  if (*color_filter != COLOR_FILTER_NONE || strcmp(lower, "none") == 0) {
    return true;
  }

  // Invalid value
  if (error_msg) {
    char msg[256];
    safe_snprintf(msg, sizeof(msg),
                  "Invalid color filter '%s'. Valid values: none, black, white, green, magenta, fuchsia, "
                  "orange, teal, cyan, pink, red, yellow",
                  arg);
    *error_msg = platform_strdup(msg);
  }
  return false;
}

bool parse_render_mode(const char *arg, void *dest, char **error_msg) {
  if (!arg || !dest) {
    if (error_msg) {
      *error_msg = platform_strdup("Internal error: NULL argument or destination");
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
  if (strcmp(lower, "half-block") == 0 || strcmp(lower, "hb") == 0 || strcmp(lower, "2") == 0) {
    *render_mode = RENDER_MODE_HALF_BLOCK;
    return true;
  }

  // Invalid value - suggest closest match
  if (error_msg) {
    char msg[256];
    const char *suggestion = asciichat_suggest_enum_value("render-mode", arg);
    if (suggestion) {
      safe_snprintf(msg, sizeof(msg), "Invalid render mode '%s'. Did you mean '%s'?", arg, suggestion);
    } else {
      safe_snprintf(msg, sizeof(msg), "Invalid render mode '%s'. Valid values: foreground, background, half-block",
                    arg);
    }
    *error_msg = platform_strdup(msg);
  }
  return false;
}

bool parse_palette_type(const char *arg, void *dest, char **error_msg) {
  if (!arg || !dest) {
    if (error_msg) {
      *error_msg = platform_strdup("Internal error: NULL argument or destination");
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

  // Invalid value - suggest closest match
  if (error_msg) {
    char msg[256];
    const char *suggestion = asciichat_suggest_enum_value("palette", arg);
    if (suggestion) {
      safe_snprintf(msg, sizeof(msg), "Invalid palette type '%s'. Did you mean '%s'?", arg, suggestion);
    } else {
      safe_snprintf(msg, sizeof(msg),
                    "Invalid palette type '%s'. Valid values: standard, blocks, digital, minimal, cool, custom", arg);
    }
    *error_msg = platform_strdup(msg);
  }
  return false;
}

bool parse_log_level(const char *arg, void *dest, char **error_msg) {
  if (!dest) {
    if (error_msg) {
      *error_msg = platform_strdup("Internal error: NULL destination");
    }
    return false;
  }

  log_level_t *log_level = (log_level_t *)dest;

  // If no argument provided, use the default log level (based on build type)
  if (!arg || arg[0] == '\0') {
    *log_level = DEFAULT_LOG_LEVEL;
    return true;
  }

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
    char msg[256];
    safe_snprintf(msg, sizeof(msg), "Invalid log level '%s'. Valid values: dev, debug, info, warn, error, fatal", arg);
    *error_msg = platform_strdup(msg);
  }
  return false;
}

bool parse_port_option(const char *arg, void *dest, char **error_msg) {
  if (!arg || !dest) {
    if (error_msg) {
      *error_msg = platform_strdup("Internal error: NULL argument or destination");
    }
    return false;
  }

  int *port_value = (int *)dest;
  uint16_t port_num;

  // Use the existing parse_port function for validation
  asciichat_error_t err = parse_port(arg, &port_num);
  if (err != ASCIICHAT_OK) {
    if (error_msg) {
      char msg[256];
      safe_snprintf(msg, sizeof(msg), "Invalid port '%s'. Port must be a number between 1 and 65535.", arg);
      *error_msg = platform_strdup(msg);
    }
    return false;
  }

  *port_value = (int)port_num;
  return true;
}

// ============================================================================
// Positional Argument Parsers
// ============================================================================

#include <ascii-chat/util/ip.h>
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
      *error_msg = platform_strdup("Internal error: NULL argument or config");
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
    // Allow overwriting defaults (localhost, 0.0.0.0)
    if (address[0] != '\0' && !is_localhost_ipv4(address) && strcmp(address, "localhost") != 0 &&
        strcmp(address, "0.0.0.0") != 0) {
      if (error_msg) {
        char msg[256];
        safe_snprintf(msg, sizeof(msg),
                      "Cannot specify multiple IPv4 addresses.\n"
                      "Already have: %s\n"
                      "Cannot add: %s",
                      address, addr_to_check);
        *error_msg = platform_strdup(msg);
      }
      return -1;
    }
    SAFE_SNPRINTF(address, OPTIONS_BUFF_SIZE, "%s", addr_to_check);
    consumed = 1;
  } else if (is_valid_ipv6(addr_to_check)) {
    // Check if we already have a non-default IPv6 address
    // Allow overwriting default (::1)
    if (address6[0] != '\0' && !is_localhost_ipv6(address6)) {
      if (error_msg) {
        char msg[256];
        safe_snprintf(msg, sizeof(msg),
                      "Cannot specify multiple IPv6 addresses.\n"
                      "Already have: %s\n"
                      "Cannot add: %s",
                      address6, addr_to_check);
        *error_msg = platform_strdup(msg);
      }
      return -1;
    }
    SAFE_SNPRINTF(address6, OPTIONS_BUFF_SIZE, "%s", addr_to_check);
    consumed = 1;
  } else {
    if (error_msg) {
      char msg[512];
      safe_snprintf(msg, sizeof(msg),
                    "Invalid IP address '%s'.\n"
                    "Server bind addresses must be valid IPv4 or IPv6 addresses.\n"
                    "Examples:\n"
                    "  ascii-chat server 0.0.0.0\n"
                    "  ascii-chat server ::1\n"
                    "  ascii-chat server 0.0.0.0 ::1",
                    arg);
      *error_msg = platform_strdup(msg);
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
      if (address[0] != '\0' && !is_localhost_ipv4(address) && strcmp(address, "localhost") != 0 &&
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
      if (address6[0] != '\0' && !is_localhost_ipv6(address6)) {
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
      *error_msg = platform_strdup("Internal error: NULL argument or config");
    }
    return -1;
  }

  log_debug("parse_client_address: Processing argument: '%s'", arg);

  // Access address and port fields from options_state struct
  char *address = (char *)config + offsetof(struct options_state, address);
  int *port = (int *)((char *)config + offsetof(struct options_state, port));

  // Check for WebSocket URL (ws:// or wss://) FIRST before session string validation
  // WebSocket URLs are passed through without validation or port extraction
  if (strncmp(arg, "ws://", 5) == 0 || strncmp(arg, "wss://", 6) == 0) {
    log_debug("Detected WebSocket URL: %s", arg);
    SAFE_SNPRINTF(address, OPTIONS_BUFF_SIZE, "%s", arg);
    // Don't set port - WebSocket transport handles URL parsing internally
    return 1; // Consumed 1 argument
  }

  // Check if this is a session string (format: adjective-noun-noun)
  // Session strings have exactly 2 hyphens, only lowercase letters, length 5-47
  bool is_session = is_session_string(arg);
  log_debug("parse_client_address: is_session_string('%s') = %s", arg, is_session ? "true" : "false");

  if (is_session) {
    // This is a session string, not a server address
    char *session_string = (char *)config + offsetof(struct options_state, session_string);
    SAFE_SNPRINTF(session_string, SESSION_STRING_BUFFER_SIZE, "%s", arg);
    log_debug("parse_client_address: Stored session string: %s", arg);
    return 1; // Consumed 1 arg
  }

  // Not a session string, parse as server address
  log_debug("parse_client_address: Parsing as server address (not a session string)");

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
            *error_msg = platform_strdup("IPv6 address too long");
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
            char msg[256];
            safe_snprintf(msg, sizeof(msg), "Invalid port number '%s'. Must be 1-65535.", port_str);
            *error_msg = platform_strdup(msg);
          }
          return -1;
        }
        *port = (int)port_num;
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
            *error_msg = platform_strdup("Address too long");
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
            char msg[256];
            safe_snprintf(msg, sizeof(msg), "Invalid port number '%s'. Must be 1-65535.", port_str);
            *error_msg = platform_strdup(msg);
          }
          return -1;
        }
        *port = (int)port_num;
      } else {
        // Multiple colons - likely bare IPv6 address
        SAFE_SNPRINTF(address, OPTIONS_BUFF_SIZE, "%s", arg);
      }
    }
  } else {
    // No colon - just an address
    SAFE_SNPRINTF(address, OPTIONS_BUFF_SIZE, "%s", arg);
  }

  // Validate addresses using comprehensive IPv4/IPv6 detection
  bool has_dot = strchr(address, '.') != NULL;
  bool has_colon = strchr(address, ':') != NULL;
  bool starts_with_digit = address[0] >= '0' && address[0] <= '9';

  // Potential IPv6 address (has colons) - validate as IPv6
  if (has_colon) {
    if (!is_valid_ipv6(address)) {
      if (error_msg) {
        char msg[512];
        safe_snprintf(msg, sizeof(msg),
                      "Invalid IPv6 address '%s'.\n"
                      "IPv6 addresses must be valid hex notation with colons.\n"
                      "Examples: ::1, 2001:db8::1, fe80::1\n"
                      "Or use hostnames like example.com",
                      address);
        *error_msg = platform_strdup(msg);
      }
      return -1;
    }
  } else if (has_dot && starts_with_digit) {
    // Potential IPv4 address (has dots and starts with digit) - validate strictly
    if (!is_valid_ipv4(address)) {
      if (error_msg) {
        char msg[512];
        safe_snprintf(msg, sizeof(msg),
                      "Invalid IPv4 address '%s'.\n"
                      "IPv4 addresses must have exactly 4 octets (0-255) separated by dots.\n"
                      "Examples: 127.0.0.1, 192.168.1.1\n"
                      "For hostnames, use letters: example.com, localhost",
                      address);
        *error_msg = platform_strdup(msg);
      }
      return -1;
    }
  }
  // Otherwise treat as valid hostname (no validation needed)

  // Note: Port conflict checking would require additional state
  // (checking if --port flag was used). For now, this is a simplified version.
  // Full implementation would need to track whether port was set via flag.

  log_debug("parse_client_address: Set address='%s', port=%d", address[0] ? address : "(empty)", *port);

  return 1; // Consumed 1 arg
}

// ============================================================================
// Palette Characters Parser
// ============================================================================

bool parse_palette_chars(const char *arg, void *dest, char **error_msg) {
  if (!arg || !dest) {
    if (error_msg) {
      *error_msg = platform_strdup("Internal error: NULL argument or destination");
    }
    return false;
  }

  // The dest pointer points to the palette_custom field in options_t
  // We need to get the full options_t struct to call parse_palette_chars_option
  // Since we only have the field pointer, we need to handle this directly

  char *palette_custom = (char *)dest;

  size_t len = strlen(arg);
  if (len == 0) {
    if (error_msg) {
      *error_msg = platform_strdup("Invalid palette-chars: value cannot be empty");
    }
    return false;
  }

  if (len >= 256) {
    if (error_msg) {
      char msg[256];
      safe_snprintf(msg, sizeof(msg), "Invalid palette-chars: too long (%zu chars, max 255)", len);
      *error_msg = platform_strdup(msg);
    }
    return false;
  }

  // Copy the palette characters
  SAFE_STRNCPY(palette_custom, arg, 256);
  palette_custom[255] = '\0';

  // Also set the palette type to custom by calculating back to options_t pointer
  // dest points to options_t.palette_custom, so we can get options_t* using offset arithmetic
  options_t *opts = (options_t *)((char *)dest - offsetof(options_t, palette_custom));
  opts->palette_type = PALETTE_CUSTOM;

  return true;
}

bool parse_verbose_flag(const char *arg, void *dest, char **error_msg) {
  (void)error_msg; // Unused but required by function signature

  // If arg is NULL or starts with a flag, just increment
  // Otherwise try to parse as integer count
  unsigned short int *verbose_level = (unsigned short int *)dest;

  if (!arg || arg[0] == '\0') {
    // No argument provided, just increment
    (*verbose_level)++;
    return true;
  }

  // Try to parse as integer count
  char *endptr;
  long value = strtol(arg, &endptr, 10);
  if (*endptr == '\0' && value >= 0 && value <= 100) {
    *verbose_level = (unsigned short int)value;
    return true;
  }

  // If it didn't parse as int, treat as flag increment
  (*verbose_level)++;
  return true;
}

/**
 * @brief Custom parser for --seek flag
 *
 * Accepts both "hh:mm:ss.ms" format and plain seconds format.
 * Examples:
 * - "30" = 30 seconds
 * - "30.5" = 30.5 seconds
 * - "1:30" = 1 minute 30 seconds (90 seconds)
 * - "1:30.5" = 1 minute 30.5 seconds (90.5 seconds)
 * - "0:1:30.5" = 1 minute 30.5 seconds (90.5 seconds)
 * - "1:2:30.5" = 1 hour 2 minutes 30.5 seconds (3750.5 seconds)
 */
bool parse_timestamp(const char *arg, void *dest, char **error_msg) {
  if (!arg || arg[0] == '\0') {
    if (error_msg) {
      *error_msg = platform_strdup("--seek requires a timestamp argument");
    }
    return false;
  }

  double *timestamp = (double *)dest;
  char *endptr;
  long strtol_result;

  // Count colons to determine format
  int colon_count = 0;
  for (const char *p = arg; *p; p++) {
    if (*p == ':')
      colon_count++;
  }

  if (colon_count == 0) {
    // Plain seconds format: "30" or "30.5"
    *timestamp = strtod(arg, &endptr);
    if (*endptr != '\0' || *timestamp < 0.0) {
      if (error_msg) {
        *error_msg = platform_strdup("Invalid timestamp: expected non-negative seconds");
      }
      return false;
    }
    return true;
  } else if (colon_count == 1) {
    // MM:SS or MM:SS.ms format
    strtol_result = strtol(arg, &endptr, 10);
    if (*endptr != ':' || strtol_result < 0) {
      if (error_msg) {
        *error_msg = platform_strdup("Invalid timestamp: expected MM:SS or MM:SS.ms format");
      }
      return false;
    }
    long minutes = strtol_result;
    double seconds = strtod(endptr + 1, &endptr);
    if (*endptr != '\0' && *endptr != '.' && *endptr != '\0') {
      if (error_msg) {
        *error_msg = platform_strdup("Invalid timestamp: expected MM:SS or MM:SS.ms format");
      }
      return false;
    }
    *timestamp = minutes * 60.0 + seconds;
    return true;
  } else if (colon_count == 2) {
    // HH:MM:SS or HH:MM:SS.ms format
    strtol_result = strtol(arg, &endptr, 10);
    if (*endptr != ':' || strtol_result < 0) {
      if (error_msg) {
        *error_msg = platform_strdup("Invalid timestamp: expected HH:MM:SS or HH:MM:SS.ms format");
      }
      return false;
    }
    long hours = strtol_result;

    strtol_result = strtol(endptr + 1, &endptr, 10);
    if (*endptr != ':' || strtol_result < 0 || strtol_result >= 60) {
      if (error_msg) {
        *error_msg = platform_strdup("Invalid timestamp: minutes must be 0-59");
      }
      return false;
    }
    long minutes = strtol_result;

    double seconds = strtod(endptr + 1, &endptr);
    if (*endptr != '\0') {
      if (error_msg) {
        *error_msg = platform_strdup("Invalid timestamp: expected HH:MM:SS or HH:MM:SS.ms format");
      }
      return false;
    }
    *timestamp = hours * 3600.0 + minutes * 60.0 + seconds;
    return true;
  } else {
    if (error_msg) {
      *error_msg = platform_strdup("Invalid timestamp format: too many colons");
    }
    return false;
  }
}

bool parse_volume(const char *arg, void *dest, char **error_msg) {
  if (!arg || !dest) {
    if (error_msg) {
      *error_msg = platform_strdup("Internal error: NULL argument or destination");
    }
    return false;
  }

  float *volume = (float *)dest;
  char *endptr;
  float val = strtof(arg, &endptr);

  if (*endptr != '\0' || arg == endptr) {
    if (error_msg) {
      *error_msg = platform_strdup("Invalid volume value. Must be a number between 0.0 and 1.0");
    }
    return false;
  }

  if (val < 0.0f || val > 1.0f) {
    if (error_msg) {
      char buf[256];
      SAFE_SNPRINTF(buf, sizeof(buf), "Volume must be between 0.0 and 1.0 (got %.2f)", val);
      *error_msg = platform_strdup(buf);
    }
    return false;
  }

  *volume = val;
  return true;
}

bool parse_log_file(const char *arg, void *dest, char **error_msg) {
  if (!arg || !dest) {
    if (error_msg) {
      *error_msg = platform_strdup("Internal error: NULL argument or destination");
    }
    return false;
  }

  // Validate and normalize the log file path
  char *normalized = NULL;
  asciichat_error_t result = path_validate_user_path(arg, PATH_ROLE_LOG_FILE, &normalized);

  if (result != ASCIICHAT_OK) {
    if (error_msg) {
      asciichat_error_context_t err_ctx;
      if (HAS_ERRNO(&err_ctx)) {
        *error_msg = platform_strdup(err_ctx.context_message);
      } else {
        *error_msg = platform_strdup("Log file path validation failed");
      }
    }
    return false;
  }

  // Copy validated path to destination
  char *log_file_buf = (char *)dest;
  const size_t max_size = 256;
  SAFE_STRNCPY(log_file_buf, normalized, max_size - 1);
  log_file_buf[max_size - 1] = '\0';

  SAFE_FREE(normalized);
  return true;
}

bool parse_audio_source(const char *arg, void *dest, char **error_msg) {
  if (!arg || !dest) {
    if (error_msg) {
      *error_msg = platform_strdup("Internal error: NULL argument or destination");
    }
    return false;
  }

  audio_source_t *audio_source = (audio_source_t *)dest;
  char lower[32];
  to_lower(arg, lower, sizeof(lower));

  // Auto (smart selection based on media state)
  if (strcmp(lower, "auto") == 0) {
    *audio_source = AUDIO_SOURCE_AUTO;
    return true;
  }

  // Microphone only
  if (strcmp(lower, "mic") == 0) {
    *audio_source = AUDIO_SOURCE_MIC;
    return true;
  }

  // Media only
  if (strcmp(lower, "media") == 0) {
    *audio_source = AUDIO_SOURCE_MEDIA;
    return true;
  }

  // Both microphone and media
  if (strcmp(lower, "both") == 0) {
    *audio_source = AUDIO_SOURCE_BOTH;
    return true;
  }

  if (error_msg) {
    *error_msg = platform_strdup("Audio source must be 'auto', 'mic', 'media', or 'both'");
  }
  return false;
}
