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
#include "log/logging.h"
#include "options/options.h"
#include "platform/terminal.h"
#include "platform/abstraction.h"
#include "video/ansi_fast.h"
#include "video/palette.h"
#include "video/ascii.h"
#include "video/image.h"
#include "audio/audio.h"
#include "asciichat_errno.h"

#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>

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

  /** @brief Audio playback is enabled */
  bool audio_playback_enabled;

  /** @brief Audio context for playback (borrowed, not owned) */
  void *audio_ctx;
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
 * @param snapshot_mode Whether snapshot mode is enabled
 */
static void full_terminal_reset_internal(bool snapshot_mode) {
  // Always reset and clear scrollback when rendering to TTY (including snapshot mode)
  // Only skip when output is piped/redirected (those cases won't call this function with has_tty=true)
  (void)terminal_reset(STDOUT_FILENO);
  (void)terminal_clear_screen();
  (void)terminal_cursor_home(STDOUT_FILENO);
  (void)terminal_clear_scrollback(STDOUT_FILENO); // Clear history to avoid old logs visible above ASCII
  if (!snapshot_mode) {
    (void)terminal_hide_cursor(STDOUT_FILENO, true);
  }
  (void)terminal_flush(STDOUT_FILENO);
}

/* ============================================================================
 * Session Display Lifecycle Functions
 * ============================================================================ */

session_display_ctx_t *session_display_create(const session_display_config_t *config) {
  // Auto-create config from command-line options if NULL
  session_display_config_t auto_config = {0};
  if (!config) {
    auto_config.snapshot_mode = GET_OPTION(snapshot_mode);
    auto_config.palette_type = GET_OPTION(palette_type);
    auto_config.custom_palette = GET_OPTION(palette_custom_set) ? GET_OPTION(palette_custom) : NULL;
    auto_config.color_mode = TERM_COLOR_AUTO;
    config = &auto_config;
  }

  // Check if we should exit before starting initialization
  if (config->should_exit_callback && config->should_exit_callback(config->callback_data)) {
    return NULL;
  }

  // Allocate context
  session_display_ctx_t *ctx = SAFE_CALLOC(1, sizeof(session_display_ctx_t), session_display_ctx_t *);

  // Store configuration
  ctx->snapshot_mode = config->snapshot_mode;
  ctx->palette_type = config->palette_type;
  ctx->audio_playback_enabled = config->enable_audio_playback;
  ctx->audio_ctx = config->audio_ctx;
  atomic_init(&ctx->first_frame, true);

  // Get TTY info for direct terminal access
  ctx->tty_info = get_current_tty();

  // Determine if we have a valid TTY
  // CRITICAL: Check if stdout is a TTY, not just any fd (stdin/stderr could be TTY while stdout is piped)
  // If stdout is piped/redirected, never perform terminal operations regardless of other fds
  if (ctx->tty_info.fd >= 0) {
    ctx->has_tty = (platform_isatty(ctx->tty_info.fd) != 0) && (platform_isatty(STDOUT_FILENO) != 0);
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

  // Initialize ANSI fast lookup tables based on terminal capabilities
  if (ctx->caps.color_level == TERM_COLOR_TRUECOLOR) {
    ansi_fast_init();
  } else if (ctx->caps.color_level == TERM_COLOR_256) {
    ansi_fast_init_256color();
  } else if (ctx->caps.color_level == TERM_COLOR_16) {
    ansi_fast_init_16color();
  }
  // TERM_COLOR_MONO requires no init

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
  // Don't reset terminal in snapshot mode to preserve the rendered output
  if (ctx->has_tty && ctx->tty_info.fd >= 0) {
    ascii_write_destroy(ctx->tty_info.fd, !ctx->snapshot_mode);
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
 * Session Display ASCII Conversion Functions
 * ============================================================================ */

char *session_display_convert_to_ascii(session_display_ctx_t *ctx, const image_t *image) {
  if (!ctx) {
    SET_ERRNO(ERROR_INVALID_PARAM, "session_display_convert_to_ascii: ctx is NULL");
    return NULL;
  }

  if (!ctx->initialized) {
    SET_ERRNO(ERROR_INVALID_STATE, "session_display_convert_to_ascii: ctx not initialized");
    return NULL;
  }

  if (!image) {
    SET_ERRNO(ERROR_INVALID_PARAM, "session_display_convert_to_ascii: image is NULL");
    return NULL;
  }

  // Get conversion parameters from command-line options
  unsigned short int width = GET_OPTION(width);
  unsigned short int height = GET_OPTION(height);
  bool stretch = GET_OPTION(stretch);
  bool preserve_aspect_ratio = !stretch;
  bool flip = GET_OPTION(webcam_flip);

  // Make a mutable copy of terminal capabilities for ascii_convert_with_capabilities
  terminal_capabilities_t caps_copy = ctx->caps;

  // Apply horizontal flip if requested
  image_t *flipped_image = NULL;
  const image_t *display_image = image;

  if (flip && image->w > 1 && image->pixels) {
    flipped_image = image_new((size_t)image->w, (size_t)image->h);
    if (flipped_image) {
      // Flip image horizontally (mirror left-right)
      for (int y = 0; y < image->h; y++) {
        for (int x = 0; x < image->w; x++) {
          int src_idx = y * image->w + (image->w - 1 - x);
          int dst_idx = y * image->w + x;
          flipped_image->pixels[dst_idx] = image->pixels[src_idx];
        }
      }
      display_image = flipped_image;
    }
  }

  // Call the standard ASCII conversion using context's palette and capabilities
  char *result = ascii_convert_with_capabilities(display_image, width, height, &caps_copy, preserve_aspect_ratio,
                                                 stretch, ctx->palette_chars);

  // Clean up flipped image if created
  if (flipped_image) {
    image_destroy(flipped_image);
  }

  return result;
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
      full_terminal_reset_internal(ctx->snapshot_mode);
    }
  }

  // Output routing logic:
  // - TTY mode: always render with cursor control (including snapshot mode for animation)
  // - Snapshot mode on non-TTY: render only final frame WITHOUT cursor control
  // - Piped mode: render every frame WITHOUT cursor control (allows continuous output to files)
  bool use_tty_control = ctx->has_tty;

  if (use_tty_control) {
    // TTY mode: send clear codes then frame data
    const char *clear = "\033[2J\033[H";
    platform_write(STDOUT_FILENO, clear, 7);

    size_t written = 0;
    int attempts = 0;
    while (written < frame_len && attempts < 1000) {
      ssize_t result = platform_write(STDOUT_FILENO, frame_data + written, frame_len - written);
      if (result > 0) {
        written += (size_t)result;
        attempts = 0;
      } else {
        attempts++;
      }
    }
  } else if (ctx->snapshot_mode && is_final) {
    // Snapshot mode on non-TTY (piped): render final frame only WITHOUT cursor control
    size_t written = 0;
    while (written < frame_len) {
      ssize_t result = platform_write(STDOUT_FILENO, frame_data + written, frame_len - written);
      if (result <= 0) {
        log_error("Failed to write snapshot frame data");
        break;
      }
      written += (size_t)result;
    }
    // Add newline at end of snapshot output
    (void)printf("\n");
  } else if (!ctx->has_tty && !ctx->snapshot_mode) {
    // Piped mode (non-snapshot): render every frame WITHOUT cursor control
    // This allows continuous frame output to files for streaming/recording
    size_t written = 0;
    while (written < frame_len) {
      ssize_t result = platform_write(STDOUT_FILENO, frame_data + written, frame_len - written);
      if (result <= 0) {
        log_error("Failed to write piped frame data");
        break;
      }
      written += (size_t)result;
    }
    // Add newline after each frame to separate frames when captured to files
    (void)printf("\n");
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

  // Skip terminal reset in snapshot mode to preserve rendered output
  if (ctx->snapshot_mode) {
    return;
  }

  // Only perform terminal operations if we have a valid TTY
  if (ctx->has_tty && ctx->tty_info.fd >= 0) {
    (void)terminal_reset(ctx->tty_info.fd);
    (void)terminal_hide_cursor(ctx->tty_info.fd, false); // Show cursor
    (void)terminal_flush(ctx->tty_info.fd);
  }
}

void session_display_clear(session_display_ctx_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return;
  }

  // Skip terminal clear in snapshot mode to preserve rendered output
  if (ctx->snapshot_mode) {
    return;
  }

  // Only perform terminal operations when we have a valid TTY (not when piping)
  if (ctx->has_tty && ctx->tty_info.fd >= 0) {
    (void)terminal_clear_screen();
    (void)terminal_cursor_home(ctx->tty_info.fd);
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

  // Only perform terminal operations when we have a valid TTY (not when piping)
  if (ctx->has_tty && ctx->tty_info.fd >= 0) {
    (void)terminal_hide_cursor(ctx->tty_info.fd, !visible);
  }
}

bool session_display_has_audio_playback(session_display_ctx_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return false;
  }
  return ctx->audio_playback_enabled;
}

asciichat_error_t session_display_write_audio(session_display_ctx_t *ctx, const float *buffer, size_t num_samples) {
  if (!ctx || !ctx->initialized || !buffer || num_samples == 0) {
    return ASCIICHAT_OK;
  }

  if (!ctx->audio_playback_enabled || !ctx->audio_ctx) {
    // Audio not enabled, but not an error - just skip silently
    return ASCIICHAT_OK;
  }

  // For mirror mode with local files: write samples directly without jitter buffering
  // The jitter buffer is designed for network scenarios with irregular packet arrivals
  // For local playback, we just want raw samples flowing to the speakers
  audio_context_t *audio_ctx = (audio_context_t *)ctx->audio_ctx;
  if (audio_ctx && audio_ctx->playback_buffer) {
    audio_ring_buffer_t *rb = audio_ctx->playback_buffer;

    // Simple direct write: append samples to ring buffer without network-oriented complexity
    uint32_t write_idx = atomic_load(&rb->write_index);
    uint32_t read_idx = atomic_load(&rb->read_index);

    // Calculate available space in ring buffer
    uint32_t available = (read_idx - write_idx - 1) & (AUDIO_RING_BUFFER_SIZE - 1);

    if ((uint32_t)num_samples > available) {
      // Buffer full - just skip this write to avoid distortion from overwriting old audio
      return ASCIICHAT_OK;
    }

    // Direct memcpy to ring buffer, handling wrap-around
    uint32_t space_before_wrap = AUDIO_RING_BUFFER_SIZE - write_idx;
    if ((uint32_t)num_samples <= space_before_wrap) {
      memcpy(&rb->data[write_idx], buffer, num_samples * sizeof(float));
    } else {
      // Split write: first part to end of buffer, second part wraps to beginning
      uint32_t first_part = space_before_wrap;
      uint32_t second_part = num_samples - first_part;
      memcpy(&rb->data[write_idx], buffer, first_part * sizeof(float));
      memcpy(&rb->data[0], &buffer[first_part], second_part * sizeof(float));
    }

    // Update write index atomically
    atomic_store(&rb->write_index, (write_idx + num_samples) % AUDIO_RING_BUFFER_SIZE);

    return ASCIICHAT_OK;
  }

  return ASCIICHAT_OK;
}
