/**
 * @file server.h
 * @brief Server mode option parsing
 * @ingroup options
 *
 * Server-specific command-line option parsing and help text. The server mode
 * accepts connections from multiple clients, mixes their video into a grid layout,
 * and broadcasts the composite stream back to all connected clients.
 *
 * **Server-Specific Options**:
 * - Network: `[address1] [address2]` positional (0-2 bind addresses), `--port`
 * - Access Control: `--client-keys` (authorized client public keys)
 * - Limits: `--max-clients` (connection limit, 1-9 for grid layout)
 * - Audio: `--no-audio-mixer` (debug option to disable mixing)
 * - Compression: `--compression-level`, `--no-compress`, `--encode-audio`
 * - Crypto: `--key` (server identity key)
 *
 * **Positional Arguments** (Bind Addresses):
 * - 0 arguments: Bind to 127.0.0.1 and ::1 (localhost dual-stack)
 * - 1 argument: Bind only to this IPv4 OR IPv6 address
 * - 2 arguments: Bind to both addresses (must be one IPv4 AND one IPv6, order-independent)
 *
 * **Dual-Stack Examples**:
 * @code
 * ./ascii-chat server                    # 127.0.0.1 + ::1 (default)
 * ./ascii-chat server 0.0.0.0            # IPv4 only (all interfaces)
 * ./ascii-chat server ::                 # IPv6 only (all interfaces)
 * ./ascii-chat server 0.0.0.0 ::         # Both (full dual-stack)
 * ./ascii-chat server :: 0.0.0.0         # Both (order-independent)
 * ./ascii-chat server 192.168.1.100      # Specific IPv4
 * ./ascii-chat server 192.168.1.100 ::1  # Specific IPv4 + localhost IPv6
 * @endcode
 *
 * **Shared Options** (parsed in options.c):
 * - Palette: `--palette`, `--palette-chars`
 * - Output: `--stretch`, `--quiet`, `--strip-ansi`
 * - Encryption: `--encrypt`, `--no-encrypt`, `--password`, `--keyfile`
 *
 * @see options.h
 * @see common.h
 * @see client.h
 * @see mirror.h
 */

#pragma once

#include "options/options.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parse server-specific command-line options
 *
 * Parses all server mode options including:
 * - Positional arguments: 0-2 bind addresses (IPv4/IPv6)
 * - Server-specific flags (max-clients, client-keys, no-audio-mixer)
 * - Network performance flags (compression-level, no-compress, encode-audio)
 * - Shared options (palette, encryption, output options)
 *
 * **Parsing Logic**:
 * 1. getopt_long() loop processes all flags
 * 2. After loop, parse 0-2 positional arguments for bind addresses
 * 3. Validate address types (one IPv4, one IPv6 if two provided)
 * 4. Set defaults for unspecified addresses (127.0.0.1 and ::1)
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
 * @note Modifies global opt_* variables (opt_address, opt_address6, opt_port, etc.)
 * @note Prints error messages to stderr on failure
 * @note Prints help text and exits if --help flag present
 *
 * **Positional Argument Validation**:
 * @code
 * // ✅ CORRECT - valid combinations
 * ./ascii-chat server                    # 0 args: defaults
 * ./ascii-chat server 0.0.0.0            # 1 arg: IPv4 only
 * ./ascii-chat server ::                 # 1 arg: IPv6 only
 * ./ascii-chat server 0.0.0.0 ::         # 2 args: IPv4 + IPv6
 * ./ascii-chat server :: 192.168.1.100   # 2 args: IPv6 + IPv4 (order OK)
 *
 * // ❌ WRONG - invalid combinations
 * ./ascii-chat server 0.0.0.0 192.168.1.100  # Error: two IPv4 addresses
 * ./ascii-chat server :: ::1                 # Error: two IPv6 addresses
 * ./ascii-chat server invalid-address        # Error: invalid IP format
 * ./ascii-chat server 1 2 3                  # Error: too many addresses (max 2)
 * @endcode
 *
 * **Client Access Control**:
 * @code
 * // Authorize specific clients by SSH key
 * ./ascii-chat server --client-keys github:alice,github:bob
 *
 * // Or use SSH public key files
 * ./ascii-chat server --client-keys ~/.ssh/client1.pub,~/.ssh/client2.pub
 *
 * // Or inline SSH public keys
 * ./ascii-chat server --client-keys "ssh-ed25519 AAAAC3... alice@laptop"
 * @endcode
 *
 * Example usage:
 * @code
 * int main(int argc, char **argv) {
 *     asciichat_error_t err = options_init(argc, argv, MODE_SERVER);
 *     if (err != ASCIICHAT_OK) {
 *         return 1;  // Error already printed
 *     }
 *
 *     // Option globals are now populated:
 *     log_info("Binding to IPv4: %s, IPv6: %s, port: %s",
 *              opt_address, opt_address6, opt_port);
 *     log_info("Max clients: %d", opt_max_clients);
 *     log_info("Client keys: %s", opt_client_keys[0] ? opt_client_keys : "none (allow all)");
 *
 *     // ... proceed with server initialization
 * }
 * @endcode
 *
 * @see parse_client_options()
 * @see parse_mirror_options()
 * @see options_init()
 */
asciichat_error_t parse_server_options(int argc, char **argv, options_t *opts);


#ifdef __cplusplus
}
#endif
