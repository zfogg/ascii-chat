/**
 * @file discovery.h
 * @brief Discovery mode option parsing
 * @ingroup options
 *
 * Discovery-specific command-line option parsing and help text. Discovery mode
 * allows participants to join a session and dynamically become the host based
 * on NAT quality assessment.
 *
 * **Discovery Mode Philosophy**:
 * "ascii-chat should be as simple as making a phone call."
 * - One command to start a session
 * - One command to join a session
 * - Automatic host negotiation based on NAT quality
 *
 * **Discovery Mode Use Cases**:
 * - Joining a session by session string (word-word-word format)
 * - Automatic host negotiation based on NAT quality
 * - Dynamic role switching during session
 * - Seamless failover when host disconnects
 *
 * **Discovery-Specific Options**:
 * - Session: Session string (positional argument, required)
 * - Display: `--width`, `--height`, `--color-mode`, `--render-mode`
 * - Webcam: `--webcam-index`, `--webcam-flip`, `--test-pattern`
 * - Audio: `--audio`, `--microphone-index`, `--speakers-index`
 * - ACDS: `--acds-server`, `--acds-port`, `--webrtc`
 * - Security: `--key`, `--password`, `--encrypt`
 *
 * @see options.h
 * @see presets.c (options_preset_discovery)
 */

#pragma once

#include "options/options.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parse discovery-specific command-line options
 *
 * Parses all discovery mode options including:
 * - Session string (required positional argument)
 * - Display options (width, height, color-mode, render-mode)
 * - Webcam options (webcam-index, webcam-flip, test-pattern)
 * - Audio options (audio, microphone-index, speakers-index)
 * - ACDS options (acds-server, acds-port, webrtc)
 * - Security options (key, password, encrypt)
 *
 * **Parsing Logic**:
 * 1. getopt_long() loop processes all flags
 * 2. Session string is required as positional argument
 * 3. Validate ACDS configuration
 * 4. Return error if session string is missing
 *
 * @param argc Argument count from main()
 * @param argv Argument vector from main()
 * @param opts Options structure to populate
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * **Usage Examples**:
 * @code
 * // Join session with default options
 * ./ascii-chat discovery swift-river-mountain
 *
 * // Join session with audio enabled
 * ./ascii-chat discovery swift-river-mountain --audio
 *
 * // Join session with custom ACDS server
 * ./ascii-chat discovery swift-river-mountain --acds-server example.com
 * @endcode
 *
 * @see parse_client_options()
 * @see parse_server_options()
 */
asciichat_error_t parse_discovery_options(int argc, char **argv, options_t *opts);


#ifdef __cplusplus
}
#endif
