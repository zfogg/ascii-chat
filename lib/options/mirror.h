/**
 * @file mirror.h
 * @brief Mirror mode option parsing
 * @ingroup options
 *
 * Mirror-specific command-line option parsing and help text. Mirror mode is a
 * standalone webcam-to-ASCII converter that displays output locally without any
 * network communication. It's a subset of client mode with networking and
 * encryption features removed.
 *
 * **Mirror Mode Use Cases**:
 * - Testing ASCII conversion without server connection
 * - Creating ASCII art from webcam for screenshots/recordings
 * - Debugging video processing pipeline in isolation
 * - Generating ASCII art for piping to files or other programs
 *
 * **Mirror-Specific Options**:
 * - Display: `--width`, `--height`, `--color-mode`, `--render-mode`, `--fps`
 * - Webcam: `--webcam-index`, `--webcam-flip`, `--test-pattern`
 * - Output: `--stretch`, `--quiet`, `--snapshot`, `--strip-ansi`
 * - Palette: `--palette`, `--palette-chars`
 * - Debug: `--show-capabilities`, `--utf8`, `--list-webcams`
 *
 * **Excluded from Mirror Mode** (client-only):
 * - Network: No server connection, no port, no address
 * - Audio: No microphone/speaker options
 * - Encryption: No keys, no passwords, no handshake
 * - Compression: No network encoding
 * - Connection: No reconnection logic
 *
 * **Comparison with Client Mode**:
 * @code
 * // Client: Connect to server, send/receive video + audio
 * ./ascii-chat client localhost --audio --key ~/.ssh/id_ed25519
 *
 * // Mirror: Local display only, no networking
 * ./ascii-chat mirror --webcam-index 0 --snapshot
 * @endcode
 *
 * @see options.h
 * @see common.h
 * @see client.h
 * @see server.h
 */

#pragma once

#include "options/options.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parse mirror-specific command-line options
 *
 * Parses all mirror mode options including:
 * - Display options (width, height, color-mode, render-mode, fps)
 * - Webcam options (webcam-index, webcam-flip, test-pattern)
 * - Output options (stretch, quiet, snapshot, strip-ansi)
 * - Palette options (palette, palette-chars)
 * - Debug options (show-capabilities, utf8, list-webcams)
 *
 * **Parsing Logic**:
 * 1. getopt_long() loop processes all flags
 * 2. No positional arguments (mirror is standalone)
 * 3. Set default values for unspecified options
 * 4. Validate terminal dimensions and webcam index
 *
 * **Error Handling**:
 * - Invalid options: Prints error with typo suggestions
 * - Missing required arguments: Prints usage hint
 * - Invalid values: Error with clear message
 * - Returns ERROR_INVALID_PARAM on any validation failure
 *
 * @param argc Argument count from main()
 * @param argv Argument vector from main()
 * @return ASCIICHAT_OK on success, ERROR_INVALID_PARAM on parse error
 *
 * @note Modifies global opt_* variables (opt_width, opt_height, opt_webcam_index, etc.)
 * @note Prints error messages to stderr on failure
 * @note Prints help text and exits if --help flag present
 * @note No network or encryption options available in mirror mode
 *
 * **Common Usage Patterns**:
 * @code
 * // Display webcam as ASCII art in terminal
 * ./ascii-chat mirror
 *
 * // Capture single frame and exit (screenshot)
 * ./ascii-chat mirror --snapshot --snapshot-delay 3
 *
 * // Use test pattern for debugging
 * ./ascii-chat mirror --test-pattern --color-mode truecolor
 *
 * // Custom palette and dimensions
 * ./ascii-chat mirror --palette custom --palette-chars " .:-=+*#%@" --width 120 --height 40
 *
 * // Quiet mode for piping to file
 * ./ascii-chat mirror --quiet --snapshot > ascii_art.txt
 *
 * // Strip ANSI for plain text output
 * ./ascii-chat mirror --strip-ansi --snapshot > ascii_art_plain.txt
 * @endcode
 *
 * Example implementation:
 * @code
 * int main(int argc, char **argv) {
 *     asciichat_error_t err = options_init(argc, argv, MODE_MIRROR);
 *     if (err != ASCIICHAT_OK) {
 *         return 1;  // Error already printed
 *     }
 *
 *     // Option globals are now populated:
 *     log_info("Mirror mode: %dx%d @ %d FPS", opt_width, opt_height, opt_fps);
 *     log_info("Webcam: %d, Test pattern: %s",
 *              opt_webcam_index, opt_test_pattern ? "yes" : "no");
 *
 *     // Initialize webcam
 *     webcam_t *webcam = webcam_init(opt_webcam_index);
 *     if (!webcam) {
 *         log_fatal("Failed to initialize webcam");
 *         return 1;
 *     }
 *
 *     // Capture and display loop
 *     while (!should_exit) {
 *         image_t *frame = webcam_capture(webcam);
 *         ascii_buffer_t *ascii = image_to_ascii(frame, opt_width, opt_height);
 *         printf("%s", ascii->data);
 *         fflush(stdout);
 *         platform_sleep_ms(1000 / opt_fps);
 *     }
 *
 *     webcam_destroy(webcam);
 *     return 0;
 * }
 * @endcode
 *
 * @see parse_client_options()
 * @see parse_server_options()
 * @see options_init()
 */
asciichat_error_t parse_mirror_options(int argc, char **argv, options_t *opts);

/**
 * @brief Print mirror mode usage/help text
 *
 * Displays comprehensive help for all mirror options, including:
 * - Description of mirror mode and use cases
 * - All mirror-specific flags with descriptions
 * - Palette and output options
 * - Debug options
 * - Usage examples for common scenarios
 * - Notes on differences from client mode
 *
 * **Output Format**:
 * ```
 * Mirror mode: Display local webcam as ASCII art (no network connection)
 *
 * Usage: ascii-chat mirror [OPTIONS]
 *
 * Display Options:
 *   -x, --width WIDTH       Terminal width in characters (default: auto-detect)
 *   -y, --height HEIGHT     Terminal height in characters (default: auto-detect)
 *   --fps FPS               Target framerate (1-144, default: 30)
 *   --color-mode MODE       Color mode: auto, mono, 16, 256, truecolor (default: auto)
 *   --render-mode MODE      Render mode: foreground, background (default: foreground)
 *
 * Webcam Options:
 *   -c, --webcam-index N    Webcam device index (default: 0)
 *   -f, --webcam-flip       Mirror webcam horizontally (default: true)
 *   --test-pattern          Use test pattern instead of real webcam
 *   --list-webcams          List available webcams and exit
 *
 * Output Options:
 *   -s, --stretch           Stretch/shrink without preserving aspect ratio
 *   -q, --quiet             Disable console logging (log to file only)
 *   -S, --snapshot          Capture one frame and exit
 *   --snapshot-delay SEC    Delay before snapshot (default: 3.0 seconds)
 *   --strip-ansi            Strip ANSI escape sequences from output
 *
 * Palette Options:
 *   -P, --palette TYPE      Palette type: standard, blocks, digital, minimal, cool, custom
 *   -C, --palette-chars S   Custom palette characters (dark to bright)
 *
 * Debug Options:
 *   --show-capabilities     Show terminal capabilities and exit
 *   --utf8                  Force UTF-8 output (enables multi-byte characters)
 *
 * [... continues with examples ...]
 * ```
 *
 * @param stream Output stream (stdout for --help, stderr for errors)
 *
 * @note Does not exit - caller decides whether to exit after printing
 * @note Explicitly mentions that network/audio/crypto options are not available
 * @note Examples show standalone local usage patterns
 *
 * Example usage:
 * @code
 * // Print help and exit successfully
 * if (help_requested) {
 *     usage_mirror(stdout);
 *     exit(0);
 * }
 *
 * // Print usage hint on error
 * if (parse_error) {
 *     fprintf(stderr, "Error: Invalid option\n\n");
 *     usage_mirror(stderr);
 *     exit(1);
 * }
 * @endcode
 *
 * @see usage_client()
 * @see usage_server()
 */
void usage_mirror(FILE *stream);

#ifdef __cplusplus
}
#endif
