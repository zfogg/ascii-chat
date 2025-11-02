/**
 * @file options.h
 * @defgroup options Options
 * @ingroup options
 * @brief Command-line options parsing and configuration management
 *
 * This header provides functionality for parsing command-line arguments and
 * managing configuration settings:
 * - Parse command-line arguments for client and server modes
 * - Validate option values and provide default values
 * - Terminal dimension detection and configuration
 * - Color mode and render mode overrides
 * - Encryption/authentication options
 * - Palette configuration
 * - Audio/video device selection
 *
 * @note Options are parsed once at startup via options_init(). All options
 *       are stored in global variables that can be accessed throughout the application.
 */

#pragma once

#include <stdbool.h>
#include <stdio.h>
#include "platform/terminal.h"
#include "palette.h"

/** @brief Buffer size for option string values */
#define OPTIONS_BUFF_SIZE 256

/** @brief Default terminal width in characters */
#define OPT_WIDTH_DEFAULT 110
/** @brief Default terminal height in characters */
#define OPT_HEIGHT_DEFAULT 70

/**
 * @brief Safely parse string to integer with validation
 * @param str String to parse
 * @return Integer value on success, INT_MIN on error
 *
 * Parses a string to an integer with validation. Returns INT_MIN if the
 * string is invalid or cannot be parsed.
 * @ingroup options
 */
asciichat_error_t strtoint_safe(const char *str);

/** @name Terminal Dimensions
 * @{
 */
/** @brief Terminal width in characters */
extern unsigned short int opt_width, opt_height;
/** @brief Auto-detect terminal width */
extern bool auto_width, auto_height;
/** @} */

/** @name Network Options
 * @{
 */
/** @brief Server address to connect to (client) or IPv4 bind address (server) */
extern char opt_address[], opt_address6[], opt_port[];
/** @} */

/** @name Webcam Options
 * @{
 */
/** @brief Webcam device index (0 = first webcam) */
extern unsigned short int opt_webcam_index;
/** @brief Flip webcam image horizontally */
extern bool opt_webcam_flip;
/** @brief Use test pattern instead of real webcam */
extern bool opt_test_pattern;
/** @} */

/**
 * @brief Terminal color mode override (client only)
 * @ingroup options
 */
typedef enum {
  COLOR_MODE_AUTO = 0,      /**< Auto-detect terminal capabilities (default) */
  COLOR_MODE_MONO = 1,      /**< Force monochrome/grayscale output */
  COLOR_MODE_16_COLOR = 2,  /**< Force 16-color ANSI mode */
  COLOR_MODE_256_COLOR = 3, /**< Force 256-color palette mode */
  COLOR_MODE_TRUECOLOR = 4  /**< Force 24-bit truecolor mode */
} terminal_color_mode_t;

// Render mode is now defined in platform/terminal.h

/** @name Display Options
 * @{
 */
/** @brief Color mode override (client only) */
extern terminal_color_mode_t opt_color_mode;
/** @brief Render mode override */
extern render_mode_t opt_render_mode;
/** @brief Show detected capabilities and exit */
extern unsigned short int opt_show_capabilities;
/** @brief Force enable UTF-8 support via --utf8 */
extern unsigned short int opt_force_utf8;
/** @} */

/** @name Audio Configuration Options
 * @{
 */
/** @brief Enable audio streaming */
extern unsigned short int opt_audio_enabled;
/** @brief Audio input device index (-1 = use default) */
extern int opt_audio_device;
/** @} */

/** @name Display/Image Options
 * @{
 */
/** @brief Allow image to stretch or shrink without preserving aspect ratio (if non-zero) */
extern unsigned short int opt_stretch;
/** @} */

/** @name Output Options
 * @{
 */
/** @brief Disable console logging (quiet mode, if non-zero) */
extern unsigned short int opt_quiet;
/** @brief Enable snapshot mode - capture one frame and exit (client only, if non-zero) */
extern unsigned short int opt_snapshot_mode;
/** @brief Snapshot delay in seconds (default 3.0 for webcam warmup) */
extern float opt_snapshot_delay;
/** @brief Log file path (empty string means no file logging) */
extern char opt_log_file[OPTIONS_BUFF_SIZE];
/** @} */

/** @name Encryption Options
 * @{
 */
/** @brief Enable encryption */
extern unsigned short int opt_encrypt_enabled;
/** @brief SSH/GPG key file path from --key (file-based only) */
extern char opt_encrypt_key[OPTIONS_BUFF_SIZE];
/** @brief Password string from --password */
extern char opt_password[OPTIONS_BUFF_SIZE];
/** @brief Key file path from --keyfile */
extern char opt_encrypt_keyfile[OPTIONS_BUFF_SIZE];
/** @brief Disable encryption (opt-out, if non-zero) */
extern unsigned short int opt_no_encrypt;
/** @brief Expected server public key (client only) */
extern char opt_server_key[OPTIONS_BUFF_SIZE];
/** @brief Allowed client keys (server only) */
extern char opt_client_keys[OPTIONS_BUFF_SIZE];
/** @} */

/** @name Palette Configuration Options
 * @{
 */
/** @brief Selected palette type */
extern palette_type_t opt_palette_type;
/** @brief Custom palette characters */
extern char opt_palette_custom[256];
/** @brief True if custom palette was set via command line */
extern bool opt_palette_custom_set;
/** @brief Red weight for luminance calculation (default weights must add up to 1.0) */
extern const float weight_red;
/** @brief Green weight for luminance calculation */
extern const float weight_green;
/** @brief Blue weight for luminance calculation */
extern const float weight_blue;
/** @brief Red channel lookup table */
extern unsigned short int RED[], GREEN[], BLUE[], GRAY[];
/** @} */

/**
 * @brief Initialize options by parsing command-line arguments
 * @param argc Argument count
 * @param argv Argument vector
 * @param is_client true if parsing client options, false for server options
 * @return ASCIICHAT_OK on success, ERROR_USAGE on parse errors
 *
 * @note --help and --version also return ASCIICHAT_OK after printing info
 * @ingroup options
 */
asciichat_error_t options_init(int argc, char **argv, bool is_client);

/**
 * @brief Print usage information
 * @param desc File descriptor to write to (typically stdout or stderr)
 * @param is_client true for client usage, false for server usage
 * @ingroup options
 */
void usage(FILE *desc, bool is_client);

/**
 * @brief Print client usage information
 * @param desc File descriptor to write to (typically stdout or stderr)
 * @ingroup options
 */
void usage_client(FILE *desc);

/**
 * @brief Print server usage information
 * @param desc File descriptor to write to (typically stdout or stderr)
 * @ingroup options
 */
void usage_server(FILE *desc);

/**
 * @brief Update dimensions for full height display
 *
 * Adjusts width and height to use the full terminal height while maintaining
 * aspect ratio.
 * @ingroup options
 */
void update_dimensions_for_full_height(void);

/**
 * @brief Update dimensions to match terminal size
 *
 * Sets width and height to match the current terminal dimensions.
 * @ingroup options
 */
void update_dimensions_to_terminal_size(void);
