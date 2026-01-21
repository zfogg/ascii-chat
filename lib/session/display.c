/**
 * @file session/display.c
 * @brief 🖥️ Unified terminal display implementation
 * @ingroup session
 *
 * Implements the session display abstraction layer for unified terminal
 * rendering across client, mirror, and discovery modes.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include "display.h"
#include "common.h"
#include "platform/terminal.h"
#include "platform/abstraction.h"
#include "video/palette.h"
#include "video/ascii.h"
#include "asciichat_errno.h"

#include <string.h>
#include <stdio.h>
#include <stdatomic.h>

/* ============================================================================
 * Session Display Context Structure
 * ============================================================================ */

/**
 * @brief Internal session display context structure
 *
 * Contains all state for terminal display including TTY info, capabilities,
 * palette, and rendering state.
 */
struct session_display_ctx {
  /** @brief TTY information (file descriptor, path, ownership) */
  tty_info_t tty_info;

  /** @brief True if we have a valid TTY for interactive output */
  bool has_tty;

  /** @brief Detected terminal capabilities */
  terminal_capabilities_t caps;

  /** @brief Palette character string for rendering */
  char palette_chars[256];

  /** @brief Number of characters in palette */
  size_t palette_len;

  /** @brief Luminance-to-character mapping table (256 entries) */
  char luminance_palette[256];

  /** @brief Configured palette type */
  palette_type_t palette_type;

  /** @brief Snapshot mode enabled */
  bool snapshot_mode;

  /** @brief First frame flag for logging control */
  atomic_bool first_frame;

  /** @brief Context is fully initialized */
  bool initialized;
};

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * @brief Perform complete terminal reset
 *
 * Executes comprehensive terminal reset sequence for clean display state.
 * Skips terminal control operations in snapshot mode.
 *
 * @param fd File descriptor for terminal operations
 * @param snapshot_mode Whether snapshot mode is enabled
 */
static void full_terminal_reset_internal(int fd, bool snapshot_mode) {
  if (!snapshot_mode && fd >= 0) {
    (void)terminal_reset(fd);
    (void)terminal_clear_screen();
    (void)terminal_cursor_home(fd);
    (void)terminal_clear_scrollback(fd);
    (void)terminal_hide_cursor(fd, true);
    (void)terminal_flush(fd);
  }
}

/**
 * @brief Write frame data to the terminal
 *
 * @param ctx Display context
 * @param frame_data Frame data to write
 * @param frame_len Length of frame data
 * @param use_tty Use TTY if true, stdout if false
 */
static void write_frame_internal(session_display_ctx_t *ctx, const char *frame_data, size_t frame_len, bool use_tty) {
  if (use_tty && ctx->tty_info.fd >= 0) {
    // Position cursor at top-left for TTY output
    if (!ctx->snapshot_mode) {
      (void)terminal_cursor_home(ctx->tty_info.fd);
    }
    (void)platform_write(ctx->tty_info.fd, frame_data, frame_len);
    (void)terminal_flush(ctx->tty_info.fd);
  } else {
    // stdout for pipes/redirection/testing
    if (!ctx->snapshot_mode) {
      (void)terminal_cursor_home(STDOUT_FILENO);
    }
    (void)platform_write(STDOUT_FILENO, frame_data, frame_len);
    (void)fflush(stdout);
  }
}

/* ============================================================================
 * Session Display Lifecycle Functions
 * ============================================================================ */

session_display_ctx_t *session_display_create(const session_display_config_t *config) {
  if (!config) {
    SET_ERRNO(ERROR_INVALID_PARAM, "session_display_create: NULL config");
    return NULL;
  }

  // Allocate context
  session_display_ctx_t *ctx = SAFE_CALLOC(1, sizeof(session_display_ctx_t), session_display_ctx_t *);

  // Store configuration
  ctx->snapshot_mode = config->snapshot_mode;
  ctx->palette_type = config->palette_type;
  atomic_init(&ctx->first_frame, true);

  // Get TTY info for direct terminal access
  ctx->tty_info = get_current_tty();

  // Determine if we have a valid TTY
  if (ctx->tty_info.fd >= 0) {
    ctx->has_tty = platform_isatty(ctx->tty_info.fd) != 0;
  }

  // Detect terminal capabilities
  ctx->caps = detect_terminal_capabilities();

  // Apply color mode override if specified
  if (config->color_mode != TERM_COLOR_AUTO) {
    ctx->caps.color_level = config->color_mode;
  }

  // Apply command-line overrides
  ctx->caps = apply_color_mode_override(ctx->caps);

  // Initialize palette
  int palette_result = initialize_client_palette(config->palette_type, config->custom_palette, ctx->palette_chars,
                                                 &ctx->palette_len, ctx->luminance_palette);
  if (palette_result != ASCIICHAT_OK) {
    log_warn("Failed to initialize palette, using default");
    // Fall back to standard palette
    (void)initialize_client_palette(PALETTE_STANDARD, NULL, ctx->palette_chars, &ctx->palette_len,
                                    ctx->luminance_palette);
  }

  // Initialize ASCII output system if we have a TTY
  if (ctx->has_tty && ctx->tty_info.fd >= 0) {
    ascii_write_init(ctx->tty_info.fd, false);
  }

  ctx->initialized = true;
  return ctx;
}

void session_display_destroy(session_display_ctx_t *ctx) {
  if (!ctx) {
    return;
  }

  // Cleanup ASCII rendering if we had a TTY
  if (ctx->has_tty && ctx->tty_info.fd >= 0) {
    ascii_write_destroy(ctx->tty_info.fd, true);
  }

  // Close the controlling terminal if we opened it
  if (ctx->tty_info.owns_fd && ctx->tty_info.fd >= 0) {
    (void)platform_close(ctx->tty_info.fd);
    ctx->tty_info.fd = -1;
    ctx->tty_info.owns_fd = false;
  }

  ctx->initialized = false;
  SAFE_FREE(ctx);
}

/* ============================================================================
 * Session Display Query Functions
 * ============================================================================ */

bool session_display_has_tty(session_display_ctx_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return false;
  }
  return ctx->has_tty;
}

const terminal_capabilities_t *session_display_get_caps(session_display_ctx_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return NULL;
  }
  return &ctx->caps;
}

const char *session_display_get_palette_chars(session_display_ctx_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return NULL;
  }
  return ctx->palette_chars;
}

size_t session_display_get_palette_len(session_display_ctx_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return 0;
  }
  return ctx->palette_len;
}

const char *session_display_get_luminance_palette(session_display_ctx_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return NULL;
  }
  return ctx->luminance_palette;
}

int session_display_get_tty_fd(session_display_ctx_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return -1;
  }
  return ctx->tty_info.fd;
}

/* ============================================================================
 * Session Display Rendering Functions
 * ============================================================================ */

void session_display_render_frame(session_display_ctx_t *ctx, const char *frame_data, bool is_final) {
  if (!ctx || !ctx->initialized || !frame_data) {
    return;
  }

  // Calculate frame length
  size_t frame_len = strnlen(frame_data, 1024 * 1024); // Max 1MB frame
  if (frame_len == 0) {
    return;
  }

  // Handle first frame - disable terminal logging to prevent console corruption
  if (atomic_load(&ctx->first_frame)) {
    log_set_terminal_output(false);
    atomic_store(&ctx->first_frame, false);

    // Perform initial terminal reset
    if (ctx->has_tty) {
      full_terminal_reset_internal(ctx->tty_info.fd, ctx->snapshot_mode);
    }
  }

  // Output routing logic:
  // - TTY mode: render every frame
  // - Redirect mode with snapshot: only render final frame
  if (ctx->has_tty || (!ctx->has_tty && ctx->snapshot_mode && is_final)) {
    if (is_final) {
      // Write final frame to TTY as well
      write_frame_internal(ctx, frame_data, frame_len, true);
    }

    // Main frame write
    write_frame_internal(ctx, frame_data, frame_len, ctx->has_tty && !is_final);

    if (ctx->snapshot_mode && is_final) {
      // Add newline at end of snapshot output
      (void)printf("\n");
    }
  }
}

void session_display_write_raw(session_display_ctx_t *ctx, const char *data, size_t len) {
  if (!ctx || !ctx->initialized || !data || len == 0) {
    return;
  }

  if (ctx->has_tty && ctx->tty_info.fd >= 0) {
    (void)platform_write(ctx->tty_info.fd, data, len);
    (void)terminal_flush(ctx->tty_info.fd);
  } else {
    (void)platform_write(STDOUT_FILENO, data, len);
    (void)fflush(stdout);
  }
}

void session_display_reset(session_display_ctx_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return;
  }

  if (ctx->tty_info.fd >= 0) {
    (void)terminal_reset(ctx->tty_info.fd);
    (void)terminal_hide_cursor(ctx->tty_info.fd, false); // Show cursor
    (void)terminal_flush(ctx->tty_info.fd);
  }
}

void session_display_clear(session_display_ctx_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return;
  }

  int fd = ctx->has_tty ? ctx->tty_info.fd : STDOUT_FILENO;
  if (fd >= 0) {
    (void)terminal_clear_screen();
    (void)terminal_cursor_home(fd);
  }
}

void session_display_cursor_home(session_display_ctx_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return;
  }

  int fd = ctx->has_tty ? ctx->tty_info.fd : STDOUT_FILENO;
  if (fd >= 0) {
    (void)terminal_cursor_home(fd);
  }
}

void session_display_set_cursor_visible(session_display_ctx_t *ctx, bool visible) {
  if (!ctx || !ctx->initialized) {
    return;
  }

  if (ctx->tty_info.fd >= 0) {
    (void)terminal_hide_cursor(ctx->tty_info.fd, !visible);
  }
}
