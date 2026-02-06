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
#include "../common.h"

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

/* ============================================================================
 * IP Version Detection & Unified Validation
 * @{
 */

/**
 * @brief Get IP address version
 * @param ip IP address string (must not be NULL)
 * @return 4 for IPv4, 6 for IPv6, 0 for invalid
 *
 * Detects the version of an IP address without validating its full correctness.
 * Useful for protocol selection and dual-stack handling.
 *
 * @note Returns 0 for NULL, empty strings, or invalid addresses.
 * @note Does not validate address correctness beyond basic format.
 *
 * @par Example
 * @code
 * int version = get_ip_version("192.168.1.1");  // Returns 4
 * int version = get_ip_version("::1");           // Returns 6
 * int version = get_ip_version("invalid");       // Returns 0
 * @endcode
 *
 * @ingroup util
 */
int get_ip_version(const char *ip);

/**
 * @brief Check if string is a valid IP address (IPv4 or IPv6)
 * @param ip String to validate (must not be NULL)
 * @return 1 if valid IPv4 or IPv6, 0 otherwise
 *
 * Unified validation function that accepts either IPv4 or IPv6 addresses.
 * Use this when you don't care about the IP version, just validity.
 *
 * @note IPv6 addresses can be with or without brackets.
 * @note Returns 0 for NULL, empty strings, or invalid addresses.
 *
 * @par Example
 * @code
 * if (is_valid_ip("192.168.1.1")) {
 *     // Valid IPv4
 * }
 * if (is_valid_ip("::1")) {
 *     // Valid IPv6
 * }
 * @endcode
 *
 * @ingroup util
 */
int is_valid_ip(const char *ip);

/** @} */

/* ============================================================================
 * IP Address Comparison & Equality
 * @{
 */

/**
 * @brief Compare two IP addresses for equality
 * @param ip1 First IP address (must not be NULL)
 * @param ip2 Second IP address (must not be NULL)
 * @return 1 if equal, 0 if not equal or invalid
 *
 * Compares two IP addresses for equality, handling normalization automatically.
 * Works with IPv4 and IPv6, including bracketed IPv6 notation.
 *
 * NORMALIZATION RULES:
 * - IPv6 addresses are normalized before comparison
 * - Brackets are ignored: "[::1]" equals "::1"
 * - IPv6 zero compression is normalized: "::1" equals "0:0:0:0:0:0:0:1"
 * - IPv4 and IPv6 are never equal
 *
 * @note Returns 0 if either address is invalid.
 * @note Case-insensitive for IPv6 (A-F vs a-f).
 *
 * @par Example
 * @code
 * if (ip_equals("::1", "[::1]")) {
 *     // True - brackets ignored
 * }
 * if (ip_equals("192.168.1.1", "192.168.1.2")) {
 *     // False - different addresses
 * }
 * @endcode
 *
 * @ingroup util
 */
int ip_equals(const char *ip1, const char *ip2);

/**
 * @brief Compare two IP addresses for sorting
 * @param ip1 First IP address (must not be NULL)
 * @param ip2 Second IP address (must not be NULL)
 * @return -1 if ip1 < ip2, 0 if equal, 1 if ip1 > ip2, -2 on error
 *
 * Compares two IP addresses for sorting. IPv4 addresses sort before IPv6.
 * Within the same version, compares numerically (not lexicographically).
 *
 * SORT ORDER:
 * - Invalid addresses sort last
 * - IPv4 addresses sort before IPv6
 * - Within IPv4: numeric comparison (10.0.0.1 < 192.168.1.1)
 * - Within IPv6: numeric comparison (::1 < 2001::1)
 *
 * @note Returns -2 if either address is invalid.
 * @note Useful for qsort() callbacks.
 *
 * @par Example
 * @code
 * int result = ip_compare("10.0.0.1", "192.168.1.1");  // Returns -1
 * int result = ip_compare("::1", "192.168.1.1");       // Returns 1 (IPv6 > IPv4)
 * @endcode
 *
 * @ingroup util
 */
int ip_compare(const char *ip1, const char *ip2);

/**
 * @brief Compare two "IP:port" strings with IPv6 normalization
 *
 * Compares two formatted address strings (e.g., "192.168.1.1:8080" or "[::1]:8080")
 * with proper handling of different IPv6 representations.
 *
 * Handles IPv6 normalization so different representations are treated as equal:
 * - "[::1]:8080" equals "[0:0:0:0:0:0:0:1]:8080"
 * - "[2001:db8::1]:80" equals "[2001:0db8:0000:0000:0000:0000:0000:0001]:80"
 *
 * For IPv4 addresses, uses robust comparison via `ip_equals()` that handles
 * different binary representations correctly.
 *
 * @param ip_port1 First "IP:port" string (IPv4: "192.168.1.1:8080", IPv6: "[::1]:8080")
 * @param ip_port2 Second "IP:port" string to compare
 * @return 1 if addresses match (after normalization), 0 otherwise
 *
 * @par Example
 * @code
 * // IPv6 normalization
 * int match1 = compare_ip_port_strings("[::1]:8080", "[0:0:0:0:0:0:0:1]:8080");
 * // Returns 1 (equal after normalization)
 *
 * // IPv4 comparison
 * int match2 = compare_ip_port_strings("192.168.1.1:80", "192.168.1.1:80");
 * // Returns 1 (equal)
 *
 * // Different ports
 * int match3 = compare_ip_port_strings("192.168.1.1:80", "192.168.1.1:8080");
 * // Returns 0 (different ports)
 * @endcode
 *
 * @ingroup util
 */
int compare_ip_port_strings(const char *ip_port1, const char *ip_port2);

/** @} */

/* ============================================================================
 * CIDR/Subnet Utilities
 * @{
 */

/**
 * @brief Parse CIDR notation into IP and prefix length
 * @param cidr CIDR string (e.g., "192.168.1.0/24", must not be NULL)
 * @param ip_out Output buffer for IP address (without prefix, must not be NULL)
 * @param ip_out_size Size of IP output buffer
 * @param prefix_out Pointer to store prefix length (must not be NULL)
 * @return 0 on success, -1 on error
 *
 * Parses CIDR notation into separate IP address and prefix length components.
 * Supports both IPv4 and IPv6 CIDR notation.
 *
 * SUPPORTED FORMATS:
 * - IPv4: "192.168.1.0/24" -> ip="192.168.1.0", prefix=24
 * - IPv6: "2001:db8::/32" -> ip="2001:db8::", prefix=32
 * - IPv6 with brackets: "[::1]/128" -> ip="::1", prefix=128
 *
 * @note Validates that prefix length is in valid range (0-32 for IPv4, 0-128 for IPv6).
 * @note Output IP has brackets removed for IPv6.
 * @note Returns -1 on error (invalid format, out of range prefix, buffer too small).
 *
 * @par Example
 * @code
 * char ip[64];
 * int prefix;
 * if (parse_cidr("192.168.1.0/24", ip, sizeof(ip), &prefix) == 0) {
 *     // ip = "192.168.1.0", prefix = 24
 * }
 * @endcode
 *
 * @ingroup util
 */
int parse_cidr(const char *cidr, char *ip_out, size_t ip_out_size, int *prefix_out);

/**
 * @brief Check if IP address is within CIDR range
 * @param ip IP address to check (must not be NULL)
 * @param cidr CIDR range (e.g., "192.168.1.0/24", must not be NULL)
 * @return 1 if IP is in range, 0 if not or on error
 *
 * Checks if an IP address falls within a CIDR range. Automatically handles
 * both IPv4 and IPv6. IP and CIDR must be the same version.
 *
 * EXAMPLES:
 * - "192.168.1.10" in "192.168.1.0/24" -> true
 * - "192.168.2.10" in "192.168.1.0/24" -> false
 * - "2001:db8::1" in "2001:db8::/32" -> true
 *
 * @note Returns 0 if IP and CIDR are different versions (IPv4 vs IPv6).
 * @note Returns 0 on error (invalid IP, invalid CIDR).
 *
 * @par Example
 * @code
 * if (ip_in_cidr("192.168.1.10", "192.168.1.0/24")) {
 *     // IP is in the 192.168.1.0/24 subnet
 * }
 * @endcode
 *
 * @ingroup util
 */
int ip_in_cidr(const char *ip, const char *cidr);

/**
 * @brief Check if IP is within CIDR range (parsed form)
 * @param ip IP address to check (must not be NULL)
 * @param network Network address (must not be NULL)
 * @param prefix_len Prefix length (0-32 for IPv4, 0-128 for IPv6)
 * @return 1 if IP is in range, 0 if not or on error
 *
 * Checks if an IP address falls within a CIDR range using pre-parsed
 * network address and prefix length. More efficient than ip_in_cidr()
 * when checking multiple IPs against the same range.
 *
 * @note IP and network must be the same version.
 * @note Returns 0 on error (invalid IP, invalid network, out of range prefix).
 *
 * @par Example
 * @code
 * if (ip_in_cidr_parsed("192.168.1.10", "192.168.1.0", 24)) {
 *     // IP is in the 192.168.1.0/24 subnet
 * }
 * @endcode
 *
 * @ingroup util
 */
int ip_in_cidr_parsed(const char *ip, const char *network, int prefix_len);

/** @} */

/* ============================================================================
 * IPv4-Mapped IPv6 Utilities
 * @{
 */

/**
 * @brief Convert IPv4 address to IPv6-mapped format
 * @param ipv4 IPv4 address (must not be NULL)
 * @param ipv6_out Output buffer for IPv6-mapped address (must not be NULL)
 * @param out_size Size of output buffer
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Converts an IPv4 address to IPv6-mapped format (::ffff:x.x.x.x).
 * This format allows IPv4 addresses to be represented in IPv6.
 *
 * OUTPUT FORMAT: "::ffff:192.168.1.1"
 *
 * @note Output buffer should be at least 64 bytes.
 * @note Returns error if input is not a valid IPv4 address.
 *
 * @par Example
 * @code
 * char ipv6[64];
 * if (ipv4_to_ipv6_mapped("192.168.1.1", ipv6, sizeof(ipv6)) == ASCIICHAT_OK) {
 *     // ipv6 = "::ffff:192.168.1.1"
 * }
 * @endcode
 *
 * @ingroup util
 */
asciichat_error_t ipv4_to_ipv6_mapped(const char *ipv4, char *ipv6_out, size_t out_size);

/**
 * @brief Extract IPv4 address from IPv6-mapped format
 * @param ipv6 IPv6-mapped address (must not be NULL)
 * @param ipv4_out Output buffer for IPv4 address (must not be NULL)
 * @param out_size Size of output buffer
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Extracts the IPv4 address from an IPv6-mapped address (::ffff:x.x.x.x).
 *
 * INPUT FORMATS:
 * - "::ffff:192.168.1.1" -> "192.168.1.1"
 * - "::ffff:c0a8:0101" (hex notation) -> "192.168.1.1"
 *
 * @note Returns error if input is not a valid IPv6-mapped IPv4 address.
 * @note Output buffer should be at least 16 bytes.
 *
 * @par Example
 * @code
 * char ipv4[16];
 * if (extract_ipv4_from_mapped_ipv6("::ffff:192.168.1.1", ipv4, sizeof(ipv4)) == ASCIICHAT_OK) {
 *     // ipv4 = "192.168.1.1"
 * }
 * @endcode
 *
 * @ingroup util
 */
asciichat_error_t extract_ipv4_from_mapped_ipv6(const char *ipv6, char *ipv4_out, size_t out_size);

/**
 * @brief Check if IPv6 address is IPv4-mapped
 * @param ipv6 IPv6 address (must not be NULL)
 * @return 1 if IPv4-mapped, 0 otherwise
 *
 * Checks if an IPv6 address is in IPv4-mapped format (::ffff:x.x.x.x/96).
 *
 * @note Returns 0 for invalid IPv6 addresses.
 *
 * @par Example
 * @code
 * if (is_ipv4_mapped_ipv6("::ffff:192.168.1.1")) {
 *     // This is an IPv4-mapped IPv6 address
 * }
 * @endcode
 *
 * @ingroup util
 */
int is_ipv4_mapped_ipv6(const char *ipv6);

/** @} */

/* ============================================================================
 * IPv6 Canonicalization & Formatting
 * @{
 */

/**
 * @brief Canonicalize IPv6 address to standard form
 * @param ipv6 IPv6 address (must not be NULL)
 * @param canonical_out Output buffer for canonical form (must not be NULL)
 * @param out_size Size of output buffer
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Converts an IPv6 address to canonical form following RFC 5952:
 * - Lowercase hexadecimal digits
 * - Leading zeros removed from each group
 * - Longest run of consecutive zero groups compressed to ::
 * - Brackets removed
 *
 * EXAMPLES:
 * - "2001:0DB8:0000:0000:0000:0000:0000:0001" -> "2001:db8::1"
 * - "[::1]" -> "::1"
 * - "2001:DB8::1" -> "2001:db8::1"
 *
 * @note Output buffer should be at least 64 bytes.
 * @note Returns error if input is not a valid IPv6 address.
 *
 * @par Example
 * @code
 * char canonical[64];
 * if (canonicalize_ipv6("[2001:0DB8::1]", canonical, sizeof(canonical)) == ASCIICHAT_OK) {
 *     // canonical = "2001:db8::1"
 * }
 * @endcode
 *
 * @ingroup util
 */
asciichat_error_t canonicalize_ipv6(const char *ipv6, char *canonical_out, size_t out_size);

/**
 * @brief Expand IPv6 address to full form
 * @param ipv6 IPv6 address (must not be NULL)
 * @param expanded_out Output buffer for expanded form (must not be NULL)
 * @param out_size Size of output buffer
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Expands an IPv6 address to its full uncompressed form with all 8 groups
 * of 4 hexadecimal digits each, separated by colons.
 *
 * EXAMPLES:
 * - "::1" -> "0000:0000:0000:0000:0000:0000:0000:0001"
 * - "2001:db8::1" -> "2001:0db8:0000:0000:0000:0000:0000:0001"
 * - "fe80::1" -> "fe80:0000:0000:0000:0000:0000:0000:0001"
 *
 * @note Output buffer should be at least 64 bytes.
 * @note Returns error if input is not a valid IPv6 address.
 *
 * @par Example
 * @code
 * char expanded[64];
 * if (expand_ipv6("::1", expanded, sizeof(expanded)) == ASCIICHAT_OK) {
 *     // expanded = "0000:0000:0000:0000:0000:0000:0000:0001"
 * }
 * @endcode
 *
 * @ingroup util
 */
asciichat_error_t expand_ipv6(const char *ipv6, char *expanded_out, size_t out_size);

/**
 * @brief Compress IPv6 address using :: notation
 * @param ipv6 IPv6 address (must not be NULL)
 * @param compact_out Output buffer for compressed form (must not be NULL)
 * @param out_size Size of output buffer
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Compresses an IPv6 address by replacing the longest run of consecutive
 * zero groups with ::. Follows RFC 5952 compression rules.
 *
 * EXAMPLES:
 * - "2001:0db8:0000:0000:0000:0000:0000:0001" -> "2001:db8::1"
 * - "fe80:0000:0000:0000:0000:0000:0000:0001" -> "fe80::1"
 * - "2001:db8:0:0:1:0:0:1" -> "2001:db8::1:0:0:1"
 *
 * @note Output buffer should be at least 64 bytes.
 * @note Returns error if input is not a valid IPv6 address.
 * @note Alias for canonicalize_ipv6() - both produce the same output.
 *
 * @par Example
 * @code
 * char compact[64];
 * if (compact_ipv6("2001:0db8:0000:0000:0000:0000:0000:0001", compact, sizeof(compact)) == ASCIICHAT_OK) {
 *     // compact = "2001:db8::1"
 * }
 * @endcode
 *
 * @ingroup util
 */
asciichat_error_t compact_ipv6(const char *ipv6, char *compact_out, size_t out_size);

/** @} */

/* ============================================================================
 * IP Address Classification Functions
 * @{
 */

/**
 * @brief Check if IPv6 address is anycast
 * @param ipv6 IPv6 address (must not be NULL)
 * @return 1 if anycast, 0 otherwise
 *
 * Checks if an IPv6 address is a known anycast address. Anycast addresses
 * are typically indistinguishable from unicast, but some well-known ranges exist:
 * - Subnet-router anycast: last address in each subnet with all host bits zero
 * - 6to4 relay anycast: 192.88.99.0/24 (mapped to 2002:c058:6301::)
 *
 * @note This function only detects known anycast ranges.
 * @note Most anycast addresses cannot be detected without network context.
 * @note Returns 0 for invalid IPv6 addresses.
 *
 * @par Example
 * @code
 * if (is_anycast_ipv6("2002:c058:6301::")) {
 *     // This is a 6to4 relay anycast address
 * }
 * @endcode
 *
 * @ingroup util
 */
int is_anycast_ipv6(const char *ipv6);

/* ============================================================================
 * IP Address Classification Functions
 * @{
 */

/**
 * @brief Check if IPv4 address is a private/LAN address
 * @param ip IPv4 address string (must not be NULL)
 * @return 1 if LAN address, 0 otherwise
 *
 * Checks if an IPv4 address is in private address ranges:
 * - 10.0.0.0/8 (10.0.0.0 - 10.255.255.255)
 * - 172.16.0.0/12 (172.16.0.0 - 172.31.255.255)
 * - 192.168.0.0/16 (192.168.0.0 - 192.168.255.255)
 *
 * @note Returns 0 for invalid IPv4 addresses.
 *
 * @par Example
 * @code
 * if (is_lan_ipv4("192.168.1.1")) {
 *     // This is a private LAN address
 * }
 * @endcode
 *
 * @ingroup util
 */
int is_lan_ipv4(const char *ip);

/**
 * @brief Check if IPv6 address is a private/LAN address
 * @param ip IPv6 address string (must not be NULL)
 * @return 1 if LAN address, 0 otherwise
 *
 * Checks if an IPv6 address is a Unique Local Address (ULA):
 * - fc00::/7 (fc00:: - fdff:ffff:ffff:ffff:ffff:ffff:ffff:ffff)
 *
 * @note Returns 0 for invalid IPv6 addresses.
 *
 * @par Example
 * @code
 * if (is_lan_ipv6("fc00::1")) {
 *     // This is a private ULA address
 * }
 * @endcode
 *
 * @ingroup util
 */
int is_lan_ipv6(const char *ip);

/**
 * @brief Check if IPv4 address is a broadcast address
 * @param ip IPv4 address string (must not be NULL)
 * @return 1 if broadcast address, 0 otherwise
 *
 * Checks if an IPv4 address is the limited broadcast address:
 * - 255.255.255.255
 *
 * @note Subnet-specific broadcast addresses (e.g., 192.168.1.255 for 192.168.1.0/24)
 *       cannot be determined without network mask information.
 * @note Returns 0 for invalid IPv4 addresses.
 *
 * @par Example
 * @code
 * if (is_broadcast_ipv4("255.255.255.255")) {
 *     // This is the limited broadcast address
 * }
 * @endcode
 *
 * @ingroup util
 */
int is_broadcast_ipv4(const char *ip);

/**
 * @brief Check if IPv6 address is a multicast address
 * @param ip IPv6 address string (must not be NULL)
 * @return 1 if multicast address, 0 otherwise
 *
 * Checks if an IPv6 address is in the multicast range:
 * - ff00::/8 (ff00:: - ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff)
 *
 * @note IPv6 doesn't have broadcast; multicast is used instead.
 * @note Returns 0 for invalid IPv6 addresses.
 *
 * @par Example
 * @code
 * if (is_broadcast_ipv6("ff02::1")) {
 *     // This is a multicast address (all-nodes link-local)
 * }
 * @endcode
 *
 * @ingroup util
 */
int is_broadcast_ipv6(const char *ip);

/**
 * @brief Check if IPv4 address is localhost
 * @param ip IPv4 address string (must not be NULL)
 * @return 1 if localhost, 0 otherwise
 *
 * Checks if an IPv4 address is in the loopback range:
 * - 127.0.0.0/8 (127.0.0.0 - 127.255.255.255)
 *
 * @note Returns 0 for invalid IPv4 addresses.
 *
 * @par Example
 * @code
 * if (is_localhost_ipv4("127.0.0.1")) {
 *     // This is a loopback address
 * }
 * @endcode
 *
 * @ingroup util
 */
int is_localhost_ipv4(const char *ip);

/**
 * @brief Check if IPv6 address is localhost
 * @param ip IPv6 address string (must not be NULL)
 * @return 1 if localhost, 0 otherwise
 *
 * Checks if an IPv6 address is the loopback address:
 * - ::1
 *
 * @note Returns 0 for invalid IPv6 addresses.
 *
 * @par Example
 * @code
 * if (is_localhost_ipv6("::1")) {
 *     // This is the IPv6 loopback address
 * }
 * @endcode
 *
 * @ingroup util
 */
int is_localhost_ipv6(const char *ip);

/**
 * @brief Check if IPv4 address is a link-local address
 * @param ip IPv4 address string (must not be NULL)
 * @return 1 if link-local, 0 otherwise
 *
 * Checks if an IPv4 address is in the link-local range:
 * - 169.254.0.0/16 (169.254.0.0 - 169.254.255.255)
 *
 * Link-local addresses are auto-configured when DHCP is unavailable (APIPA).
 *
 * @note Returns 0 for invalid IPv4 addresses.
 *
 * @par Example
 * @code
 * if (is_link_local_ipv4("169.254.1.1")) {
 *     // This is an auto-configured link-local address
 * }
 * @endcode
 *
 * @ingroup util
 */
int is_link_local_ipv4(const char *ip);

/**
 * @brief Check if IPv6 address is a link-local address
 * @param ip IPv6 address string (must not be NULL)
 * @return 1 if link-local, 0 otherwise
 *
 * Checks if an IPv6 address is in the link-local range:
 * - fe80::/10 (fe80:: - febf:ffff:ffff:ffff:ffff:ffff:ffff:ffff)
 *
 * Link-local addresses are auto-configured and only valid on a single link.
 *
 * @note Returns 0 for invalid IPv6 addresses.
 *
 * @par Example
 * @code
 * if (is_link_local_ipv6("fe80::1")) {
 *     // This is a link-local address
 * }
 * @endcode
 *
 * @ingroup util
 */
int is_link_local_ipv6(const char *ip);

/**
 * @brief Check if IPv4 address is a public internet address
 * @param ip IPv4 address string (must not be NULL)
 * @return 1 if public internet address, 0 otherwise
 *
 * Checks if an IPv4 address is a globally routable public address.
 * Returns 0 for:
 * - Private/LAN addresses (10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16)
 * - Loopback addresses (127.0.0.0/8)
 * - Link-local addresses (169.254.0.0/16)
 * - Broadcast addresses (255.255.255.255)
 * - Multicast addresses (224.0.0.0/4)
 * - Reserved/special use addresses
 * - Invalid addresses
 *
 * @note Returns 0 for invalid IPv4 addresses.
 *
 * @par Example
 * @code
 * if (is_internet_ipv4("8.8.8.8")) {
 *     // This is a public internet address
 * }
 * @endcode
 *
 * @ingroup util
 */
int is_internet_ipv4(const char *ip);

/**
 * @brief Check if IPv6 address is a public internet address
 * @param ip IPv6 address string (must not be NULL)
 * @return 1 if public internet address, 0 otherwise
 *
 * Checks if an IPv6 address is a globally routable public address.
 * Returns 0 for:
 * - Loopback address (::1)
 * - Link-local addresses (fe80::/10)
 * - Unique local addresses (fc00::/7)
 * - Multicast addresses (ff00::/8)
 * - Special use/reserved addresses
 * - Invalid addresses
 *
 * @note Returns 0 for invalid IPv6 addresses.
 *
 * @par Example
 * @code
 * if (is_internet_ipv6("2001:4860:4860::8888")) {
 *     // This is a public internet address
 * }
 * @endcode
 *
 * @ingroup util
 */
int is_internet_ipv6(const char *ip);

/**
 * @brief Extract IP address (without port) from formatted address string
 *
 * Extracts the IP address portion from a formatted address that includes a port.
 * Handles both IPv4 (e.g., "192.168.1.1:27224") and IPv6 with brackets
 * (e.g., "[2001:db8::1]:27224").
 *
 * @param addr_with_port Formatted address string with port
 * @param ip_out Output buffer for IP address (without port)
 * @param ip_out_size Size of output buffer
 * @return 0 on success, -1 on error
 *
 * @par Example
 * @code
 * char ip[64];
 * extract_ip_from_address("192.168.1.1:27224", ip, sizeof(ip));
 * // ip now contains "192.168.1.1"
 *
 * extract_ip_from_address("[::1]:27224", ip, sizeof(ip));
 * // ip now contains "::1"
 * @endcode
 *
 * @ingroup util
 */
int extract_ip_from_address(const char *addr_with_port, char *ip_out, size_t ip_out_size);

/**
 * @brief Get human-readable string describing IP address type
 *
 * Classifies an IP address and returns a descriptive string.
 * Possible return values:
 * - "All Interfaces" - Wildcard bind address (0.0.0.0 or ::)
 * - "Localhost" - Loopback address (127.0.0.0/8 or ::1)
 * - "LAN" - Private network address (10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16, or fc00::/7)
 * - "Internet" - Public internet address
 * - "Unknown" - Invalid or unrecognized address
 * - "" - Empty/null input
 *
 * @param ip IP address to classify (without port)
 * @return Constant string describing the IP type
 *
 * @par Example
 * @code
 * const char *type1 = get_ip_type_string("0.0.0.0");       // Returns "All Interfaces"
 * const char *type2 = get_ip_type_string("127.0.0.1");     // Returns "Localhost"
 * const char *type3 = get_ip_type_string("192.168.1.1");   // Returns "LAN"
 * const char *type4 = get_ip_type_string("8.8.8.8");       // Returns "Internet"
 * const char *type5 = get_ip_type_string("::1");           // Returns "Localhost"
 * @endcode
 *
 * @ingroup util
 */
const char *get_ip_type_string(const char *ip);

/** @} */
