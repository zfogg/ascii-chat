/**
 * @file video/render/file/renderer.h
 * @ingroup video
 * @brief Terminal-to-pixel renderer and render-file output
 *
 * Platform-specific renderers (Linux: FreeType+ghostty_vt, macOS: ghostty+Metal)
 * implement the functions declared here. render_file_* is the public orchestrator
 * called from the session display layer.
 *
 * Supports theme-aware rendering that adapts colors based on the terminal's
 * background theme (dark or light) for optimal readability.
 */
#pragma once
#ifndef _WIN32

#include <stdint.h>
#include <stddef.h>
#include <ascii-chat/asciichat_errno.h>

// ── Platform renderer ─────────────────────────────────────────────────────

typedef struct terminal_renderer_s terminal_renderer_t;

/**
 * @brief Terminal rendering theme
 *
 * Determines color palette for pixel-based ANSI text rendering.
 * When TERM_RENDERER_THEME_AUTO is used, theme is detected from
 * terminal_has_dark_background() to adapt colors to user's theme.
 */
typedef enum {
  TERM_RENDERER_THEME_DARK = 0,  /**< Dark theme: use light colors for dark background */
  TERM_RENDERER_THEME_LIGHT = 1, /**< Light theme: use dark colors for light background */
  TERM_RENDERER_THEME_AUTO = 2,  /**< Auto-detect theme from terminal (default) */
} term_renderer_theme_t;

typedef struct {
  int cols;
  int rows;
  double font_size_pt; // point size, default 12.0 (fractional sizes supported)
  term_renderer_theme_t theme;
  char font_spec[512];      // resolved by platform_font_resolve()
  bool font_is_path;        // true = file path, false = family name (macOS)
  const uint8_t *font_data; // non-NULL → load from memory (Linux only)
  size_t font_data_size;
} term_renderer_config_t;

// Implemented in lib/platform/{linux,macos}/terminal.{c,m}
asciichat_error_t term_renderer_create(const term_renderer_config_t *cfg, terminal_renderer_t **out);
asciichat_error_t term_renderer_feed(terminal_renderer_t *r, const char *ansi_frame, size_t len);
const uint8_t *term_renderer_pixels(terminal_renderer_t *r);
int term_renderer_width_px(terminal_renderer_t *r);
int term_renderer_height_px(terminal_renderer_t *r);
int term_renderer_pitch(terminal_renderer_t *r);
void term_renderer_destroy(terminal_renderer_t *r);

// ── Render-file orchestrator ──────────────────────────────────────────────

typedef struct render_file_ctx_s render_file_ctx_t;

// Create: allocates renderer + FFmpeg encoder.
// cols/rows from terminal size; fps/theme from GET_OPTION().
asciichat_error_t render_file_create(const char *output_path, int cols, int rows, int fps, int theme,
                                     render_file_ctx_t **out);

// Feed one ANSI frame string — renders pixels then writes to encoder.
asciichat_error_t render_file_write_frame(render_file_ctx_t *ctx, const char *ansi_frame);

// Flush encoder and close file.  Always frees *ctx regardless of error.
asciichat_error_t render_file_destroy(render_file_ctx_t *ctx);

#endif // _WIN32
