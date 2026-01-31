/**
 * @file registry.c
 * @brief Central options registry implementation
 * @ingroup options
 *
 * Defines all command-line options exactly once with mode bitmasks.
 * This is the single source of truth for all options.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include "options/registry.h"
#include "options/parsers.h" // For parse_log_level, parse_color_mode, parse_palette_type, parse_palette_chars, etc.
#include "options/validation.h"
#include "options/actions.h" // For action_list_webcams, action_list_microphones, action_list_speakers
#include "options/common.h"
#include "common.h"
#include "log/logging.h"
#include "platform/terminal.h"
#include "video/palette.h"
#include "discovery/strings.h" // For SESSION_STRING_BUFFER_SIZE
#include "options/parsers.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h> // For islower in parse_cookies_from_browser

// ============================================================================
// Registry Entry Structure Definition
// ============================================================================
// Note: All default_*_value variables are now defined in options.h with their
// corresponding OPT_*_DEFAULT constants for single source of truth.

/**
 * @brief Registry entry - stores option definition with mode bitmask and metadata
 */
typedef struct {
  const char *long_name;
  char short_name;
  option_type_t type;
  size_t offset;
  const void *default_value;
  size_t default_value_size;
  const char *help_text;
  const char *group;
  const char *arg_placeholder; ///< Custom argument placeholder (e.g., "SHELL [FILE]" instead of "STR")
  bool required;
  const char *env_var_name;
  bool (*validate_fn)(const void *options_struct, char **error_msg);
  bool (*parse_fn)(const char *arg, void *dest, char **error_msg);
  bool owns_memory;
  bool optional_arg;
  option_mode_bitmask_t mode_bitmask;
  option_metadata_t metadata; ///< Enum values, numeric ranges, examples
} registry_entry_t;

/**
 * @brief Initialize a terminator entry (sentinel value for array end)
 * Uses designated initializers to properly initialize all fields to zero/NULL
 */
#define REGISTRY_TERMINATOR()                                                                                          \
  {.long_name = NULL,                                                                                                  \
   .short_name = '\0',                                                                                                 \
   .type = OPTION_TYPE_BOOL,                                                                                           \
   .offset = 0,                                                                                                        \
   .default_value = NULL,                                                                                              \
   .default_value_size = 0,                                                                                            \
   .help_text = NULL,                                                                                                  \
   .group = NULL,                                                                                                      \
   .arg_placeholder = NULL,                                                                                            \
   .required = false,                                                                                                  \
   .env_var_name = NULL,                                                                                               \
   .validate_fn = NULL,                                                                                                \
   .parse_fn = NULL,                                                                                                   \
   .owns_memory = false,                                                                                               \
   .optional_arg = false,                                                                                              \
   .mode_bitmask = OPTION_MODE_NONE,                                                                                   \
   .metadata = {0}}

// ============================================================================
// Static Metadata Arrays (Enum Values, Descriptions, Ranges)
// ============================================================================

// Log level metadata
static const char *g_log_level_values[] = {"dev", "debug", "info", "warn", "error", "fatal"};
static const int g_log_level_integers[] = {LOG_DEV, LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL};
static const char *g_log_level_descs[] = {"Development (most verbose, includes function traces)",
                                          "Debug (includes internal state tracking)",
                                          "Informational (key lifecycle events)",
                                          "Warnings (unusual conditions)",
                                          "Errors only",
                                          "Fatal errors only"};

// Color setting metadata (--color flag values)
static const char *g_color_setting_values[] = {"auto", "true", "false"};
static const int g_color_setting_integers[] = {COLOR_SETTING_AUTO, COLOR_SETTING_TRUE, COLOR_SETTING_FALSE};
static const char *g_color_setting_descs[] = {"Smart detection (colors if TTY and not piping/CLAUDECODE)",
                                              "Force colors ON (override TTY/pipe/CLAUDECODE)",
                                              "Force colors OFF (disable all colors)"};

// UTF-8 setting metadata (--utf8 flag values)
static const char *g_utf8_setting_values[] = {"auto", "true", "false"};
static const int g_utf8_setting_integers[] = {UTF8_SETTING_AUTO, UTF8_SETTING_TRUE, UTF8_SETTING_FALSE};
static const char *g_utf8_setting_descs[] = {"Auto-detect UTF-8 support from terminal capabilities",
                                             "Force UTF-8 ON (always use UTF-8 regardless of terminal)",
                                             "Force UTF-8 OFF (disable UTF-8 support)"};

// Color mode metadata
static const char *g_color_mode_values[] = {"auto", "none", "16", "256", "truecolor"};
static const int g_color_mode_integers[] = {TERM_COLOR_AUTO, TERM_COLOR_NONE, TERM_COLOR_16, TERM_COLOR_256,
                                            TERM_COLOR_TRUECOLOR};
static const char *g_color_mode_descs[] = {"Auto-detect from terminal", "Monochrome only", "16 colors (ANSI)",
                                           "256 colors (xterm)", "24-bit truecolor (modern terminals)"};

// Palette metadata
static const char *g_palette_values[] = {"standard", "blocks", "digital", "minimal", "cool", "custom"};
static const int g_palette_integers[] = {PALETTE_STANDARD, PALETTE_BLOCKS, PALETTE_DIGITAL,
                                         PALETTE_MINIMAL,  PALETTE_COOL,   PALETTE_CUSTOM};
static const char *g_palette_descs[] = {"Standard ASCII palette", "Block characters (full/half/quarter blocks)",
                                        "Digital/computer style", "Minimal palette (light aesthetic)",
                                        "Cool/modern style",      "Custom user-defined characters"};

// Render mode metadata
static const char *g_render_values[] = {"foreground", "fg", "background", "bg", "half-block"};
static const int g_render_integers[] = {RENDER_MODE_FOREGROUND, RENDER_MODE_FOREGROUND, // fg is alias for foreground
                                        RENDER_MODE_BACKGROUND, RENDER_MODE_BACKGROUND, // bg is alias for background
                                        RENDER_MODE_HALF_BLOCK};
static const char *g_render_descs[] = {
    "Render using foreground characters only", "Render using foreground characters only (alias)",
    "Render using background colors only", "Render using background colors only (alias)",
    "Use half-block characters for 2x vertical resolution"};

// Compression level examples (null-terminated)
static const char *g_compression_examples[] = {"1", "3", "9", NULL};

// FPS examples (null-terminated)
static const char *g_fps_examples[] = {"30", "60", "144", NULL};

// Width examples (null-terminated)
static const char *g_width_examples[] = {"80", "120", "160", NULL};

// Height examples (null-terminated)
static const char *g_height_examples[] = {"24", "40", "60", NULL};

// Max clients examples (null-terminated)
static const char *g_maxclients_examples[] = {"2", "4", "8", NULL};

// Reconnect attempts examples (null-terminated)
static const char *g_reconnect_examples[] = {"0", "5", "10", NULL};

// Webcam index examples (null-terminated)
static const char *g_webcam_examples[] = {"0", "1", "2", NULL};

// Microphone index examples (null-terminated)
static const char *g_mic_examples[] = {"-1", "0", "1", NULL};

// Speakers index examples (null-terminated)
static const char *g_speakers_examples[] = {"-1", "0", "1", NULL};

// Seek examples (null-terminated)
static const char *g_seek_examples[] = {"0", "60", "3:45", NULL};

// Cookies from browser values (null-terminated)
static const char *g_cookies_values[] = {"chrome", "firefox", "edge",  "safari", "brave",
                                         "opera",  "vivaldi", "whale", NULL};
static const char *g_cookies_descs[] = {"Google Chrome",   "Mozilla Firefox", "Microsoft Edge",
                                        "Apple Safari",    "Brave Browser",   "Opera Browser",
                                        "Vivaldi Browser", "Naver Whale",     NULL};

// Audio source metadata
static const char *g_audio_source_values[] = {"auto", "mic", "media", "both"};
static const int g_audio_source_integers[] = {AUDIO_SOURCE_AUTO, AUDIO_SOURCE_MIC, AUDIO_SOURCE_MEDIA,
                                              AUDIO_SOURCE_BOTH};
static const char *g_audio_source_descs[] = {"Smart selection (media-only when playing files, mic-only otherwise)",
                                             "Microphone only (no media audio)", "Media audio only (no microphone)",
                                             "Both microphone and media audio simultaneously"};

// ============================================================================
// LOGGING CATEGORY - Binary-level logging options
// ============================================================================
static const registry_entry_t g_logging_entries[] = {
    // LOGGING GROUP (binary-level)
    {"log-file",
     'L',
     OPTION_TYPE_CALLBACK,
     offsetof(options_t, log_file),
     "",
     0,
     "Set FILE as path for log file",
     "LOGGING",
     NULL,
     false,
     "ASCII_CHAT_LOG_FILE",
     NULL,
     parse_log_file,
     false,
     false,
     OPTION_MODE_BINARY,
     {0}},
    {"log-level",
     '\0',
     OPTION_TYPE_CALLBACK,
     offsetof(options_t, log_level),
     &default_log_level_value,
     sizeof(log_level_t),
     "Set log level: dev, debug, info, warn, error, fatal. Logs at or above this level are written.",
     "LOGGING",
     NULL,
     false,
     "ASCII_CHAT_LOG_LEVEL",
     NULL,
     parse_log_level,
     false,
     false,
     OPTION_MODE_BINARY,
     {.enum_values = g_log_level_values,
      .enum_count = 6,
      .enum_descriptions = g_log_level_descs,
      .enum_integer_values = g_log_level_integers,
      .input_type = OPTION_INPUT_ENUM}},
    {"verbose",
     'V',
     OPTION_TYPE_CALLBACK,
     offsetof(options_t, verbose_level),
     0,
     sizeof(unsigned short int),
     "Increase log verbosity (stackable: -VV, -VVV)",
     "LOGGING",
     NULL,
     false,
     "ASCII_CHAT_VERBOSE",
     NULL,
     parse_verbose_flag,
     false,
     true,
     OPTION_MODE_BINARY,
     {0}},
    {"quiet",
     'q',
     OPTION_TYPE_BOOL,
     offsetof(options_t, quiet),
     OPT_QUIET_DEFAULT,
     sizeof(bool),
     "Disable console logging (log to file only)",
     "LOGGING",
     NULL,
     false,
     "ASCII_CHAT_QUIET",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_BINARY,
     {0}},
    {"keepawake",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, enable_keepawake),
     OPT_QUIET_DEFAULT,
     sizeof(bool),
     "Explicitly enable system sleep prevention (keepawake mode)",
     "GENERAL",
     NULL,
     false,
     "ASCII_CHAT_KEEPAWAKE",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_BINARY,
     {0}},
    {"no-keepawake",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, disable_keepawake),
     OPT_QUIET_DEFAULT,
     sizeof(bool),
     "Disable system sleep prevention (allow OS to sleep)",
     "GENERAL",
     NULL,
     false,
     "ASCII_CHAT_NO_KEEPAWAKE",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_BINARY,
     {0}},
    {"color",
     '\0',
     OPTION_TYPE_CALLBACK,
     offsetof(options_t, color),
     &default_color_value,
     sizeof(int),
     "Color output setting: 'auto' (default, smart detection), 'true' (force colors on), or 'false' (force colors off)",
     "TERMINAL",
     NULL,
     false,
     "ASCII_CHAT_COLOR",
     NULL,
     parse_color_setting,
     false,
     true,
     OPTION_MODE_BINARY,
     {.enum_values = g_color_setting_values,
      .enum_count = 3,
      .enum_descriptions = g_color_setting_descs,
      .enum_integer_values = g_color_setting_integers,
      .input_type = OPTION_INPUT_ENUM}},
    REGISTRY_TERMINATOR()};

// ============================================================================
// CONFIGURATION CATEGORY - Configuration file options
// ============================================================================
static const registry_entry_t g_configuration_entries[] = {
    // CONFIGURATION GROUP (binary-level)
    {"config",
     '\0',
     OPTION_TYPE_STRING,
     offsetof(options_t, config_file),
     "",
     0,
     "Load configuration from toml FILE",
     "CONFIGURATION",
     NULL,
     false,
     NULL,
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_BINARY,
     {0}},
    {"config-create",
     '\0',
     OPTION_TYPE_BOOL,
     0,
     NULL,
     0,
     "Create default config file and exit (optionally specify output path)",
     "CONFIGURATION",
     NULL,
     false,
     NULL,
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_BINARY,
     {0}},
    {"color-scheme-create",
     '\0',
     OPTION_TYPE_STRING,
     0,
     NULL,
     0,
     "Export color scheme to TOML format (optionally specify scheme name and output file)",
     "CONFIGURATION",
     "[SCHEME] [FILE]",
     false,
     NULL,
     NULL,
     NULL,
     true, /* optional_arg - scheme name is optional */
     false,
     OPTION_MODE_BINARY,
     {.input_type = OPTION_INPUT_STRING}},
    REGISTRY_TERMINATOR()};

// ============================================================================
// SHELL CATEGORY - Shell integration options
// ============================================================================
static const registry_entry_t g_shell_entries[] = {
    // SHELL GROUP (binary-level)
    {"completions",
     '\0',
     OPTION_TYPE_STRING,
     0,
     NULL,
     0,
     "Generate shell completions (bash, fish, zsh, powershell) and output to stdout (or optional file path)",
     "SHELL",
     "SHELL [FILE]",
     false,
     NULL,
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_BINARY,
     {0}},
    {"man-page-create",
     '\0',
     OPTION_TYPE_BOOL,
     0,
     NULL,
     0,
     "Generate man page and exit (optionally specify output file)",
     "CONFIGURATION",
     NULL,
     false,
     NULL,
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_BINARY,
     {0}},

    REGISTRY_TERMINATOR()};

// ============================================================================
// TERMINAL CATEGORY - Terminal display options
// ============================================================================
static const registry_entry_t g_terminal_entries[] = {
    // TERMINAL GROUP (client, mirror, discovery)
    {"width",
     'x',
     OPTION_TYPE_INT,
     offsetof(options_t, width),
     &default_width_value,
     sizeof(int),
     "Terminal width in characters. Can be controlled using $COLUMNS. By default your terminal width is detected at "
     "runtime and this value is updated automatically.",
     "TERMINAL",
     NULL,
     false,
     "ASCII_CHAT_WIDTH",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {.numeric_range = {20, 512, 0}, .examples = g_width_examples, .input_type = OPTION_INPUT_NUMERIC}},
    {"height",
     'y',
     OPTION_TYPE_INT,
     offsetof(options_t, height),
     &default_height_value,
     sizeof(int),
     "Terminal height in characters. Can be controlled using $ROWS. By default your terminal height is detected at "
     "runtime and this value is updated automatically.",
     "TERMINAL",
     NULL,
     false,
     "ASCII_CHAT_HEIGHT",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {.numeric_range = {10, 256, 0}, .examples = g_height_examples, .input_type = OPTION_INPUT_NUMERIC}},
    {"color-scheme",
     '\0',
     OPTION_TYPE_STRING,
     offsetof(options_t, color_scheme_name),
     OPT_COLOR_SCHEME_NAME_DEFAULT,
     sizeof(((options_t *)0)->color_scheme_name),
     "Color scheme for logging output (pastel, nord, solarized-dark, dracula, gruvbox-dark, monokai, etc.)",
     "TERMINAL",
     "NAME",
     false,
     "ASCII_CHAT_COLOR_SCHEME",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_SERVER | OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {.input_type = OPTION_INPUT_STRING}},

    REGISTRY_TERMINATOR()};

// ============================================================================
// WEBCAM CATEGORY - Webcam capture options
// ============================================================================
static const registry_entry_t g_webcam_entries[] = {
    // WEBCAM GROUP (client, mirror, discovery)
    {"webcam-index",
     'c',
     OPTION_TYPE_INT,
     offsetof(options_t, webcam_index),
     &default_webcam_index_value,
     sizeof(unsigned short int),
     "Webcam device index to use for video input",
     "WEBCAM",
     NULL,
     false,
     "ASCII_CHAT_WEBCAM_INDEX",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {.numeric_range = {0, 10, 1}, .examples = g_webcam_examples, .input_type = OPTION_INPUT_NUMERIC}},
    {"webcam-flip",
     'g',
     OPTION_TYPE_BOOL,
     offsetof(options_t, webcam_flip),
     &default_webcam_flip_value,
     sizeof(bool),
     "Flip webcam output horizontally before using it",
     "WEBCAM",
     NULL,
     false,
     "ASCII_CHAT_WEBCAM_FLIP",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {0}},
    {"test-pattern",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, test_pattern),
     &default_test_pattern_value,
     sizeof(bool),
     "Use test pattern instead of webcam",
     "WEBCAM",
     NULL,
     false,
     "WEBCAM_DISABLED",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {0}},
    {"list-webcams",
     '\0',
     OPTION_TYPE_ACTION,
     0,
     NULL,
     0,
     "List available webcam devices by index and exit.",
     "WEBCAM",
     NULL,
     false,
     NULL,
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {0}},
    {"list-microphones",
     '\0',
     OPTION_TYPE_ACTION,
     0,
     NULL,
     0,
     "List available audio input devices by index and exit.",
     "AUDIO",
     NULL,
     false,
     NULL,
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {0}},
    {"list-speakers",
     '\0',
     OPTION_TYPE_ACTION,
     0,
     NULL,
     0,
     "List available audio output devices by index and exit.",
     "AUDIO",
     NULL,
     false,
     NULL,
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {0}},

    REGISTRY_TERMINATOR()};

// ============================================================================
// DISPLAY CATEGORY - Display layout options
// ============================================================================
static const registry_entry_t g_display_entries[] = {
    // DISPLAY GROUP (client, mirror, discovery)
    {"color-mode",
     '\0',
     OPTION_TYPE_CALLBACK,
     offsetof(options_t, color_mode),
     &default_color_mode_value,
     sizeof(terminal_color_mode_t),
     "Terminal color level (auto, none, 16, 256, truecolor). This controls what ANSI escape codes ascii-chat will use "
     "for console logging and display output if color is enabled. See also --color.",
     "TERMINAL",
     NULL,
     false,
     "ASCII_CHAT_COLOR_MODE",
     NULL,
     parse_color_mode,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {.enum_values = g_color_mode_values,
      .enum_count = 5,
      .enum_descriptions = g_color_mode_descs,
      .enum_integer_values = g_color_mode_integers,
      .input_type = OPTION_INPUT_ENUM}},
    {"render-mode",
     'M',
     OPTION_TYPE_CALLBACK,
     offsetof(options_t, render_mode),
     &default_render_mode_value,
     sizeof(render_mode_t),
     "Render mode of text for your client to display. Values: foreground, background, half-block.",
     "DISPLAY",
     NULL,
     false,
     "ASCII_CHAT_RENDER_MODE",
     NULL,
     parse_render_mode,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {.enum_values = g_render_values,
      .enum_count = 5,
      .enum_descriptions = g_render_descs,
      .enum_integer_values = g_render_integers,
      .input_type = OPTION_INPUT_ENUM}},
    {"palette",
     'P',
     OPTION_TYPE_CALLBACK,
     offsetof(options_t, palette_type),
     &default_palette_type_value,
     sizeof(palette_type_t),
     "Palette type with which to render images to text art. Values: standard, blocks, digital, minimal, cool, "
     "custom. All but \"custom\" are built-in presets that all look different and nice. Try them out!",
     "DISPLAY",
     NULL,
     false,
     "ASCII_CHAT_PALETTE",
     NULL,
     parse_palette_type,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {.enum_values = g_palette_values,
      .enum_count = 6,
      .enum_descriptions = g_palette_descs,
      .enum_integer_values = g_palette_integers,
      .input_type = OPTION_INPUT_ENUM}},
    {"palette-chars",
     'C',
     OPTION_TYPE_CALLBACK,
     offsetof(options_t, palette_custom),
     "",
     0,
     "Ordered sequence of characters from darkest to brightest to use with custom palette (--palette=custom) for "
     "rendering images to text art for your client. These characters only will be used to create the rendered output. "
     "Can be UTF-8 content (see --utf8).",
     "DISPLAY",
     NULL,
     false,
     "ASCII_CHAT_PALETTE_CHARS",
     NULL,
     parse_palette_chars,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {0}},
    {"show-capabilities",
     '\0',
     OPTION_TYPE_ACTION,
     0,
     NULL,
     0,
     "Show detected terminal capabilities and exit (useful for debugging and scripting)",
     "TERMINAL",
     NULL,
     false,
     NULL,
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {0}},
    {"utf8",
     '\0',
     OPTION_TYPE_CALLBACK,
     offsetof(options_t, force_utf8),
     &default_force_utf8_value,
     sizeof(int),
     "UTF-8 support setting: 'auto' (default, auto-detect), 'true' (force UTF-8 on), or 'false' (force UTF-8 off)",
     "TERMINAL",
     NULL,
     false,
     "ASCII_CHAT_UTF8",
     NULL,
     parse_utf8_setting,
     false,
     true,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {.enum_values = g_utf8_setting_values,
      .enum_count = 3,
      .enum_descriptions = g_utf8_setting_descs,
      .enum_integer_values = g_utf8_setting_integers,
      .input_type = OPTION_INPUT_ENUM}},
    {"stretch",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, stretch),
     &default_stretch_value,
     sizeof(bool),
     "Allow aspect ratio distortion of image for rendering ascii output. This can allow the rendered ascii to fill "
     "your terminal.",
     "DISPLAY",
     NULL,
     false,
     "ASCII_CHAT_STRETCH",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {0}},
    {"strip-ansi",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, strip_ansi),
     &default_strip_ansi_value,
     sizeof(bool),
     "Strip ANSI escape sequences from output before printing. Useful for scripting and debugging.",
     "TERMINAL",
     NULL,
     false,
     "ASCII_CHAT_STRIP_ANSI",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {0}},
    {"fps",
     '\0',
     OPTION_TYPE_INT,
     offsetof(options_t, fps),
     0,
     sizeof(int),
     "Target framerate for rendering ascii (1-144, 0=use default).",
     "DISPLAY",
     NULL,
     false,
     "ASCII_CHAT_FPS",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {.numeric_range = {1, 144, 0}, .examples = g_fps_examples, .input_type = OPTION_INPUT_NUMERIC}},
    {"snapshot",
     'S',
     OPTION_TYPE_BOOL,
     offsetof(options_t, snapshot_mode),
     &default_snapshot_mode_value,
     sizeof(bool),
     "Snapshot mode (one frame and exit)",
     "DISPLAY",
     NULL,
     false,
     "ASCII_CHAT_SNAPSHOT",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {0}},
    {"snapshot-delay",
     'D',
     OPTION_TYPE_DOUBLE,
     offsetof(options_t, snapshot_delay),
     &default_snapshot_delay_value,
     sizeof(double),
     "Snapshot delay in seconds. The timer starts right before the client-side program prints the first frame. "
     "--snapshot --snapshot-delay=0 will print the first frame and exit.",
     "DISPLAY",
     NULL,
     false,
     "ASCII_CHAT_SNAPSHOT_DELAY",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {0}},

    REGISTRY_TERMINATOR()};

// ============================================================================
// NETWORK CATEGORY - Network protocol options
// ============================================================================
static const registry_entry_t g_network_entries[] = {
    // NETWORK GROUP - compression options (client, server, discovery)
    {"compression-level",
     '\0',
     OPTION_TYPE_INT,
     offsetof(options_t, compression_level),
     &default_compression_level_value,
     sizeof(int),
     "zstd compression level (1-9)",
     "NETWORK",
     NULL,
     false,
     "ASCII_CHAT_COMPRESSION_LEVEL",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY,
     {.numeric_range = {1, 9, 1}, .examples = g_compression_examples, .input_type = OPTION_INPUT_NUMERIC}},
    {"no-compress",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, no_compress),
     &default_no_compress_value,
     sizeof(bool),
     "Disable compression",
     "NETWORK",
     NULL,
     false,
     "ASCII_CHAT_NO_COMPRESS",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY,
     {0}},

    REGISTRY_TERMINATOR()};

// ============================================================================
// SECURITY CATEGORY - Security and authentication options
// ============================================================================
static const registry_entry_t g_security_entries[] = {
    // SECURITY GROUP (client, server, discovery)
    {"encrypt",
     'E',
     OPTION_TYPE_BOOL,
     offsetof(options_t, encrypt_enabled),
     &default_encrypt_enabled_value,
     sizeof(bool),
     "Enable end-to-end encryption (requires the other party to be encrypted as well)",
     "SECURITY",
     NULL,
     false,
     "ASCII_CHAT_ENCRYPT",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY | OPTION_MODE_DISCOVERY_SVC,
     {0}},
    {"key",
     'K',
     OPTION_TYPE_STRING,
     offsetof(options_t, encrypt_key),
     "",
     0,
     "Server identity key (SSH Ed25519 or GPG key file, gpg:FINGERPRINT, github:USER[.gpg], gitlab:USER[.gpg], or "
     "HTTPS URL like https://example.com/key.pub or .gpg)",
     "SECURITY",
     NULL,
     false,
     "ASCII_CHAT_KEY",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY | OPTION_MODE_DISCOVERY_SVC,
     {0}},
    {"password",
     '\0',
     OPTION_TYPE_STRING,
     offsetof(options_t, password),
     "",
     0,
     "Shared password for authentication (8-256 characters)",
     "SECURITY",
     NULL,
     false,
     "ASCII_CHAT_PASSWORD",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY | OPTION_MODE_DISCOVERY_SVC,
     {0}},
    {"no-encrypt",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, no_encrypt),
     &default_no_encrypt_value,
     sizeof(bool),
     "Disable encryption (requires the other party to be unencrypted as well)",
     "SECURITY",
     NULL,
     false,
     "ASCII_CHAT_NO_ENCRYPT",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY | OPTION_MODE_DISCOVERY_SVC,
     {0}},
    {"server-key",
     '\0',
     OPTION_TYPE_STRING,
     offsetof(options_t, server_key),
     "",
     0,
     "Expected server public key for verification (SSH Ed25519 or GPG key file, gpg:FINGERPRINT, github:USER[.gpg], "
     "gitlab:USER[.gpg], or HTTPS URL like https://example.com/key.pub or .gpg)",
     "SECURITY",
     NULL,
     false,
     "ASCII_CHAT_SERVER_KEY",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY,
     {0}},
    {"client-keys",
     '\0',
     OPTION_TYPE_STRING,
     offsetof(options_t, client_keys),
     "",
     0,
     "Allowed client keys (comma-separated: file paths with one key per line, github:USER[.gpg], gitlab:USER[.gpg], "
     "gpg:KEYID, or HTTPS URLs)",
     "SECURITY",
     NULL,
     false,
     "ASCII_CHAT_CLIENT_KEYS",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY | OPTION_MODE_DISCOVERY_SVC,
     {0}},
    {"discovery-insecure",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, discovery_insecure),
     &default_discovery_insecure_value,
     sizeof(bool),
     "Skip server key verification (MITM-vulnerable, requires explicit opt-in)",
     "SECURITY",
     NULL,
     false,
     "ASCII_CHAT_DISCOVERY_INSECURE",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY,
     {0}},
    {"discovery-server-key",
     '\0',
     OPTION_TYPE_STRING,
     offsetof(options_t, discovery_service_key),
     "",
     0,
     "Discovery server public key for verification (SSH Ed25519 or GPG key file, gpg:FINGERPRINT, github:USER, "
     "gitlab:USER, or HTTPS URL like https://discovery.ascii-chat.com/key.pub)",
     "SECURITY",
     NULL,
     false,
     "ASCII_CHAT_DISCOVERY_SERVER_KEY",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY,
     {0}},

    // NETWORK GROUP (general network options, various modes)
    {"port",
     'p',
     OPTION_TYPE_CALLBACK,
     offsetof(options_t, port),
     OPT_PORT_DEFAULT,
     OPTIONS_BUFF_SIZE,
     "Port to host a server or discovery-service on, or port to connect to a server as a client",
     "NETWORK",
     NULL,
     false,
     "ASCII_CHAT_PORT",
     NULL,
     parse_port_option,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY_SVC | OPTION_MODE_DISCOVERY,
     {0}},
    {"max-clients",
     '\0',
     OPTION_TYPE_INT,
     offsetof(options_t, max_clients),
     &default_max_clients_value,
     sizeof(int),
     "Maximum concurrent clients",
     "NETWORK",
     NULL,
     false,
     "ASCII_CHAT_MAX_CLIENTS",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY_SVC, // Server and Discovery Service
     {.numeric_range = {1, 99, 1}, .examples = g_maxclients_examples, .input_type = OPTION_INPUT_NUMERIC}},
    {"reconnect-attempts",
     '\0',
     OPTION_TYPE_INT,
     offsetof(options_t, reconnect_attempts),
     &default_reconnect_attempts_value,
     sizeof(int),
     "Number of reconnection attempts before giving up (-1=infinite, 0=none)",
     "NETWORK",
     NULL,
     false,
     "ASCII_CHAT_RECONNECT_ATTEMPTS",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY, // Client and Discovery
     {.numeric_range = {-1, 99, 1}, .examples = g_reconnect_examples, .input_type = OPTION_INPUT_NUMERIC}},
    {"port-forwarding",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, enable_upnp),
     &default_enable_upnp_value,
     sizeof(bool),
     "Use UPnP/NAT-PMP port mapping to open a port in your router to ascii-chat (might fail with some routers)",
     "NETWORK",
     NULL,
     false,
     "ASCII_CHAT_PORT_FORWARDING",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY_SVC,
     {0}},
    {"scan",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, lan_discovery),
     &default_lan_discovery_value,
     sizeof(bool),
     "Scan for servers on local network via mDNS",
     "NETWORK",
     NULL,
     false,
     "ASCII_CHAT_SCAN",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY,
     {0}},

    // Discovery registration option
    {"discovery",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, discovery),
     &default_discovery_value,
     sizeof(bool),
     "Enable discovery session registration",
     "NETWORK",
     NULL,
     false,
     "ASCII_CHAT_DISCOVERY",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_SERVER,
     {0}},

    // WebRTC options
    {"webrtc",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, webrtc),
     &default_webrtc_value,
     sizeof(bool),
     "Make calls using WebRTC p2p connections",
     "NETWORK",
     NULL,
     false,
     "ASCII_CHAT_WEBRTC",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY,
     {0}},
    {"no-webrtc",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, no_webrtc),
     &default_no_webrtc_value,
     sizeof(bool),
     "Disable WebRTC, use direct TCP only",
     "NETWORK",
     NULL,
     false,
     "ASCII_CHAT_NO_WEBRTC",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY,
     {0}},
    {"prefer-webrtc",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, prefer_webrtc),
     &default_prefer_webrtc_value,
     sizeof(bool),
     "Try WebRTC before direct TCP",
     "NETWORK",
     NULL,
     false,
     "ASCII_CHAT_PREFER_WEBRTC",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY,
     {0}},
    {"webrtc-skip-stun",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, webrtc_skip_stun),
     &default_webrtc_skip_stun_value,
     sizeof(bool),
     "Skip WebRTC+STUN stage, go straight to TURN relay",
     "NETWORK",
     NULL,
     false,
     "ASCII_CHAT_WEBRTC_SKIP_STUN",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY,
     {0}},
    {"webrtc-disable-turn",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, webrtc_disable_turn),
     &default_webrtc_disable_turn_value,
     sizeof(bool),
     "Disable WebRTC+TURN relay, use STUN only",
     "NETWORK",
     NULL,
     false,
     "ASCII_CHAT_WEBRTC_DISABLE_TURN",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY,
     {0}},
    {"stun-servers",
     '\0',
     OPTION_TYPE_STRING,
     offsetof(options_t, stun_servers),
     OPT_STUN_SERVERS_DEFAULT,
     0,
     "Comma-separated list of WebRTC+STUN server URLs",
     "NETWORK",
     NULL,
     false,
     "ASCII_CHAT_STUN_SERVERS",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY_SVC | OPTION_MODE_DISCOVERY,
     {0}},
    {"turn-servers",
     '\0',
     OPTION_TYPE_STRING,
     offsetof(options_t, turn_servers),
     OPT_TURN_SERVERS_DEFAULT,
     0,
     "Comma-separated list of WebRTC+TURN server URLs",
     "NETWORK",
     NULL,
     false,
     "ASCII_CHAT_TURN_SERVERS",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY_SVC | OPTION_MODE_DISCOVERY,
     {0}},
    {"turn-username",
     '\0',
     OPTION_TYPE_STRING,
     offsetof(options_t, turn_username),
     OPT_TURN_USERNAME_DEFAULT,
     0,
     "Username for WebRTC+TURN authentication",
     "NETWORK",
     NULL,
     false,
     "ASCII_CHAT_TURN_USERNAME",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY_SVC | OPTION_MODE_DISCOVERY,
     {0}},
    {"turn-credential",
     '\0',
     OPTION_TYPE_STRING,
     offsetof(options_t, turn_credential),
     OPT_TURN_CREDENTIAL_DEFAULT,
     0,
     "Credential/password for WebRTC+TURN authentication",
     "NETWORK",
     NULL,
     false,
     "ASCII_CHAT_TURN_CREDENTIAL",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY_SVC | OPTION_MODE_DISCOVERY,
     {0}},
    {"turn-secret",
     '\0',
     OPTION_TYPE_STRING,
     offsetof(options_t, turn_secret),
     "",
     0,
     "Shared secret for dynamic WebRTC+TURN credential generation (HMAC-SHA1)",
     "NETWORK",
     NULL,
     false,
     "ASCII_CHAT_TURN_SECRET",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY_SVC | OPTION_MODE_DISCOVERY,
     {0}},

    REGISTRY_TERMINATOR()};

// ============================================================================
// MEDIA CATEGORY - Media file and stream options
// ============================================================================
static const registry_entry_t g_media_entries[] = {
    // Media File Streaming Options
    {"file",
     'f',
     OPTION_TYPE_STRING,
     offsetof(options_t, media_file),
     "",
     0,
     "Stream from media file or stdin (use '-' for stdin). Supported formats: see man ffmpeg-formats; codecs: see man "
     "ffmpeg-codecs",
     "MEDIA",
     NULL,
     false,
     "ASCII_CHAT_FILE",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {0}},
    {"url",
     'u',
     OPTION_TYPE_STRING,
     offsetof(options_t, media_url),
     "",
     0,
     "Stream from network URL (HTTP/HTTPS/YouTube/RTSP). URL handler: see man yt-dlp; supported formats: see man "
     "ffmpeg-formats; codecs: see man ffmpeg-codecs",
     "MEDIA",
     NULL,
     false,
     "ASCII_CHAT_URL",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {0}},
    {"loop",
     'l',
     OPTION_TYPE_BOOL,
     offsetof(options_t, media_loop),
     &default_media_loop_value,
     sizeof(bool),
     "Loop media file playback (not supported for --url)",
     "MEDIA",
     NULL,
     false,
     "ASCII_CHAT_LOOP",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {0}},
    {"pause",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, pause),
     &default_pause_value,
     sizeof(bool),
     "Start playback paused (toggle with spacebar, requires --file or --url)",
     "MEDIA",
     NULL,
     false,
     "ASCII_CHAT_PAUSE",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {0}},
    {"seek",
     's',
     OPTION_TYPE_CALLBACK,
     offsetof(options_t, media_seek_timestamp),
     &default_media_seek_value,
     sizeof(double),
     "Seek to timestamp before playback (format: seconds, MM:SS, or HH:MM:SS.ms)",
     "MEDIA",
     NULL,
     false,
     "ASCII_CHAT_SEEK",
     NULL,
     parse_timestamp,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {.examples = g_seek_examples, .input_type = OPTION_INPUT_STRING}},
    {"cookies-from-browser",
     '\0',
     OPTION_TYPE_CALLBACK,
     offsetof(options_t, cookies_from_browser),
     NULL,
     0,
     "yt-dlp option (man yt-dlp). Browser for reading cookies from (chrome, firefox, edge, safari, brave, opera, "
     "vivaldi, whale). Use without argument to default to chrome.",
     "MEDIA",
     NULL,
     false,
     "ASCII_CHAT_COOKIES_FROM_BROWSER",
     NULL,
     parse_cookies_from_browser,
     false,
     true,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {.enum_values = g_cookies_values, .enum_descriptions = g_cookies_descs, .input_type = OPTION_INPUT_ENUM}},
    {"no-cookies-from-browser",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, no_cookies_from_browser),
     false,
     sizeof(bool),
     "yt-dlp option (man yt-dlp). Explicitly disable reading cookies from browser",
     "MEDIA",
     NULL,
     false,
     "ASCII_CHAT_NO_COOKIES_FROM_BROWSER",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {0}},

    REGISTRY_TERMINATOR()};

// ============================================================================
// AUDIO CATEGORY - Audio processing options
// ============================================================================
static const registry_entry_t g_audio_entries[] = {
    // AUDIO GROUP (client, discovery)
    {"audio",
     'A',
     OPTION_TYPE_BOOL,
     offsetof(options_t, audio_enabled),
     &default_audio_enabled_value,
     sizeof(bool),
     "Enable audio streaming",
     "AUDIO",
     NULL,
     false,
     "ASCII_CHAT_AUDIO",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY,
     {0}},
    {"microphone-index",
     '\0',
     OPTION_TYPE_INT,
     offsetof(options_t, microphone_index),
     &default_microphone_index_value,
     sizeof(int),
     "Microphone device index for audio input",
     "AUDIO",
     NULL,
     false,
     "ASCII_CHAT_MICROPHONE_INDEX",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY,
     {.numeric_range = {-1, 10, 1}, .examples = g_mic_examples, .input_type = OPTION_INPUT_NUMERIC}},
    {"speakers-index",
     '\0',
     OPTION_TYPE_INT,
     offsetof(options_t, speakers_index),
     &default_speakers_index_value,
     sizeof(int),
     "Speakers device index to use for audio output",
     "AUDIO",
     NULL,
     false,
     "ASCII_CHAT_SPEAKERS_INDEX",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY,
     {.numeric_range = {-1, 10, 1}, .examples = g_speakers_examples, .input_type = OPTION_INPUT_NUMERIC}},
    {"microphone-volume",
     '\0',
     OPTION_TYPE_CALLBACK,
     offsetof(options_t, microphone_sensitivity),
     &default_microphone_sensitivity_value,
     sizeof(float),
     "Microphone volume multiplier (0.0-1.0)",
     "AUDIO",
     NULL,
     false,
     "ASCII_CHAT_MICROPHONE_VOLUME",
     NULL,
     parse_volume,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {.numeric_range = {0, 1, 0}}},
    {"ivolume",
     '\0',
     OPTION_TYPE_CALLBACK,
     offsetof(options_t, microphone_sensitivity),
     &default_microphone_sensitivity_value,
     sizeof(float),
     "Alias for --microphone-volume.",
     "AUDIO",
     NULL,
     false,
     "ASCII_CHAT_IVOLUME",
     NULL,
     parse_volume,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {.numeric_range = {0, 1, 0}}},
    {"speakers-volume",
     '\0',
     OPTION_TYPE_CALLBACK,
     offsetof(options_t, speakers_volume),
     &default_speakers_volume_value,
     sizeof(float),
     "Speakers volume multiplier (0.0-1.0)",
     "AUDIO",
     NULL,
     false,
     "ASCII_CHAT_SPEAKERS_VOLUME",
     NULL,
     parse_volume,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {.numeric_range = {0, 1, 0}}},
    {"volume",
     '\0',
     OPTION_TYPE_CALLBACK,
     offsetof(options_t, speakers_volume),
     &default_speakers_volume_value,
     sizeof(float),
     "Alias for --speakers-volume.",
     "AUDIO",
     NULL,
     false,
     "ASCII_CHAT_VOLUME",
     NULL,
     parse_volume,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {.numeric_range = {0, 1, 0}}},
    {"audio-source",
     '\0',
     OPTION_TYPE_CALLBACK,
     offsetof(options_t, audio_source),
     &default_audio_source_value,
     sizeof(audio_source_t),
     "Select which audio sources to use: auto (smart), mic, media, or both",
     "AUDIO",
     NULL,
     false,
     "ASCII_CHAT_AUDIO_SOURCE",
     NULL,
     parse_audio_source,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {.enum_values = g_audio_source_values,
      .enum_count = 4,
      .enum_descriptions = g_audio_source_descs,
      .enum_integer_values = g_audio_source_integers,
      .input_type = OPTION_INPUT_ENUM}},
#ifdef DEBUG
    {"audio-analysis",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, audio_analysis_enabled),
     &default_audio_analysis_value,
     sizeof(bool),
     "Enable audio analysis (debug)",
     "AUDIO",
     NULL,
     false,
     "ASCII_CHAT_AUDIO_ANALYSIS",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY,
     {0}},
#endif
    {"no-audio-playback",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, audio_no_playback),
     &default_no_audio_playback_value,
     sizeof(bool),
     "Disable speakers output",
     "AUDIO",
     NULL,
     false,
     "ASCII_CHAT_NO_AUDIO_PLAYBACK",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY,
     {0}},
    {"encode-audio",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, encode_audio),
     &default_encode_audio_value,
     sizeof(bool),
     "Enable Opus audio encoding",
     "AUDIO",
     NULL,
     false,
     "ASCII_CHAT_ENCODE_AUDIO",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY,
     {0}},
    {"no-encode-audio",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, encode_audio),
     &default_no_encode_audio_value,
     sizeof(bool),
     "Disable Opus audio encoding",
     "AUDIO",
     NULL,
     false,
     "ASCII_CHAT_NO_ENCODE_AUDIO",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY,
     {0}},
    {"no-audio-mixer",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, no_audio_mixer),
     &default_no_audio_mixer_value,
     sizeof(bool),
     "Use simple audio mixing without ducking or compression (debug mode only)",
     "AUDIO",
     NULL,
     false,
     "ASCII_CHAT_NO_AUDIO_MIXER",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_SERVER,
     {0}},

    REGISTRY_TERMINATOR()};

// ============================================================================
// DATABASE CATEGORY - Discovery service database options
// ============================================================================
static const registry_entry_t g_database_entries[] = {
    // ACDS Server Specific Options
    {"database",
     '\0',
     OPTION_TYPE_STRING,
     offsetof(options_t, discovery_database_path),
     "",
     0,
     "Path to SQLite database for discovery session storage",
     "DATABASE",
     NULL,
     false,
     "ASCII_CHAT_DATABASE",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_DISCOVERY_SVC,
     {0}},
    {"discovery-server",
     '\0',
     OPTION_TYPE_STRING,
     offsetof(options_t, discovery_server),
     OPT_ENDPOINT_DISCOVERY_SERVICE,
     0,
     "Discovery service endpoint (IP address or hostname).",
     "NETWORK",
     NULL,
     false,
     "ASCII_CHAT_DISCOVERY_SERVER",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY,
     {0}},
    {"discovery-port",
     '\0',
     OPTION_TYPE_INT,
     offsetof(options_t, discovery_port),
     &default_discovery_port_value,
     sizeof(int),
     "Discovery service port (1-65535)",
     "NETWORK",
     NULL,
     false,
     "ASCII_CHAT_DISCOVERY_PORT",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY,
     {0}},
    {"discovery-expose-ip",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, discovery_expose_ip),
     &default_discovery_expose_ip_value,
     sizeof(bool),
     "Allow public IP disclosure in discovery sessions (requires confirmation)",
     "NETWORK",
     NULL,
     false,
     "ASCII_CHAT_DISCOVERY_EXPOSE_IP",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY,
     {0}},
    {"require-server-identity",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, require_server_identity),
     false,
     sizeof(bool),
     "Require servers to provide signed Ed25519 identity",
     "SECURITY",
     NULL,
     false,
     "ASCII_CHAT_REQUIRE_SERVER_IDENTITY",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_DISCOVERY_SVC,
     {0}},
    {"require-client-identity",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, require_client_identity),
     false,
     sizeof(bool),
     "Require clients to provide signed Ed25519 identity",
     "SECURITY",
     NULL,
     false,
     "ASCII_CHAT_REQUIRE_CLIENT_IDENTITY",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_DISCOVERY_SVC,
     {0}},

    // Generic placeholder to mark end of array
    REGISTRY_TERMINATOR()};

// ============================================================================
// Master Registry - Composition of all category arrays
// ============================================================================
// Array of pointers to each category's entries for organized access
static struct {
  const registry_entry_t *entries;
  size_t count;
  const char *name;
} g_category_builders[] = {
    {g_logging_entries, sizeof(g_logging_entries) / sizeof(g_logging_entries[0]), "LOGGING"},
    {g_configuration_entries, sizeof(g_configuration_entries) / sizeof(g_configuration_entries[0]), "CONFIGURATION"},
    {g_shell_entries, sizeof(g_shell_entries) / sizeof(g_shell_entries[0]), "SHELL"},
    {g_terminal_entries, sizeof(g_terminal_entries) / sizeof(g_terminal_entries[0]), "TERMINAL"},
    {g_webcam_entries, sizeof(g_webcam_entries) / sizeof(g_webcam_entries[0]), "WEBCAM"},
    {g_audio_entries, sizeof(g_audio_entries) / sizeof(g_audio_entries[0]), "AUDIO"},
    {g_display_entries, sizeof(g_display_entries) / sizeof(g_display_entries[0]), "DISPLAY"},
    {g_network_entries, sizeof(g_network_entries) / sizeof(g_network_entries[0]), "NETWORK"},
    {g_security_entries, sizeof(g_security_entries) / sizeof(g_security_entries[0]), "SECURITY"},
    {g_media_entries, sizeof(g_media_entries) / sizeof(g_media_entries[0]), "MEDIA"},
    {g_database_entries, sizeof(g_database_entries) / sizeof(g_database_entries[0]), "DATABASE"},
    {NULL, 0, NULL}};

// Unified view of all registry entries (for backward compatibility)
static registry_entry_t g_options_registry[2048];

static size_t g_registry_size = 0;
static bool g_metadata_populated = false;

// Metadata is now initialized compile-time in registry entries

/**
 * @brief Initialize registry from category builders
 * Populates g_options_registry by concatenating all category arrays
 */
static void registry_init_from_builders(void) {
  static bool initialized = false;
  if (initialized) {
    return;
  }

  size_t offset = 0;
  for (int i = 0; g_category_builders[i].entries != NULL; i++) {
    size_t count = g_category_builders[i].count;
    // Copy category entries, excluding the null terminator
    size_t copy_count = count - 1; // Exclude null terminator from each category
    if (offset + copy_count <= 2048) {
      memcpy(&g_options_registry[offset], g_category_builders[i].entries, copy_count * sizeof(registry_entry_t));
      offset += copy_count;
    }
  }

  // Add final null terminator
  if (offset < 2048) {
    g_options_registry[offset] = (registry_entry_t){.long_name = NULL,
                                                    .short_name = '\0',
                                                    .type = OPTION_TYPE_BOOL,
                                                    .offset = 0,
                                                    .default_value = NULL,
                                                    .default_value_size = 0,
                                                    .help_text = NULL,
                                                    .group = NULL,
                                                    .required = false,
                                                    .env_var_name = NULL,
                                                    .validate_fn = NULL,
                                                    .parse_fn = NULL,
                                                    .owns_memory = false,
                                                    .optional_arg = false,
                                                    .mode_bitmask = OPTION_MODE_NONE,
                                                    .metadata = {0}};
  }

  initialized = true;
}

/**
 * @brief Validate that no short or long options appear more than once in the registry
 * @return Result of SET_ERRNO if duplicates found, ASCIICHAT_OK if valid
 */
static asciichat_error_t registry_validate_unique_options(void) {
  // Check for duplicate long options
  for (size_t i = 0; g_options_registry[i].long_name != NULL; i++) {
    const char *long_name = g_options_registry[i].long_name;
    if (!long_name || long_name[0] == '\0') {
      continue; // Skip empty long names
    }

    for (size_t j = i + 1; g_options_registry[j].long_name != NULL; j++) {
      if (strcmp(g_options_registry[j].long_name, long_name) == 0) {
        return SET_ERRNO(ERROR_CONFIG, "Duplicate long option '--%s' at registry indices %zu and %zu", long_name, i, j);
      }
    }
  }

  // Check for duplicate short options (skip if '\0')
  for (size_t i = 0; g_options_registry[i].long_name != NULL; i++) {
    char short_name = g_options_registry[i].short_name;
    if (short_name == '\0') {
      continue; // Skip if no short option
    }

    for (size_t j = i + 1; g_options_registry[j].long_name != NULL; j++) {
      if (g_options_registry[j].short_name == short_name) {
        return SET_ERRNO(ERROR_CONFIG,
                         "Duplicate short option '-%c' for '--%s' and '--%s' at registry indices %zu and %zu",
                         short_name, g_options_registry[i].long_name, g_options_registry[j].long_name, i, j);
      }
    }
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Initialize registry size and metadata
 */
static void registry_init_size(void) {
  registry_init_from_builders();

  if (g_registry_size == 0) {
    for (size_t i = 0; g_options_registry[i].long_name != NULL; i++) {
      g_registry_size++;
    }
    // Validate that all options have unique short and long names
    registry_validate_unique_options();
    // DEPRECATED: Metadata is now initialized compile-time in registry entries
    // registry_populate_metadata_for_critical_options();
    g_metadata_populated = true;
  }
}

asciichat_error_t options_registry_add_all_to_builder(options_builder_t *builder) {
  if (!builder) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Builder is NULL");
  }

  registry_init_size();

  for (size_t i = 0; i < g_registry_size; i++) {
    const registry_entry_t *entry = &g_options_registry[i];
    if (!entry->long_name) {
      continue;
    }
    // Silent - no debug logging needed

    switch (entry->type) {
    case OPTION_TYPE_STRING:
      options_builder_add_string(builder, entry->long_name, entry->short_name, entry->offset,
                                 entry->default_value ? (const char *)entry->default_value : "", entry->help_text,
                                 entry->group, entry->required, entry->env_var_name, entry->validate_fn);
      break;
    case OPTION_TYPE_INT:
      options_builder_add_int(builder, entry->long_name, entry->short_name, entry->offset,
                              entry->default_value ? *(const int *)entry->default_value : 0, entry->help_text,
                              entry->group, entry->required, entry->env_var_name, entry->validate_fn);
      break;
    case OPTION_TYPE_BOOL:
      options_builder_add_bool(builder, entry->long_name, entry->short_name, entry->offset,
                               entry->default_value ? *(const bool *)entry->default_value : false, entry->help_text,
                               entry->group, entry->required, entry->env_var_name);
      break;
    case OPTION_TYPE_DOUBLE:
      options_builder_add_double(builder, entry->long_name, entry->short_name, entry->offset,
                                 entry->default_value ? *(const double *)entry->default_value : 0.0, entry->help_text,
                                 entry->group, entry->required, entry->env_var_name, entry->validate_fn);
      break;
    case OPTION_TYPE_CALLBACK:
      // Always use metadata-aware function to preserve enum/metadata information
      options_builder_add_callback_with_metadata(builder, entry->long_name, entry->short_name, entry->offset,
                                                 entry->default_value, entry->default_value_size, entry->parse_fn,
                                                 entry->help_text, entry->group, entry->required, entry->env_var_name,
                                                 entry->optional_arg, &entry->metadata);
      break;
    case OPTION_TYPE_ACTION:
      // Actions are now registered as options with help text
      // Look up the corresponding action function based on option name
      if (strcmp(entry->long_name, "list-webcams") == 0) {
        options_builder_add_action(builder, entry->long_name, entry->short_name, action_list_webcams, entry->help_text,
                                   entry->group);
      } else if (strcmp(entry->long_name, "list-microphones") == 0) {
        options_builder_add_action(builder, entry->long_name, entry->short_name, action_list_microphones,
                                   entry->help_text, entry->group);
      } else if (strcmp(entry->long_name, "list-speakers") == 0) {
        options_builder_add_action(builder, entry->long_name, entry->short_name, action_list_speakers, entry->help_text,
                                   entry->group);
      } else if (strcmp(entry->long_name, "show-capabilities") == 0) {
        options_builder_add_action(builder, entry->long_name, entry->short_name, action_show_capabilities,
                                   entry->help_text, entry->group);
      }
      break;
    }

    // Set mode bitmask on the last added descriptor
    options_builder_set_mode_bitmask(builder, entry->mode_bitmask);

    // Set custom arg_placeholder if defined
    if (entry->arg_placeholder) {
      options_builder_set_arg_placeholder(builder, entry->arg_placeholder);
    }
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Get a registry entry by long name
 * @note This is used internally for option lookup
 */
static const registry_entry_t *registry_find_entry_by_name(const char *long_name) {
  if (!long_name) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Long name is NULL");
    return NULL;
  }

  for (size_t i = 0; g_options_registry[i].long_name != NULL; i++) {
    if (strcmp(g_options_registry[i].long_name, long_name) == 0) {
      return &g_options_registry[i];
    }
  }
  return NULL;
}

/**
 * @brief Get a registry entry by short name
 * @note This is used internally for option lookup
 */
static const registry_entry_t *registry_find_entry_by_short(char short_name) {
  if (short_name == '\0') {
    return NULL;
  }

  for (size_t i = 0; g_options_registry[i].long_name != NULL; i++) {
    if (g_options_registry[i].short_name == short_name) {
      return &g_options_registry[i];
    }
  }
  return NULL;
}

/**
 * @brief Get raw access to registry for completions filtering
 *
 * Returns a pointer to the internal registry array. The array is NULL-terminated
 * (final entry has long_name == NULL). Used by completions generators.
 *
 * @return Pointer to registry array (read-only), or NULL on error
 */
const registry_entry_t *options_registry_get_raw(void) {
  registry_init_size();
  return g_options_registry;
}

/**
 * @brief Get total number of registry entries
 *
 * @return Number of options in registry (not including NULL terminator)
 */
size_t options_registry_get_count(void) {
  registry_init_size();
  return g_registry_size;
}

const option_descriptor_t *options_registry_find_by_name(const char *long_name) {
  if (!long_name) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Long name is NULL");
    return NULL;
  }

  registry_init_size();

  const registry_entry_t *entry = registry_find_entry_by_name(long_name);
  if (!entry) {
    // Don't log error for binary-level options like "config" that aren't in registry
    if (strcmp(long_name, "config") != 0) {
      SET_ERRNO(ERROR_NOT_FOUND, "Option not found: %s", long_name);
    }
    return NULL;
  }

  /* Create descriptor from registry entry */
  static option_descriptor_t desc;
  desc.long_name = entry->long_name;
  desc.short_name = entry->short_name;
  desc.type = entry->type;
  desc.offset = entry->offset;
  desc.help_text = entry->help_text;
  desc.group = entry->group;
  desc.hide_from_mode_help = false;
  desc.hide_from_binary_help = false;
  desc.default_value = entry->default_value;
  desc.required = entry->required;
  desc.env_var_name = entry->env_var_name;
  desc.validate = entry->validate_fn;
  desc.parse_fn = entry->parse_fn;
  desc.action_fn = NULL;
  desc.owns_memory = entry->owns_memory;
  desc.optional_arg = entry->optional_arg;
  desc.mode_bitmask = entry->mode_bitmask;

  return &desc;
}

const option_descriptor_t *options_registry_find_by_short(char short_name) {
  if (short_name == '\0') {
    SET_ERRNO(ERROR_INVALID_PARAM, "Short name is empty");
    return NULL;
  }

  registry_init_size();

  const registry_entry_t *entry = registry_find_entry_by_short(short_name);
  if (!entry) {
    SET_ERRNO(ERROR_NOT_FOUND, "Option with short name '%c' not found", short_name);
    return NULL;
  }

  /* Create descriptor from registry entry */
  static option_descriptor_t desc;
  desc.long_name = entry->long_name;
  desc.short_name = entry->short_name;
  desc.type = entry->type;
  desc.offset = entry->offset;
  desc.help_text = entry->help_text;
  desc.group = entry->group;
  desc.hide_from_mode_help = false;
  desc.hide_from_binary_help = false;
  desc.default_value = entry->default_value;
  desc.required = entry->required;
  desc.env_var_name = entry->env_var_name;
  desc.validate = entry->validate_fn;
  desc.parse_fn = entry->parse_fn;
  desc.action_fn = NULL;
  desc.owns_memory = entry->owns_memory;
  desc.optional_arg = entry->optional_arg;
  desc.mode_bitmask = entry->mode_bitmask;

  return &desc;
}

/**
 * @brief Convert registry entry to option descriptor
 */
static option_descriptor_t registry_entry_to_descriptor(const registry_entry_t *entry) {
  option_descriptor_t desc = {0};
  if (entry) {
    desc.long_name = entry->long_name;
    desc.short_name = entry->short_name;
    desc.type = entry->type;
    desc.offset = entry->offset;
    desc.help_text = entry->help_text;
    desc.group = entry->group;
    desc.arg_placeholder = entry->arg_placeholder;
    desc.hide_from_mode_help = false;
    // Hide discovery service options from binary-level help (they're for discovery-service mode only)
    desc.hide_from_binary_help = (entry->mode_bitmask == OPTION_MODE_DISCOVERY_SVC);
    desc.default_value = entry->default_value;
    desc.required = entry->required;
    desc.env_var_name = entry->env_var_name;
    desc.validate = entry->validate_fn;
    desc.parse_fn = entry->parse_fn;
    desc.action_fn = NULL;
    desc.owns_memory = entry->owns_memory;
    desc.optional_arg = entry->optional_arg;
    desc.mode_bitmask = entry->mode_bitmask;
    desc.metadata = entry->metadata;
  }
  return desc;
}

const option_descriptor_t *options_registry_get_for_mode(asciichat_mode_t mode, size_t *num_options) {
  if (!num_options) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Number of options is NULL");
    return NULL;
  }

  registry_init_size();

  /* Convert mode to bitmask */
  option_mode_bitmask_t mode_bitmask = 0;
  switch (mode) {
  case MODE_SERVER:
    mode_bitmask = OPTION_MODE_SERVER;
    break;
  case MODE_CLIENT:
    mode_bitmask = OPTION_MODE_CLIENT;
    break;
  case MODE_MIRROR:
    mode_bitmask = OPTION_MODE_MIRROR;
    break;
  case MODE_DISCOVERY_SERVICE:
    mode_bitmask = OPTION_MODE_DISCOVERY_SVC;
    break;
  case MODE_DISCOVERY:
    mode_bitmask = OPTION_MODE_DISCOVERY;
    break;
  default:
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid mode: %d", mode);
    *num_options = 0;
    return NULL;
  }

  /* Count matching options */
  size_t count = 0;
  for (size_t i = 0; i < g_registry_size; i++) {
    if (g_options_registry[i].mode_bitmask & mode_bitmask) {
      count++;
    }
  }

  if (count == 0) {
    *num_options = 0;
    return NULL;
  }

  /* Allocate array for matching options */
  option_descriptor_t *filtered = SAFE_MALLOC(count * sizeof(option_descriptor_t), option_descriptor_t *);
  if (!filtered) {
    SET_ERRNO(ERROR_INVALID_STATE, "Failed to allocate filtered options array");
    *num_options = 0;
    return NULL;
  }

  /* Copy matching options */
  size_t idx = 0;
  for (size_t i = 0; i < g_registry_size; i++) {
    if (g_options_registry[i].mode_bitmask & mode_bitmask) {
      filtered[idx++] = registry_entry_to_descriptor(&g_options_registry[i]);
    }
  }

  *num_options = count;
  return filtered;
}

const option_descriptor_t *options_registry_get_binary_options(size_t *num_options) {
  if (!num_options) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Number of options is NULL");
    return NULL;
  }

  registry_init_size();

  /* Count binary-level options */
  size_t count = 0;
  for (size_t i = 0; i < g_registry_size; i++) {
    if (g_options_registry[i].mode_bitmask & OPTION_MODE_BINARY) {
      count++;
    }
  }

  if (count == 0) {
    *num_options = 0;
    return NULL;
  }

  /* Allocate array for binary options */
  option_descriptor_t *binary_opts = SAFE_MALLOC(count * sizeof(option_descriptor_t), option_descriptor_t *);
  if (!binary_opts) {
    SET_ERRNO(ERROR_INVALID_STATE, "Failed to allocate binary options array");
    *num_options = 0;
    return NULL;
  }

  /* Copy binary options */
  size_t idx = 0;
  for (size_t i = 0; i < g_registry_size; i++) {
    if (g_options_registry[i].mode_bitmask & OPTION_MODE_BINARY) {
      binary_opts[idx++] = registry_entry_to_descriptor(&g_options_registry[i]);
    }
  }

  *num_options = count;
  return binary_opts;
}

/**
 * @brief Check if an option applies to the given mode for display purposes
 *
 * This implements the same filtering logic as the help system's option_applies_to_mode().
 * Used by options_registry_get_for_display() to ensure completions match help output.
 *
 * @param entry Registry entry to check
 * @param mode Mode to check (use MODE_DISCOVERY for binary help)
 * @param for_binary_help If true, show all options for any mode; if false, filter by mode
 * @return true if option should be displayed for this mode
 */
static bool registry_entry_applies_to_mode(const registry_entry_t *entry, asciichat_mode_t mode, bool for_binary_help) {
  if (!entry) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Entry is NULL");
    return false;
  }

  // Hardcoded list of options to hide from binary help (matches builder.c line 752)
  // These are options that have hide_from_binary_help=true set in builder.c
  const char *hidden_from_binary[] = {NULL};

  // When for_binary_help is true (i.e., for 'ascii-chat --help'),
  // we want to show all options that apply to any mode, plus binary-level options.
  if (for_binary_help) {
    // Check if this option is explicitly hidden from binary help
    for (int i = 0; hidden_from_binary[i] != NULL; i++) {
      if (strcmp(entry->long_name, hidden_from_binary[i]) == 0) {
        return false; // Hidden from binary help
      }
    }

    // An option applies if its mode_bitmask has any bit set for any valid mode.
    // OPTION_MODE_ALL is a bitmask of all modes (including OPTION_MODE_BINARY).
    return (entry->mode_bitmask & OPTION_MODE_ALL) != 0;
  }

  // For mode-specific help, show only options for that mode.
  // Do not show binary options here unless it also specifically applies to the mode.
  if (mode < 0 || mode > MODE_DISCOVERY) {
    return false;
  }
  option_mode_bitmask_t mode_bit = (1 << mode);

  // Check if it's a binary option. If so, only show if it also explicitly applies to this mode.
  if ((entry->mode_bitmask & OPTION_MODE_BINARY) && !(entry->mode_bitmask & mode_bit)) {
    return false; // Binary options not shown in mode-specific help unless also mode-specific
  }

  return (entry->mode_bitmask & mode_bit) != 0;
}

const option_descriptor_t *options_registry_get_for_display(asciichat_mode_t mode, bool for_binary_help,
                                                            size_t *num_options) {
  if (!num_options) {
    SET_ERRNO(ERROR_INVALID_PARAM, "num_options is NULL");
    return NULL;
  }

  registry_init_size();

  // Count matching options
  size_t count = 0;
  for (size_t i = 0; i < g_registry_size; i++) {
    if (registry_entry_applies_to_mode(&g_options_registry[i], mode, for_binary_help)) {
      count++;
    }
  }

  if (count == 0) {
    *num_options = 0;
    return NULL;
  }

  // Allocate array
  option_descriptor_t *descriptors = SAFE_MALLOC(count * sizeof(option_descriptor_t), option_descriptor_t *);
  if (!descriptors) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate descriptors array");
    *num_options = 0;
    return NULL;
  }

  // Copy matching options
  size_t idx = 0;
  for (size_t i = 0; i < g_registry_size; i++) {
    if (registry_entry_applies_to_mode(&g_options_registry[i], mode, for_binary_help)) {
      descriptors[idx++] = registry_entry_to_descriptor(&g_options_registry[i]);
    }
  }

  *num_options = count;
  return descriptors;
}

// ============================================================================
// Completion Metadata Access (Phase 3 Implementation)
// ============================================================================

const option_metadata_t *options_registry_get_metadata(const char *long_name) {
  if (!long_name) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Long name is NULL");
    return NULL;
  }

  // Look up option in registry and return its metadata
  registry_init_size();
  for (size_t i = 0; i < g_registry_size; i++) {
    const registry_entry_t *entry = &g_options_registry[i];
    if (entry->long_name && strcmp(entry->long_name, long_name) == 0) {
      // Return the metadata from the registry entry
      return &entry->metadata;
    }
  }

  // If not found, return empty metadata
  static option_metadata_t empty_metadata = {0};
  return &empty_metadata;
}

const char **options_registry_get_enum_values(const char *option_name, const char ***descriptions, size_t *count) {
  if (!option_name || !count) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Option name is NULL or count is NULL");
    if (count)
      *count = 0;
    return NULL;
  }

  const option_metadata_t *meta = options_registry_get_metadata(option_name);
  if (!meta || meta->input_type != OPTION_INPUT_ENUM || !meta->enum_values || meta->enum_values[0] == NULL) {
    SET_ERRNO(ERROR_NOT_FOUND, "Option '%s' not found", option_name);
    *count = 0;
    if (descriptions)
      *descriptions = NULL;
    return NULL;
  }

  *count = meta->enum_count;
  if (descriptions) {
    *descriptions = meta->enum_descriptions;
  }
  return meta->enum_values;
}

bool options_registry_get_numeric_range(const char *option_name, int *min_out, int *max_out, int *step_out) {
  if (!option_name || !min_out || !max_out || !step_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Option name is NULL or min_out, max_out, or step_out is NULL");
    return false;
  }

  const option_metadata_t *meta = options_registry_get_metadata(option_name);
  if (!meta || meta->input_type != OPTION_INPUT_NUMERIC) {
    *min_out = 0;
    *max_out = 0;
    *step_out = 0;
    return false;
  }

  *min_out = meta->numeric_range.min;
  *max_out = meta->numeric_range.max;
  *step_out = meta->numeric_range.step;
  return true;
}

const char **options_registry_get_examples(const char *option_name, size_t *count) {
  if (!option_name || !count) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Option name is NULL or count is NULL");
    if (count)
      *count = 0;
    return NULL;
  }

  const option_metadata_t *meta = options_registry_get_metadata(option_name);
  if (!meta || !meta->examples || meta->examples[0] == NULL) {
    *count = 0;
    return NULL;
  }

  // Count examples by finding NULL terminator
  size_t example_count = 0;
  for (size_t i = 0; meta->examples[i] != NULL; i++) {
    example_count++;
  }

  *count = example_count;
  return meta->examples;
}

option_input_type_t options_registry_get_input_type(const char *option_name) {
  if (!option_name) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Option name is NULL");
    return OPTION_INPUT_NONE;
  }

  const option_metadata_t *meta = options_registry_get_metadata(option_name);
  if (!meta) {
    SET_ERRNO(ERROR_NOT_FOUND, "Option '%s' not found", option_name);
    return OPTION_INPUT_NONE;
  }

  return meta->input_type;
}

// ============================================================================
// Metadata Initialization for Critical Options
// ============================================================================
