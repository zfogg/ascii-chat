/**
 * @file options/validation.c
 * @ingroup options_validation
 * @brief Implementation of options validation functions
 */

#include <ascii-chat/options/validation.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <ascii-chat/common.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/util.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/util/parsing.h>
#include <ascii-chat/video/palette.h>
#include <ascii-chat/options/options.h>

// ============================================================================
// Generic Integer Range Validator
// ============================================================================

/**
 * @brief Generic validator for integers within a range
 *
 * Used by multiple specific validators (FPS, max clients, compression level, etc.)
 * to eliminate code duplication.
 *
 * @param value_str String to validate
 * @param min Minimum allowed value (inclusive)
 * @param max Maximum allowed value (inclusive)
 * @param param_name Parameter name for error messages
 * @param error_msg Buffer for error message
 * @param error_msg_size Size of error message buffer
 * @return Parsed integer value on success, INT_MIN on error
 */
static int validate_int_range(const char *value_str, int min, int max, const char *param_name, char *error_msg,
                              size_t error_msg_size) {
  if (!value_str || strlen(value_str) == 0) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "%s value is required", param_name);
    }
    return INT_MIN;
  }

  int val = strtoint_safe(value_str);
  if (val == INT_MIN || val < min || val > max) {
    if (error_msg) {
      if (min == max) {
        SAFE_SNPRINTF(error_msg, error_msg_size, "Invalid %s '%s'. Must be exactly %d.", param_name, value_str, min);
      } else if (min == 1 && max == INT_MAX) {
        SAFE_SNPRINTF(error_msg, error_msg_size, "Invalid %s '%s'. Must be a positive integer.", param_name, value_str);
      } else if (min == 0 && max == INT_MAX) {
        SAFE_SNPRINTF(error_msg, error_msg_size, "Invalid %s '%s'. Must be a non-negative integer.", param_name,
                      value_str);
      } else {
        SAFE_SNPRINTF(error_msg, error_msg_size, "Invalid %s '%s'. Must be between %d and %d.", param_name, value_str,
                      min, max);
      }
    }
    return INT_MIN;
  }
  return val;
}

/**
 * Validate port number (1-65535)
 * Returns 0 on success, non-zero on error
 */
int validate_opt_port(const char *value_str, char *error_msg, size_t error_msg_size) {
  if (!value_str || strlen(value_str) == 0) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Port value is required");
    }
    return -1;
  }

  // Use safe integer parsing with range validation
  uint16_t port_num;
  if (parse_port(value_str, &port_num) != ASCIICHAT_OK) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Invalid port value '%s'. Port must be a number between 1 and 65535.",
                    value_str);
    }
    return -1;
  }
  return 0;
}

/**
 * Validate port option callback (matches option_descriptor validate signature)
 * Used during options parsing to validate port values with PCRE2
 */
bool validate_port_callback(const void *options_struct, char **error_msg) {
  const options_t *opts = (const options_t *)options_struct;

  // Port is stored as an int, but we need to validate it as a string
  // The issue is that strtol() already accepted invalid formats like " 80", "+80", "0123"
  // So we can't validate after parsing - we need to prevent the parse in the first place
  // This callback runs AFTER parsing, so it's too late

  // Instead, check if the port value is in range (this will catch the range but not format issues)
  if (opts->port < 1 || opts->port > 65535) {
    if (error_msg) {
      *error_msg = strdup("Port must be between 1 and 65535");
    }
    return false;
  }

  return true;
}

/**
 * Validate positive integer
 * Returns parsed value on success, -1 on error
 */
int validate_opt_positive_int(const char *value_str, char *error_msg, size_t error_msg_size) {
  int result = validate_int_range(value_str, 1, INT_MAX, "Value", error_msg, error_msg_size);
  return (result == INT_MIN) ? -1 : result;
}

/**
 * Validate non-negative integer
 * Returns parsed value on success, -1 on error
 */
int validate_opt_non_negative_int(const char *value_str, char *error_msg, size_t error_msg_size) {
  int result = validate_int_range(value_str, 0, INT_MAX, "Value", error_msg, error_msg_size);
  return (result == INT_MIN) ? -1 : result;
}

/**
 * Validate color mode string
 * Returns parsed color mode on success, -1 on error
 */
int validate_opt_color_mode(const char *value_str, char *error_msg, size_t error_msg_size) {
  if (!value_str) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Color mode value is required");
    }
    return -1;
  }

  if (strcmp(value_str, "auto") == 0) {
    return COLOR_MODE_AUTO;
  }
  if (strcmp(value_str, "none") == 0 || strcmp(value_str, "mono") == 0) {
    return COLOR_MODE_NONE;
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
    SAFE_SNPRINTF(error_msg, error_msg_size,
                  "Invalid color mode '%s'. Valid modes: auto, none, mono, 16, 256, truecolor", value_str);
  }
  return -1;
}

/**
 * Validate render mode string
 * Returns parsed render mode on success, -1 on error
 */
int validate_opt_render_mode(const char *value_str, char *error_msg, size_t error_msg_size) {
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
int validate_opt_palette(const char *value_str, char *error_msg, size_t error_msg_size) {
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
 * Validate log level string
 * Returns parsed log level on success, -1 on error
 */
int validate_opt_log_level(const char *value_str, char *error_msg, size_t error_msg_size) {
  if (!value_str) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Log level value is required");
    }
    return -1;
  }

  if (platform_strcasecmp(value_str, "dev") == 0) {
    return LOG_DEV;
  } else if (platform_strcasecmp(value_str, "debug") == 0) {
    return LOG_DEBUG;
  } else if (platform_strcasecmp(value_str, "info") == 0) {
    return LOG_INFO;
  } else if (platform_strcasecmp(value_str, "warn") == 0) {
    return LOG_WARN;
  } else if (platform_strcasecmp(value_str, "error") == 0) {
    return LOG_ERROR;
  } else if (platform_strcasecmp(value_str, "fatal") == 0) {
    return LOG_FATAL;
  } else {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size,
                    "Invalid log level '%s'. Valid levels: dev, debug, info, warn, error, fatal", value_str);
    }
    return -1;
  }
}

/**
 * Validate IP address or hostname
 * Returns 0 on success, -1 on error
 * Sets parsed_address on success (resolved if hostname)
 */
int validate_opt_ip_address(const char *value_str, char *parsed_address, size_t address_size, bool is_client,
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
float validate_opt_float_non_negative(const char *value_str, char *error_msg, size_t error_msg_size) {
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
 * Validate volume value (0.0-1.0)
 * Returns parsed value on success, -1.0f on error
 */
float validate_opt_volume(const char *value_str, char *error_msg, size_t error_msg_size) {
  if (!value_str || strlen(value_str) == 0) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Volume value is required");
    }
    return -1.0f;
  }

  char *endptr;
  float val = strtof(value_str, &endptr);
  if (*endptr != '\0' || value_str == endptr) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Invalid volume value '%s'. Must be a number.", value_str);
    }
    return -1.0f;
  }
  if (val < 0.0f || val > 1.0f) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Volume must be between 0.0 and 1.0 (got %.2f)", val);
    }
    return -1.0f;
  }
  return val;
}

/**
 * Validate max clients (1-32)
 * Returns parsed value on success, -1 on error
 */
int validate_opt_max_clients(const char *value_str, char *error_msg, size_t error_msg_size) {
  int result = validate_int_range(value_str, 1, 32, "Max clients", error_msg, error_msg_size);
  return (result == INT_MIN) ? -1 : result;
}

/**
 * Validate compression level (1-9)
 * Returns parsed value on success, -1 on error
 */
int validate_opt_compression_level(const char *value_str, char *error_msg, size_t error_msg_size) {
  int result = validate_int_range(value_str, 1, 9, "Compression level", error_msg, error_msg_size);
  return (result == INT_MIN) ? -1 : result;
}

/**
 * Validate FPS value (1-144)
 * Returns parsed value on success, -1 on error
 */
int validate_opt_fps(const char *value_str, char *error_msg, size_t error_msg_size) {
  int result = validate_int_range(value_str, 1, 144, "FPS", error_msg, error_msg_size);
  return (result == INT_MIN) ? -1 : result;
}

/**
 * Validate reconnect value (off, auto, 0, -1, or 1-999)
 * Returns:
 *   0 for "off" (no retries)
 *  -1 for "auto" (unlimited retries)
 *   1-999 for specific retry count
 *  INT_MIN on parse error
 */
int validate_opt_reconnect(const char *value_str, char *error_msg, size_t error_msg_size) {
  if (!value_str || strlen(value_str) == 0) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Reconnect value is required");
    }
    return INT_MIN;
  }

  // Check for string values first
  if (platform_strcasecmp(value_str, "off") == 0) {
    return 0; // No retries
  }
  if (platform_strcasecmp(value_str, "auto") == 0) {
    return -1; // Unlimited retries
  }

  // Parse as integer
  int val = strtoint_safe(value_str);
  if (val == INT_MIN) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Invalid reconnect value '%s'. Use 'off', 'auto', or a number 0-999.",
                    value_str);
    }
    return INT_MIN;
  }

  // 0 means off, -1 means auto, 1-999 is valid range
  if (val == 0) {
    return 0; // No retries
  }
  if (val == -1) {
    return -1; // Unlimited retries
  }
  if (val < 1 || val > 999) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Invalid reconnect count '%s'. Must be 'off', 'auto', or 1-999.",
                    value_str);
    }
    return INT_MIN;
  }
  return val;
}

/**
 * Validate device index (-1 for default, or 0+ for specific device)
 * Returns parsed value on success, INT_MIN on error
 */
int validate_opt_device_index(const char *value_str, char *error_msg, size_t error_msg_size) {
  if (!value_str || strlen(value_str) == 0) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Device index value is required");
    }
    return INT_MIN;
  }

  int index = strtoint_safe(value_str);
  if (index == INT_MIN) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size,
                    "Invalid device index '%s'. Must be -1 (default) or a non-negative integer.", value_str);
    }
    return INT_MIN;
  }

  // -1 is valid (system default), otherwise must be >= 0
  if (index < -1) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size,
                    "Invalid device index '%d'. Must be -1 (default) or a non-negative integer.", index);
    }
    return INT_MIN;
  }
  return index;
}

/**
 * Validate password (8-256 characters, no null bytes)
 * Returns 0 on success, -1 on error
 */
int validate_opt_password(const char *value_str, char *error_msg, size_t error_msg_size) {
  if (!value_str) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Password value is required");
    }
    return -1;
  }

  size_t len = strlen(value_str);
  if (len < 8) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Password too short (%zu chars). Must be at least 8 characters.", len);
    }
    return -1;
  }
  if (len > 256) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Password too long (%zu chars). Must be at most 256 characters.", len);
    }
    return -1;
  }

  // Note: No need to check for embedded null bytes - strlen() already stopped at the first null,
  // so by definition there are no null bytes within [0, len).

  return 0;
}

/**
 * Collect multiple --key flags into identity_keys array
 *
 * Scans argv for all --key or -K flags and populates:
 * - opts->encrypt_key with the first key (backward compatibility)
 * - opts->identity_keys[] with all keys
 * - opts->num_identity_keys with count
 *
 * This enables multi-key support for servers/ACDS that need to present
 * different identity keys (SSH, GPG) based on client expectations.
 */
int options_collect_identity_keys(options_t *opts, int argc, char *argv[]) {
  if (!opts || !argv) {
    log_error("options_collect_identity_keys: Invalid arguments");
    return -1;
  }

  size_t key_count = 0;

  // Scan argv for all --key or -K flags
  for (int i = 1; i < argc && key_count < MAX_IDENTITY_KEYS; i++) {
    const char *arg = argv[i];

    // Check if this is a --key or -K flag
    bool is_key_flag = false;
    const char *key_value = NULL;

    if (strcmp(arg, "--key") == 0 || strcmp(arg, "-K") == 0) {
      // Next argument is the key path
      if (i + 1 < argc) {
        key_value = argv[i + 1];
        is_key_flag = true;
        i++; // Skip the value argument
      }
    } else if (strncmp(arg, "--key=", 6) == 0) {
      // --key=value format
      key_value = arg + 6;
      is_key_flag = true;
    }

    if (is_key_flag && key_value && strlen(key_value) > 0) {
      // Store in identity_keys array
      SAFE_STRNCPY(opts->identity_keys[key_count], key_value, OPTIONS_BUFF_SIZE);

      // First key also goes into encrypt_key for backward compatibility
      if (key_count == 0) {
        SAFE_STRNCPY(opts->encrypt_key, key_value, OPTIONS_BUFF_SIZE);
      }

      key_count++;
      log_debug("Collected identity key #%zu: %s", key_count, key_value);
    }
  }

  opts->num_identity_keys = key_count;

  if (key_count > 0) {
    log_info("Collected %zu identity key(s) for multi-key support", key_count);
  }

  return (int)key_count;
}
