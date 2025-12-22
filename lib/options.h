/**
 * @defgroup options Options Module
 * @ingroup module_core
 * @brief ⚙️ The command-line flags available
 *
 * @file options.h
 * @brief ⚙️ Command-line options parsing and configuration management for ascii-chat
 * @ingroup options
 * @addtogroup options
 * @{
 *
 * This header provides comprehensive functionality for parsing command-line arguments
 * and managing configuration settings for both client and server modes of ascii-chat.
 * It serves as the central configuration system, parsing user preferences and providing
 * defaults for all application settings.
 *
 * **Design Philosophy**:
 *
 * The options system follows a global configuration pattern where all options are stored
 * in global variables that can be accessed throughout the application. This design:
 * - **Simplifies access**: No need to pass configuration objects around
 * - **Single source of truth**: Options are parsed once at startup
 * - **Validation at parse time**: Invalid options are rejected immediately
 * - **Sensible defaults**: All options have reasonable default values
 * - **Mode-aware**: Different options for client vs server modes
 *
 * **Option Categories**:
 *
 * Options are organized into logical categories:
 * - **Terminal Dimensions**: Width, height, auto-detection
 * - **Network Configuration**: Server address, port, IPv4/IPv6
 * - **Webcam Settings**: Device index, flip, test pattern
 * - **Display Options**: Color mode, render mode, UTF-8, capabilities
 * - **Audio Configuration**: Audio enable/disable, device selection
 * - **Image Options**: Aspect ratio preservation, stretching
 * - **Output Options**: Quiet mode, snapshot mode, log file
 * - **Encryption Options**: Key files, passwords, client/server keys
 * - **Palette Configuration**: Palette type, custom characters, luminance weights
 *
 * **Option Parsing**:
 *
 * Options are parsed using the standard POSIX getopt() interface (with Windows compatibility
 * via platform/windows/getopt.h). The parser:
 * - Supports both short (`-w 80`) and long (`--width 80`) option formats
 * - Validates option values (e.g., numeric ranges, file existence)
 * - Provides helpful error messages for invalid options
 * - Prints usage information for `--help`
 * - Handles mode-specific options (client vs server)
 *
 * **Default Values**:
 *
 * All options have sensible defaults that work out-of-the-box:
 * - Terminal dimensions: Auto-detect from terminal size
 * - Network: `localhost:27224` (IPv4)
 * - Webcam: First available device (index 0)
 * - Color mode: Auto-detect terminal capabilities
 * - Encryption: Disabled by default (can be enabled with `--key` or `--password`)
 *
 * **Option Lifecycle**:
 *
 * 1. **Initialization**: Call `options_init()` at program startup
 * 2. **Parsing**: `options_init()` parses command-line arguments
 * 3. **Validation**: Options are validated and defaults applied
 * 4. **Usage**: Access option values via global variables throughout the application
 * 5. **Updates**: Some options can be updated dynamically (e.g., terminal dimensions)
 *
 * **Usage Example**:
 *
 * @code{.c}
 * #include "options.h"
 *
 * int main(int argc, char **argv) {
 *     // Parse command-line options
 *     asciichat_error_t err = options_init(argc, argv, true);  // is_client = true
 *     if (err != ASCIICHAT_OK) {
 *         if (err == ERROR_USAGE) {
 *             usage(stderr, true);  // Print usage and exit
 *         }
 *         return 1;
 *     }
 *
 *     // Access parsed options
 *     printf("Connecting to %s:%s\n", opt_address, opt_port);
 *     printf("Terminal size: %dx%d\n", opt_width, opt_height);
 *     if (opt_encrypt_enabled) {
 *         printf("Encryption enabled\n");
 *     }
 *
 *     // Use options throughout application
 *     // ...
 *
 *     return 0;
 * }
 * @endcode
 *
 * **Command-Line Options**:
 *
 * Common options (see `usage()` function for complete list):
 * - `--width`, `-w`: Set terminal width in characters
 * - `--height`, `-h`: Set terminal height in characters
 * - `--address`, `-a`: Server address (client) or bind address (server)
 * - `--port`, `-p`: Server port
 * - `--key`, `-k`: SSH key file path for encryption
 * - `--password`: Password for authentication
 * - `--color`, `--256`, `--truecolor`: Force color mode
 * - `--palette`: Select ASCII palette
 * - `--help`: Print usage information and exit
 *
 * **Thread Safety**:
 *
 * Options are parsed once at startup in the main thread before any worker threads
 * are created. After parsing, options are effectively read-only (with some exceptions
 * for dynamic updates like terminal dimensions). Therefore, thread safety is not
 * a concern for most options. If options need to be modified at runtime, ensure
 * proper synchronization.
 *
 * **Option Validation**:
 *
 * The parser validates:
 * - Numeric ranges (e.g., webcam index must be >= 0)
 * - File existence (e.g., key files, log files)
 * - Format correctness (e.g., IP addresses, port numbers)
 * - Mode compatibility (some options are client-only or server-only)
 *
 * Invalid options result in `ERROR_USAGE` being returned, and usage information
 * is printed to help the user correct their command line.
 *
 * @note All options are stored as global variables. This is intentional for simplicity.
 * @note Options are parsed once at startup via `options_init()`.
 * @note Most options remain constant after parsing (read-only).
 * @note Some options (e.g., terminal dimensions) can be updated dynamically.
 *
 * @see options_init() for option parsing
 * @see usage() for usage information
 * @see platform/terminal.h for terminal capability detection
 * @see palette.h for palette configuration
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#pragma once

#include <stdbool.h>
#include <stdio.h>
#include "platform/terminal.h"
#include "palette.h"

/**
 * @name Configuration Constants
 * @{
 */

/** @brief Buffer size for option string values
 *
 * Maximum size for string-based options (e.g., addresses, file paths, passwords).
 * Used for arrays that store option values.
 *
 * @ingroup options
 */
#define OPTIONS_BUFF_SIZE 256

/** @brief Default terminal width in characters
 *
 * Default width used when terminal size cannot be detected or when auto-detection
 * is disabled. This default provides a reasonable size for ASCII art display.
 *
 * @note This is used as a fallback if `--width` is not specified and auto-detection fails.
 *
 * @ingroup options
 */
#define OPT_WIDTH_DEFAULT 110

/** @brief Default terminal height in characters
 *
 * Default height used when terminal size cannot be detected or when auto-detection
 * is disabled. This default provides a reasonable size for ASCII art display.
 *
 * @note This is used as a fallback if `--height` is not specified and auto-detection fails.
 *
 * @ingroup options
 */
#define OPT_HEIGHT_DEFAULT 70

/** @} */

/**
 * @name Utility Functions
 * @{
 */

/**
 * @brief Safely parse string to integer with validation
 * @param str String to parse (must not be NULL)
 * @return Integer value on success, INT_MIN on error
 *
 * Parses a string to an integer with comprehensive validation:
 * - Validates that string is not NULL or empty
 * - Performs base-10 conversion using strtol()
 * - Checks for partial conversions (characters left unconverted)
 * - Validates result is within int range (INT_MIN to INT_MAX)
 * - Returns INT_MIN on any error condition
 *
 * This function is used internally by the options parser to safely convert
 * command-line argument strings to integer values with proper error handling.
 *
 * @note Returns INT_MIN on error (which is distinguishable from valid negative
 *       values since INT_MIN is a valid integer, but unlikely to be used as an
 *       option value). The options parser checks for INT_MIN to detect parse errors.
 *
 * @note Thread-safe: Uses only local variables, no static state.
 *
 * @par Example:
 * @code{.c}
 * const char *arg = "80";
 * int width = strtoint_safe(arg);
 * if (width == INT_MIN) {
 *     // Parse error
 * } else {
 *     // Valid integer: width == 80
 * }
 * @endcode
 *
 * @ingroup options
 */
int strtoint_safe(const char *str);

/** @} */

/**
 * @name Terminal Dimensions
 * @{
 */

/** @brief Terminal width in characters
 *
 * Width of the terminal/console in characters (columns). This determines the
 * width of the ASCII art output.
 *
 * **Default**: `OPT_WIDTH_DEFAULT` (110 characters) or auto-detected from terminal
 *
 * **Auto-detection**: If `auto_width` is true, this value is updated from terminal
 * size detection. Otherwise, uses the value from `--width` option or default.
 *
 * **Usage**: Accessed throughout the application to size ASCII art output buffers
 * and determine display dimensions.
 *
 * @note This is a global variable - options are parsed once at startup.
 * @note Can be updated dynamically via `update_dimensions_to_terminal_size()`.
 *
 * @ingroup options
 */
extern ASCIICHAT_API unsigned short int opt_width;

/** @brief Terminal height in characters
 *
 * Height of the terminal/console in characters (rows). This determines the
 * height of the ASCII art output.
 *
 * **Default**: `OPT_HEIGHT_DEFAULT` (70 characters) or auto-detected from terminal
 *
 * **Auto-detection**: If `auto_height` is true, this value is updated from terminal
 * size detection. Otherwise, uses the value from `--height` option or default.
 *
 * **Usage**: Accessed throughout the application to size ASCII art output buffers
 * and determine display dimensions.
 *
 * @note This is a global variable - options are parsed once at startup.
 * @note Can be updated dynamically via `update_dimensions_to_terminal_size()`.
 *
 * @ingroup options
 */
extern ASCIICHAT_API unsigned short int opt_height;

/** @brief Auto-detect terminal width from terminal size
 *
 * If true, automatically detects terminal width from the terminal's current size.
 * If false, uses the value specified via `--width` or the default width.
 *
 * **Default**: `true` (auto-detection enabled)
 *
 * **Auto-detection behavior**: When enabled, queries the terminal for its current
 * width using platform-specific methods (TIOCGWINSZ on POSIX, Console API on Windows).
 * Falls back to default width if detection fails.
 *
 * **Command-line**: `--width` disables auto-detection and sets explicit width.
 * `--auto-width` enables auto-detection (default).
 *
 * @ingroup options
 */
extern ASCIICHAT_API bool auto_width;

/** @brief Auto-detect terminal height from terminal size
 *
 * If true, automatically detects terminal height from the terminal's current size.
 * If false, uses the value specified via `--height` or the default height.
 *
 * **Default**: `true` (auto-detection enabled)
 *
 * **Auto-detection behavior**: When enabled, queries the terminal for its current
 * height using platform-specific methods (TIOCGWINSZ on POSIX, Console API on Windows).
 * Falls back to default height if detection fails.
 *
 * **Command-line**: `--height` disables auto-detection and sets explicit height.
 * `--auto-height` enables auto-detection (default).
 *
 * @ingroup options
 */
extern ASCIICHAT_API bool auto_height;

/** @} */

/**
 * @name Network Options
 * @{
 */

/** @brief Server address (client) or IPv4 bind address (server)
 *
 * Network address used for connections:
 * - **Client mode**: Address of the server to connect to
 * - **Server mode**: IPv4 address to bind to (use "0.0.0.0" for all interfaces)
 *
 * **Default**: `"localhost"` (both client and server)
 *
 * **Supported formats**:
 * - IPv4 addresses: `"192.168.1.100"`, `"127.0.0.1"`
 * - Hostnames: `"example.com"`, `"localhost"`
 * - IPv6 addresses: Use `opt_address6` for IPv6
 *
 * **Command-line**: `--address <addr>` or `-a <addr>`
 *
 * **Example**: `--address 192.168.1.100` or `-a localhost`
 *
 * @note Maximum length: `OPTIONS_BUFF_SIZE` (256 characters)
 * @note Server mode: Use `"0.0.0.0"` to bind to all IPv4 interfaces
 * @note For IPv6 addresses, use `opt_address6` instead
 *
 * @ingroup options
 */
extern ASCIICHAT_API char opt_address[];

/** @brief IPv6 bind address (server only)
 *
 * IPv6 address for the server to bind to. Used when server needs to bind to
 * specific IPv6 interfaces. Can be used alongside `opt_address` for dual-stack
 * support.
 *
 * **Default**: Empty string (IPv6 binding disabled)
 *
 * **Supported formats**:
 * - IPv6 addresses: `"::1"` (localhost), `"::"` (all interfaces), `"2001:db8::1"`
 * - Hostnames that resolve to IPv6 addresses
 *
 * **Command-line**: `--address6 <addr>`
 *
 * **Example**: `--address6 ::` (bind to all IPv6 interfaces)
 *
 * @note Maximum length: `OPTIONS_BUFF_SIZE` (256 characters)
 * @note Server mode only: Not used in client mode
 * @note Can be used with `opt_address` for dual-stack (IPv4 + IPv6)
 *
 * @ingroup options
 */
extern ASCIICHAT_API char opt_address6[];

/** @brief Server port number
 *
 * Port number for network connections:
 * - **Client mode**: Port of the server to connect to
 * - **Server mode**: Port to listen on for incoming connections
 *
 * **Default**: `"27224"` (ascii-chat default port)
 *
 * **Valid range**: 1-65535 (standard TCP port range)
 *
 * **Command-line**: `--port <port>` or `-p <port>`
 *
 * **Example**: `--port 27224` or `-p 8080`
 *
 * @note Maximum length: `OPTIONS_BUFF_SIZE` (256 characters)
 * @note Port must be a valid number in range 1-65535
 * @note Parser validates port number format and range
 *
 * @ingroup options
 */
extern ASCIICHAT_API char opt_port[];

/** @} */

/**
 * @name Server Options
 * @{
 */

/** @brief Maximum concurrent clients (server only)
 *
 * Maximum number of clients that can connect to the server simultaneously.
 * This limit prevents resource exhaustion and ensures stable performance.
 *
 * **Default**: `10` (10 concurrent clients)
 *
 * **Valid range**: 1 to 32
 *
 * **Command-line**: `--max-clients <N>`
 *
 * **Example**: `--max-clients 16` (allow up to 16 concurrent clients)
 *
 * @note Server only: This option is ignored by clients
 * @note Clients beyond this limit are rejected with an error message
 * @note Higher values require more server resources (CPU, memory, bandwidth)
 *
 * @ingroup options
 */
extern ASCIICHAT_API int opt_max_clients;

/** @} */

/**
 * @name Network Performance Options
 * @{
 */

/** @brief zstd compression level for video frames
 *
 * Compression level for zstd algorithm used to compress video frames before transmission.
 * Higher levels provide better compression ratios but use more CPU time.
 *
 * **Default**: `1` (fastest standard level, optimal for real-time streaming)
 *
 * **Valid range**: 1 to 9
 * - Level 1: Fastest compression, lowest ratio (best for real-time)
 * - Level 3: Balanced speed/ratio
 * - Level 9: Slower compression, best ratio (for limited bandwidth)
 *
 * **Command-line**: `--compression-level <N>`
 *
 * **Example**: `--compression-level 3` (better compression for slow connections)
 *
 * @note Both client and server must use compatible compression
 * @note Levels above 5 may cause frame drops on slower machines
 * @note Compression is skipped if compressed size >= original size
 *
 * @ingroup options
 */
extern ASCIICHAT_API int opt_compression_level;

/** @brief Disable compression entirely
 *
 * When enabled, disables all video frame compression regardless of the
 * compression level setting. Useful for debugging, testing, or when CPU
 * usage is more critical than bandwidth.
 *
 * **Default**: `false` (compression enabled)
 *
 * **Command-line**: `--no-compress`
 *
 * **Example**: `--no-compress` (send uncompressed frames)
 *
 * **Interaction with audio encoding**:
 * - When `--no-compress` is set, audio encoding is also disabled by default
 * - Use `--encode-audio` to explicitly enable audio encoding despite `--no-compress`
 * - Use `--no-encode-audio` to explicitly disable audio encoding
 *
 * @note Both client and server can use this flag independently
 * @note Disabling compression significantly increases bandwidth usage
 * @note Useful for local testing or debugging compression issues
 * @note When enabled, --compression-level is ignored
 *
 * @ingroup options
 */
extern ASCIICHAT_API bool opt_no_compress;

/** @brief Enable Opus audio encoding
 *
 * When enabled, audio is compressed using the Opus codec before transmission.
 * When disabled, raw float samples are sent without encoding.
 *
 * **Default**: `true` (Opus encoding enabled)
 *
 * **Command-line**: `--encode-audio` (force enable), `--no-encode-audio` (force disable)
 *
 * **Example**:
 * - `--encode-audio` (force Opus encoding even with `--no-compress`)
 * - `--no-encode-audio` (send raw float audio samples)
 *
 * **Interaction with --no-compress**:
 * - If `--no-compress` is set WITHOUT explicit audio encoding flag: encoding disabled
 * - If `--encode-audio` is explicitly set: encoding enabled (overrides `--no-compress`)
 * - If `--no-encode-audio` is explicitly set: encoding disabled
 *
 * @note Audio encoding significantly reduces bandwidth (raw: ~176KB/s, Opus: ~6KB/s at 48kbps)
 * @note Disabling encoding is useful for debugging audio issues or testing raw audio pipeline
 * @note Both client and server must agree on encoding format
 *
 * @ingroup options
 */
extern ASCIICHAT_API bool opt_encode_audio;

/** @} */

/**
 * @name Client Reconnection Options
 * @{
 */

/** @brief Number of reconnection attempts after connection loss
 *
 * Controls automatic reconnection behavior when the connection to the server
 * is lost unexpectedly (network issues, server restart, etc.).
 *
 * **Default**: `0` (no automatic reconnection)
 *
 * **Valid values**:
 * - `0` or "off": No reconnection attempts (exit on disconnect)
 * - Positive number (1-999): Exact number of reconnection attempts
 * - `-1` or "auto": Unlimited reconnection attempts (retry forever)
 *
 * **Command-line**: `--reconnect <value>`
 *
 * **Examples**:
 * - `--reconnect off` (disable reconnection)
 * - `--reconnect 5` (try 5 times then give up)
 * - `--reconnect auto` (reconnect indefinitely)
 *
 * @note Client only: This option is ignored by servers
 * @note Snapshot mode always disables reconnection
 * @note Each attempt waits 2 seconds before retrying
 *
 * @ingroup options
 */
extern ASCIICHAT_API int opt_reconnect_attempts;

/** @} */

/**
 * @name Webcam Options
 * @{
 */

/** @brief Webcam device index (0 = first webcam)
 *
 * Index of the webcam device to use for video capture. Index 0 refers to the
 * first available webcam device, index 1 to the second, etc.
 *
 * **Default**: `0` (first available webcam)
 *
 * **Valid range**: 0 to maximum available devices - 1
 *
 * **Command-line**: `--webcam <index>` or `-w <index>`
 *
 * **Example**: `--webcam 1` (use second webcam device)
 *
 * @note Device index is validated against available devices
 * @note If specified device is not available, an error is returned
 * @note Use `--list-devices` to list available webcam devices
 *
 * @ingroup options
 */
extern ASCIICHAT_API unsigned short int opt_webcam_index;

/** @brief Flip webcam image horizontally
 *
 * If true, flips the webcam image horizontally (mirror mode). Useful for
 * correcting mirrored webcam output or for aesthetic preferences.
 *
 * **Default**: `true` (flip enabled)
 *
 * **Command-line**: `--flip` (enable flip), `--no-flip` (disable flip)
 *
 * **Example**: `--no-flip` (disable horizontal flipping)
 *
 * @note Flip is applied after image capture, before ASCII conversion
 * @note Flip only affects horizontal axis (left-right mirror)
 *
 * @ingroup options
 */
extern ASCIICHAT_API bool opt_webcam_flip;

/** @brief Use test pattern instead of real webcam
 *
 * If true, generates a test pattern instead of capturing from the webcam.
 * Useful for testing ASCII conversion without requiring a physical webcam.
 *
 * **Default**: `false` (use real webcam)
 *
 * **Command-line**: `--test-pattern` (enable test pattern)
 *
 * **Example**: `--test-pattern` (use test pattern for testing)
 *
 * @note Test pattern generates synthetic video frames for testing
 * @note Useful for development and testing without hardware
 * @note Client mode only: Test pattern is generated locally
 *
 * @ingroup options
 */
extern ASCIICHAT_API bool opt_test_pattern;

/**
 * @brief Disable audio mixer (send silence instead of mixing)
 * @note For debugging audio issues - useful for isolating mixer performance problems
 */
extern ASCIICHAT_API bool opt_no_audio_mixer;

/** @} */

/**
 * @name Terminal Color Mode
 * @{
 */

/**
 * @brief Terminal color mode override (client only)
 *
 * Enumeration of terminal color modes that can be forced via command-line options.
 * This allows users to override automatic terminal capability detection and force
 * a specific color mode for ASCII art rendering.
 *
 * **Mode Selection**:
 * - `COLOR_MODE_AUTO`: Automatically detect terminal color capabilities (default)
 * - `COLOR_MODE_NONE`: Force no color output (plain ASCII, no color ANSI codes)
 * - `COLOR_MODE_16_COLOR`: Force 16-color ANSI mode (basic color support)
 * - `COLOR_MODE_256_COLOR`: Force 256-color palette mode (extended colors)
 * - `COLOR_MODE_TRUECOLOR`: Force 24-bit truecolor mode (full RGB colors)
 *
 * **Command-line Options**:
 * - `--color` or `--16`: Force 16-color mode
 * - `--256`: Force 256-color mode
 * - `--truecolor`: Force 24-bit truecolor mode
 * - `--no-color` or `--mono`: Force monochrome mode
 * - No option: Auto-detect (COLOR_MODE_AUTO)
 *
 * **Default**: `COLOR_MODE_AUTO` (automatically detect terminal capabilities)
 *
 * **Usage**:
 * The color mode is used to select the appropriate palette and rendering method:
 * - Monochrome: ASCII characters only, no color
 * - 16-color: Basic ANSI colors (foreground colors)
 * - 256-color: Extended ANSI palette (more color choices)
 * - Truecolor: Full 24-bit RGB colors (maximum quality)
 *
 * **Auto-detection**:
 * When `COLOR_MODE_AUTO` is selected, the system detects terminal capabilities by:
 * - Checking `$TERM` environment variable
 * - Checking `$COLORTERM` environment variable
 * - Querying terminal type databases
 * - Testing runtime capabilities (where available)
 *
 * **Overrides**:
 * Command-line color mode overrides take precedence over auto-detection.
 * This allows users to force a specific mode even if auto-detection suggests otherwise.
 *
 * @note Client mode only: Color mode is not used in server mode
 * @note See `platform/terminal.h` for terminal capability detection details
 * @note Color mode affects palette selection and rendering quality
 *
 * @par Example:
 * @code{.c}
 * if (opt_color_mode == COLOR_MODE_TRUECOLOR) {
 *     // Use 24-bit RGB palette for maximum quality
 * } else if (opt_color_mode == COLOR_MODE_256_COLOR) {
 *     // Use 256-color palette
 * } else if (opt_color_mode == COLOR_MODE_AUTO) {
 *     // Use detected terminal capabilities
 * }
 * @endcode
 *
 * @ingroup options
 */
typedef enum {
  /** @brief Auto-detect terminal capabilities (default)
   *
   * Automatically detects terminal color capabilities and selects appropriate mode.
   * Uses terminal capability detection (see `platform/terminal.h`).
   *
   * @ingroup options
   */
  COLOR_MODE_AUTO = 0,

  /** @brief Force no color output (plain ASCII)
   *
   * Disables all colors and ANSI escape codes. Produces completely plain
   * ASCII output with no escape sequences of any kind.
   *
   * @ingroup options
   */
  COLOR_MODE_NONE = 1,

  /** @brief Force 16-color ANSI mode
   *
   * Forces basic 16-color ANSI mode. Uses standard ANSI color codes for
   * foreground colors only.
   *
   * @ingroup options
   */
  COLOR_MODE_16_COLOR = 2,

  /** @brief Force 256-color palette mode
   *
   * Forces extended 256-color ANSI palette mode. Provides more color choices
   * than 16-color mode while maintaining compatibility with most terminals.
   *
   * @ingroup options
   */
  COLOR_MODE_256_COLOR = 3,

  /** @brief Force 24-bit truecolor mode
   *
   * Forces full 24-bit RGB truecolor mode. Provides maximum color quality
   * but requires terminal support for truecolor escape sequences.
   *
   * @ingroup options
   */
  COLOR_MODE_TRUECOLOR = 4
} terminal_color_mode_t;

/** @} */

// Render mode is now defined in platform/terminal.h

/**
 * @name Display Options
 * @{
 */

/** @brief Color mode override (client only)
 *
 * Terminal color mode override selected via command-line options. This overrides
 * automatic terminal capability detection and forces a specific color mode.
 *
 * **Default**: `COLOR_MODE_AUTO` (automatically detect terminal capabilities)
 *
 * **Command-line**: `--color`, `--256`, `--truecolor`, `--no-color`
 *
 * **Usage**: Used throughout the application to determine color palette and
 * rendering method for ASCII art conversion.
 *
 * @note Client mode only: Not used in server mode
 * @note See `terminal_color_mode_t` for available modes
 * @note Overrides auto-detection when set to non-AUTO value
 *
 * @ingroup options
 */
extern ASCIICHAT_API terminal_color_mode_t opt_color_mode;

/** @brief Render mode override
 *
 * Rendering mode for ASCII art output. Determines how colors are applied to
 * ASCII characters (foreground colors, background colors, or half-block Unicode).
 *
 * **Default**: `RENDER_MODE_FOREGROUND` (text colors only)
 *
 * **Available modes** (from `platform/terminal.h`):
 * - `RENDER_MODE_FOREGROUND`: Text colors (foreground only)
 * - `RENDER_MODE_BACKGROUND`: Block colors (background fills)
 * - `RENDER_MODE_HALF_BLOCK`: Unicode half-blocks (mixed foreground/background)
 *
 * **Command-line**: `--fg` (foreground), `--bg` (background), `--half-block`
 *
 * **Example**: `--bg` (use background colors for block-style rendering)
 *
 * @note Render mode affects visual appearance and requires different terminal capabilities
 * @note Half-block mode requires UTF-8 support and Unicode half-block characters
 * @note Background mode may not work on all terminals
 *
 * @ingroup options
 */
extern ASCIICHAT_API render_mode_t opt_render_mode;

/** @brief Show detected capabilities and exit (client only)
 *
 * If non-zero, prints detected terminal capabilities and exits immediately.
 * Useful for debugging terminal detection or verifying terminal configuration.
 *
 * **Default**: `0` (do not show capabilities)
 *
 * **Command-line**: `--show-capabilities` or `--capabilities`
 *
 * **Output**: Prints detailed terminal capability report including:
 * - Color support level (none, 16, 256, truecolor)
 * - UTF-8 encoding support
 * - Terminal type and environment variables
 * - Render mode preferences
 * - Detection reliability
 *
 * **Example**: `--show-capabilities` (print capabilities and exit)
 *
 * @note Client mode only: Not available in server mode
 * @note Program exits after printing capabilities (does not start application)
 * @note Useful for debugging terminal detection issues
 *
 * @ingroup options
 */
extern ASCIICHAT_API unsigned short int opt_show_capabilities;

/** @brief Force enable UTF-8 support via --utf8
 *
 * If non-zero, forces UTF-8 encoding support even if terminal detection suggests
 * otherwise. This enables Unicode palette characters and half-block rendering.
 *
 * **Default**: `0` (auto-detect UTF-8 support)
 *
 * **Command-line**: `--utf8` (force UTF-8 support)
 *
 * **Usage**: When enabled, the application assumes UTF-8 encoding and uses
 * Unicode characters for palette rendering (e.g., half-blocks, special characters).
 *
 * **Example**: `--utf8` (force UTF-8 encoding support)
 *
 * @note Client mode only: Not used in server mode
 * @note Overrides terminal capability detection for UTF-8
 * @note Required for half-block render mode to work properly
 *
 * @ingroup options
 */
extern ASCIICHAT_API unsigned short int opt_force_utf8;

/** @} */

/**
 * @name Audio Configuration Options
 * @{
 */

/** @brief Enable audio streaming
 *
 * If non-zero, enables audio capture and streaming. When enabled, the client
 * captures audio from the selected input device and streams it to the server.
 *
 * **Default**: `0` (audio disabled)
 *
 * **Command-line**: `--audio` (enable audio streaming)
 *
 * **Requirements**:
 * - Audio input device must be available
 * - PortAudio library must be initialized
 * - Server must support audio streaming
 *
 * **Example**: `--audio` (enable audio capture and streaming)
 *
 * @note Audio requires PortAudio library support
 * @note Audio device selection via `opt_audio_device`
 * @note Client mode only: Audio streaming is client-side only
 *
 * @ingroup options
 */
extern ASCIICHAT_API unsigned short int opt_audio_enabled;

/** @brief Audio input device index (-1 = use default)
 *
 * Index of the audio input device to use for audio capture. Index -1 indicates
 * to use the system default audio input device.
 *
 * **Default**: `-1` (use default audio device)
 *
 * **Valid range**: -1 (default) or 0 to maximum available devices - 1
 *
 * **Command-line**: `--audio-device <index>` (specify audio device index)
 *
 * **Example**: `--audio-device 1` (use second audio input device)
 *
 * @note Device index -1 means use system default device
 * @note Device index is validated against available PortAudio devices
 * @note Use PortAudio device enumeration to list available devices
 *
 * @ingroup options
 */
extern ASCIICHAT_API int opt_audio_device;

/** @brief Enable audio analysis for debugging
 *
 * When enabled, tracks sent and received audio characteristics for debugging
 * audio quality issues. Generates analysis report in snapshot mode.
 *
 * **Default**: `0` (disabled)
 *
 * **Command-line**: `--audio-analysis` (enable audio analysis)
 *
 * **Output**: Prints audio statistics (peak level, clipping, silence, packets) on exit
 *
 * **Note**: Only useful with `--audio` and `--snapshot` flags
 *
 * @ingroup options
 */
extern ASCIICHAT_API unsigned short int opt_audio_analysis_enabled;

/** @} */

/**
 * @name Display/Image Options
 * @{
 */

/** @brief Allow image to stretch or shrink without preserving aspect ratio
 *
 * If non-zero, allows the image to be stretched or shrunk to fit the terminal
 * dimensions without preserving the original aspect ratio. When zero (default),
 * the image maintains its aspect ratio and may have letterboxing/pillarboxing.
 *
 * **Default**: `0` (preserve aspect ratio)
 *
 * **Command-line**: `--stretch` (allow aspect ratio distortion)
 *
 * **Behavior**:
 * - **Stretch disabled (default)**: Image maintains aspect ratio, may have black bars
 * - **Stretch enabled**: Image fills entire terminal, may be distorted
 *
 * **Example**: `--stretch` (allow image distortion to fill terminal)
 *
 * @note Stretching may cause image distortion (squishing/stretching)
 * @note Aspect ratio preservation is generally preferred for quality
 * @note Stretching is useful when terminal size doesn't match image aspect ratio
 *
 * @ingroup options
 */
extern ASCIICHAT_API unsigned short int opt_stretch;

/** @} */

/**
 * @name Output Options
 * @{
 */

/** @brief Disable console logging (quiet mode)
 *
 * If non-zero, suppresses console log output. Only critical errors and warnings
 * are displayed. Useful for scripted usage or when log output is not needed.
 *
 * **Default**: `0` (normal logging enabled)
 *
 * **Command-line**: `--quiet` or `-q` (enable quiet mode)
 *
 * **Log Levels**:
 * - Normal mode: All log levels (debug, info, warning, error)
 * - Quiet mode: Only errors and critical warnings
 *
 * **Example**: `--quiet` or `-q` (suppress console logging)
 *
 * @note Quiet mode only affects console output, not log file output
 * @note Errors are still displayed even in quiet mode
 * @note Useful for non-interactive usage (scripts, automation)
 *
 * @ingroup options
 */
extern ASCIICHAT_API unsigned short int opt_quiet;

/** @brief Verbose logging level (stackable)
 *
 * Increases logging verbosity. Each -V or --verbose decreases the log level
 * threshold by one, showing more detailed output.
 *
 * **Default**: `0` (default log level based on build type)
 *
 * **Command-line**: `--verbose` or `-V` (stackable: -VV, -VVV, etc.)
 *
 * **Verbosity levels**:
 * - 0: Default (INFO in Release, DEBUG in Debug)
 * - 1: DEBUG level (shows debug messages)
 * - 2: DEV level (shows all messages including developer diagnostics)
 *
 * **Example**: `-VV` or `--verbose --verbose` (maximum verbosity)
 *
 * @note Can be combined: `-VVV` is equivalent to `--verbose --verbose --verbose`
 * @note Mutually exclusive with `--quiet` (verbose takes precedence if both specified)
 * @note Affects both console and log file output
 *
 * @ingroup options
 */
extern ASCIICHAT_API unsigned short int opt_verbose_level;

/** @brief Enable snapshot mode - capture one frame and exit (client only)
 *
 * If non-zero, enables snapshot mode where the client connects to a server,
 * receives a single frame, displays it as ASCII art, and exits immediately.
 * Useful for one-time capture of the current call state without persistent streaming.
 *
 * **Default**: `0` (normal video streaming mode)
 *
 * **Command-line**: `--snapshot` (enable snapshot mode)
 *
 * **Behavior**:
 * - Connects to the specified server (or localhost by default)
 * - Waits for `opt_snapshot_delay` seconds (for frame warmup)
 * - Receives one frame from server
 * - Converts frame to ASCII art
 * - Prints ASCII art to stdout
 * - Exits immediately after displaying the frame
 *
 * **Connection Behavior**:
 * - **No retries**: If the connection fails, snapshot mode exits with an error
 *   immediately instead of retrying. This is intentional for scripting and CI/CD
 *   usage where quick failure is preferred over indefinite retry loops.
 * - **No reconnection**: If the connection is lost during snapshot capture,
 *   exits with an error instead of attempting to reconnect.
 *
 * **Example**: `--snapshot` (capture one frame and exit)
 *
 * @note Client mode only: Not available in server mode
 * @note Connects to server (unlike mirror mode which is standalone)
 * @note Uses `opt_snapshot_delay` for warmup time before capture
 * @note Exits with error code 1 on any connection failure (no retries)
 *
 * @ingroup options
 */
extern ASCIICHAT_API unsigned short int opt_snapshot_mode;

/** @brief Enable mirror mode - view webcam locally without server (client only)
 *
 * If non-zero, enables mirror mode where the client displays the webcam feed
 * as ASCII art directly to the terminal without connecting to any server.
 * Useful for previewing your webcam appearance before joining a chat.
 *
 * **Default**: `0` (normal network mode)
 *
 * **Command-line**: `--mirror` (enable mirror mode)
 *
 * **Behavior**:
 * - Captures frames from webcam continuously
 * - Converts frames to ASCII art locally
 * - Displays ASCII art to terminal in real-time
 * - No network connection required
 * - Press Ctrl+C to exit
 *
 * **Snapshot Support**:
 * Can be combined with `--snapshot` to capture a single frame from your local
 * webcam without connecting to a server:
 * - `--mirror --snapshot` - Capture one local webcam frame and exit
 * - Uses `opt_snapshot_delay` for webcam warmup before capture
 *
 * **Example**: `--mirror` (view local webcam as ASCII art)
 * **Example**: `--mirror --snapshot` (capture single frame locally)
 *
 * @note Client mode only: Not available in server mode
 * @note Does not connect to server (standalone mode)
 * @note Audio is disabled in mirror mode (not compatible with --audio)
 *
 * @ingroup options
 */
extern ASCIICHAT_API unsigned short int opt_mirror_mode;

/** @brief Snapshot delay in seconds (default 3.0 for webcam warmup)
 *
 * Delay in seconds to wait before capturing frame in snapshot mode. This delay
 * allows the webcam to warm up and adjust to lighting conditions before capture.
 *
 * **Default**: `3.0` (3 seconds)
 *
 * **Command-line**: `--snapshot-delay <seconds>` (set snapshot delay)
 *
 * **Purpose**: Webcams often need time to adjust exposure, white balance, and
 * focus when first activated. This delay ensures a higher quality snapshot.
 *
 * **Example**: `--snapshot-delay 5.0` (wait 5 seconds before capture)
 *
 * @note Only used when `opt_snapshot_mode` is enabled
 * @note Delay is in floating-point seconds (can be fractional, e.g., 2.5)
 * @note Typical values: 2.0-5.0 seconds for good webcam warmup
 *
 * @ingroup options
 */
extern ASCIICHAT_API float opt_snapshot_delay;

/** @brief Strip ANSI escape sequences from output (client only)
 *
 * When enabled, all ANSI escape sequences (colors, cursor movement, etc.)
 * are removed from the output, producing plain ASCII text.
 *
 * **Default**: `0` (disabled - ANSI codes preserved)
 *
 * **Command-line**: `--strip-ansi` (enable ANSI stripping)
 *
 * **Purpose**: Useful when piping or redirecting output to files or commands
 * that don't understand ANSI escape codes. Produces clean plain text output.
 *
 * **Example**: `ascii-chat client --mirror --snapshot --strip-ansi > file.txt`
 *
 * @note This is more aggressive than --color-mode mono, which only disables
 *       color codes. --strip-ansi removes ALL escape sequences.
 *
 * @ingroup options
 */
extern ASCIICHAT_API unsigned short int opt_strip_ansi;

/** @brief Log file path (empty string means no file logging)
 *
 * Path to log file for writing log output. If empty string, no file logging
 * is performed (only console logging if not in quiet mode).
 *
 * **Default**: Empty string (no file logging)
 *
 * **Command-line**: `--log-file <path>` or `--log <path>` (set log file path)
 *
 * **Logging behavior**:
 * - If path is empty: No file logging (console only)
 * - If path is set: Logs are written to file (and console if not quiet)
 *
 * **Example**: `--log-file /tmp/ascii-chat.log` (write logs to file)
 *
 * @note Maximum path length: `OPTIONS_BUFF_SIZE` (256 characters)
 * @note Log file is opened in append mode (existing logs preserved)
 * @note File must be writable (permissions checked)
 * @note Empty string ("") means no file logging
 *
 * @ingroup options
 */
extern ASCIICHAT_API char opt_log_file[OPTIONS_BUFF_SIZE];

/** @brief Log level threshold for console and file output
 *
 * Controls which log messages are displayed. Only messages at this level
 * or more severe are shown.
 *
 * **Default**:
 * - DEBUG in debug builds (when NDEBUG is not defined)
 * - INFO in release builds (when NDEBUG is defined)
 *
 * **Command-line**: `--log-level <level>` (set minimum log level)
 *
 * **Valid log levels** (from most verbose to least):
 * - `dev`: Development-level messages (most verbose)
 * - `debug`: Debug messages
 * - `info`: Informational messages
 * - `warn`: Warning messages
 * - `error`: Error messages
 * - `fatal`: Fatal error messages (least verbose)
 *
 * **Example**: `--log-level warn` (show warnings and errors only)
 *
 * **Environment variable**: Can be overridden by `LOG_LEVEL` environment variable
 * - Format: Same as command-line levels (case-insensitive)
 * - Example: `LOG_LEVEL=debug ./ascii-chat server`
 *
 * **Interaction with --verbose**:
 * - `--verbose` (or `-V`) decreases the log level threshold, showing more messages
 * - Applied after `--log-level` option parsing
 * - Each `-V` flag lowers the threshold by one level
 * - Example: `--log-level warn -VV` would lower warn to debug level
 *
 * @note Log level is a global setting that applies to all logging output
 * @note Option is available for both client and server modes
 * @note Environment variable `LOG_LEVEL` takes precedence over default, but not command-line
 * @note Default level is set based on build type (debug vs release)
 *
 * @ingroup options
 */
extern ASCIICHAT_API log_level_t opt_log_level;

/** @} */

/**
 * @name Encryption Options
 * @{
 */

/** @brief Enable encryption
 *
 * If non-zero, enables end-to-end encryption for the connection. Encryption
 * is enabled automatically when a key file (`--key`) or password (`--password`)
 * is provided, or can be explicitly disabled with `--no-encrypt`.
 *
 * **Default**: `0` (encryption disabled, unless key/password provided)
 *
 * **Auto-enable**: Encryption is automatically enabled if:
 * - `--key` option is provided (SSH key file)
 * - `--password` option is provided (password authentication)
 * - Default SSH key is found at `~/.ssh/id_ed25519`
 *
 * **Manual control**: Use `--no-encrypt` to explicitly disable encryption even
 * if keys are available.
 *
 * @note Encryption uses X25519 key exchange with XSalsa20-Poly1305 encryption
 * @note See @ref topic_handshake "Handshake Protocol" for encryption details
 * @note Encryption requires key file or password for authentication
 *
 * @ingroup options
 */
extern ASCIICHAT_API unsigned short int opt_encrypt_enabled;

/** @brief SSH/GPG key file path from --key (file-based only)
 *
 * Path to SSH key file for encryption and authentication. Supports Ed25519
 * SSH keys in OpenSSH format.
 *
 * **Default**: Empty string (no key file specified)
 *
 * **Command-line**: `--key <path>` or `-k <path>` (specify key file path)
 *
 * **Auto-detection**: If not specified, checks for default SSH key at
 * `~/.ssh/id_ed25519` (Ed25519 keys only).
 *
 * **Supported formats**:
 * - OpenSSH Ed25519: `~/.ssh/id_ed25519` (private key)
 * - Raw hex file: File containing 64 hex characters for X25519 key
 *
 * **Example**: `--key ~/.ssh/id_ed25519` or `-k /path/to/key`
 *
 * @note Maximum path length: `OPTIONS_BUFF_SIZE` (256 characters)
 * @note Key file must exist and be readable
 * @note Only Ed25519 keys are supported (modern, secure, fast)
 * @note If key is encrypted, user will be prompted for passphrase
 * @note Key is used for both encryption and authentication
 *
 * @ingroup options
 */
extern ASCIICHAT_API char opt_encrypt_key[OPTIONS_BUFF_SIZE];

/** @brief Password string from --password
 *
 * Password for password-based authentication. The password is used to derive
 * an encryption key using Argon2id key derivation.
 *
 * **Default**: Empty string (no password)
 *
 * **Command-line**: `--password <password>` (specify password)
 *
 * **Security**: Password is stored in memory temporarily during authentication
 * and is securely zeroed after use. Password is never transmitted over the network
 * in plaintext - only HMAC derived from password is sent.
 *
 * **Authentication**: Both client and server must use the same password for
 * successful authentication.
 *
 * **Example**: `--password mysecretpassword`
 *
 * @note Maximum password length: `OPTIONS_BUFF_SIZE` (256 characters)
 * @note Password is securely zeroed from memory after authentication
 * @note Password is never logged or printed (security sensitive)
 * @note Prefer SSH keys over passwords when possible (more secure)
 *
 * @ingroup options
 */
extern ASCIICHAT_API char opt_password[OPTIONS_BUFF_SIZE];

/** @brief Key file path from --keyfile
 *
 * Alternative key file path option (legacy or additional key file support).
 * Used for specifying key files in addition to or instead of `--key`.
 *
 * **Default**: Empty string (no keyfile specified)
 *
 * **Command-line**: `--keyfile <path>` (specify key file path)
 *
 * **Usage**: Similar to `--key`, but provided as an alternative option name
 * for compatibility or when multiple key files are needed.
 *
 * @note Maximum path length: `OPTIONS_BUFF_SIZE` (256 characters)
 * @note See `opt_encrypt_key` for key file format details
 *
 * @ingroup options
 */
extern ASCIICHAT_API char opt_encrypt_keyfile[OPTIONS_BUFF_SIZE];

/** @brief Disable encryption (opt-out)
 *
 * If non-zero, explicitly disables encryption even if key files are available
 * or default SSH keys are detected. This allows users to opt-out of encryption
 * when encryption is enabled by default.
 *
 * **Default**: `0` (encryption enabled if keys available)
 *
 * **Command-line**: `--no-encrypt` (explicitly disable encryption)
 *
 * **Use cases**:
 * - Testing without encryption setup
 * - Local network usage where encryption is not needed
 * - Debugging encryption issues
 *
 * **Example**: `--no-encrypt` (disable encryption even if keys are found)
 *
 * @note Overrides automatic encryption enablement
 * @note Use with caution - disables security features
 * @note Not recommended for production use over untrusted networks
 *
 * @ingroup options
 */
extern ASCIICHAT_API unsigned short int opt_no_encrypt;

/** @brief Expected server public key (client only)
 *
 * Expected server public key for server identity verification. If provided,
 * the client verifies that the server's public key matches this value, preventing
 * man-in-the-middle attacks.
 *
 * **Default**: Empty string (no key verification, uses TOFU - Trust On First Use)
 *
 * **Command-line**: `--server-key <key>` (specify expected server key)
 *
 * **Key formats**:
 * - SSH Ed25519 format: `"ssh-ed25519 AAAAC3... comment"` (direct key string)
 * - Raw hex: 64 hex characters for X25519 key
 * - File path: Path to `.pub` file or any file containing key (reads first line)
 *
 * **File support**:
 * When a file path is provided, the file is read and the first line is parsed
 * as an SSH public key. Common formats:
 * - `.pub` file: Standard SSH public key file (one key per file)
 * - Any text file: First line containing SSH key format is used
 *
 * **Verification**: If server key doesn't match, connection is rejected with error.
 * This provides stronger security than TOFU (Trust On First Use) model.
 *
 * **Examples**:
 * - `--server-key ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAI...` (direct key)
 * - `--server-key /path/to/server_key.pub` (read from .pub file)
 * - `--server-key ~/.ssh/known_hosts` (read first key from file)
 *
 * @note Client mode only: Not used in server mode
 * @note Maximum key length: `OPTIONS_BUFF_SIZE` (256 characters)
 * @note File path: Reads first line of file and parses as SSH key format
 * @note Overrides TOFU (known_hosts) verification when provided
 * @note Provides protection against MITM attacks
 *
 * @ingroup options
 */
extern ASCIICHAT_API char opt_server_key[OPTIONS_BUFF_SIZE];

/** @brief Allowed client keys (server only)
 *
 * Whitelist of allowed client public keys for access control. Only clients
 * with public keys matching this list are allowed to connect. Supports
 * multiple formats including file paths and comma-separated lists.
 *
 * **Default**: Empty string (no whitelist, all clients allowed)
 *
 * **Command-line**: `--client-keys <keys>` (specify allowed client keys)
 *
 * **Supported formats**:
 * - **File path**: Path to file containing SSH public keys (one per line)
 * - **Comma-separated list**: `"key1,key2,key3"` (multiple keys)
 * - **Single key**: Direct key string (SSH Ed25519 format or raw hex)
 *
 * **File support**:
 * When a file path is provided, the file is read and parsed for SSH public keys.
 * The file can contain multiple public keys, one per line. Supported file formats:
 * - **authorized_keys format**: Standard SSH authorized_keys file format
 *   ```
 *   ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAI... comment1
 *   ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAI... comment2
 *   ```
 * - **.pub file with multiple entries**: File containing multiple SSH public key
 *   entries, one per line (each line in `ssh-ed25519 AAAAC3... comment` format).
 *   This format is similar to `authorized_keys` and can contain any number of keys.
 * - **known_hosts format**: SSH known_hosts file format (all keys from file)
 *
 * **File parsing**:
 * - Reads all lines from the file
 * - Parses each line as an SSH public key
 * - Supports both `authorized_keys` and `known_hosts` file formats
 * - Only Ed25519 keys are parsed (RSA/ECDSA keys are skipped)
 *
 * **Access control**: When set, only clients with matching public keys can
 * successfully complete authentication and connect.
 *
 * **Examples**:
 * - `--client-keys ~/.ssh/authorized_keys` (read all keys from authorized_keys file)
 * - `--client-keys /path/to/client_keys.pub` (read keys from .pub file with multiple entries)
 * - `--client-keys "key1,key2,key3"` (comma-separated list of keys)
 * - `--client-keys ssh-ed25519 AAAAC3...` (single key string)
 *
 * @note Server mode only: Not used in client mode
 * @note Maximum length: `OPTIONS_BUFF_SIZE` (256 characters)
 * @note File path: Reads entire file and parses all SSH public keys (one per line)
 * @note Supports multiple keys via file (one per line) or comma-separated list
 * @note Only Ed25519 keys are supported (RSA/ECDSA keys in file are ignored)
 * @note Provides access control for server security
 *
 * @ingroup options
 */
extern ASCIICHAT_API char opt_client_keys[OPTIONS_BUFF_SIZE];

/** @} */

/**
 * @name Palette Configuration Options
 * @{
 */

/** @brief Selected palette type
 *
 * ASCII character palette type selected for image-to-ASCII conversion. Determines
 * which set of ASCII characters is used to represent different brightness levels.
 *
 * **Default**: Auto-selected based on terminal capabilities and render mode
 *
 * **Available palettes** (from `palette.h`):
 * - Standard ASCII: Basic ASCII characters (.,:;+=xX$&)
 * - Extended ASCII: Extended character set
 * - Unicode: Unicode block characters
 * - Custom: User-defined character set
 *
 * **Command-line**: `--palette <type>` (specify palette type)
 *
 * **Example**: `--palette unicode` (use Unicode block characters)
 *
 * @note Palette type affects visual appearance and character density
 * @note Some palettes require UTF-8 support (Unicode palettes)
 * @note See `palette.h` for available palette types
 *
 * @ingroup options
 */
extern ASCIICHAT_API palette_type_t opt_palette_type;

/** @brief Custom palette characters
 *
 * User-defined ASCII character palette for custom rendering. Used when
 * `opt_palette_type` is set to custom palette type.
 *
 * **Default**: Empty string (no custom palette)
 *
 * **Command-line**: `--palette custom --custom-palette " .:-=+*#%@"`
 *
 * **Format**: String of characters ordered from darkest to brightest. Characters
 * at the beginning represent darker regions, characters at the end represent
 * brighter regions.
 *
 * **Example**: `--custom-palette " .:-=+*#%@"` (custom character gradient)
 *
 * @note Maximum length: 256 characters
 * @note Characters should be ordered from darkest to brightest
 * @note Custom palette is only used when `opt_palette_type` is custom
 * @note See `opt_palette_custom_set` to check if custom palette was provided
 *
 * @ingroup options
 */
extern ASCIICHAT_API char opt_palette_custom[256];

/** @brief True if custom palette was set via command line
 *
 * Flag indicating whether a custom palette was explicitly provided via
 * command-line options. Used to distinguish between "no custom palette"
 * and "custom palette not yet set".
 *
 * **Default**: `false` (no custom palette provided)
 *
 * **Usage**: Check this flag to determine if `opt_palette_custom` contains
 * a user-provided palette or is just initialized to empty string.
 *
 * @note Set to true when `--custom-palette` option is provided
 * @note Used to distinguish empty custom palette from unset custom palette
 *
 * @ingroup options
 */
extern ASCIICHAT_API bool opt_palette_custom_set;

/** @brief Red weight for luminance calculation (default weights must add up to 1.0)
 *
 * Weight for red channel in luminance calculation. Used to convert RGB values
 * to grayscale/luminance for ASCII conversion. Luminance = weight_red * R + weight_green * G + weight_blue * B.
 *
 * **Default**: Typically `0.299` (standard ITU-R BT.601 weights)
 *
 * **Standard weights** (ITU-R BT.601):
 * - Red: 0.299
 * - Green: 0.587
 * - Blue: 0.114
 * - Sum: 1.000
 *
 * **Usage**: These weights are used when converting color images to grayscale
 * for ASCII art conversion. Different weights produce different grayscale results.
 *
 * @note Weights must sum to 1.0 for accurate luminance calculation
 * @note Standard ITU-R BT.601 weights are used by default
 * @note These are read-only constants (not modified after initialization)
 *
 * @ingroup options
 */
extern const float weight_red;

/** @brief Green weight for luminance calculation
 *
 * Weight for green channel in luminance calculation. See `weight_red` for details.
 *
 * **Default**: Typically `0.587` (standard ITU-R BT.601 weights)
 *
 * @ingroup options
 */
extern const float weight_green;

/** @brief Blue weight for luminance calculation
 *
 * Weight for blue channel in luminance calculation. See `weight_red` for details.
 *
 * **Default**: Typically `0.114` (standard ITU-R BT.601 weights)
 *
 * @ingroup options
 */
extern const float weight_blue;

/** @brief Red channel lookup table
 *
 * Lookup table for red channel values. Used for efficient color-to-ASCII
 * character mapping. Precomputed table for fast palette lookups.
 *
 * **Usage**: These lookup tables are used internally by the ASCII conversion
 * algorithm for efficient color mapping.
 *
 * @note These are internal implementation details
 * @note Tables are precomputed based on palette and color mode
 * @note Access these tables via palette functions, not directly
 *
 * @ingroup options
 */
extern unsigned short int RED[];

/** @brief Green channel lookup table
 *
 * Lookup table for green channel values. See `RED[]` for details.
 *
 * @ingroup options
 */
extern unsigned short int GREEN[];

/** @brief Blue channel lookup table
 *
 * Lookup table for blue channel values. See `RED[]` for details.
 *
 * @ingroup options
 */
extern unsigned short int BLUE[];

/** @brief Grayscale lookup table
 *
 * Lookup table for grayscale values. Used for monochrome ASCII conversion
 * when color information is not needed or available.
 *
 * @ingroup options
 */
extern unsigned short int GRAY[];

/** @} */

/**
 * @name Option Parsing Functions
 * @{
 */

/**
 * @brief Initialize options by parsing command-line arguments
 * @param argc Argument count from main()
 * @param argv Argument vector from main()
 * @param is_client true if parsing client options, false for server options
 * @return ASCIICHAT_OK on success, ERROR_USAGE on parse errors
 *
 * Parses command-line arguments and initializes all option global variables.
 * This function must be called once at program startup before accessing any options.
 *
 * **Parsing Process**:
 * 1. Parse command-line arguments using getopt() (POSIX-compliant)
 * 2. Validate option values (ranges, file existence, formats)
 * 3. Apply default values for unspecified options
 * 4. Perform mode-specific validation (client vs server)
 * 5. Initialize global option variables
 *
 * **Special Return Values**:
 * - `ASCIICHAT_OK`: Parsing succeeded (normal case)
 * - `ASCIICHAT_OK`: Also returned for `--help` and `--version` (after printing info)
 * - `ERROR_USAGE`: Parse error or invalid option (usage info should be printed)
 *
 * **Mode-Specific Behavior**:
 * - **Client mode** (`is_client = true`): Parses client-specific options
 *   (color mode, webcam, snapshot mode, etc.)
 * - **Server mode** (`is_client = false`): Parses server-specific options
 *   (bind address, client keys whitelist, etc.)
 *
 * **Validation**:
 * - Numeric ranges (e.g., port 1-65535, webcam index >= 0)
 * - File existence (key files, log files)
 * - Format correctness (IP addresses, port numbers)
 * - Mode compatibility (rejects client-only options in server mode)
 *
 * **Default Value Application**:
 * After parsing, unspecified options are set to defaults:
 * - Terminal dimensions: Auto-detect or use `OPT_WIDTH_DEFAULT`/`OPT_HEIGHT_DEFAULT`
 * - Network: `localhost:27224`
 * - Webcam: Index 0 (first device)
 * - Color mode: Auto-detect
 * - Encryption: Enabled if keys found, disabled otherwise
 *
 * **Environment Variables**:
 * The following environment variables are checked during option parsing:
 * - `WEBCAM_DISABLED`: When set to "1", "true", "yes", or "on", automatically
 *   enables test pattern mode (`opt_test_pattern = true`). Useful for CI/CD
 *   environments and testing without a physical webcam.
 *
 * @par Example:
 * @code{.c}
 * int main(int argc, char **argv) {
 *     // Parse options (client mode)
 *     asciichat_error_t err = options_init(argc, argv, true);
 *     if (err != ASCIICHAT_OK) {
 *         if (err == ERROR_USAGE) {
 *             usage(stderr, true);  // Print usage
 *         }
 *         return 1;
 *     }
 *
 *     // Options are now available via global variables
 *     printf("Connecting to %s:%s\n", opt_address, opt_port);
 *
 *     return 0;
 * }
 * @endcode
 *
 * @note Must be called once at program startup before accessing options
 * @note Global option variables are initialized by this function
 * @note Returns `ERROR_USAGE` for invalid options (caller should print usage)
 * @note `--help` and `--version` cause early exit (function still returns ASCIICHAT_OK)
 *
 * @ingroup options
 */
asciichat_error_t options_init(int argc, char **argv, bool is_client);

/**
 * @brief Print usage information for client or server
 * @param desc File descriptor to write to (typically stdout or stderr)
 * @param is_client true for client usage, false for server usage
 *
 * Prints comprehensive usage information including:
 * - Program description and synopsis
 * - All available command-line options
 * - Option descriptions and default values
 * - Usage examples
 * - Mode-specific options (client vs server)
 *
 * **Usage**: Called automatically by `options_init()` for `--help`, or manually
 * by application code when `ERROR_USAGE` is returned.
 *
 * **Output**: Formatted usage text including:
 * - Program name and version
 * - Synopsis with command syntax
 * - Option list with short/long forms and descriptions
 * - Examples of common usage patterns
 *
 * @par Example:
 * @code{.c}
 * if (options_init(argc, argv, true) == ERROR_USAGE) {
 *     usage(stderr, true);  // Print client usage
 *     return 1;
 * }
 * @endcode
 *
 * @note Typically printed to `stderr` for error cases, `stdout` for `--help`
 * @note Output is mode-specific (different options for client vs server)
 * @note Includes all available options with descriptions
 *
 * @ingroup options
 */
void usage(FILE *desc, bool is_client);

/**
 * @brief Print client usage information
 * @param desc File descriptor to write to (typically stdout or stderr)
 *
 * Convenience function that prints client-specific usage information.
 * Equivalent to `usage(desc, true)`.
 *
 * **Usage**: Called when displaying client help or error messages.
 *
 * **Includes**: All client-specific options:
 * - Display options (color mode, render mode, UTF-8)
 * - Webcam options (device index, flip, test pattern)
 * - Snapshot mode options
 * - Terminal dimension options
 * - Client-specific encryption options (server key verification)
 *
 * @ingroup options
 */
void usage_client(FILE *desc);

/**
 * @brief Print server usage information
 * @param desc File descriptor to write to (typically stdout or stderr)
 *
 * Convenience function that prints server-specific usage information.
 * Equivalent to `usage(desc, false)`.
 *
 * **Usage**: Called when displaying server help or error messages.
 *
 * **Includes**: All server-specific options:
 * - Network binding options (address, port, IPv6)
 * - Server-specific encryption options (client keys whitelist)
 * - Server configuration options
 *
 * @ingroup options
 */
void usage_server(FILE *desc);

/** @} */

/**
 * @name Dimension Update Functions
 * @{
 */

/**
 * @brief Update dimensions for full height display
 *
 * Adjusts `opt_width` and `opt_height` to use the full terminal height while
 * maintaining the original aspect ratio. Useful for maximizing vertical space
 * usage when the terminal is resized.
 *
 * **Calculation**:
 * - Uses full terminal height (from terminal size detection)
 * - Calculates width to maintain aspect ratio
 * - Updates `opt_width` and `opt_height` global variables
 *
 * **Usage**: Call this function when terminal is resized or when you want to
 * maximize vertical space usage while preserving aspect ratio.
 *
 * **Example**: If terminal is 120×40 and original dimensions were 80×30:
 * - Original aspect ratio: 80/30 = 2.67
 * - New dimensions: 107×40 (maintains 2.67 aspect ratio, uses full height)
 *
 * @note Updates global variables `opt_width` and `opt_height`
 * @note Maintains aspect ratio of current dimensions
 * @note Uses terminal size detection to get full height
 * @note Useful for dynamic terminal resizing
 *
 * @ingroup options
 */
void update_dimensions_for_full_height(void);

/**
 * @brief Update dimensions to match terminal size
 *
 * Sets `opt_width` and `opt_height` to exactly match the current terminal
 * dimensions. This function queries the terminal for its size and updates
 * the option variables accordingly.
 *
 * **Detection**: Uses platform-specific terminal size detection:
 * - **POSIX**: `TIOCGWINSZ` ioctl on stdout
 * - **Windows**: Console API `GetConsoleScreenBufferInfo`
 * - **Fallback**: Environment variables (`$COLUMNS`, `$LINES`)
 * - **Final fallback**: Default dimensions (`OPT_WIDTH_DEFAULT`, `OPT_HEIGHT_DEFAULT`)
 *
 * **Usage**: Call this function:
 * - After terminal resize (POSIX: SIGWINCH signal handler)
 * - When auto-detection is enabled and dimensions need refreshing
 * - When you want to sync dimensions with current terminal size
 *
 * **Example**: Terminal is 160×60:
 * - `opt_width` is set to 160
 * - `opt_height` is set to 60
 *
 * @note Updates global variables `opt_width` and `opt_height`
 * @note Uses platform-specific terminal size detection
 * @note Handles detection failures gracefully (uses fallbacks)
 * @note Useful for terminal resize handling
 *
 * @ingroup options
 */
void update_dimensions_to_terminal_size(void);

/** @} */
