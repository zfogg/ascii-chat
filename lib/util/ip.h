#pragma once

/**
 * @file util/ip.h
 * @brief 🌍 IP Address Parsing and Formatting Utilities
 * @ingroup util
 * @addtogroup util
 * @{
 *
 * This header provides utilities for validating, parsing, and formatting IPv4
 * and IPv6 addresses with proper bracket notation support. Functions handle
 * various address formats including bracketed IPv6 addresses.
 *
 * CORE FEATURES:
 * ==============
 * - IPv4 address validation and parsing
 * - IPv6 address validation and parsing
 * - Bracket notation support for IPv6 ([::1])
 * - IP address with port formatting and parsing
 * - Hostname support (non-IP addresses)
 * - Cross-platform compatibility
 *
 * IP ADDRESS FORMATS:
 * ===================
 * The system supports various IP address formats:
 * - IPv4: "192.0.2.1" (standard dotted decimal)
 * - IPv6: "::1" or "[::1]" (with or without brackets)
 * - IPv6: "2001:db8::1" or "[2001:db8::1]" (full format)
 * - With port: "192.0.2.1:8080" or "[::1]:8080"
 *
 * BRACKET NOTATION:
 * =================
 * IPv6 addresses in URLs and socket addresses use bracket notation:
 * - Format: "[2001:db8::1]:8080"
 * - Brackets separate address from port (colons are part of IPv6 address)
 * - parse_ipv6_address() removes brackets for internal use
 * - format_ip_with_port() adds brackets for IPv6 addresses
 *
 * @note All parsing functions validate input addresses.
 * @note IPv6 addresses are normalized (brackets removed) for internal storage.
 * @note Port numbers are validated (0-65535 range).
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <stdint.h>
#include <stddef.h>
#include "common.h"

/* ============================================================================
 * IP Address Validation Functions
 * @{
 */

/**
 * @brief Check if a string is a valid IPv4 address
 * @param ip String to validate (must not be NULL)
 * @return 1 if valid IPv4, 0 otherwise
 *
 * Validates that a string is a valid IPv4 address in dotted decimal notation
 * (e.g., "192.0.2.1"). Checks format correctness and numeric value ranges
 * for each octet (0-255).
 *
 * @note Returns 0 for invalid format, out-of-range values, or non-IPv4 strings.
 * @note Does not validate that address is routable or assigned.
 *
 * @par Example
 * @code
 * if (is_valid_ipv4("192.0.2.1")) {
 *     // Valid IPv4 address
 * }
 * @endcode
 *
 * @ingroup util
 */
int is_valid_ipv4(const char *ip);

/**
 * @brief Check if a string is a valid IPv6 address
 * @param ip String to validate (with or without brackets, must not be NULL)
 * @return 1 if valid IPv6, 0 otherwise
 *
 * Validates that a string is a valid IPv6 address. Accepts addresses with
 * or without brackets (e.g., "::1" or "[::1]"). Checks format correctness
 * including compressed zero notation (::) and valid hexadecimal groups.
 *
 * @note Returns 0 for invalid format, malformed addresses, or non-IPv6 strings.
 * @note Handles both bracketed and unbracketed IPv6 addresses.
 * @note Does not validate that address is routable or assigned.
 *
 * @par Example
 * @code
 * if (is_valid_ipv6("[::1]")) {
 *     // Valid IPv6 address
 * }
 * @endcode
 *
 * @ingroup util
 */
int is_valid_ipv6(const char *ip);

/** @} */

/* ============================================================================
 * IP Address Parsing Functions
 * @{
 */

/**
 * @brief Parse IPv6 address, removing brackets if present
 * @param input Input address (may have brackets, must not be NULL)
 * @param output Output buffer for parsed address (without brackets, must not be NULL)
 * @param output_size Size of output buffer (must be > 0)
 * @return 0 on success, -1 on error
 *
 * Parses an IPv6 address and removes brackets if present. Handles both
 * bracketed and unbracketed formats. Validates the address and normalizes
 * it to unbracketed form for internal use.
 *
 * SUPPORTED FORMATS:
 * - "::1" -> "::1"
 * - "[::1]" -> "::1"
 * - "2001:db8::1" -> "2001:db8::1"
 * - "[2001:db8::1]" -> "2001:db8::1"
 *
 * @note Output buffer must be large enough to hold IPv6 address (at least 39 bytes).
 * @note Returns -1 on error (invalid address, buffer too small, etc.).
 * @note Brackets are removed from output (address is normalized).
 *
 * @ingroup util
 */
int parse_ipv6_address(const char *input, char *output, size_t output_size);

/**
 * @brief Parse IP address and port from string
 * @param input Input string with port (must not be NULL, format: "ip:port" or "[ip]:port")
 * @param ip_output Output buffer for IP address (without brackets, must not be NULL)
 * @param ip_output_size Size of IP output buffer (must be > 0)
 * @param port_output Pointer to store port number (must not be NULL)
 * @return 0 on success, -1 on error
 *
 * Parses an IP address (IPv4 or IPv6) with port number from a string. Handles
 * various formats including bracketed IPv6 addresses. Also accepts hostnames
 * (non-IP addresses).
 *
 * SUPPORTED FORMATS:
 * - IPv4: "192.0.2.1:8080"
 * - IPv6: "[2001:db8::1]:8080"
 * - Hostname: "example.com:8080"
 *
 * @note IPv6 addresses must use brackets in input format.
 * @note Port numbers are validated (0-65535 range).
 * @note Output IP address is normalized (brackets removed).
 * @note Returns -1 on error (invalid format, buffer too small, invalid port, etc.).
 *
 * @par Example
 * @code
 * char ip[64];
 * uint16_t port;
 * if (parse_ip_with_port("[::1]:8080", ip, sizeof(ip), &port) == 0) {
 *     // ip = "::1", port = 8080
 * }
 * @endcode
 *
 * @ingroup util
 */
int parse_ip_with_port(const char *input, char *ip_output, size_t ip_output_size, uint16_t *port_output);

/** @} */

/* ============================================================================
 * IP Address Formatting Functions
 * @{
 */

/**
 * @brief Format IP address with port number
 * @param ip IP address (without brackets, must not be NULL)
 * @param port Port number (0-65535)
 * @param output Output buffer (must not be NULL)
 * @param output_size Size of output buffer (must be > 0)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Formats an IP address with port number. Automatically adds brackets for
 * IPv6 addresses to separate address from port. IPv4 addresses are formatted
 * without brackets.
 *
 * OUTPUT FORMATS:
 * - IPv4: "192.0.2.1:8080"
 * - IPv6: "[2001:db8::1]:8080"
 *
 * @note Output buffer must be large enough (at least 64 bytes recommended).
 * @note IPv6 addresses automatically get brackets in output.
 * @note Returns error code on failure (buffer too small, invalid address, etc.).
 *
 * @par Example
 * @code
 * char output[64];
 * if (format_ip_with_port("::1", 8080, output, sizeof(output)) == ASCIICHAT_OK) {
 *     // output = "[::1]:8080"
 * }
 * @endcode
 *
 * @ingroup util
 */
asciichat_error_t format_ip_with_port(const char *ip, uint16_t port, char *output, size_t output_size);

/** @} */
