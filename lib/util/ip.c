/**
 * @file util/ip.c
 * @ingroup util
 * @brief 🌍 IPv4/IPv6 address parsing, validation, and formatting utilities
 */

#include <ascii-chat/util/ip.h>
#include <ascii-chat/util/parsing.h>
#include <ascii-chat/common.h>
#include <ascii-chat/common/buffer_sizes.h>
#include <ascii-chat/platform/network.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/util/pcre2.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pcre2.h>

// ============================================================================
// PCRE2 IPv4 Address Validator
// ============================================================================

/**
 * @brief IPv4 address validator using PCRE2
 *
 * Validates the format of IPv4 addresses:
 * - Each octet is 0-255
 * - Rejects leading zeros (e.g., "192.168.001.1" is invalid)
 * - No other characters allowed
 */

// Regex pattern validates IPv4 format:
// - Each octet: 0-255 without leading zeros
// - Exactly 4 octets separated by dots
static const char *IPV4_PATTERN = "^(?:(?:25[0-5]|2[0-4][0-9]|1[0-9]{2}|[1-9]?[0-9])\\.){3}"
                                  "(?:25[0-5]|2[0-4][0-9]|1[0-9]{2}|[1-9]?[0-9])$";

static pcre2_singleton_t *g_ipv4_regex = NULL;

/**
 * Get compiled IPv4 regex (lazy initialization)
 */
static pcre2_code *ipv4_regex_get(void) {
  if (g_ipv4_regex == NULL) {
    g_ipv4_regex = asciichat_pcre2_singleton_compile(IPV4_PATTERN, 0);
  }
  return asciichat_pcre2_singleton_get_code(g_ipv4_regex);
}

// Helper function to validate IPv4 address format
int is_valid_ipv4(const char *ip) {
  if (!ip) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid IP format: NULL");
    return 0; // Invalid
  }

  size_t ip_len = strlen(ip);
  if (ip_len == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid IP format: empty string");
    return 0; // Invalid
  }

  if (ip_len > 256) {
    char ip_buffer[BUFFER_SIZE_SMALL];
    SAFE_STRNCPY(ip_buffer, ip, sizeof(ip_buffer));
    SET_ERRNO(ERROR_INVALID_PARAM, "Suspiciously long ip: %s", ip_buffer);
    return 0; // Invalid
  }

  // Get compiled IPv4 regex
  pcre2_code *regex = ipv4_regex_get();
  if (!regex) {
    log_error("IPv4 validator not initialized");
    return 0;
  }

  // Create match data for regex matching
  pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(regex, NULL);
  if (!match_data) {
    log_error("Failed to allocate match data for IPv4 regex");
    return 0;
  }

  // Validate format using PCRE2 regex
  int match_result = pcre2_match(regex, (PCRE2_SPTR8)ip, ip_len, 0, 0, match_data, NULL);

  pcre2_match_data_free(match_data);

  if (match_result < 0) {
    return 0; // Format validation failed
  }

  return 1; // Valid IPv4 format
}

// Helper function to validate IPv6 address format
// Uses POSIX inet_pton() instead of manual parsing - more robust and battle-tested
int is_valid_ipv6(const char *ip) {
  if (!ip) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid IP format: NULL");
    return 0; // Invalid
  }

  size_t ip_len = strlen(ip);
  if (ip_len == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid IP format: empty string");
    return 0; // Invalid
  }

  if (ip_len > 256) {
    char ip_buffer[BUFFER_SIZE_SMALL];
    SAFE_STRNCPY(ip_buffer, ip, sizeof(ip_buffer));
    SET_ERRNO(ERROR_INVALID_PARAM, "Suspiciously long ip: %s", ip_buffer);
    return 0; // Invalid
  }

  // Use POSIX inet_pton() for robust IPv6 validation
  // It handles all RFC 5952 compliant formats:
  // - Full format: 2001:0db8:85a3:0000:0000:8a2e:0370:7334
  // - Compressed: 2001:db8:85a3::8a2e:370:7334
  // - Loopback: ::1
  // - All zeros: ::
  // - IPv4-mapped: ::ffff:192.0.2.1
  struct in6_addr addr;
  int result = inet_pton(AF_INET6, ip, &addr);

  if (result == 1) {
    return 1; // Valid IPv6
  } else if (result == 0) {
    return 0; // Not a valid IPv6 address
  } else {
    // result < 0 means system error (e.g., unsupported address family)
    SET_ERRNO(ERROR_INVALID_PARAM, "Failed to validate IPv6 address");
    return 0;
  }
}

// Parse IPv6 address, removing brackets if present
int parse_ipv6_address(const char *input, char *output, size_t output_size) {
  if (!input || !output || output_size == 0)
    return -1;

  size_t input_len = strlen(input);
  if (input_len == 0)
    return -1; // Empty string

  const char *start = input;
  const char *end = input + input_len;

  // Remove brackets if present - but brackets must be matched
  if (*start == '[') {
    if (input_len < 2 || *(end - 1) != ']')
      return -1; // Malformed brackets
    start++;
    end--;

    // Check for double brackets [[...]]
    if (start < end && *start == '[')
      return -1; // Double opening bracket
  } else if (*(end - 1) == ']') {
    // Closing bracket without opening bracket
    return -1;
  }

  size_t len = (size_t)(end - start);
  if (len == 0)
    return -1; // Empty content after removing brackets
  if (len >= output_size)
    return -1; // Buffer too small

  memcpy(output, start, len);
  output[len] = '\0';
  return 0;
}

// Format IP address from socket address structure
asciichat_error_t format_ip_address(int family, const struct sockaddr *addr, char *output, size_t output_size) {
  if (!addr || !output || output_size == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid arguments to format_ip_address");
  }

  const void *ip_ptr = NULL;

  if (family == AF_INET) {
    const struct sockaddr_in *addr_in = (const struct sockaddr_in *)addr;
    ip_ptr = &addr_in->sin_addr;
  } else if (family == AF_INET6) {
    const struct sockaddr_in6 *addr_in6 = (const struct sockaddr_in6 *)addr;
    ip_ptr = &addr_in6->sin6_addr;
  } else {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Unsupported address family: %d", family);
  }

  if (inet_ntop(family, ip_ptr, output, (socklen_t)output_size) == NULL) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to format IP address");
  }

  return ASCIICHAT_OK;
}

// Format IP address with port number
asciichat_error_t format_ip_with_port(const char *ip, uint16_t port, char *output, size_t output_size) {
  if (!ip || !output || output_size == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid IP format: %s", ip);
  }

  if (strlen(ip) == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid IP format: %s", ip);
  }

  if (strlen(ip) > 256) {
    char ip_buffer[BUFFER_SIZE_SMALL];
    SAFE_STRNCPY(ip_buffer, ip, sizeof(ip_buffer));
    return SET_ERRNO(ERROR_INVALID_PARAM, "Suspiciously long ip: %s", ip_buffer);
  }

  // Check if it's IPv6 (contains ':')
  if (strchr(ip, ':') != NULL) {
    // IPv6 - use bracket notation
    size_t written = SAFE_SNPRINTF(output, output_size, "[%s]:%u", ip, port);
    if (written >= output_size) {
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid ip=%s or port=%u", ip, port);
    }
  } else {
    // IPv4 - no brackets
    size_t written = SAFE_SNPRINTF(output, output_size, "%s:%u", ip, port);
    if (written >= output_size) {
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid ip=%s or port=%u", ip, port);
    }
  }

  return ASCIICHAT_OK;
}

// Helper function to validate hostname format
// Returns 1 if valid hostname, 0 otherwise
static int is_valid_hostname(const char *hostname) {
  if (!hostname || strlen(hostname) == 0 || strlen(hostname) > 253) {
    return 0;
  }

  // Hostname must not start or end with hyphen or dot
  size_t len = strlen(hostname);
  if (hostname[0] == '-' || hostname[0] == '.' || hostname[len - 1] == '-' || hostname[len - 1] == '.') {
    return 0;
  }

  // Check each character: alphanumeric, hyphen, or dot
  int label_len = 0;
  for (size_t i = 0; i < len; i++) {
    char c = hostname[i];
    if (c == '.') {
      // Label cannot be empty or exceed 63 characters
      if (label_len == 0 || label_len > 63) {
        return 0;
      }
      label_len = 0;
    } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-') {
      label_len++;
      if (label_len > 63) {
        return 0;
      }
    } else {
      return 0; // Invalid character
    }
  }

  // Check last label
  if (label_len == 0 || label_len > 63) {
    return 0;
  }

  return 1;
}

// Parse IP address and port from string
int parse_ip_with_port(const char *input, char *ip_output, size_t ip_output_size, uint16_t *port_output) {
  if (!input || !ip_output || !port_output || ip_output_size == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid IP format: %s", input);
    return -1;
  }

  if (strlen(input) == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid IP format: %s", input);
    return -1;
  }

  if (strlen(input) > 256) {
    char input_buffer[BUFFER_SIZE_SMALL];
    SAFE_STRNCPY(input_buffer, input, sizeof(input_buffer));
    SET_ERRNO(ERROR_INVALID_PARAM, "Suspiciously long ip: %s", input_buffer);
    return -1;
  }

  // Check for IPv6 bracket notation: [2001:db8::1]:8080
  if (input[0] == '[') {
    // Find closing bracket
    const char *bracket_end = strchr(input, ']');
    if (!bracket_end)
      return -1; // Malformed - opening bracket but no closing bracket

    // Extract IP address (without brackets)
    size_t ip_len = (size_t)(bracket_end - input - 1);
    if (ip_len >= ip_output_size)
      return -1; // IP too long

    memcpy(ip_output, input + 1, ip_len);
    ip_output[ip_len] = '\0';

    // Check for port after closing bracket
    if (bracket_end[1] == ':') {
      // Parse port using safe integer parsing
      if (parse_port(bracket_end + 2, port_output) != ASCIICHAT_OK) {
        return -1; // Invalid port
      }
    } else {
      return -1; // Expected ':' after closing bracket
    }
  } else {
    // IPv4 or hostname: "192.0.2.1:8080" or "example.com:8080"
    // Find last ':' for port separation
    const char *colon = strrchr(input, ':');
    if (!colon)
      return -1; // No port separator

    // Extract IP/hostname
    size_t ip_len = (size_t)(colon - input);
    if (ip_len >= ip_output_size)
      return -1; // IP too long

    memcpy(ip_output, input, ip_len);
    ip_output[ip_len] = '\0';

    // Reject IPv6 without brackets (check if extracted IP contains ':')
    // This catches cases like "::1:8080" which should be "[::1]:8080"
    if (strchr(ip_output, ':') != NULL)
      return -1; // IPv6 address without brackets - invalid format

    // Parse port using safe integer parsing
    if (parse_port(colon + 1, port_output) != ASCIICHAT_OK) {
      return -1; // Invalid port
    }
  }

  return 0;
}

// Parse address with optional port from string
// Handles IPv4, IPv6 (with or without brackets), and hostnames
// If no port is specified, uses default_port
int parse_address_with_optional_port(const char *input, char *address_output, size_t address_output_size,
                                     uint16_t *port_output, uint16_t default_port) {
  if (!input || !address_output || !port_output || address_output_size == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid arguments to parse_address_with_optional_port");
    return -1;
  }

  size_t input_len = strlen(input);
  if (input_len == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Empty address string");
    return -1;
  }

  if (input_len > 256) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Address string too long");
    return -1;
  }

  // Case 1: Bracketed IPv6 address with or without port
  // Examples: "[::1]" or "[::1]:8080" or "[2001:db8::1]:443"
  if (input[0] == '[') {
    const char *bracket_end = strchr(input, ']');
    if (!bracket_end) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Malformed bracketed address (missing ']'): %s", input);
      return -1;
    }

    // Extract address without brackets
    size_t addr_len = (size_t)(bracket_end - input - 1);
    if (addr_len == 0) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Empty bracketed address: %s", input);
      return -1;
    }
    if (addr_len >= address_output_size) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Address too long for buffer");
      return -1;
    }

    memcpy(address_output, input + 1, addr_len);
    address_output[addr_len] = '\0';

    // Validate it's a valid IPv6 address
    if (!is_valid_ipv6(address_output)) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Invalid IPv6 address: %s", address_output);
      return -1;
    }

    // Check for port after closing bracket
    if (bracket_end[1] == '\0') {
      // No port specified, use default
      *port_output = default_port;
    } else if (bracket_end[1] == ':') {
      // Port specified
      if (parse_port(bracket_end + 2, port_output) != ASCIICHAT_OK) {
        SET_ERRNO(ERROR_INVALID_PARAM, "Invalid port in: %s", input);
        return -1;
      }
    } else {
      SET_ERRNO(ERROR_INVALID_PARAM, "Invalid format after ']' (expected ':' or end): %s", input);
      return -1;
    }
    return 0;
  }

  // Case 2: Check if it's a valid IPv6 address without brackets (no port allowed in this case)
  // Examples: "::1" or "2001:db8::1" or "fe80::1"
  // Note: IPv6 addresses contain colons, so we can't use ':' as port separator without brackets
  if (is_valid_ipv6(input)) {
    if (strlen(input) >= address_output_size) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Address too long for buffer");
      return -1;
    }
    SAFE_STRNCPY(address_output, input, address_output_size);
    *port_output = default_port;
    return 0;
  }

  // Case 3: IPv4 or hostname, possibly with port
  // Examples: "192.168.1.1" or "192.168.1.1:8080" or "localhost" or "example.com:443"
  const char *last_colon = strrchr(input, ':');

  if (last_colon == NULL) {
    // No colon - just address/hostname, no port
    if (strlen(input) >= address_output_size) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Address too long for buffer");
      return -1;
    }

    // Validate it's a valid IPv4 or hostname
    if (!is_valid_ipv4(input) && !is_valid_hostname(input)) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Invalid address format: %s", input);
      return -1;
    }

    SAFE_STRNCPY(address_output, input, address_output_size);
    *port_output = default_port;
    return 0;
  }

  // Has colon - extract address and port
  size_t addr_len = (size_t)(last_colon - input);
  if (addr_len == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Empty address before port: %s", input);
    return -1;
  }
  if (addr_len >= address_output_size) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Address too long for buffer");
    return -1;
  }

  memcpy(address_output, input, addr_len);
  address_output[addr_len] = '\0';

  // Check if extracted address contains ':' - that would be IPv6 without brackets (invalid with port)
  if (strchr(address_output, ':') != NULL) {
    SET_ERRNO(ERROR_INVALID_PARAM, "IPv6 addresses must use brackets when specifying port: [%s]:%s instead of %s",
              address_output, last_colon + 1, input);
    return -1;
  }

  // Validate address part is valid IPv4 or hostname
  if (!is_valid_ipv4(address_output) && !is_valid_hostname(address_output)) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid address format: %s", address_output);
    return -1;
  }

  // Parse port
  if (parse_port(last_colon + 1, port_output) != ASCIICHAT_OK) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid port in: %s", input);
    return -1;
  }

  return 0;
}
