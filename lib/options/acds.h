/**
 * @file acds.h
 * @brief ACDS (Discovery Service) mode option parsing
 * @ingroup options
 *
 * ACDS-specific command-line option parsing and help text. The ACDS mode
 * runs a discovery service for session management and WebRTC signaling.
 *
 * **ACDS-Specific Options**:
 * - Network: `[address1] [address2]` positional (0-2 bind addresses), `--port`
 * - Database: `--database` (SQLite database path)
 * - Identity: `--key` (Ed25519 identity key path)
 * - Logging: `--log-file`, `--log-level`
 *
 * **Positional Arguments** (Bind Addresses):
 * - 0 arguments: Bind to all IPv4 (0.0.0.0) and IPv6 (::) interfaces
 * - 1 argument: Bind only to this IPv4 OR IPv6 address
 * - 2 arguments: Bind to both addresses (must be one IPv4 AND one IPv6, order-independent)
 *
 * **Dual-Stack Examples**:
 * @code
 * ./acds                          # All interfaces (0.0.0.0 + ::)
 * ./acds 0.0.0.0                  # IPv4 only (all interfaces)
 * ./acds ::                       # IPv6 only (all interfaces)
 * ./acds 0.0.0.0 ::               # Both (full dual-stack)
 * ./acds :: 0.0.0.0               # Both (order-independent)
 * ./acds 192.168.1.100            # Specific IPv4
 * ./acds 192.168.1.100 ::1        # Specific IPv4 + localhost IPv6
 * @endcode
 *
 * @see options.h
 * @see common.h
 */

#pragma once

#include "options/options.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief TCP listen port (ACDS mode only)
 *
 * Port number for the discovery service to listen on.
 *
 * **Default**: `27225` (ACDS default port)
 *
 * **Command-line**: `--port <port>` or `-p <port>`
 *
 * @note ACDS mode only
 * @ingroup options
 */
extern ASCIICHAT_API int opt_acds_port;

/**
 * @brief SQLite database path (ACDS mode only)
 *
 * Path to SQLite database for session storage and management.
 *
 * **Default**: `~/.config/ascii-chat/acds.db`
 *
 * **Command-line**: `--database <path>` or `-d <path>`
 *
 * @note ACDS mode only
 * @ingroup options
 */
extern ASCIICHAT_API char opt_acds_database_path[OPTIONS_BUFF_SIZE];

/**
 * @brief Ed25519 identity key path (ACDS mode only)
 *
 * Path to Ed25519 identity key for server identity.
 *
 * **Default**: `~/.config/ascii-chat/acds_identity`
 *
 * **Command-line**: `--key <path>` or `-k <path>`
 *
 * @note ACDS mode only
 * @ingroup options
 */
extern ASCIICHAT_API char opt_acds_key_path[OPTIONS_BUFF_SIZE];

/**
 * @brief Parse ACDS-specific command-line options
 *
 * Parses all ACDS mode options including:
 * - Positional arguments: 0-2 bind addresses (IPv4/IPv6)
 * - ACDS-specific flags (port, database, key)
 * - Logging flags (log-file, log-level)
 *
 * **Parsing Logic**:
 * 1. getopt_long() loop processes all flags
 * 2. After loop, parse 0-2 positional arguments for bind addresses
 * 3. Validate address types (one IPv4, one IPv6 if two provided)
 * 4. Set defaults for unspecified options
 *
 * **Address Auto-Detection**:
 * Uses is_valid_ipv4() and is_valid_ipv6() to determine address type automatically.
 * Order-independent: `0.0.0.0 ::` and `:: 0.0.0.0` are equivalent.
 *
 * **Error Handling**:
 * - Invalid options: Prints error with typo suggestions
 * - Missing required arguments: Prints usage hint
 * - Multiple IPv4 or IPv6 addresses: Error (must be one of each)
 * - Invalid IP format: Error with clear message
 * - Returns ERROR_INVALID_PARAM on any validation failure
 *
 * @param argc Argument count from main()
 * @param argv Argument vector from main()
 * @return ASCIICHAT_OK on success, ERROR_INVALID_PARAM on parse error
 *
 * @note Modifies global opt_* variables (opt_address, opt_address6, opt_acds_port, etc.)
 * @note Prints error messages to stderr on failure
 * @note Prints help text and exits if --help flag present
 *
 * **Positional Argument Validation**:
 * @code
 * // ✅ CORRECT - valid combinations
 * ./acds                          # 0 args: defaults (all interfaces)
 * ./acds 0.0.0.0                  # 1 arg: IPv4 only
 * ./acds ::                       # 1 arg: IPv6 only
 * ./acds 0.0.0.0 ::               # 2 args: IPv4 + IPv6
 * ./acds :: 192.168.1.100         # 2 args: IPv6 + IPv4 (order OK)
 *
 * // ❌ WRONG - invalid combinations
 * ./acds 0.0.0.0 192.168.1.100    # Error: two IPv4 addresses
 * ./acds :: ::1                   # Error: two IPv6 addresses
 * ./acds invalid-address          # Error: invalid IP format
 * ./acds 1 2 3                    # Error: too many addresses (max 2)
 * @endcode
 *
 * Example usage:
 * @code
 * int main(int argc, char **argv) {
 *     asciichat_error_t err = options_init(argc, argv, MODE_ACDS);
 *     if (err != ASCIICHAT_OK) {
 *         return 1;  // Error already printed
 *     }
 *
 *     // Option globals are now populated:
 *     log_info("Binding to IPv4: %s, IPv6: %s, port: %d",
 *              opt_address, opt_address6, opt_acds_port);
 *     log_info("Database: %s", opt_acds_database_path);
 *     log_info("Identity key: %s", opt_acds_key_path);
 *
 *     // ... proceed with ACDS initialization
 * }
 * @endcode
 *
 * @see acds_usage()
 * @see options_init()
 */
asciichat_error_t acds_options_parse(int argc, char **argv);

/**
 * @brief Print ACDS version information
 *
 * Prints version, build type, build date, compiler, and C library information.
 * Typically called when --version flag is provided.
 *
 * @note Does not exit - caller decides whether to exit after printing
 *
 * Example usage:
 * @code
 * if (version_requested) {
 *     acds_print_version();
 *     exit(0);
 * }
 * @endcode
 */
void acds_print_version(void);

/**
 * @brief Print ACDS mode usage/help text
 *
 * Displays comprehensive help for all ACDS options, including:
 * - Description of ACDS mode
 * - Positional argument format (bind addresses)
 * - Dual-stack binding examples
 * - All ACDS-specific flags with descriptions
 * - Usage examples for common scenarios
 *
 * **Output Format**:
 * ```
 * ACDS mode: Discovery service for session management and WebRTC signaling
 *
 * Usage: acds [OPTIONS] [address1] [address2]
 *
 * Positional Arguments (Bind Addresses):
 *   [address1] [address2]   Bind addresses (0-2 arguments)
 *                           0 args: Bind to all interfaces (0.0.0.0 and ::)
 *                           1 arg:  Bind to this IPv4 OR IPv6 address
 *                           2 args: Bind to both (one IPv4, one IPv6)
 *
 * Examples:
 *   acds                          # All interfaces (0.0.0.0 + ::)
 *   acds 0.0.0.0                  # All IPv4 interfaces
 *   acds ::                       # All IPv6 interfaces
 *   acds 0.0.0.0 ::               # All interfaces (dual-stack)
 *   acds 192.168.1.100 ::1        # Specific IPv4 + localhost IPv6
 *
 * Network Options:
 *   -p, --port PORT         Listening port (default: 27225)
 *
 * [... continues with all option categories ...]
 * ```
 *
 * @param stream Output stream (stdout for --help, stderr for errors)
 *
 * @note Does not exit - caller decides whether to exit after printing
 * @note Always includes mode description, positional args, and all flags
 * @note Examples show real-world deployment scenarios
 *
 * Example usage:
 * @code
 * // Print help and exit successfully
 * if (help_requested) {
 *     acds_usage(stdout);
 *     exit(0);
 * }
 *
 * // Print usage hint on error
 * if (parse_error) {
 *     fprintf(stderr, "Error: Invalid bind address\n\n");
 *     acds_usage(stderr);
 *     exit(1);
 * }
 * @endcode
 *
 * @see usage_server()
 * @see usage_client()
 */
void acds_usage(FILE *stream);

#ifdef __cplusplus
}
#endif
