/**
 * @file lib/options/colorscheme.h
 * @brief Color scheme management system for ascii-chat
 * @ingroup options
 *
 * Provides comprehensive color scheme support with theme-aware adaptation:
 * - Built-in color schemes (pastel, nord, solarized, dracula, gruvbox, monokai)
 * - RGB to ANSI conversion (16, 256, truecolor)
 * - Light/dark theme variants that adapt to terminal background
 * - TOML configuration file support
 * - Terminal theme detection (dark vs light background)
 * - Early initialization before logging
 *
 * Color schemes define how log messages are displayed with different colors
 * for various log levels (debug, info, warn, error, etc.). Schemes automatically
 * adapt to the user's terminal theme (dark or light background) and can be
 * selected via CLI arguments, config files, or programmatically.
 *
 * ## Theme System
 *
 * The theme system respects the user's terminal background color preference:
 * - **Dark Theme**: Terminal has a dark/black background (default for most dev terminals)
 * - **Light Theme**: Terminal has a light/white background
 *
 * Terminal theme is detected automatically via terminal_has_dark_background() which
 * uses OSC 11 queries, environment variables, and terminal type hints. Color schemes
 * then adapt their palettes accordingly to maintain readability.
 */

#pragma once

#include "../common.h"
#include "../platform/terminal.h"
#include "../video/image.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Color Definitions
 * ============================================================================ */

/**
 * @brief Terminal theme detection result
 *
 * Represents the detected or selected terminal theme (color background preference).
 * Used to choose appropriate color schemes and contrast levels.
 */
typedef enum {
  TERM_BACKGROUND_UNKNOWN = 0, /**< Theme not detected or auto-detect disabled */
  TERM_BACKGROUND_LIGHT = 1,   /**< Light theme: light/white background, use dark text colors */
  TERM_BACKGROUND_DARK = 2     /**< Dark theme: dark/black background, use light text colors */
} terminal_background_t;

/**
 * @brief Color scheme definition
 *
 * Defines a color scheme with RGB colors for logging levels.
 * Supports both dark and light mode variants.
 */
typedef struct color_scheme_t {
  char name[64];         /**< Scheme name (e.g., "pastel", "nord") */
  char description[256]; /**< Scheme description */

  /* Dark mode colors (default, 8 colors for log levels) */
  rgb_pixel_t log_colors_dark[8]; /**< DEV, DEBUG, WARN, INFO, ERROR, FATAL, GREY, RESET */

  /* Light mode colors (optional) */
  bool has_light_variant;          /**< Whether light variant is defined */
  rgb_pixel_t log_colors_light[8]; /**< Light variant colors */

  /* Metadata */
  bool is_builtin;                            /**< Whether scheme is built-in */
  char source_file[PLATFORM_MAX_PATH_LENGTH]; /**< Source file path if loaded from file */
} color_scheme_t;

/**
 * @brief Compiled ANSI escape codes for a color scheme
 *
 * Contains pre-compiled ANSI escape codes for all log colors
 * in different terminal color modes. Stores pointers to allocated strings
 * rather than inline char arrays, matching the interface expected by callers.
 */
typedef struct {
  const char *codes_16[8];        /**< Array of 16-color ANSI code strings */
  const char *codes_256[8];       /**< Array of 256-color ANSI code strings */
  const char *codes_truecolor[8]; /**< Array of 24-bit truecolor ANSI code strings */
} compiled_color_scheme_t;

/* ============================================================================
 * Color System API
 * ============================================================================ */

/**
 * @brief Initialize the color system
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Must be called once at program startup before using any color functions.
 */
asciichat_error_t colorscheme_init(void);

/**
 * @brief Shutdown the color system
 *
 * Frees all allocated resources. Call at program shutdown.
 */
void colorscheme_destroy(void);

/**
 * @brief Get currently active color scheme
 * @return Pointer to active color scheme (must not be freed)
 *
 * Returns the currently active color scheme. Default is "pastel" if not set.
 */
const color_scheme_t *colorscheme_get_active_scheme(void);

/**
 * @brief Set the active color scheme
 * @param name Scheme name (e.g., "pastel", "nord", "solarized-dark")
 * @return ASCIICHAT_OK on success, error code if scheme not found
 *
 * Changes the active color scheme. Scheme must be either built-in or
 * previously loaded from a TOML file.
 */
asciichat_error_t colorscheme_set_active_scheme(const char *name);

/**
 * @brief Load a built-in color scheme
 * @param name Scheme name
 * @param scheme Pointer to store loaded scheme
 * @return ASCIICHAT_OK on success, error code if not found
 *
 * Loads a built-in color scheme by name. "default" is aliased to "pastel".
 */
asciichat_error_t colorscheme_load_builtin(const char *name, color_scheme_t *scheme);

/**
 * @brief Load a color scheme from a TOML file
 * @param path Path to TOML file
 * @param scheme Pointer to store loaded scheme
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Loads a color scheme from a TOML configuration file.
 * See docs/colors.toml for format specification.
 */
asciichat_error_t colorscheme_load_from_file(const char *path, color_scheme_t *scheme);

/**
 * @brief Compile a color scheme to ANSI codes
 * @param scheme Source scheme (must not be NULL)
 * @param mode Terminal color mode (16, 256, or truecolor)
 * @param background Terminal background (light or dark)
 * @param compiled Output compiled codes (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Compiles RGB colors to ANSI escape codes for the specified terminal mode
 * and background. Applies background-appropriate color variant (light/dark).
 */
asciichat_error_t colorscheme_compile_scheme(const color_scheme_t *scheme, terminal_color_mode_t mode,
                                             terminal_background_t background, compiled_color_scheme_t *compiled);

/**
 * @brief Clean up allocated strings in a compiled color scheme
 * @param compiled Compiled color scheme to clean (NULL-safe)
 *
 * Frees all allocated color code strings and zeros the structure.
 */
void colorscheme_cleanup_compiled(compiled_color_scheme_t *compiled);

/**
 * @brief Export a color scheme to TOML format
 * @param scheme_name Name of scheme to export
 * @param file_path Output file path (NULL = stdout)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Exports a color scheme to TOML format. If file_path is NULL, writes to stdout.
 */
asciichat_error_t colorscheme_export_scheme(const char *scheme_name, const char *file_path);

/* ============================================================================
 * Color Conversion Utilities
 * ============================================================================ */

/**
 * @brief Parse a hex color string
 * @param hex Hex color string (e.g., "#FF0000" or "FF0000")
 * @param r Pointer to store red channel
 * @param g Pointer to store green channel
 * @param b Pointer to store blue channel
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Parses hex color strings in #RRGGBB or RRGGBB format.
 */
asciichat_error_t parse_hex_color(const char *hex, uint8_t *r, uint8_t *g, uint8_t *b);

/* Note: rgb_to_16color() and rgb_to_256color() are defined in lib/video/ansi_fast.h
 * To use them in colors.c, include ansi_fast.h */

/**
 * @brief Generate truecolor ANSI escape code
 * @param r Red channel (0-255)
 * @param g Green channel (0-255)
 * @param b Blue channel (0-255)
 * @param buf Output buffer for escape code
 * @param size Buffer size (minimum 20 bytes)
 *
 * Generates ANSI truecolor escape code: \x1b[38;2;R;G;Bm
 */
void rgb_to_truecolor_ansi(uint8_t r, uint8_t g, uint8_t b, char *buf, size_t size);

/* ============================================================================
 * Terminal Background Detection
 * ============================================================================ */

/**
 * @brief Detect terminal background (light or dark)
 * @return Detected terminal background
 *
 * Uses multiple methods to detect terminal background:
 * 1. TERM_BACKGROUND environment variable
 * 2. COLORFGBG environment variable
 * 3. OSC 11 terminal query (if supported)
 * 4. Defaults to DARK
 */
terminal_background_t detect_terminal_background(void);

/* ============================================================================
 * Early Color Initialization (for main() before log_init)
 * ============================================================================ */

/**
 * @brief Initialize color scheme early, before logging
 * @param argc Command-line argument count
 * @param argv Command-line arguments
 * @return ASCIICHAT_OK on success, error code on failure (non-fatal)
 *
 * Called from main() BEFORE log_init() to apply color scheme to logging.
 * Scans for --color-scheme and loads ~/.config/ascii-chat/colors.toml.
 *
 * Priority: --color-scheme CLI > colors.toml > built-in default
 */
asciichat_error_t options_colorscheme_init_early(int argc, const char *const argv[]);

/* ============================================================================
 * Internal: Shared Mutex for Color Compilation
 * ============================================================================ */

/**
 * @brief Shared mutex for color scheme compilation
 *
 * Used by lib/options/colorscheme.c and lib/log/logging.c to synchronize
 * color scheme compilation. Defined in colorscheme.c and declared here
 * for use by logging.c.
 */
extern mutex_t g_colorscheme_mutex;

#ifdef __cplusplus
}
#endif
