/**
 * @file parsers.h
 * @brief Custom option parsers for enum types
 * @ingroup options
 *
 * This module provides custom callback parsers for enum-based options
 * used by the options builder system. Each parser converts a string
 * argument to the appropriate enum value.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#pragma once

#include <stdbool.h>
#include "platform/terminal.h"
#include "video/palette.h"
#include "log/logging.h"

/**
 * @brief Parse color setting option (--color flag)
 * @param arg String argument (e.g., "auto", "true", "false")
 * @param dest Destination pointer (int*, will store color_setting_t value)
 * @param error_msg Optional error message output (set on failure)
 * @return true on success, false on error
 *
 * Valid values:
 * - "auto", "a", "0" - Smart detection (COLOR_SETTING_AUTO, default)
 * - "true", "yes", "1", "on" - Force colors ON (COLOR_SETTING_TRUE)
 * - "false", "no", "-1", "off" - Force colors OFF (COLOR_SETTING_FALSE)
 *
 * This controls whether colors are enabled ("auto"), always on ("true"),
 * or always off ("false") regardless of TTY detection or environment variables.
 */
bool parse_color_setting(const char *arg, void *dest, char **error_msg);

/**
 * @brief Parse terminal color level option
 * @param arg String argument (e.g., "auto", "none", "16", "256", "truecolor")
 * @param dest Destination pointer (terminal_color_mode_t*)
 * @param error_msg Optional error message output (set on failure)
 * @return true on success, false on error
 *
 * Valid values:
 * - "auto", "a" - Auto-detect from terminal
 * - "none", "mono", "monochrome", "0" - No color (TERM_COLOR_NONE)
 * - "16", "16color", "ansi" - 16-color mode (TERM_COLOR_16)
 * - "256", "256color" - 256-color mode (TERM_COLOR_256)
 * - "truecolor", "true", "tc", "rgb", "24bit" - Truecolor mode (TERM_COLOR_TRUECOLOR)
 */
bool parse_color_mode(const char *arg, void *dest, char **error_msg);

/**
 * @brief Parse render mode option
 * @param arg String argument (e.g., "foreground", "background", "half-block")
 * @param dest Destination pointer (render_mode_t*)
 * @param error_msg Optional error message output (set on failure)
 * @return true on success, false on error
 *
 * Valid values:
 * - "foreground", "fg", "0" - Foreground mode (RENDER_MODE_FOREGROUND)
 * - "background", "bg", "1" - Background mode (RENDER_MODE_BACKGROUND)
 * - "half-block", "half", "hb", "2" - Half-block mode (RENDER_MODE_HALF_BLOCK)
 */
bool parse_render_mode(const char *arg, void *dest, char **error_msg);

/**
 * @brief Parse palette type option
 * @param arg String argument (e.g., "standard", "blocks", "custom")
 * @param dest Destination pointer (palette_type_t*)
 * @param error_msg Optional error message output (set on failure)
 * @return true on success, false on error
 *
 * Valid values:
 * - "standard", "std", "0" - Standard palette (PALETTE_STANDARD)
 * - "blocks", "block", "1" - Blocks palette (PALETTE_BLOCKS)
 * - "digital", "dig", "2" - Digital palette (PALETTE_DIGITAL)
 * - "minimal", "min", "3" - Minimal palette (PALETTE_MINIMAL)
 * - "cool", "4" - Cool palette (PALETTE_COOL)
 * - "custom", "5" - Custom palette (PALETTE_CUSTOM)
 */
bool parse_palette_type(const char *arg, void *dest, char **error_msg);

/**
 * @brief Parse custom palette characters option
 * @param arg String of characters ordered from darkest to brightest
 * @param dest Destination pointer (char[256])
 * @param error_msg Optional error message output (set on failure)
 * @return true on success, false on error
 *
 * The characters should be ordered from darkest (leftmost) to brightest (rightmost).
 * Maximum length is 255 characters.
 *
 * Example: " .:-=+*#%@"
 */
bool parse_palette_chars(const char *arg, void *dest, char **error_msg);

/**
 * @brief Parse log level option
 * @param arg String argument (e.g., "debug", "info", "warn")
 * @param dest Destination pointer (log_level_t*)
 * @param error_msg Optional error message output (set on failure)
 * @return true on success, false on error
 *
 * Valid values:
 * - "dev", "development", "0" - Development level (LOG_DEV)
 * - "debug", "dbg", "1" - Debug level (LOG_DEBUG)
 * - "info", "information", "2" - Info level (LOG_INFO)
 * - "warn", "warning", "3" - Warning level (LOG_WARN)
 * - "error", "err", "4" - Error level (LOG_ERROR)
 * - "fatal", "5" - Fatal level (LOG_FATAL)
 */
bool parse_log_level(const char *arg, void *dest, char **error_msg);

/**
 * @brief Parse and validate port option for CLI
 * @param arg Port string to parse
 * @param dest Destination pointer (char* for port string storage)
 * @param error_msg Error message output (set on failure, caller must free)
 * @return true on success, false on error
 *
 * Validates port is a number in the range 1-65535.
 */
bool parse_port_option(const char *arg, void *dest, char **error_msg);

// ============================================================================
// Positional Argument Parsers
// ============================================================================

/**
 * @brief Parse server bind address positional argument
 * @param arg Current bind address argument
 * @param config Pointer to options struct (must contain address/address6 fields)
 * @param remaining Remaining positional args (for multi-arg parsing)
 * @param num_remaining Count of remaining args
 * @param error_msg Error message output (set on failure)
 * @return Number of args consumed (0-2), or -1 on error
 *
 * Server bind address parsing rules:
 * - 0 args total: Uses defaults (127.0.0.1 + ::1 for dual-stack localhost)
 * - 1 arg: Single IPv4 OR IPv6 bind address
 * - 2 args: One IPv4 AND one IPv6 bind address (order-independent)
 * - Cannot specify multiple addresses of the same type
 * - IPv6 addresses can be wrapped in brackets (e.g., [::1])
 *
 * This parser can consume 0-2 arguments depending on what's provided.
 * It tracks internal state to ensure only one IPv4 and one IPv6 are specified.
 *
 * Example usage with options_builder_add_positional():
 * ```c
 * options_builder_add_positional(
 *     builder,
 *     "bind-address",
 *     "IPv4 or IPv6 bind address (can specify 0-2 addresses)",
 *     false,  // Not required (defaults to localhost)
 *     parse_server_bind_address
 * );
 * ```
 */
int parse_server_bind_address(const char *arg, void *config, char **remaining, int num_remaining, char **error_msg);

/**
 * @brief Parse client address positional argument
 * @param arg Address argument in format [address][:port]
 * @param config Pointer to options struct (must contain address and port fields)
 * @param remaining Remaining positional args (unused for client)
 * @param num_remaining Count of remaining args
 * @param error_msg Error message output (set on failure)
 * @return Number of args consumed (always 1), or -1 on error
 *
 * Client address parsing rules:
 * - Parses single argument: [address][:port]
 * - IPv6 with brackets and port: [::1]:8080
 * - IPv4/hostname with port: 192.168.1.1:8080 or example.com:8080
 * - Bare IPv6: ::1 (detected by multiple colons)
 * - Bare hostname/IPv4: 192.168.1.1 or example.com
 * - IPv4 addresses starting with digit are validated strictly
 * - Detects port conflict if --port flag was already used
 *
 * This parser handles the complex logic of separating address from optional port
 * while correctly handling IPv6 addresses that contain colons.
 *
 * Example usage with options_builder_add_positional():
 * ```c
 * options_builder_add_positional(
 *     builder,
 *     "address",
 *     "[address][:port] - Server address (IPv4, IPv6, or hostname) with optional port",
 *     false,  // Not required (defaults to localhost:27224)
 *     parse_client_address
 * );
 * ```
 */
int parse_client_address(const char *arg, void *config, char **remaining, int num_remaining, char **error_msg);

/**
 * @brief Custom parser for --verbose flag
 *
 * Allows --verbose to work both as a flag (without argument) and with an optional
 * count argument. Increments verbose_level each time called.
 */
bool parse_verbose_flag(const char *arg, void *dest, char **error_msg);

/**
 * @brief Custom parser for --cookies-from-browser flag
 *
 * Allows --cookies-from-browser to work both as a flag (without argument) and with an optional
 * argument. Sets cookies_from_browser to the provided browser name.
 */
bool parse_cookies_from_browser(const char *arg, void *dest, char **error_msg);

/**
 * @brief Custom parser for --no-cookies-from-browser flag
 */
bool parse_no_cookies_from_browser(const char *arg, void *dest, char **error_msg);

/**
 * @brief Custom parser for --timestamp flag
 *
 * Allows --timestamp to work both as a flag with an argument.
 * Sets media_seek_timestamp to the provided timestamp in seconds.
 */
bool parse_timestamp(const char *arg, void *dest, char **error_msg);

/**
 * @brief Custom parser for volume options (--volume, --speakers-volume, --microphone-volume)
 *
 * Validates that the volume is a float value between 0.0 and 1.0.
 * Sets the destination float to the parsed volume value.
 */
bool parse_volume(const char *arg, void *dest, char **error_msg);

/**
 * @brief Custom parser for log file paths (--log-file, -L)
 *
 * Validates that the log file path is safe:
 * - Rejects attempts to write to protected system directories (/etc, /System, /Windows, etc.)
 * - Allows overwriting existing ascii-chat log files
 * - Allows paths in safe locations (/tmp, /var/log, home directory, cwd, etc.)
 * - Returns error message if validation fails
 */
bool parse_log_file(const char *arg, void *dest, char **error_msg);
