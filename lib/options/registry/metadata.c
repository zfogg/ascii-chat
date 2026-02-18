/**
 * @file metadata.c
 * @brief Static metadata arrays for option completions and validation
 * @ingroup options
 *
 * Centralizes all enum values, descriptions, numeric ranges, and example arrays
 * used by option completions and help text generation.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <ascii-chat/log/logging.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/video/palette.h>
#include <ascii-chat/options/options.h>

// ============================================================================
// Log Level Metadata
// ============================================================================

const char *g_log_level_values[] = {"dev", "debug", "info", "warn", "error", "fatal", NULL};
const int g_log_level_integers[] = {LOG_DEV, LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL};
const char *g_log_level_descs[] = {"Development (most verbose, includes function traces)",
                                   "Debug (includes internal state tracking)",
                                   "Informational (key lifecycle events)",
                                   "Warnings (unusual conditions)",
                                   "Errors only",
                                   "Fatal errors only",
                                   NULL};

// ============================================================================
// Color Setting Metadata (--color flag values)
// ============================================================================

const char *g_color_setting_values[] = {"auto", "true", "false", NULL};
const int g_color_setting_integers[] = {COLOR_SETTING_AUTO, COLOR_SETTING_TRUE, COLOR_SETTING_FALSE};
const char *g_color_setting_descs[] = {"Smart detection (colors if TTY and not piping/CLAUDECODE)",
                                       "Force colors ON (override TTY/pipe/CLAUDECODE)",
                                       "Force colors OFF (disable all colors)", NULL};

// ============================================================================
// UTF-8 Setting Metadata (--utf8 flag values)
// ============================================================================

const char *g_utf8_setting_values[] = {"auto", "true", "false", NULL};
const int g_utf8_setting_integers[] = {UTF8_SETTING_AUTO, UTF8_SETTING_TRUE, UTF8_SETTING_FALSE};
const char *g_utf8_setting_descs[] = {"Auto-detect UTF-8 support from terminal capabilities",
                                      "Force UTF-8 ON (always use UTF-8 regardless of terminal)",
                                      "Force UTF-8 OFF (disable UTF-8 support)", NULL};

// ============================================================================
// Color Mode Metadata
// ============================================================================

const char *g_color_mode_values[] = {"auto", "none", "16", "256", "truecolor", NULL};
const int g_color_mode_integers[] = {TERM_COLOR_AUTO, TERM_COLOR_NONE, TERM_COLOR_16, TERM_COLOR_256,
                                     TERM_COLOR_TRUECOLOR};
const char *g_color_mode_descs[] = {"Auto-detect from terminal",
                                    "Monochrome only",
                                    "16 colors (ANSI)",
                                    "256 colors (xterm)",
                                    "24-bit truecolor (modern terminals)",
                                    NULL};

// ============================================================================
// Color Filter Metadata
// ============================================================================

const char *g_color_filter_values[] = {"none", "black", "white", "green", "magenta", "fuchsia", "orange",
                                       "teal", "cyan",  "pink",  "red",   "yellow",  NULL};
const int g_color_filter_integers[] = {COLOR_FILTER_NONE,   COLOR_FILTER_BLACK,   COLOR_FILTER_WHITE,
                                       COLOR_FILTER_GREEN,  COLOR_FILTER_MAGENTA, COLOR_FILTER_FUCHSIA,
                                       COLOR_FILTER_ORANGE, COLOR_FILTER_TEAL,    COLOR_FILTER_CYAN,
                                       COLOR_FILTER_PINK,   COLOR_FILTER_RED,     COLOR_FILTER_YELLOW};
const char *g_color_filter_descs[] = {"No filtering (default)",
                                      "Dark content on white background",
                                      "White content on black background",
                                      "Green color tint (#00FF41)",
                                      "Magenta color tint (#FF00FF)",
                                      "Fuchsia color tint (#FF00AA)",
                                      "Orange color tint (#FF8800)",
                                      "Teal color tint (#00DDDD)",
                                      "Cyan color tint (#00FFFF)",
                                      "Pink color tint (#FFB6C1)",
                                      "Red color tint (#FF3333)",
                                      "Yellow color tint (#FFEB99)",
                                      NULL};

// ============================================================================
// Palette Metadata
// ============================================================================

const char *g_palette_values[] = {"standard", "blocks", "digital", "minimal", "cool", "custom", NULL};
const int g_palette_integers[] = {PALETTE_STANDARD, PALETTE_BLOCKS, PALETTE_DIGITAL,
                                  PALETTE_MINIMAL,  PALETTE_COOL,   PALETTE_CUSTOM};
const char *g_palette_descs[] = {"Standard ASCII palette",
                                 "Block characters (full/half/quarter blocks)",
                                 "Digital/computer style",
                                 "Minimal palette (light aesthetic)",
                                 "Cool/modern style",
                                 "Custom user-defined characters",
                                 NULL};

// ============================================================================
// Render Mode Metadata
// ============================================================================

const char *g_render_values[] = {"foreground", "fg", "background", "bg", "half-block", NULL};
const int g_render_integers[] = {RENDER_MODE_FOREGROUND, RENDER_MODE_FOREGROUND, // fg is alias for foreground
                                 RENDER_MODE_BACKGROUND, RENDER_MODE_BACKGROUND, // bg is alias for background
                                 RENDER_MODE_HALF_BLOCK};
const char *g_render_descs[] = {"Render using foreground characters only",
                                "Render using foreground characters only (alias)",
                                "Render using background colors only",
                                "Render using background colors only (alias)",
                                "Use half-block characters for 2x vertical resolution",
                                NULL};

// ============================================================================
// Audio Source Metadata
// ============================================================================

const char *g_audio_source_values[] = {"auto", "mic", "media", "both", NULL};
const int g_audio_source_integers[] = {AUDIO_SOURCE_AUTO, AUDIO_SOURCE_MIC, AUDIO_SOURCE_MEDIA, AUDIO_SOURCE_BOTH};
const char *g_audio_source_descs[] = {"Smart selection (media-only when playing files, mic-only otherwise)",
                                      "Microphone only (no media audio)", "Media audio only (no microphone)",
                                      "Both microphone and media audio simultaneously", NULL};

// ============================================================================
// Log Format Output Type Metadata (--log-format flag values)
// ============================================================================

const char *g_log_format_output_values[] = {"text", "json", NULL};
const int g_log_format_output_integers[] = {LOG_OUTPUT_TEXT, LOG_OUTPUT_JSON};
const char *g_log_format_output_descs[] = {"Human-readable text format (default)",
                                           "Machine-readable JSON (NDJSON) format", NULL};

// ============================================================================
// Example Arrays (null-terminated)
// ============================================================================

const char *g_compression_examples[] = {"1", "3", "9", NULL};
const char *g_fps_examples[] = {"30", "60", "144", NULL};
const char *g_width_examples[] = {"80", "120", "160", NULL};
const char *g_height_examples[] = {"24", "40", "60", NULL};
const char *g_maxclients_examples[] = {"2", "4", "8", NULL};
const char *g_reconnect_examples[] = {"0", "5", "10", NULL};
const char *g_webcam_examples[] = {"0", "1", "2", NULL};
const char *g_mic_examples[] = {"-1", "0", "1", NULL};
const char *g_speakers_examples[] = {"-1", "0", "1", NULL};
const char *g_seek_examples[] = {"0", "60", "3:45", NULL};
