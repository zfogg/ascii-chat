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

#include <ascii-chat/session/display.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/video/ansi_fast.h>
#include <ascii-chat/video/palette.h>
#include <ascii-chat/video/ascii.h>
#include <ascii-chat/video/color_filter.h>
#include <ascii-chat/video/digital_rain.h>
#include <ascii-chat/video/image.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/video/simd/neon.h>

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
typedef struct session_display_ctx {
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

  /** @brief Help screen active flag (toggled with '?') - atomic for thread-safe access */
  atomic_bool help_screen_active;

  /** @brief Digital rain effect context (NULL if disabled) */
  digital_rain_t *digital_rain;

  /** @brief Last frame timestamp for digital rain delta time calculation */
  uint64_t last_frame_time_ns;
} session_display_ctx_t;

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
  atomic_init(&ctx->help_screen_active, false);

  // Get TTY info for direct terminal access
  ctx->tty_info = get_current_tty();

  // Determine if we have a valid TTY
  // Check if stdout is a TTY, not just any fd (stdin/stderr could be TTY while stdout is piped).
  // If stdout is piped/redirected, never perform terminal operations regardless of other fds.
  if (ctx->tty_info.fd >= 0) {
    ctx->has_tty = (platform_isatty(ctx->tty_info.fd) != 0) && terminal_is_stdout_tty();
  }

  // In piped mode, force all logs to stderr to prevent frame data corruption
  if (terminal_should_force_stderr()) {
    log_set_force_stderr(true);
    // Set stdout to line buffering to ensure output is flushed at newlines
    // This helps with snapshot mode where each frame ends with a newline
    //(void)setvbuf(stdout, NULL, _IOLBF, 0);
  }

  // Detect terminal capabilities
  ctx->caps = detect_terminal_capabilities();

  // Set wants_padding based on snapshot mode and TTY status
  // Disable padding when:
  // - In snapshot mode (one frame and exit)
  // - When stdout is not a TTY (piped/redirected output)
  // Enable padding for interactive terminal sessions
  bool is_snapshot_mode = config->snapshot_mode;
  bool is_interactive = terminal_is_interactive();
  ctx->caps.wants_padding = is_interactive && !is_snapshot_mode;

  log_debug("Padding mode: wants_padding=%d (snapshot=%d, interactive=%d, stdin_tty=%d, stdout_tty=%d)",
            ctx->caps.wants_padding, is_snapshot_mode, is_interactive, terminal_is_stdin_tty(),
            terminal_is_stdout_tty());

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

  // Pre-warm UTF-8 palette cache during initialization (not during rendering)
  // This is expensive on first use (creates lookup tables), so warm it up now to avoid frame lag
  // This function is imported from lib/video/simd/common.h
  extern utf8_palette_cache_t *get_utf8_palette_cache(const char *ascii_chars);
  (void)get_utf8_palette_cache((const char *)ctx->palette_chars);
  log_debug("UTF-8 palette cache pre-warmed during display initialization");

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

  // Initialize digital rain effect if enabled
  if (GET_OPTION(matrix_rain)) {
    // Get terminal dimensions for grid size
    unsigned short int width_us = (unsigned short int)GET_OPTION(width);
    unsigned short int height_us = (unsigned short int)GET_OPTION(height);

    // If dimensions are not set, detect from terminal
    if (width_us == 0 || height_us == 0) {
      (void)get_terminal_size(&width_us, &height_us);
    }

    int width = (int)width_us;
    int height = (int)height_us;

    ctx->digital_rain = digital_rain_init(width, height);
    if (ctx->digital_rain) {
      // Set color from color filter if active
      color_filter_t filter = GET_OPTION(color_filter);
      digital_rain_set_color_from_filter(ctx->digital_rain, filter);
      log_info("Digital rain effect enabled: %dx%d grid", width, height);
    } else {
      log_warn("Failed to initialize digital rain effect");
    }
    ctx->last_frame_time_ns = time_get_ns();
  }

  ctx->initialized = true;
  return ctx;
}

void session_display_destroy(session_display_ctx_t *ctx) {
  if (!ctx) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Session display context is NULL");
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

  // Cleanup digital rain effect
  if (ctx->digital_rain) {
    digital_rain_destroy(ctx->digital_rain);
    ctx->digital_rain = NULL;
  }

  ctx->initialized = false;
  SAFE_FREE(ctx);
}

/* ============================================================================
 * Session Display Query Functions
 * ============================================================================ */

bool session_display_has_tty(session_display_ctx_t *ctx) {
  if (!ctx || !ctx->initialized) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: ctx=%p", ctx);
    return false;
  }
  return ctx->has_tty;
}

const terminal_capabilities_t *session_display_get_caps(session_display_ctx_t *ctx) {
  if (!ctx || !ctx->initialized) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: ctx=%p", ctx);
    return NULL;
  }
  return &ctx->caps;
}

const char *session_display_get_palette_chars(session_display_ctx_t *ctx) {
  if (!ctx || !ctx->initialized) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: ctx=%p", ctx);
    return NULL;
  }
  return ctx->palette_chars;
}

size_t session_display_get_palette_len(session_display_ctx_t *ctx) {
  if (!ctx || !ctx->initialized) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: ctx=%p", ctx);
    return 0;
  }
  return ctx->palette_len;
}

const char *session_display_get_luminance_palette(session_display_ctx_t *ctx) {
  if (!ctx || !ctx->initialized) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: ctx=%p", ctx);
    return NULL;
  }
  return ctx->luminance_palette;
}

int session_display_get_tty_fd(session_display_ctx_t *ctx) {
  if (!ctx || !ctx->initialized) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: ctx=%p", ctx);
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
  color_filter_t color_filter = GET_OPTION(color_filter);

  // Make a mutable copy of terminal capabilities for ascii_convert_with_capabilities
  terminal_capabilities_t caps_copy = ctx->caps;

  // MEASURE EVERY OPERATION - Debug systematic timing
  uint64_t t_flip_start = time_get_ns();

  // Apply horizontal flip if requested
  image_t *flipped_image = NULL;
  const image_t *display_image = image;

  if (flip && image->w > 1 && image->pixels) {
    START_TIMER("image_flip");
    uint64_t t_flip_alloc_start = time_get_ns();
    flipped_image = image_new((size_t)image->w, (size_t)image->h);
    uint64_t t_flip_alloc_end = time_get_ns();

    if (flipped_image) {
      uint64_t t_flip_memcpy_start = time_get_ns();
      // OPTIMIZATION: Copy entire image first (sequential memory access - cache-friendly)
      memcpy(flipped_image->pixels, image->pixels, (size_t)image->w * (size_t)image->h * sizeof(rgb_pixel_t));
      uint64_t t_flip_memcpy_end = time_get_ns();

      uint64_t t_flip_reverse_start = time_get_ns();
      // Then flip each row in-place (maintains cache locality)
#if SIMD_SUPPORT_NEON
      // Use NEON-accelerated flip on ARM processors
      image_flip_horizontal_neon(flipped_image);
#else
      // Scalar fallback for non-NEON systems
      for (int y = 0; y < image->h; y++) {
        rgb_pixel_t *row = &flipped_image->pixels[y * image->w];
        for (int x = 0; x < image->w / 2; x++) {
          rgb_pixel_t temp = row[x];
          row[x] = row[image->w - 1 - x];
          row[image->w - 1 - x] = temp;
        }
      }
#endif
      uint64_t t_flip_reverse_end = time_get_ns();
      display_image = flipped_image;

      log_dev("TIMING_FLIP: alloc=%llu us, memcpy=%llu us, reverse=%llu us",
              (t_flip_alloc_end - t_flip_alloc_start) / 1000, (t_flip_memcpy_end - t_flip_memcpy_start) / 1000,
              (t_flip_reverse_end - t_flip_reverse_start) / 1000);
    }
    STOP_TIMER_AND_LOG_EVERY(dev, 3 * NS_PER_SEC_INT, 3 * NS_PER_MS_INT, "image_flip",
                             "IMAGE_FLIP: Horizontal flip complete (%.2f ms)");
  }
  uint64_t t_flip_end = time_get_ns();

  uint64_t t_filter_start = time_get_ns();
  // Apply color filter if specified
  if (color_filter != COLOR_FILTER_NONE && display_image->pixels) {
    START_TIMER("color_filter");
    // If we have a flipped_image, we can modify it in-place since it's our copy
    // If not, we need to create a copy for filtering
    image_t *filter_image = NULL;
    if (flipped_image) {
      filter_image = flipped_image;
    } else {
      // Create a copy for filtering since the original is const
      uint64_t t_filter_alloc_start = time_get_ns();
      filter_image = image_new((size_t)display_image->w, (size_t)display_image->h);
      uint64_t t_filter_alloc_end = time_get_ns();

      if (filter_image && display_image->pixels) {
        uint64_t t_filter_memcpy_start = time_get_ns();
        memcpy(filter_image->pixels, display_image->pixels,
               (size_t)display_image->w * (size_t)display_image->h * sizeof(rgb_pixel_t));
        uint64_t t_filter_memcpy_end = time_get_ns();
        display_image = filter_image;

        log_dev("TIMING_FILTER_COPY: alloc=%llu us, memcpy=%llu us", (t_filter_alloc_end - t_filter_alloc_start) / 1000,
                (t_filter_memcpy_end - t_filter_memcpy_start) / 1000);
      }
    }

    // Apply the color filter in-place
    if (filter_image && filter_image->pixels) {
      uint64_t t_filter_apply_start = time_get_ns();
      // Convert current time to seconds for rainbow animation
      float time_seconds = (float)t_filter_apply_start / (float)NS_PER_SEC_INT;
      apply_color_filter((uint8_t *)filter_image->pixels, filter_image->w, filter_image->h, filter_image->w * 3,
                         color_filter, time_seconds);
      uint64_t t_filter_apply_end = time_get_ns();

      log_dev("TIMING_FILTER_APPLY: %llu us", (t_filter_apply_end - t_filter_apply_start) / 1000);
    }
    STOP_TIMER_AND_LOG_EVERY(dev, 3 * NS_PER_SEC_INT, 5 * NS_PER_MS_INT, "color_filter",
                             "COLOR_FILTER: Filter complete (%.2f ms)");
  }
  uint64_t t_filter_end = time_get_ns();

  uint64_t t_convert_start = time_get_ns();
  // Call the standard ASCII conversion using context's palette and capabilities
  START_TIMER("ascii_convert_with_capabilities");
  char *result = ascii_convert_with_capabilities(display_image, width, height, &caps_copy, preserve_aspect_ratio,
                                                 stretch, ctx->palette_chars);
  STOP_TIMER_AND_LOG_EVERY(dev, 3 * NS_PER_SEC_INT, 5 * NS_PER_MS_INT, "ascii_convert_with_capabilities",
                           "ASCII_CONVERT: Conversion complete (%.2f ms)");
  uint64_t t_convert_end = time_get_ns();

  // Apply digital rain effect if enabled
  if (result && ctx->digital_rain) {
    uint64_t t_rain_start = time_get_ns();
    uint64_t current_time_ns = t_rain_start;
    float delta_time = (float)(current_time_ns - ctx->last_frame_time_ns) / (float)NS_PER_SEC_INT;
    ctx->last_frame_time_ns = current_time_ns;

    char *rain_result = digital_rain_apply(ctx->digital_rain, result, delta_time);
    if (rain_result) {
      SAFE_FREE(result);
      result = rain_result;
    }

    uint64_t t_rain_end = time_get_ns();
    log_dev("DIGITAL_RAIN: Effect applied (%.2f ms)", (double)(t_rain_end - t_rain_start) / (double)NS_PER_MS_INT);
  }

  uint64_t t_cleanup_start = time_get_ns();
  // Clean up flipped/filtered image if created
  START_TIMER("ascii_convert_cleanup");
  if (flipped_image) {
    image_destroy(flipped_image);
  }
  // Note: if we created a separate filter_image (not flipped_image), it needs cleanup too
  if (color_filter != COLOR_FILTER_NONE && !flipped_image && display_image != image) {
    image_destroy((image_t *)display_image);
  }
  STOP_TIMER_AND_LOG_EVERY(dev, 3 * NS_PER_SEC_INT, 2 * NS_PER_MS_INT, "ascii_convert_cleanup",
                           "ASCII_CONVERT_CLEANUP: Cleanup complete (%.2f ms)");
  uint64_t t_cleanup_end = time_get_ns();

  // Log total breakdown with actual measured times
  log_dev("CONVERT_TIMING: flip=%llu us, filter=%llu us, convert=%llu us, cleanup=%llu us, TOTAL=%llu us",
          (t_flip_end - t_flip_start) / 1000, (t_filter_end - t_filter_start) / 1000,
          (t_convert_end - t_convert_start) / 1000, (t_cleanup_end - t_cleanup_start) / 1000,
          (t_cleanup_end - t_flip_start) / 1000);

  return result;
}

/* ============================================================================
 * Session Display Rendering Functions
 * ============================================================================ */

void session_display_render_frame(session_display_ctx_t *ctx, const char *frame_data) {
  if (!ctx || !ctx->initialized) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Display context is NULL or uninitialized");
    return;
  }

  if (!frame_data) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Frame data is NULL");
    return;
  }

  // Suppress frame rendering when help screen is active
  // Network reception continues in background, frames are just not displayed
  if (atomic_load(&ctx->help_screen_active)) {
    return;
  }

  // Calculate frame length
  size_t frame_len = strnlen(frame_data, 1024 * 1024); // Max 1MB frame
  if (frame_len == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Frame data is empty");
    return;
  }

  // Debug: check for lines that might shoot off to the right
  // Find longest line in frame data (visible characters between newlines, excluding ANSI codes)
  size_t max_line_chars = 0;
  size_t current_line_chars = 0;
  bool in_ansi_code = false;

  for (size_t i = 0; i < frame_len; i++) {
    char c = frame_data[i];

    if (c == '\033') {
      // Start of ANSI escape sequence
      in_ansi_code = true;
    } else if (in_ansi_code && c == 'm') {
      // End of ANSI escape sequence
      in_ansi_code = false;
    } else if (!in_ansi_code && c == '\n') {
      // End of line
      if (current_line_chars > max_line_chars) {
        max_line_chars = current_line_chars;
      }
      current_line_chars = 0;
    } else if (!in_ansi_code) {
      // Regular character (not ANSI code)
      current_line_chars++;
    }
  }

  if (current_line_chars > 0) {
    if (current_line_chars > max_line_chars) {
      max_line_chars = current_line_chars;
    }
  }

  unsigned short int term_width = GET_OPTION(width);
  if (max_line_chars > term_width) {
    log_warn("FRAME_ANALYSIS: Line %zu chars exceeds terminal width %u - this may cause wrapping!", max_line_chars,
             term_width);
  }

  // Handle first frame - perform initial terminal reset only
  if (atomic_load(&ctx->first_frame)) {
    atomic_store(&ctx->first_frame, false);

    // NOTE: log_set_terminal_output(false) skipped here to avoid deadlocks with audio worker threads
    // Terminal logging will continue during rendering but won't corrupt the final frame

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

  START_TIMER("frame_write");
  if (use_tty_control) {
    // TTY mode: just reset cursor to home and redraw frame without clearing
    // This avoids flashing at high framerates and is more efficient than clearing
    (void)terminal_cursor_home(STDOUT_FILENO);
    // Send frame data (overwrites previous frame from cursor position)
    (void)platform_write_all(STDOUT_FILENO, frame_data, frame_len);
    (void)terminal_flush(STDOUT_FILENO);
  } else {
    // Piped mode: combine frame and newline into single write call
    // Allocate temporary buffer for frame + newline to minimize syscalls
    char *write_buf = SAFE_MALLOC(frame_len + 1, char *);
    if (write_buf) {
      memcpy(write_buf, frame_data, frame_len);
      write_buf[frame_len] = '\n';

      // Use thread-safe console lock for proper synchronization
      bool prev_lock_state = log_lock_terminal();
      (void)platform_write_all(STDOUT_FILENO, write_buf, frame_len + 1);
      log_unlock_terminal(prev_lock_state);

      SAFE_FREE(write_buf);
    } else {
      // Fallback: two writes if allocation fails
      (void)platform_write_all(STDOUT_FILENO, frame_data, frame_len);
      bool prev_lock_state = log_lock_terminal();
      const char newline = '\n';
      (void)platform_write_all(STDOUT_FILENO, &newline, 1);
      log_unlock_terminal(prev_lock_state);
    }

    // Flush kernel write buffer so piped data appears immediately to readers
    (void)terminal_flush(STDOUT_FILENO);
  }
  STOP_TIMER_AND_LOG_EVERY(dev, 3 * NS_PER_SEC_INT, 5 * NS_PER_MS_INT, "frame_write",
                           "FRAME_WRITE: Write and flush complete (%.2f ms)");
}

void session_display_write_raw(session_display_ctx_t *ctx, const char *data, size_t len) {
  if (!ctx || !ctx->initialized || !data || len == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: ctx=%p, data=%p, len=%zu", ctx, data, len);
    return;
  }

  int fd = -1;
  if (ctx->has_tty && ctx->tty_info.fd >= 0) {
    fd = ctx->tty_info.fd;
  } else {
    fd = STDOUT_FILENO;
  }

  // Write all data with automatic retry on transient errors
  (void)platform_write_all(fd, data, len);

  // Flush immediately after write to TTY to ensure data is sent
  (void)terminal_flush(fd);
}

void session_display_reset(session_display_ctx_t *ctx) {
  if (!ctx || !ctx->initialized) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Session display context is NULL or uninitialized");
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
    SET_ERRNO(ERROR_INVALID_PARAM, "Session display context is NULL or uninitialized");
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
    SET_ERRNO(ERROR_INVALID_PARAM, "Session display context is NULL or uninitialized");
    return;
  }

  int fd = ctx->has_tty ? ctx->tty_info.fd : STDOUT_FILENO;
  if (fd >= 0) {
    (void)terminal_cursor_home(fd);
  }
}

void session_display_set_cursor_visible(session_display_ctx_t *ctx, bool visible) {
  if (!ctx || !ctx->initialized) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Session display context is NULL or uninitialized");
    return;
  }

  // Only perform terminal operations when we have a valid TTY (not when piping)
  if (ctx->has_tty && ctx->tty_info.fd >= 0) {
    (void)terminal_hide_cursor(ctx->tty_info.fd, !visible);
  }
}

bool session_display_has_audio_playback(session_display_ctx_t *ctx) {
  if (!ctx || !ctx->initialized) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Session display context is NULL or uninitialized");
    return false;
  }
  return ctx->audio_playback_enabled;
}

asciichat_error_t session_display_write_audio(session_display_ctx_t *ctx, const float *buffer, size_t num_samples) {
  if (!ctx || !ctx->initialized || !buffer || num_samples == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: ctx=%p, buffer=%p, num_samples=%zu", ctx, buffer,
                     num_samples);
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

/* ============================================================================
 * Help Screen Functions (from help_screen module)
 * ============================================================================ */

/**
 * @brief Toggle help screen on/off (implemented in display.c for struct access)
 */
void session_display_toggle_help(session_display_ctx_t *ctx) {
  if (!ctx) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Session display context is NULL");
    return;
  }

  bool current = atomic_load(&ctx->help_screen_active);
  atomic_store(&ctx->help_screen_active, !current);
}

/**
 * @brief Check if help screen is currently active (implemented in display.c for struct access)
 */
bool session_display_is_help_active(session_display_ctx_t *ctx) {
  if (!ctx) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Session display context is NULL");
    return false;
  }

  return atomic_load(&ctx->help_screen_active);
}
