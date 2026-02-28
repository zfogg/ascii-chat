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
#include <ascii-chat/log/log.h>
#include <ascii-chat/util/pcre2.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif
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

  // Remove brackets if present (IPv6 addresses can be bracketed)
  char normalized[INET6_ADDRSTRLEN];
  const char *ip_to_validate = ip;

  if (ip[0] == '[') {
    // Has brackets, need to remove them
    if (parse_ipv6_address(ip, normalized, sizeof(normalized)) != 0) {
      return 0; // Failed to parse (malformed brackets)
    }
    ip_to_validate = normalized;
  }

  // Use POSIX inet_pton() for robust IPv6 validation
  // It handles all RFC 5952 compliant formats:
  // - Full format: 2001:0db8:85a3:0000:0000:8a2e:0370:7334
  // - Compressed: 2001:db8:85a3::8a2e:370:7334
  // - Loopback: ::1
  // - All zeros: ::
  // - IPv4-mapped: ::ffff:192.0.2.1
  struct in6_addr addr;
  int result = inet_pton(AF_INET6, ip_to_validate, &addr);

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

// ============================================================================
// IP Version Detection & Unified Validation
// ============================================================================

// Get IP address version
int get_ip_version(const char *ip) {
  if (!ip || strlen(ip) == 0) {
    return 0;
  }

  // Quick heuristic: if it contains a colon, it's likely IPv6
  // (unless it's a port separator, but we're just checking the IP part)
  if (strchr(ip, ':') != NULL) {
    // Could be IPv6 or bracketed IPv6
    if (is_valid_ipv6(ip)) {
      return 6;
    }
    return 0; // Has colon but not valid IPv6
  }

  // No colon, check if it's IPv4
  if (is_valid_ipv4(ip)) {
    return 4;
  }

  return 0; // Not valid IPv4 or IPv6
}

// Check if string is a valid IP address (IPv4 or IPv6)
int is_valid_ip(const char *ip) {
  if (!ip) {
    return 0;
  }

  // Try IPv4 first (faster check)
  if (is_valid_ipv4(ip)) {
    return 1;
  }

  // Try IPv6
  if (is_valid_ipv6(ip)) {
    return 1;
  }

  return 0;
}

// ============================================================================
// IP Address Comparison & Equality
// ============================================================================

// Compare two IP addresses for equality
int ip_equals(const char *ip1, const char *ip2) {
  if (!ip1 || !ip2) {
    return 0;
  }

  // Get versions
  int v1 = get_ip_version(ip1);
  int v2 = get_ip_version(ip2);

  // If different versions or either invalid, not equal
  if (v1 != v2 || v1 == 0) {
    return 0;
  }

  if (v1 == 4) {
    // IPv4: direct string comparison after validation
    struct in_addr addr1, addr2;
    if (inet_pton(AF_INET, ip1, &addr1) != 1 || inet_pton(AF_INET, ip2, &addr2) != 1) {
      return 0;
    }
    return addr1.s_addr == addr2.s_addr;
  } else {
    // IPv6: normalize then compare
    char norm1[INET6_ADDRSTRLEN], norm2[INET6_ADDRSTRLEN];

    // Remove brackets if present
    if (parse_ipv6_address(ip1, norm1, sizeof(norm1)) != 0) {
      return 0;
    }
    if (parse_ipv6_address(ip2, norm2, sizeof(norm2)) != 0) {
      return 0;
    }

    // Parse to binary and compare
    struct in6_addr addr1, addr2;
    if (inet_pton(AF_INET6, norm1, &addr1) != 1 || inet_pton(AF_INET6, norm2, &addr2) != 1) {
      return 0;
    }

    return memcmp(&addr1, &addr2, sizeof(addr1)) == 0;
  }
}

// Compare two IP addresses for sorting
int ip_compare(const char *ip1, const char *ip2) {
  if (!ip1 || !ip2) {
    return -2; // Error
  }

  int v1 = get_ip_version(ip1);
  int v2 = get_ip_version(ip2);

  // Invalid addresses sort last
  if (v1 == 0 && v2 == 0) {
    return 0; // Both invalid, consider equal
  }
  if (v1 == 0) {
    return 1; // ip1 invalid, sorts after ip2
  }
  if (v2 == 0) {
    return -1; // ip2 invalid, sorts after ip1
  }

  // IPv4 sorts before IPv6
  if (v1 != v2) {
    return (v1 < v2) ? -1 : 1;
  }

  if (v1 == 4) {
    // IPv4: compare numerically
    struct in_addr addr1, addr2;
    if (inet_pton(AF_INET, ip1, &addr1) != 1 || inet_pton(AF_INET, ip2, &addr2) != 1) {
      return -2; // Error
    }

    uint32_t val1 = ntohl(addr1.s_addr);
    uint32_t val2 = ntohl(addr2.s_addr);

    if (val1 < val2)
      return -1;
    if (val1 > val2)
      return 1;
    return 0;
  } else {
    // IPv6: compare byte by byte
    char norm1[INET6_ADDRSTRLEN], norm2[INET6_ADDRSTRLEN];

    if (parse_ipv6_address(ip1, norm1, sizeof(norm1)) != 0) {
      return -2;
    }
    if (parse_ipv6_address(ip2, norm2, sizeof(norm2)) != 0) {
      return -2;
    }

    struct in6_addr addr1, addr2;
    if (inet_pton(AF_INET6, norm1, &addr1) != 1 || inet_pton(AF_INET6, norm2, &addr2) != 1) {
      return -2;
    }

    return memcmp(&addr1, &addr2, sizeof(addr1));
  }
}

// ============================================================================
// CIDR/Subnet Utilities
// ============================================================================

// Parse CIDR notation into IP and prefix length
int parse_cidr(const char *cidr, char *ip_out, size_t ip_out_size, int *prefix_out) {
  if (!cidr || !ip_out || !prefix_out || ip_out_size == 0) {
    return -1;
  }

  // Find the '/' separator
  const char *slash = strchr(cidr, '/');
  if (!slash) {
    return -1; // No slash found
  }

  // Extract IP part
  size_t ip_len = (size_t)(slash - cidr);
  if (ip_len == 0 || ip_len >= ip_out_size) {
    return -1;
  }

  char ip_temp[256];
  if (ip_len >= sizeof(ip_temp)) {
    return -1;
  }

  memcpy(ip_temp, cidr, ip_len);
  ip_temp[ip_len] = '\0';

  // Remove brackets if IPv6
  if (ip_temp[0] == '[') {
    if (parse_ipv6_address(ip_temp, ip_out, ip_out_size) != 0) {
      return -1;
    }
  } else {
    if (ip_len >= ip_out_size) {
      return -1;
    }
    memcpy(ip_out, ip_temp, ip_len + 1);
  }

  // Validate IP
  int version = get_ip_version(ip_out);
  if (version == 0) {
    return -1; // Invalid IP
  }

  // Parse prefix length
  const char *prefix_str = slash + 1;
  if (strlen(prefix_str) == 0) {
    return -1;
  }

  char *endptr;
  long prefix = strtol(prefix_str, &endptr, 10);
  if (*endptr != '\0' || prefix < 0) {
    return -1; // Invalid prefix
  }

  // Validate prefix range based on IP version
  if (version == 4 && (prefix > 32)) {
    return -1;
  }
  if (version == 6 && (prefix > 128)) {
    return -1;
  }

  *prefix_out = (int)prefix;
  return 0;
}

// Helper: Apply netmask to IPv4 address
static uint32_t apply_ipv4_mask(uint32_t ip, int prefix_len) {
  if (prefix_len == 0) {
    return 0;
  }
  if (prefix_len >= 32) {
    return ip;
  }
  uint32_t mask = ~((1U << (32 - prefix_len)) - 1);
  return ip & mask;
}

// Helper: Apply netmask to IPv6 address
static void apply_ipv6_mask(struct in6_addr *addr, int prefix_len) {
  if (prefix_len < 0 || prefix_len > 128) {
    return;
  }

  int bytes = prefix_len / 8;
  int bits = prefix_len % 8;

  // Zero out bytes after the prefix
  for (int i = bytes + (bits > 0 ? 1 : 0); i < 16; i++) {
    addr->s6_addr[i] = 0;
  }

  // Mask the partial byte
  if (bits > 0 && bytes < 16) {
    uint8_t mask = (uint8_t)(0xFF << (8 - bits));
    addr->s6_addr[bytes] &= mask;
  }
}

// Check if IP is within CIDR range (parsed form)
int ip_in_cidr_parsed(const char *ip, const char *network, int prefix_len) {
  if (!ip || !network) {
    return 0;
  }

  int ip_version = get_ip_version(ip);
  int net_version = get_ip_version(network);

  // Must be same version
  if (ip_version != net_version || ip_version == 0) {
    return 0;
  }

  if (ip_version == 4) {
    // IPv4 comparison
    struct in_addr ip_addr, net_addr;
    if (inet_pton(AF_INET, ip, &ip_addr) != 1 || inet_pton(AF_INET, network, &net_addr) != 1) {
      return 0;
    }

    uint32_t ip_val = ntohl(ip_addr.s_addr);
    uint32_t net_val = ntohl(net_addr.s_addr);

    uint32_t ip_masked = apply_ipv4_mask(ip_val, prefix_len);
    uint32_t net_masked = apply_ipv4_mask(net_val, prefix_len);

    return ip_masked == net_masked;
  } else {
    // IPv6 comparison
    char ip_norm[INET6_ADDRSTRLEN], net_norm[INET6_ADDRSTRLEN];

    if (parse_ipv6_address(ip, ip_norm, sizeof(ip_norm)) != 0) {
      return 0;
    }
    if (parse_ipv6_address(network, net_norm, sizeof(net_norm)) != 0) {
      return 0;
    }

    struct in6_addr ip_addr, net_addr;
    if (inet_pton(AF_INET6, ip_norm, &ip_addr) != 1 || inet_pton(AF_INET6, net_norm, &net_addr) != 1) {
      return 0;
    }

    // Apply mask to both addresses
    struct in6_addr ip_masked = ip_addr;
    struct in6_addr net_masked = net_addr;
    apply_ipv6_mask(&ip_masked, prefix_len);
    apply_ipv6_mask(&net_masked, prefix_len);

    return memcmp(&ip_masked, &net_masked, sizeof(ip_masked)) == 0;
  }
}

// Check if IP address is within CIDR range
int ip_in_cidr(const char *ip, const char *cidr) {
  if (!ip || !cidr) {
    return 0;
  }

  char network[64];
  int prefix;

  if (parse_cidr(cidr, network, sizeof(network), &prefix) != 0) {
    return 0; // Invalid CIDR
  }

  return ip_in_cidr_parsed(ip, network, prefix);
}

// ============================================================================
// IPv4-Mapped IPv6 Utilities
// ============================================================================

// Convert IPv4 address to IPv6-mapped format
asciichat_error_t ipv4_to_ipv6_mapped(const char *ipv4, char *ipv6_out, size_t out_size) {
  if (!ipv4 || !ipv6_out || out_size == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid arguments to ipv4_to_ipv6_mapped");
  }

  // Validate IPv4
  if (!is_valid_ipv4(ipv4)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid IPv4 address: %s", ipv4);
  }

  // Format as ::ffff:x.x.x.x
  size_t written = SAFE_SNPRINTF(ipv6_out, out_size, "::ffff:%s", ipv4);
  if (written >= out_size) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Output buffer too small");
  }

  return ASCIICHAT_OK;
}

// Check if IPv6 address is IPv4-mapped
int is_ipv4_mapped_ipv6(const char *ipv6) {
  if (!ipv6) {
    return 0;
  }

  // Normalize (remove brackets)
  char normalized[INET6_ADDRSTRLEN];
  if (parse_ipv6_address(ipv6, normalized, sizeof(normalized)) != 0) {
    return 0;
  }

  // Validate IPv6
  if (!is_valid_ipv6(normalized)) {
    return 0;
  }

  struct in6_addr addr;
  if (inet_pton(AF_INET6, normalized, &addr) != 1) {
    return 0;
  }

  // Check for ::ffff:0:0/96 prefix
  // First 10 bytes should be 0, next 2 should be 0xff
  for (int i = 0; i < 10; i++) {
    if (addr.s6_addr[i] != 0) {
      return 0;
    }
  }
  if (addr.s6_addr[10] != 0xFF || addr.s6_addr[11] != 0xFF) {
    return 0;
  }

  return 1;
}

// Extract IPv4 address from IPv6-mapped format
asciichat_error_t extract_ipv4_from_mapped_ipv6(const char *ipv6, char *ipv4_out, size_t out_size) {
  if (!ipv6 || !ipv4_out || out_size == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid arguments to extract_ipv4_from_mapped_ipv6");
  }

  if (!is_ipv4_mapped_ipv6(ipv6)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Not an IPv4-mapped IPv6 address: %s", ipv6);
  }

  // Normalize
  char normalized[INET6_ADDRSTRLEN];
  if (parse_ipv6_address(ipv6, normalized, sizeof(normalized)) != 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Failed to parse IPv6 address");
  }

  struct in6_addr addr;
  if (inet_pton(AF_INET6, normalized, &addr) != 1) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Failed to convert IPv6 address");
  }

  // Extract last 4 bytes and format as IPv4
  struct in_addr ipv4_addr;
  memcpy(&ipv4_addr.s_addr, &addr.s6_addr[12], 4);

  if (inet_ntop(AF_INET, &ipv4_addr, ipv4_out, (socklen_t)out_size) == NULL) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to format IPv4 address");
  }

  return ASCIICHAT_OK;
}

// ============================================================================
// IPv6 Canonicalization & Formatting
// ============================================================================

// Expand IPv6 address to full form
asciichat_error_t expand_ipv6(const char *ipv6, char *expanded_out, size_t out_size) {
  if (!ipv6 || !expanded_out || out_size == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid arguments to expand_ipv6");
  }

  // Normalize (remove brackets)
  char normalized[INET6_ADDRSTRLEN];
  if (parse_ipv6_address(ipv6, normalized, sizeof(normalized)) != 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Failed to parse IPv6 address");
  }

  // Validate
  if (!is_valid_ipv6(normalized)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid IPv6 address: %s", ipv6);
  }

  struct in6_addr addr;
  if (inet_pton(AF_INET6, normalized, &addr) != 1) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Failed to convert IPv6 address");
  }

  // Format as full expanded form: xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx
  size_t written =
      SAFE_SNPRINTF(expanded_out, out_size, "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x",
                    (addr.s6_addr[0] << 8) | addr.s6_addr[1], (addr.s6_addr[2] << 8) | addr.s6_addr[3],
                    (addr.s6_addr[4] << 8) | addr.s6_addr[5], (addr.s6_addr[6] << 8) | addr.s6_addr[7],
                    (addr.s6_addr[8] << 8) | addr.s6_addr[9], (addr.s6_addr[10] << 8) | addr.s6_addr[11],
                    (addr.s6_addr[12] << 8) | addr.s6_addr[13], (addr.s6_addr[14] << 8) | addr.s6_addr[15]);

  if (written >= out_size) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Output buffer too small");
  }

  return ASCIICHAT_OK;
}

// Canonicalize IPv6 address to standard form
asciichat_error_t canonicalize_ipv6(const char *ipv6, char *canonical_out, size_t out_size) {
  if (!ipv6 || !canonical_out || out_size == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid arguments to canonicalize_ipv6");
  }

  // Normalize (remove brackets)
  char normalized[INET6_ADDRSTRLEN];
  if (parse_ipv6_address(ipv6, normalized, sizeof(normalized)) != 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Failed to parse IPv6 address");
  }

  // Validate
  if (!is_valid_ipv6(normalized)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid IPv6 address: %s", ipv6);
  }

  struct in6_addr addr;
  if (inet_pton(AF_INET6, normalized, &addr) != 1) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Failed to convert IPv6 address");
  }

  // Use inet_ntop which produces canonical form
  if (inet_ntop(AF_INET6, &addr, canonical_out, (socklen_t)out_size) == NULL) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to format IPv6 address");
  }

  return ASCIICHAT_OK;
}

// Compress IPv6 address using :: notation (alias for canonicalize)
asciichat_error_t compact_ipv6(const char *ipv6, char *compact_out, size_t out_size) {
  return canonicalize_ipv6(ipv6, compact_out, out_size);
}

// Check if IPv6 address is anycast
int is_anycast_ipv6(const char *ipv6) {
  if (!ipv6) {
    return 0;
  }

  // Normalize
  char normalized[INET6_ADDRSTRLEN];
  if (parse_ipv6_address(ipv6, normalized, sizeof(normalized)) != 0) {
    return 0;
  }

  // Validate
  if (!is_valid_ipv6(normalized)) {
    return 0;
  }

  struct in6_addr addr;
  if (inet_pton(AF_INET6, normalized, &addr) != 1) {
    return 0;
  }

  // 6to4 relay anycast: 192.88.99.0/24 (mapped to 2002:c058:6301::)
  // Check if it matches 2002:c058:6301::
  if (addr.s6_addr[0] == 0x20 && addr.s6_addr[1] == 0x02 && addr.s6_addr[2] == 0xc0 && addr.s6_addr[3] == 0x58 &&
      addr.s6_addr[4] == 0x63 && addr.s6_addr[5] == 0x01) {
    // Check if remaining bytes are zero (anycast)
    int is_zero = 1;
    for (int i = 6; i < 16; i++) {
      if (addr.s6_addr[i] != 0) {
        is_zero = 0;
        break;
      }
    }
    if (is_zero) {
      return 1;
    }
  }

  // Subnet-router anycast: would need subnet context to determine
  // We can't detect this without knowing the subnet

  return 0;
}

// ============================================================================
// IP Address Classification Functions
// ============================================================================

// Extract IP address (without port) from formatted address string
int extract_ip_from_address(const char *addr_with_port, char *ip_out, size_t ip_out_size) {
  if (!addr_with_port || !ip_out || ip_out_size == 0) {
    return -1;
  }

  // Check for IPv6 bracket notation: [2001:db8::1]:27224
  if (addr_with_port[0] == '[') {
    const char *bracket_end = strchr(addr_with_port, ']');
    if (!bracket_end) {
      return -1;
    }
    size_t ip_len = (size_t)(bracket_end - addr_with_port - 1);
    if (ip_len >= ip_out_size) {
      return -1;
    }
    memcpy(ip_out, addr_with_port + 1, ip_len);
    ip_out[ip_len] = '\0';
    return 0;
  }

  // IPv4: Find last colon and extract IP part
  const char *colon = strrchr(addr_with_port, ':');
  if (!colon) {
    // No port, just copy the whole thing
    SAFE_STRNCPY(ip_out, addr_with_port, ip_out_size);
    return 0;
  }

  size_t ip_len = (size_t)(colon - addr_with_port);
  if (ip_len >= ip_out_size) {
    return -1;
  }
  memcpy(ip_out, addr_with_port, ip_len);
  ip_out[ip_len] = '\0';
  return 0;
}

// Get human-readable IP type string (Localhost/LAN/Internet)
const char *get_ip_type_string(const char *ip) {
  if (!ip || ip[0] == '\0') {
    return "";
  }

  // Check for wildcard "bind to all interfaces" addresses
  // IPv4: 0.0.0.0
  if (strcmp(ip, "0.0.0.0") == 0) {
    return "All Interfaces";
  }

  // IPv6: :: (unspecified address)
  if (strcmp(ip, "::") == 0) {
    return "All Interfaces";
  }

  // Check IPv4
  if (is_localhost_ipv4(ip)) {
    return "Localhost";
  }
  if (is_lan_ipv4(ip)) {
    return "LAN";
  }
  if (is_internet_ipv4(ip)) {
    return "Internet";
  }

  // Check IPv6
  if (is_localhost_ipv6(ip)) {
    return "Localhost";
  }
  if (is_lan_ipv6(ip)) {
    return "LAN";
  }
  if (is_internet_ipv6(ip)) {
    return "Internet";
  }

  return "Unknown";
}

// Compare two "IP:port" strings with IPv6 normalization
int compare_ip_port_strings(const char *ip_port1, const char *ip_port2) {
  if (!ip_port1 || !ip_port2) {
    return 0; // Not equal
  }

  // Fast path: if strings are identical, no parsing needed
  if (strcmp(ip_port1, ip_port2) == 0) {
    return 1; // Equal
  }

  // Parse both IP:port strings
  char ip1[64], ip2[64];
  uint16_t port1 = 0, port2 = 0;

  if (parse_ip_with_port(ip_port1, ip1, sizeof(ip1), &port1) != 0) {
    return 0; // Invalid format - not equal
  }
  if (parse_ip_with_port(ip_port2, ip2, sizeof(ip2), &port2) != 0) {
    return 0; // Invalid format - not equal
  }

  // Ports must match exactly
  if (port1 != port2) {
    return 0; // Not equal
  }

  // For IPv6, normalize both addresses before comparison
  int version1 = get_ip_version(ip1);
  int version2 = get_ip_version(ip2);

  // IP versions must match
  if (version1 != version2 || version1 == 0) {
    return 0; // Not equal
  }

  if (version1 == 6) {
    // Normalize both IPv6 addresses
    char canonical1[INET6_ADDRSTRLEN], canonical2[INET6_ADDRSTRLEN];
    if (canonicalize_ipv6(ip1, canonical1, sizeof(canonical1)) != ASCIICHAT_OK) {
      return 0; // Failed to normalize - not equal
    }
    if (canonicalize_ipv6(ip2, canonical2, sizeof(canonical2)) != ASCIICHAT_OK) {
      return 0; // Failed to normalize - not equal
    }
    return (strcmp(canonical1, canonical2) == 0) ? 1 : 0;
  } else {
    // IPv4: use ip_equals for robust comparison
    return ip_equals(ip1, ip2) ? 1 : 0;
  }
}

// Check if IPv4 address is a private/LAN address
int is_lan_ipv4(const char *ip) {
  if (!is_valid_ipv4(ip)) {
    return 0;
  }

  struct in_addr addr;
  if (inet_pton(AF_INET, ip, &addr) != 1) {
    return 0;
  }

  uint32_t ip_val = ntohl(addr.s_addr);

  // 10.0.0.0/8: 10.0.0.0 - 10.255.255.255
  if ((ip_val & 0xFF000000) == 0x0A000000) {
    return 1;
  }

  // 172.16.0.0/12: 172.16.0.0 - 172.31.255.255
  if ((ip_val & 0xFFF00000) == 0xAC100000) {
    return 1;
  }

  // 192.168.0.0/16: 192.168.0.0 - 192.168.255.255
  if ((ip_val & 0xFFFF0000) == 0xC0A80000) {
    return 1;
  }

  return 0;
}

// Check if IPv6 address is a private/LAN address (ULA)
int is_lan_ipv6(const char *ip) {
  if (!ip) {
    return 0;
  }

  // Remove brackets if present
  char normalized[INET6_ADDRSTRLEN];
  if (parse_ipv6_address(ip, normalized, sizeof(normalized)) != 0) {
    return 0;
  }

  // Validate the normalized address
  if (!is_valid_ipv6(normalized)) {
    return 0;
  }

  struct in6_addr addr;
  if (inet_pton(AF_INET6, normalized, &addr) != 1) {
    return 0;
  }

  // fc00::/7: Unique Local Addresses (ULA)
  // First byte should be 0xfc or 0xfd
  if ((addr.s6_addr[0] & 0xFE) == 0xFC) {
    return 1;
  }

  return 0;
}

// Check if IPv4 address is a broadcast address
int is_broadcast_ipv4(const char *ip) {
  if (!is_valid_ipv4(ip)) {
    return 0;
  }

  struct in_addr addr;
  if (inet_pton(AF_INET, ip, &addr) != 1) {
    return 0;
  }

  uint32_t ip_val = ntohl(addr.s_addr);

  // Limited broadcast: 255.255.255.255
  if (ip_val == 0xFFFFFFFF) {
    return 1;
  }

  return 0;
}

// Check if IPv6 address is a multicast address
int is_broadcast_ipv6(const char *ip) {
  if (!ip) {
    return 0;
  }

  // Remove brackets if present
  char normalized[INET6_ADDRSTRLEN];
  if (parse_ipv6_address(ip, normalized, sizeof(normalized)) != 0) {
    return 0;
  }

  // Validate the normalized address
  if (!is_valid_ipv6(normalized)) {
    return 0;
  }

  struct in6_addr addr;
  if (inet_pton(AF_INET6, normalized, &addr) != 1) {
    return 0;
  }

  // ff00::/8: Multicast addresses
  // First byte should be 0xff
  if (addr.s6_addr[0] == 0xFF) {
    return 1;
  }

  return 0;
}

// Check if IPv4 address is localhost
int is_localhost_ipv4(const char *ip) {
  if (!is_valid_ipv4(ip)) {
    return 0;
  }

  struct in_addr addr;
  if (inet_pton(AF_INET, ip, &addr) != 1) {
    return 0;
  }

  uint32_t ip_val = ntohl(addr.s_addr);

  // 127.0.0.0/8: Loopback addresses
  if ((ip_val & 0xFF000000) == 0x7F000000) {
    return 1;
  }

  return 0;
}

// Check if IPv6 address is localhost
int is_localhost_ipv6(const char *ip) {
  if (!ip) {
    return 0;
  }

  // Remove brackets if present
  char normalized[INET6_ADDRSTRLEN];
  if (parse_ipv6_address(ip, normalized, sizeof(normalized)) != 0) {
    return 0;
  }

  // Validate the normalized address
  if (!is_valid_ipv6(normalized)) {
    return 0;
  }

  struct in6_addr addr;
  if (inet_pton(AF_INET6, normalized, &addr) != 1) {
    return 0;
  }

  // ::1: Loopback address
  // All bytes should be 0 except the last byte which should be 1
  for (int i = 0; i < 15; i++) {
    if (addr.s6_addr[i] != 0) {
      return 0;
    }
  }
  if (addr.s6_addr[15] != 1) {
    return 0;
  }

  return 1;
}

// Check if IPv4 address is a link-local address
int is_link_local_ipv4(const char *ip) {
  if (!is_valid_ipv4(ip)) {
    return 0;
  }

  struct in_addr addr;
  if (inet_pton(AF_INET, ip, &addr) != 1) {
    return 0;
  }

  uint32_t ip_val = ntohl(addr.s_addr);

  // 169.254.0.0/16: Link-local addresses (APIPA)
  if ((ip_val & 0xFFFF0000) == 0xA9FE0000) {
    return 1;
  }

  return 0;
}

// Check if IPv6 address is a link-local address
int is_link_local_ipv6(const char *ip) {
  if (!ip) {
    return 0;
  }

  // Remove brackets if present
  char normalized[INET6_ADDRSTRLEN];
  if (parse_ipv6_address(ip, normalized, sizeof(normalized)) != 0) {
    return 0;
  }

  // Validate the normalized address
  if (!is_valid_ipv6(normalized)) {
    return 0;
  }

  struct in6_addr addr;
  if (inet_pton(AF_INET6, normalized, &addr) != 1) {
    return 0;
  }

  // fe80::/10: Link-local addresses
  // First byte should be 0xfe, second byte & 0xc0 should be 0x80
  if (addr.s6_addr[0] == 0xFE && (addr.s6_addr[1] & 0xC0) == 0x80) {
    return 1;
  }

  return 0;
}

// Check if IPv4 address is a public internet address
int is_internet_ipv4(const char *ip) {
  if (!is_valid_ipv4(ip)) {
    return 0;
  }

  struct in_addr addr;
  if (inet_pton(AF_INET, ip, &addr) != 1) {
    return 0;
  }

  uint32_t ip_val = ntohl(addr.s_addr);

  // Exclude private/LAN addresses
  if (is_lan_ipv4(ip)) {
    return 0;
  }

  // Exclude loopback
  if (is_localhost_ipv4(ip)) {
    return 0;
  }

  // Exclude link-local
  if (is_link_local_ipv4(ip)) {
    return 0;
  }

  // Exclude broadcast
  if (is_broadcast_ipv4(ip)) {
    return 0;
  }

  // Exclude multicast: 224.0.0.0/4
  if ((ip_val & 0xF0000000) == 0xE0000000) {
    return 0;
  }

  // Exclude 0.0.0.0/8: "This network"
  if ((ip_val & 0xFF000000) == 0x00000000) {
    return 0;
  }

  // Exclude 100.64.0.0/10: Shared Address Space (RFC 6598)
  if ((ip_val & 0xFFC00000) == 0x64400000) {
    return 0;
  }

  // Exclude 192.0.0.0/24: IETF Protocol Assignments
  if ((ip_val & 0xFFFFFF00) == 0xC0000000) {
    return 0;
  }

  // Exclude 192.0.2.0/24: TEST-NET-1
  if ((ip_val & 0xFFFFFF00) == 0xC0000200) {
    return 0;
  }

  // Exclude 198.18.0.0/15: Benchmarking
  if ((ip_val & 0xFFFE0000) == 0xC6120000) {
    return 0;
  }

  // Exclude 198.51.100.0/24: TEST-NET-2
  if ((ip_val & 0xFFFFFF00) == 0xC6336400) {
    return 0;
  }

  // Exclude 203.0.113.0/24: TEST-NET-3
  if ((ip_val & 0xFFFFFF00) == 0xCB007100) {
    return 0;
  }

  // Exclude 240.0.0.0/4: Reserved for future use
  if ((ip_val & 0xF0000000) == 0xF0000000) {
    return 0;
  }

  // If none of the exclusions matched, it's a public internet address
  return 1;
}

// Check if IPv6 address is a public internet address
int is_internet_ipv6(const char *ip) {
  if (!ip) {
    return 0;
  }

  // Remove brackets if present
  char normalized[INET6_ADDRSTRLEN];
  if (parse_ipv6_address(ip, normalized, sizeof(normalized)) != 0) {
    return 0;
  }

  // Validate the normalized address
  if (!is_valid_ipv6(normalized)) {
    return 0;
  }

  struct in6_addr addr;
  if (inet_pton(AF_INET6, normalized, &addr) != 1) {
    return 0;
  }

  // Exclude loopback
  if (is_localhost_ipv6(ip)) {
    return 0;
  }

  // Exclude link-local
  if (is_link_local_ipv6(ip)) {
    return 0;
  }

  // Exclude ULA
  if (is_lan_ipv6(ip)) {
    return 0;
  }

  // Exclude multicast
  if (is_broadcast_ipv6(ip)) {
    return 0;
  }

  // Exclude unspecified address: ::
  int is_unspecified = 1;
  for (int i = 0; i < 16; i++) {
    if (addr.s6_addr[i] != 0) {
      is_unspecified = 0;
      break;
    }
  }
  if (is_unspecified) {
    return 0;
  }

  // Exclude IPv4-mapped IPv6: ::ffff:0:0/96
  if (addr.s6_addr[0] == 0 && addr.s6_addr[1] == 0 && addr.s6_addr[2] == 0 && addr.s6_addr[3] == 0 &&
      addr.s6_addr[4] == 0 && addr.s6_addr[5] == 0 && addr.s6_addr[6] == 0 && addr.s6_addr[7] == 0 &&
      addr.s6_addr[8] == 0 && addr.s6_addr[9] == 0 && addr.s6_addr[10] == 0xFF && addr.s6_addr[11] == 0xFF) {
    return 0;
  }

  // Exclude IPv4-compatible IPv6: ::/96 (deprecated)
  if (addr.s6_addr[0] == 0 && addr.s6_addr[1] == 0 && addr.s6_addr[2] == 0 && addr.s6_addr[3] == 0 &&
      addr.s6_addr[4] == 0 && addr.s6_addr[5] == 0 && addr.s6_addr[6] == 0 && addr.s6_addr[7] == 0 &&
      addr.s6_addr[8] == 0 && addr.s6_addr[9] == 0 && addr.s6_addr[10] == 0 && addr.s6_addr[11] == 0) {
    // But not ::, which we already excluded above
    if (addr.s6_addr[12] != 0 || addr.s6_addr[13] != 0 || addr.s6_addr[14] != 0 || addr.s6_addr[15] != 0) {
      return 0;
    }
  }

  // Exclude documentation prefix: 2001:db8::/32
  if (addr.s6_addr[0] == 0x20 && addr.s6_addr[1] == 0x01 && addr.s6_addr[2] == 0x0D && addr.s6_addr[3] == 0xB8) {
    return 0;
  }

  // Exclude 6to4: 2002::/16
  if (addr.s6_addr[0] == 0x20 && addr.s6_addr[1] == 0x02) {
    return 0;
  }

  // If none of the exclusions matched, it's likely a global unicast address
  // Global unicast is typically 2000::/3, but let's accept anything that's not excluded
  return 1;
}
