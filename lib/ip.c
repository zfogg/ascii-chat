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

  for (const char *p = ip; *p != '\0'; p++) {
    if (*p == '.') {
      if (digits == 0 || value > 255)
        return 0;
      segments++;
      value = 0;
      digits = 0;
    } else if (*p >= '0' && *p <= '9') {
      value = value * 10 + (*p - '0');
      digits++;
      if (digits > 3 || value > 255)
        return 0;
    } else {
      return 0; // Invalid character
    }
  }

  // Check last segment
  if (digits == 0 || value > 255)
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

  // Check for valid characters: hex digits, colons, dots (for IPv4-mapped)
  const char *p = ip;
  int has_colon = 0;
  while (*p) {
    if (*p == ':') {
      has_colon = 1;
    } else if (*p == '.') {
      // Dots are allowed for IPv4-mapped addresses
    } else if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))) {
      return 0; // Invalid character for IPv6
    }
    p++;
  }

  // Must have at least one colon for IPv6
  if (!has_colon)
    return 0;

  // Let getaddrinfo() do the full validation
  return 1;
}

// Parse IPv6 address, removing brackets if present
int parse_ipv6_address(const char *input, char *output, size_t output_size) {
  if (!input || !output || output_size == 0)
    return -1;

  const char *start = input;
  const char *end = input + strlen(input);

  // Remove brackets if present
  if (*start == '[') {
    start++;
    if (end > start && *(end - 1) == ']') {
      end--;
    }
  }

  size_t len = (size_t)(end - start);
  if (len >= output_size)
    return -1;

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
    // Find last ':' (in case IPv6 without brackets - this shouldn't happen but handle it)
    const char *colon = strrchr(input, ':');
    if (!colon)
      return -1; // No port separator

    // Extract IP/hostname
    size_t ip_len = (size_t)(colon - input);
    if (ip_len >= ip_output_size)
      return -1; // IP too long

    memcpy(ip_output, input, ip_len);
    ip_output[ip_len] = '\0';

    // Parse port
    long port_long = strtol(colon + 1, NULL, 10);
    if (port_long <= 0 || port_long > 65535)
      return -1; // Invalid port
    *port_output = (uint16_t)port_long;
  }

  return 0;
}
