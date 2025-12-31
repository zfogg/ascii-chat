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
#include "core/common.h"

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

/**
 * @brief Parse address with optional port from string
 * @param input Input string (address or address:port, must not be NULL)
 * @param address_output Output buffer for address (without brackets, must not be NULL)
 * @param address_output_size Size of address output buffer (must be > 0)
 * @param port_output Pointer to store port number (must not be NULL)
 * @param default_port Default port to use if not specified in input
 * @return 0 on success, -1 on error
 *
 * Parses an address (IPv4, IPv6, or hostname) with an optional port number.
 * If no port is specified, uses the default_port value. Handles various formats
 * including bracketed IPv6 addresses.
 *
 * SUPPORTED FORMATS (without port - uses default_port):
 * - IPv4: "192.0.2.1"
 * - IPv6: "::1" or "[::1]"
 * - Hostname: "example.com" or "localhost"
 *
 * SUPPORTED FORMATS (with port):
 * - IPv4: "192.0.2.1:8080"
 * - IPv6: "[2001:db8::1]:8080" (brackets required with port)
 * - Hostname: "example.com:8080"
 *
 * @note IPv6 addresses with port MUST use brackets: "[::1]:8080"
 * @note IPv6 addresses without port can omit brackets: "::1" or "[::1]"
 * @note Port numbers are validated (1-65535 range).
 * @note Output address is normalized (brackets removed for IPv6).
 * @note Returns -1 on error (invalid format, buffer too small, invalid port, etc.).
 *
 * @par Example
 * @code
 * char addr[64];
 * uint16_t port;
 *
 * // Without port - uses default
 * parse_address_with_optional_port("localhost", addr, sizeof(addr), &port, 27224);
 * // addr = "localhost", port = 27224
 *
 * // With port
 * parse_address_with_optional_port("192.168.1.1:8080", addr, sizeof(addr), &port, 27224);
 * // addr = "192.168.1.1", port = 8080
 *
 * // IPv6 without port
 * parse_address_with_optional_port("::1", addr, sizeof(addr), &port, 27224);
 * // addr = "::1", port = 27224
 *
 * // IPv6 with port
 * parse_address_with_optional_port("[::1]:8080", addr, sizeof(addr), &port, 27224);
 * // addr = "::1", port = 8080
 * @endcode
 *
 * @ingroup util
 */
int parse_address_with_optional_port(const char *input, char *address_output, size_t address_output_size,
                                     uint16_t *port_output, uint16_t default_port);

/** @} */

/* ============================================================================
 * IP Address Formatting Functions
 * @{
 */

/**
 * @brief Format IP address from socket address structure
 * @param family Address family (AF_INET for IPv4 or AF_INET6 for IPv6)
 * @param addr Socket address pointer (must not be NULL)
 * @param output Output buffer for formatted IP address (must not be NULL)
 * @param output_size Size of output buffer (must be > 0)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Formats an IP address from a socket address structure. Automatically handles
 * both IPv4 and IPv6 addresses by extracting the address from the appropriate
 * socket structure fields.
 *
 * OUTPUT FORMATS:
 * - IPv4: "192.0.2.1"
 * - IPv6: "2001:db8::1"
 *
 * @note Output buffer should be at least 64 bytes to accommodate any IPv6 address.
 * @note For IPv4: addr should point to sockaddr_in, for IPv6: should point to sockaddr_in6.
 * @note Returns error code on failure (invalid family, buffer too small, etc.).
 *
 * @par Example
 * @code
 * char ip_str[64];
 * struct sockaddr_in addr;
 * // ... initialize addr ...
 * if (format_ip_address(AF_INET, &addr, ip_str, sizeof(ip_str)) == ASCIICHAT_OK) {
 *     // ip_str contains the formatted IP address
 * }
 * @endcode
 *
 * @ingroup util
 */
asciichat_error_t format_ip_address(int family, const struct sockaddr *addr, char *output, size_t output_size);

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
