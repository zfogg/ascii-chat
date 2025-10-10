#include "ip.h"
#include "common.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Helper function to validate IPv4 address format
int is_valid_ipv4(const char *ip) {
  if (!ip)
    return 0;

  int segments = 0;
  int value = 0;
  int digits = 0;
  int has_leading_zero = 0;

  for (const char *p = ip; *p != '\0'; p++) {
    if (*p == '.') {
      // Single '0' is valid, but '01', '001', etc. are not
      if (digits == 0 || value > 255 || (has_leading_zero && digits > 1))
        return 0;
      segments++;
      value = 0;
      digits = 0;
      has_leading_zero = 0;
    } else if (*p >= '0' && *p <= '9') {
      // Check for leading zero
      if (digits == 0 && *p == '0') {
        has_leading_zero = 1;
      }

      value = value * 10 + (*p - '0');
      digits++;
      if (digits > 3 || value > 255)
        return 0;
    } else {
      return 0; // Invalid character
    }
  }

  // Check last segment - single '0' is valid
  if (digits == 0 || value > 255 || (has_leading_zero && digits > 1))
    return 0;

  return segments == 3; // Should have exactly 3 dots (4 segments)
}

// Helper function to validate IPv6 address format
int is_valid_ipv6(const char *ip) {
  if (!ip)
    return 0;

  // Special case: "::" is valid (all zeros)
  if (strcmp(ip, "::") == 0)
    return 1;

  // Check if it contains at least one colon (basic IPv6 requirement)
  if (strchr(ip, ':') == NULL)
    return 0;

  // Reject leading or trailing single colons
  size_t len = strlen(ip);
  if (len > 0 && ip[0] == ':' && (len < 2 || ip[1] != ':'))
    return 0; // Leading single colon (not :: which is handled above)
  if (len > 0 && ip[len - 1] == ':' && (len < 2 || ip[len - 2] != ':'))
    return 0; // Trailing single colon

  // Check for IPv4-mapped address (contains dots)
  int has_ipv4_mapped = (strchr(ip, '.') != NULL);

  // Check for valid characters and count double colons
  const char *p = ip;
  int double_colon_count = 0;
  int segment_len = 0;
  int segment_count = 0;
  int last_was_colon = 0;

  while (*p) {
    if (*p == ':') {
      if (last_was_colon) {
        double_colon_count++;
        if (double_colon_count > 1)
          return 0; // Multiple :: not allowed
      }
      if (segment_len > 0) {
        segment_count++;
        if (segment_len > 4)
          return 0; // Hex segment too long
      }
      segment_len = 0;
      last_was_colon = 1;
    } else if (*p == '.') {
      // Dots are allowed for IPv4-mapped addresses
      // Don't count this as part of hex segment
      last_was_colon = 0;
    } else if ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
      segment_len++;
      last_was_colon = 0;
    } else {
      return 0; // Invalid character for IPv6
    }
    p++;
  }

  // Count last segment if exists and not ending with colon
  if (segment_len > 0) {
    if (!has_ipv4_mapped && segment_len > 4)
      return 0; // Last hex segment too long
    segment_count++;
  }

  // For IPv4-mapped addresses, we have fewer hex segments (typically 6 or 7)
  // For regular IPv6, we need exactly 8 segments (if no ::) or fewer (if :: present)
  if (!has_ipv4_mapped) {
    if (double_colon_count == 0 && segment_count != 8)
      return 0; // Need exactly 8 segments without ::
    if (segment_count > 8)
      return 0; // Too many segments
  }

  return 1;
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

// Format IP address with port number
int format_ip_with_port(const char *ip, uint16_t port, char *output, size_t output_size) {
  if (!ip || !output || output_size == 0)
    return -1;

  // Check if it's IPv6 (contains ':')
  if (strchr(ip, ':') != NULL) {
    // IPv6 - use bracket notation
    int written = SAFE_SNPRINTF(output, output_size, "[%s]:%u", ip, port);
    if (written < 0 || (size_t)written >= output_size)
      return -1;
  } else {
    // IPv4 - no brackets
    int written = SAFE_SNPRINTF(output, output_size, "%s:%u", ip, port);
    if (written < 0 || (size_t)written >= output_size)
      return -1;
  }

  return 0;
}

// Parse IP address and port from string
int parse_ip_with_port(const char *input, char *ip_output, size_t ip_output_size, uint16_t *port_output) {
  if (!input || !ip_output || !port_output || ip_output_size == 0)
    return -1;

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
      // Parse port
      long port_long = strtol(bracket_end + 2, NULL, 10);
      if (port_long <= 0 || port_long > 65535)
        return -1; // Invalid port
      *port_output = (uint16_t)port_long;
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

    // Parse port
    long port_long = strtol(colon + 1, NULL, 10);
    if (port_long <= 0 || port_long > 65535)
      return -1; // Invalid port
    *port_output = (uint16_t)port_long;
  }

  return 0;
}
