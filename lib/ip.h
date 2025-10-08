#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * @file ip.h
 * @brief IP address parsing and formatting utilities
 *
 * Provides utilities for validating, parsing, and formatting IPv4/IPv6 addresses
 * with proper bracket notation support for IPv6.
 */

/**
 * Check if a string is a valid IPv4 address
 *
 * @param ip String to validate
 * @return 1 if valid IPv4, 0 otherwise
 */
int is_valid_ipv4(const char *ip);

/**
 * Check if a string is a valid IPv6 address
 *
 * @param ip String to validate (with or without brackets)
 * @return 1 if valid IPv6, 0 otherwise
 */
int is_valid_ipv6(const char *ip);

/**
 * Parse IPv6 address, removing brackets if present
 *
 * Handles formats: "::1", "[::1]", "2001:db8::1", "[2001:db8::1]"
 *
 * @param input Input address (may have brackets)
 * @param output Output buffer for parsed address (without brackets)
 * @param output_size Size of output buffer
 * @return 0 on success, -1 on error
 */
int parse_ipv6_address(const char *input, char *output, size_t output_size);

/**
 * Format IP address with port number
 *
 * Formats as:
 * - IPv4: "192.0.2.1:8080"
 * - IPv6: "[2001:db8::1]:8080"
 *
 * @param ip IP address (without brackets)
 * @param port Port number
 * @param output Output buffer
 * @param output_size Size of output buffer
 * @return 0 on success, -1 on error
 */
int format_ip_with_port(const char *ip, uint16_t port, char *output, size_t output_size);

/**
 * Parse IP address and port from string
 *
 * Handles formats:
 * - IPv4: "192.0.2.1:8080"
 * - IPv6: "[2001:db8::1]:8080"
 * - Hostname: "example.com:8080"
 *
 * @param input Input string with port
 * @param ip_output Output buffer for IP address (without brackets)
 * @param ip_output_size Size of IP output buffer
 * @param port_output Pointer to store port number
 * @return 0 on success, -1 on error
 */
int parse_ip_with_port(const char *input, char *ip_output, size_t ip_output_size, uint16_t *port_output);
