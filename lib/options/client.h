/**
 * @file client.h
 * @brief Client mode option parsing
 * @ingroup options
 *
 * Client-specific command-line option parsing and help text. The client mode
 * connects to a server and displays received video streams while capturing and
 * sending local webcam/audio data.
 *
 * **Client-Specific Options**:
 * - Network: `[address][:port]` positional, `--port`, `--lan`
 * - Webcam: `--webcam-index`, `--webcam-flip`, `--test-pattern`
 * - Display: `--width`, `--height`, `--color-mode`, `--render-mode`, `--fps`
 * - Audio: `--audio`, `--microphone-index`, `--speakers-index`, `--audio-analysis`
 * - Crypto: `--key`, `--server-key` (TOFU verification)
 * - Connection: `--reconnect`
 * - Compression: `--compression-level`, `--no-compress`, `--encode-audio`
 * - Debug: `--show-capabilities`, `--utf8`, `--list-webcams`, `--list-microphones`, `--list-speakers`
 *
 * **Positional Arguments**:
 * - Format: `[address][:port]`
 * - Examples: `localhost`, `192.168.1.100`, `example.com:8080`, `[::1]:27224`
 * - Port in positional conflicts with `--port` flag
 *
 * **Shared Options** (parsed in options.c):
 * - Palette: `--palette`, `--palette-chars`
 * - Output: `--stretch`, `--quiet`, `--snapshot`, `--strip-ansi`
 * - Encryption: `--encrypt`, `--no-encrypt`, `--password`, `--keyfile`
 *
 * @see options.h
 * @see common.h
 * @see server.h
 * @see mirror.h
 */

#pragma once

#include "options/options.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parse client-specific command-line options
 *
 * Parses all client mode options including:
 * - Positional argument: `[address][:port]` for server connection
 * - Client-specific flags (webcam, audio, display, network options)
 * - Shared options (palette, encryption, output options)
 *
 * **Parsing Logic**:
 * 1. getopt_long() loop processes all flags
 * 2. After loop, parse positional argument for server address
 * 3. Validate port conflicts (positional vs --port flag)
 * 4. Set default values for unspecified options
 *
 * **Error Handling**:
 * - Invalid options: Prints error with typo suggestions
 * - Missing required arguments: Prints usage hint
 * - Port conflicts: Errors if both positional port and --port specified
 * - Returns ERROR_INVALID_PARAM on any validation failure
 *
 * @param argc Argument count from main()
 * @param argv Argument vector from main()
 * @return ASCIICHAT_OK on success, ERROR_INVALID_PARAM on parse error
 *
 * @note Modifies global opt_* variables (opt_address, opt_port, opt_width, etc.)
 * @note Prints error messages to stderr on failure
 * @note Prints help text and exits if --help flag present
 *
 * **Positional Argument Parsing**:
 * After getopt_long() completes, optind points to first non-option argument.
 * Format: `[address][:port]`
 * - IPv4: `192.168.1.100`, `192.168.1.100:8080`
 * - IPv6: `[::1]`, `[2001:db8::1]:8080`
 * - Hostname: `localhost`, `example.com:27224`
 *
 * Port conflicts:
 * @code
 * // ❌ WRONG - conflicting port specifications
 * ./ascii-chat client localhost:8080 --port 9090
 * // Error: Cannot specify port in both positional argument and --port flag
 *
 * // ✅ CORRECT - specify port only once
 * ./ascii-chat client localhost:8080
 * ./ascii-chat client localhost --port 8080
 * @endcode
 *
 * Example usage:
 * @code
 * int main(int argc, char **argv) {
 *     asciichat_error_t err = options_init(argc, argv, MODE_CLIENT);
 *     if (err != ASCIICHAT_OK) {
 *         return 1;  // Error already printed
 *     }
 *
 *     // Option globals are now populated:
 *     log_info("Connecting to %s:%s", opt_address, opt_port);
 *     log_info("Terminal size: %dx%d", opt_width, opt_height);
 *     log_info("Webcam: %d, Audio: %s", opt_webcam_index,
 *              opt_audio_enabled ? "enabled" : "disabled");
 *
 *     // ... proceed with client initialization
 * }
 * @endcode
 *
 * @see parse_server_options()
 * @see parse_mirror_options()
 * @see options_init()
 */
asciichat_error_t parse_client_options(int argc, char **argv, options_t *opts);

/**
 * @brief Print client mode usage/help text
 *
 * Displays comprehensive help for all client options, including:
 * - Description of client mode
 * - Positional argument format and examples
 * - All client-specific flags with descriptions
 * - Shared flags (palette, encryption, output)
 * - Usage examples for common scenarios
 *
 * **Output Format**:
 * ```
 * Client mode: Connect to ascii-chat server and display video grid
 *
 * Usage: ascii-chat client [OPTIONS] [address][:port]
 *
 * Positional Arguments:
 *   [address][:port]        Server address with optional port (default: localhost:27224)
 *                           Examples: localhost, 192.168.1.100:8080, [::1]:27224
 *
 * Network Options:
 *   -p, --port PORT         Server port (default: 27224)
 *                           Note: Conflicts with port in positional argument
 *
 * Webcam Options:
 *   -c, --webcam-index N    Webcam device index (default: 0)
 *   -f, --webcam-flip       Mirror webcam horizontally (default: true)
 *   --test-pattern          Use test pattern instead of real webcam
 *   --list-webcams          List available webcams and exit
 *
 * [... continues with all option categories ...]
 * ```
 *
 * @param stream Output stream (stdout for --help, stderr for errors)
 *
 * @note Does not exit - caller decides whether to exit after printing
 * @note Always includes mode description, positional args, and all flags
 * @note Examples show real-world usage patterns
 *
 * Example usage:
 * @code
 * // Print help and exit successfully
 * if (help_requested) {
 *     usage_client(stdout);
 *     exit(0);
 * }
 *
 * // Print usage hint on error
 * if (parse_error) {
 *     fprintf(stderr, "Error: Invalid option\n\n");
 *     usage_client(stderr);
 *     exit(1);
 * }
 * @endcode
 *
 * @see usage_server()
 * @see usage_mirror()
 */
void usage_client(FILE *stream);

#ifdef __cplusplus
}
#endif
