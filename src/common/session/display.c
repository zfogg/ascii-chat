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

#include "session/display.h"
#include "session/render.h"
#include <ascii-chat/util/time.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/ui/splash.h>
#include <ascii-chat/ui/fps_counter.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/video/ascii/ansi_fast.h>
#include <ascii-chat/video/ascii/palette.h>
#include <ascii-chat/video/ascii.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/video/rgba/color_filter.h>
#include <ascii-chat/video/anim/digital_rain.h>
#include <ascii-chat/video/rgba/image.h>
#include <ascii-chat/terminal/fd/reader.h>
#ifndef _WIN32
#include <ascii-chat/media/render/renderer.h>
#endif
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/video/ascii/neon/foreground.h>

#include <string.h>
#include <stdio.h>
#include <ascii-chat/atomic.h>
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
  atomic_t first_frame;

  /** @brief Context is fully initialized */
  bool initialized;

  /** @brief Audio playback is enabled */
  bool audio_playback_enabled;

  /** @brief Audio context for playback (borrowed, not owned) */
  void *audio_ctx;

  /** @brief Keyboard help active flag (toggled with '?') - atomic for thread-safe access */
  atomic_t keyboard_help_active;

  /** @brief Digital rain effect context (NULL if disabled) */
  digital_rain_t *digital_rain;

  /** @brief Last frame timestamp for digital rain delta time calculation */
  uint64_t last_frame_time_ns;

  /** @brief FPS counter for measuring output throughput */
  fps_counter_t *fps_counter;

  /** @brief Video FPS for render-file encoding */
  uint32_t render_fps;

#ifndef _WIN32
  /** @brief Render-to-file context (NULL if disabled) */
  render_file_ctx_t *render_file;

  /** @brief Stdin frame reader for ASCII-to-video rendering (borrowed ref, not owned) */
  terminal_fd_reader_t *stdin_reader;
#endif
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
  ctx->render_fps = config->render_fps;
  atomic_store_bool(&ctx->first_frame, true);
  atomic_store_bool(&ctx->keyboard_help_active, false);

  // Initialize FPS counter
  ctx->fps_counter = fps_counter_create();

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
    // Disable buffering for stdout to ensure frames appear immediately
    // Each frame ends with newline, so unbuffered output is critical for real-time display
    (void)setvbuf(stdout, NULL, _IONBF, 0);
  }

  // Also disable buffering for TTY mode to ensure smooth 40+ FPS animation
  setvbuf(stdout, NULL, _IONBF, 0);

  // Detect terminal capabilities
  ctx->caps = detect_terminal_capabilities();

  // Set wants_padding based on terminal output mode
  // Enable padding for all rendering modes (including snapshot) to center output
  // The padding adds newlines at the top, which is useful for visual centering
  bool is_snapshot_mode = config->snapshot_mode;
  ctx->caps.wants_padding = true;

  // Calculate pad_height: when padding is enabled, add top padding for centering
  // Halfblock mode uses 2 source rows per output row, so padding is halved
  if (ctx->caps.wants_padding) {
    ctx->caps.pad_height = 5; // Halfblock needs half the padding (10 / 2 = 5)
  } else {
    ctx->caps.pad_height = 0;
  }

  log_debug("Padding mode: wants_padding=%d (snapshot=%d, has_tty=%d, stdin_tty=%d, stdout_tty=%d), pad_height=%zu",
            ctx->caps.wants_padding, is_snapshot_mode, ctx->has_tty, terminal_is_stdin_tty(), terminal_is_stdout_tty(),
            ctx->caps.pad_height);

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
    unsigned short int width_us = terminal_get_effective_width();
    unsigned short int height_us = terminal_get_effective_height();

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

#ifndef _WIN32
  // Initialize render-file if enabled (FFmpeg encodes to stdout when "-" is specified)
  const char *render_file_opt = GET_OPTION(render_file);
  log_info("DISPLAY_CREATE: render-file opt='%s' (len=%zu), will initialize=%s",
           render_file_opt ? render_file_opt : "(null)",
           render_file_opt ? strlen(render_file_opt) : 0,
           (render_file_opt && strlen(render_file_opt) > 0) ? "YES" : "NO");
  if (render_file_opt && strlen(render_file_opt) > 0) {
    int width = (int)GET_OPTION(width);
    int height = (int)GET_OPTION(height);
    log_info("render-file: Creating encoder with cols=%d, rows=%d", width, height);
    // Determine encoder FPS: in snapshot mode, use actual capture rate for correct duration;
    // in normal mode, use configured fps option
    uint32_t encoder_fps = 0;
    if (GET_OPTION(snapshot_mode) && config->render_fps > 0) {
      // Snapshot mode with known capture rate - use capture rate for correct duration
      encoder_fps = config->render_fps;
      log_debug("render-file: SNAPSHOT MODE - using capture rate FPS=%u (snapshot_delay=%.2f sec)", encoder_fps,
                GET_OPTION(snapshot_delay));
    } else if (config->render_fps > 0) {
      // Normal mode with known capture rate - use it
      encoder_fps = config->render_fps;
      log_debug("render-file: Using capture rate FPS=%u for encoding", encoder_fps);
    } else {
      // No capture rate available - use option default
      encoder_fps = (uint32_t)GET_OPTION(fps);
      if (encoder_fps == 0)
        encoder_fps = 60;
      log_debug("render-file: Using option FPS=%u (no capture rate available)", encoder_fps);
    }
    log_info("render-file: Final encoder FPS=%u (config_render_fps=%u, option_fps=%u)", encoder_fps, config->render_fps,
             (uint32_t)GET_OPTION(fps));
    log_info("render-file: Calling render_file_create with path='%s', %dx%d, fps=%d",
             render_file_opt, width, height, encoder_fps);
    asciichat_error_t rf_err = render_file_create(render_file_opt, width, height, (int)encoder_fps,
                                                  GET_OPTION(render_theme), &ctx->render_file);
    if (rf_err != ASCIICHAT_OK) {
      log_error("render-file: FAILED to create with error %d (ctx->render_file=%p)",
                rf_err, (void *)ctx->render_file);
    } else {
      log_info("render-file: SUCCESS - encoder initialized, ctx->render_file=%p", (void *)ctx->render_file);
      // Set audio sources if render_file_set_audio_source is available
      if (ctx->render_file && config->render_file_audio_source) {
        render_file_set_audio_source((render_file_ctx_t *)ctx->render_file, config->render_file_audio_source, NULL);
        log_debug("render-file: audio source set (media_source=%p)", config->render_file_audio_source);
      }
      if (ctx->render_file && config->render_file_audio_capture_rb) {
        render_file_set_audio_source((render_file_ctx_t *)ctx->render_file, NULL, config->render_file_audio_capture_rb);
        log_debug("render-file: audio capture ring buffer set");
      }
    }
  } else {
    log_info("render-file: NOT initializing (render_file_opt is %s)", render_file_opt ? "empty" : "NULL");
  }
#endif

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

  // Cleanup FPS counter
  if (ctx->fps_counter) {
    fps_counter_destroy(ctx->fps_counter);
    ctx->fps_counter = NULL;
  }

#ifndef _WIN32
  // Cleanup render-file if active
  if (ctx->render_file) {
    render_file_destroy(ctx->render_file);
    ctx->render_file = NULL;
  }
#endif

  ctx->initialized = false;

  // Clear the cached display context in splash state to prevent use-after-free
  // when worker threads try to access the display after it's been freed
  splash_clear_display_context();

  SAFE_FREE(ctx);
}

void session_display_set_stdin_reader(session_display_ctx_t *ctx, void *reader) {
  if (!ctx) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Display context is NULL");
    return;
  }

#ifndef _WIN32
  ctx->stdin_reader = (terminal_fd_reader_t *)reader;
  log_debug("session_display: stdin_reader set");
#else
  (void)reader; // Unused on Windows
#endif
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

void *session_display_get_stdin_reader(session_display_ctx_t *ctx) {
  if (!ctx || !ctx->initialized) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: ctx=%p", ctx);
    return NULL;
  }

#ifndef _WIN32
  return ctx->stdin_reader;
#else
  return NULL;
#endif
}

uint32_t session_display_get_render_fps(session_display_ctx_t *ctx) {
  if (!ctx)
    return 0;
  return ctx->render_fps;
}

bool session_display_has_first_frame(session_display_ctx_t *ctx) {
  if (!ctx) {
    return false;
  }
  // Return true if first_frame flag is false (meaning first frame has been rendered)
  return !atomic_load_bool(&ctx->first_frame);
}

void session_display_reset_first_frame(session_display_ctx_t *ctx) {
  if (!ctx) {
    return;
  }
  // Reset first_frame flag to true so splash screen can be shown again on next render
  atomic_store_bool(&ctx->first_frame, true);
}

bool session_display_has_render_file(session_display_ctx_t *ctx) {
  if (!ctx) {
    return false;
  }
  return ctx->render_file != NULL;
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

  // Determine if we should apply flip_x and flip_y
  bool flip_x_enabled = GET_OPTION(flip_x);
  bool flip_y_enabled = GET_OPTION(flip_y);

  color_filter_t color_filter = GET_OPTION(color_filter);

  // Handle dynamic matrix rain effect toggle
  bool matrix_rain_enabled = GET_OPTION(matrix_rain);
  if (matrix_rain_enabled && !ctx->digital_rain) {
    // Initialize digital rain if it's now enabled but wasn't before
    unsigned short int width_us = terminal_get_effective_width();
    unsigned short int height_us = terminal_get_effective_height();
    int width_for_rain = (int)width_us;
    int height_for_rain = (int)height_us;
    ctx->digital_rain = digital_rain_init(width_for_rain, height_for_rain);
    if (ctx->digital_rain) {
      digital_rain_set_color_from_filter(ctx->digital_rain, color_filter);
      log_info("Matrix rain effect: enabled");
    }
  } else if (!matrix_rain_enabled && ctx->digital_rain) {
    // Disable digital rain if it's now disabled but was enabled before
    digital_rain_destroy(ctx->digital_rain);
    ctx->digital_rain = NULL;
    log_info("Matrix rain effect: disabled");
  }

  // Make a mutable copy of terminal capabilities for ascii_convert_with_capabilities
  terminal_capabilities_t caps_copy = ctx->caps;

  // Re-evaluate render_mode on every frame to pick up live changes
  caps_copy.render_mode = (render_mode_t)GET_OPTION(render_mode);

  // MEASURE EVERY OPERATION - Debug systematic timing
  uint64_t t_flip_start = time_get_ns();

  // Apply horizontal and/or vertical flips if requested
  image_t *flipped_image = NULL;
  const image_t *display_image = image;

  if ((flip_x_enabled || flip_y_enabled) && image->w > 1 && image->h > 1 && image->pixels) {
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

      // Apply horizontal flip (X-axis)
      if (flip_x_enabled) {
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
      }

      // Apply vertical flip (Y-axis)
      if (flip_y_enabled) {
        for (int y = 0; y < image->h / 2; y++) {
          rgb_pixel_t *top_row = &flipped_image->pixels[y * image->w];
          rgb_pixel_t *bottom_row = &flipped_image->pixels[(image->h - 1 - y) * image->w];
          for (int x = 0; x < image->w; x++) {
            rgb_pixel_t temp = top_row[x];
            top_row[x] = bottom_row[x];
            bottom_row[x] = temp;
          }
        }
      }

      uint64_t t_flip_reverse_end = time_get_ns();
      display_image = flipped_image;

      log_dev("TIMING_FLIP: alloc=%llu us, memcpy=%llu us, flip=%llu us (x=%d, y=%d)",
              (t_flip_alloc_end - t_flip_alloc_start) / 1000, (t_flip_memcpy_end - t_flip_memcpy_start) / 1000,
              (t_flip_reverse_end - t_flip_reverse_start) / 1000, flip_x_enabled, flip_y_enabled);
    }
    STOP_TIMER_AND_LOG_EVERY(dev, 3 * NS_PER_SEC_INT, 3 * NS_PER_MS_INT, "image_flip",
                             "IMAGE_FLIP: Flip complete (%.2f ms)");
  }
  uint64_t t_flip_end = time_get_ns();

  uint64_t t_filter_start = time_get_ns();

  // Apply color filter to RGB pixels before ASCII conversion
  // Rainbow filter is handled separately via ANSI color replacement
  image_t *filtered_image = NULL;
  if (color_filter != COLOR_FILTER_NONE && color_filter != COLOR_FILTER_RAINBOW) {
    // Create a copy to avoid modifying the original
    filtered_image = image_new((size_t)display_image->w, (size_t)display_image->h);
    if (filtered_image) {
      memcpy(filtered_image->pixels, display_image->pixels,
             (size_t)display_image->w * (size_t)display_image->h * sizeof(rgb_pixel_t));

      // Apply filter to the copy
      int stride = (int)display_image->w * 3;
      float time_seconds = (float)t_filter_start / (float)NS_PER_SEC_INT;
      apply_color_filter((uint8_t *)filtered_image->pixels, (uint32_t)display_image->w, (uint32_t)display_image->h,
                         (uint32_t)stride, color_filter, time_seconds);
    }
  }

  const image_t *ascii_input_image = filtered_image ? filtered_image : display_image;
  uint64_t t_filter_end = time_get_ns();

  uint64_t t_convert_start = time_get_ns();
  // Call the standard ASCII conversion using the filtered (or original) image
  START_TIMER("ascii_convert_with_capabilities");
  char *result = ascii_convert_with_capabilities(ascii_input_image, width, height, &caps_copy, preserve_aspect_ratio,
                                                 stretch, ctx->palette_chars);
  STOP_TIMER_AND_LOG_EVERY(dev, 3 * NS_PER_SEC_INT, 5 * NS_PER_MS_INT, "ascii_convert_with_capabilities",
                           "ASCII_CONVERT: Conversion complete (%.2f ms)");
  uint64_t t_convert_end = time_get_ns();

  // Apply rainbow color filter to ANSI output by replacing RGB values
  // This preserves character selection while applying the filter colors
  if (result && color_filter == COLOR_FILTER_RAINBOW) {
    uint64_t t_color_replace_start = time_get_ns();
    float time_seconds = (float)t_filter_start / (float)NS_PER_SEC_INT;

    char *rainbow_result = rainbow_replace_ansi_colors(result, time_seconds);
    if (rainbow_result) {
      SAFE_FREE(result);
      result = rainbow_result;
    }

    uint64_t t_color_replace_end = time_get_ns();
    char color_replace_str[32];
    time_pretty(t_color_replace_end - t_color_replace_start, -1, color_replace_str, sizeof(color_replace_str));
    log_dev("COLOR_REPLACE: %s", color_replace_str);
  }

  // Apply digital rain effect if enabled
  if (result && ctx->digital_rain) {
    uint64_t t_rain_start = time_get_ns();
    uint64_t current_time_ns = t_rain_start;
    float delta_time = (float)(current_time_ns - ctx->last_frame_time_ns) / (float)NS_PER_SEC_INT;
    ctx->last_frame_time_ns = current_time_ns;

    // Update digital rain color from current filter (allows live filter changes)
    color_filter_t current_filter = GET_OPTION(color_filter);
    digital_rain_set_color_from_filter(ctx->digital_rain, current_filter);

    char *rain_result = digital_rain_apply(ctx->digital_rain, result, delta_time);
    if (rain_result) {
      SAFE_FREE(result);
      result = rain_result;
    }

    uint64_t t_rain_end = time_get_ns();
    char rain_str[32];
    time_pretty(t_rain_end - t_rain_start, -1, rain_str, sizeof(rain_str));
    log_dev("DIGITAL_RAIN: Effect applied (%s)", rain_str);
  }

  uint64_t t_cleanup_start = time_get_ns();
  // Clean up flipped and filtered images if created
  START_TIMER("ascii_convert_cleanup");
  if (filtered_image) {
    image_destroy(filtered_image);
  }
  if (flipped_image) {
    image_destroy(flipped_image);
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

/* Forward declaration for FPS overlay rendering */
void session_display_render_fps_overlay(session_display_ctx_t *ctx);

void session_display_render_frame(session_display_ctx_t *ctx, const char *frame_data) {
  // Legacy function: shim that calls both new split functions for backward compatibility
  // This maintains API compatibility with existing code while delegating to specialized functions

  if (!ctx || !frame_data) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Display context or frame data is NULL");
    return;
  }

  // Write ASCII to terminal (main thread output path)
  session_display_write_ascii(ctx, frame_data);
}

void session_display_write_ascii(session_display_ctx_t *ctx, const char *ascii) {
  if (!ctx || !ctx->initialized) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Display context is NULL or uninitialized");
    return;
  }

  if (!ascii) {
    SET_ERRNO(ERROR_INVALID_PARAM, "ASCII data is NULL");
    return;
  }

  // Re-evaluate color_level on every frame to pick up live color_mode changes
  terminal_color_mode_t color_mode = (terminal_color_mode_t)GET_OPTION(color_mode);
  if (color_mode != TERM_COLOR_AUTO) {
    ctx->caps.color_level = color_mode;
  }

  // Suppress frame rendering when help screen is active
  if (atomic_load_bool(&ctx->keyboard_help_active)) {
    return;
  }

  // Calculate frame length
  size_t frame_len = strnlen(ascii, 1024 * 1024); // Max 1MB frame
  if (frame_len == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "ASCII data is empty");
    return;
  }

  // Apply digital rain effect if enabled
  char *display_frame = (char *)ascii;
  char *rain_result = NULL;
  if (ctx->digital_rain) {
    uint64_t t_rain_start = time_get_ns();
    uint64_t current_time_ns = t_rain_start;
    float delta_time = (float)(current_time_ns - ctx->last_frame_time_ns) / (float)NS_PER_SEC_INT;
    ctx->last_frame_time_ns = current_time_ns;

    // Update digital rain color from current filter
    color_filter_t current_filter = GET_OPTION(color_filter);
    digital_rain_set_color_from_filter(ctx->digital_rain, current_filter);

    rain_result = digital_rain_apply(ctx->digital_rain, (char *)ascii, delta_time);
    if (rain_result) {
      display_frame = rain_result;
      frame_len = strnlen(rain_result, 1024 * 1024);

      uint64_t t_rain_end = time_get_ns();
      char rain_str[32];
      time_pretty(t_rain_end - t_rain_start, -1, rain_str, sizeof(rain_str));
      log_info("DIGITAL_RAIN (write_ascii): Effect applied (%s)", rain_str);
    }
  }

  // Handle first frame - perform initial terminal reset and splash cleanup
  bool was_first_frame = atomic_exchange_bool(&ctx->first_frame, false);
  if (was_first_frame) {
    // Stop splash screen when first frame is ready
    splash_intro_done();

    // Perform initial terminal reset
    if (ctx->has_tty && !ctx->render_file) {
      (void)terminal_reset(STDOUT_FILENO);
      (void)terminal_clear_screen();
      (void)terminal_cursor_home(STDOUT_FILENO);
      (void)terminal_clear_scrollback(STDOUT_FILENO);
      (void)terminal_cursor_show();
      if (!ctx->snapshot_mode) {
        (void)terminal_cursor_hide();
      }
      (void)terminal_flush(STDOUT_FILENO);
    }
  }

  // Output routing logic
  bool use_tty_control = ctx->has_tty && (!ctx->snapshot_mode || terminal_is_interactive());

  START_TIMER("frame_write");

  if (use_tty_control) {
    // TTY mode: Buffer cursor control + frame data together for atomic frame display
    const char *cursor_home_sequence = "\033[H\033[3J"; // 7 bytes total
    size_t cursor_seq_len = 7;
    size_t total_size = cursor_seq_len + frame_len;

    char *frame_buffer = SAFE_MALLOC(total_size, char *);
    if (frame_buffer) {
      memcpy(frame_buffer, cursor_home_sequence, cursor_seq_len);
      memcpy(frame_buffer + cursor_seq_len, display_frame, frame_len);

      log_debug("FRAME_WRITE_TTY: Writing %zu bytes (cursor=%zu + frame=%zu) to stdout", total_size, cursor_seq_len,
                frame_len);
      ssize_t written = platform_write_all(STDOUT_FILENO, frame_buffer, total_size);
      log_debug("FRAME_WRITE_TTY: Wrote %zd bytes (requested %zu)", written, total_size);

      // Start snapshot timer on first ASCII frame rendered
      if (GET_OPTION(snapshot_mode) && !g_snapshot_first_frame_rendered) {
        g_snapshot_first_frame_rendered = true;
        g_snapshot_first_frame_rendered_ns = time_get_ns();
        log_info("SNAPSHOT: FIRST ASCII FRAME RENDERED (write_ascii) - Timer started");
      }

      // Tick FPS counter
      if (ctx->fps_counter) {
        fps_counter_tick(ctx->fps_counter);
      }

      SAFE_FREE(frame_buffer);
    }

    // Flush buffers
    log_debug("FRAME_FLUSH: Flushing stdout");
    (void)fflush(stdout);
    int flush_fd = (ctx->tty_info.fd >= 0) ? ctx->tty_info.fd : STDOUT_FILENO;
    (void)terminal_flush(flush_fd);

    // Render FPS counter overlay if enabled
    if (ctx->fps_counter && GET_OPTION(fps_counter)) {
      session_display_render_fps_overlay(ctx);
    }
  } else if (terminal_is_interactive()) {
    // Piped to interactive terminal: output ASCII frames with newline
    char *write_buf = SAFE_MALLOC(frame_len + 1, char *);
    if (write_buf) {
      memcpy(write_buf, display_frame, frame_len);
      write_buf[frame_len] = '\n';
      (void)platform_write_all(STDOUT_FILENO, write_buf, frame_len + 1);
      SAFE_FREE(write_buf);
    }

    // Flush buffers
    (void)fflush(stdout);
    int flush_fd = (ctx->tty_info.fd >= 0) ? ctx->tty_info.fd : STDOUT_FILENO;
    (void)terminal_flush(flush_fd);
  } else {
    // Non-interactive piped output
    // BUT: Don't write ASCII frames to stdout if render_file is using stdout (--render-file="-")
    if (!ctx->render_file) {
      // No render-file, safe to write ASCII frames to stdout
      char *write_buf = SAFE_MALLOC(frame_len + 1, char *);
      if (write_buf) {
        memcpy(write_buf, display_frame, frame_len);
        write_buf[frame_len] = '\n';
        (void)platform_write_all(STDOUT_FILENO, write_buf, frame_len + 1);

        // Start snapshot timer on first ASCII frame rendered
        if (GET_OPTION(snapshot_mode) && !g_snapshot_first_frame_rendered) {
          g_snapshot_first_frame_rendered = true;
          g_snapshot_first_frame_rendered_ns = time_get_ns();
          log_info("SNAPSHOT: FIRST ASCII FRAME RENDERED (write_ascii piped) - Timer started");
        }

        SAFE_FREE(write_buf);
      }

      // Flush buffers
      (void)fflush(stdout);
      int flush_fd = (ctx->tty_info.fd >= 0) ? ctx->tty_info.fd : STDOUT_FILENO;
      (void)terminal_flush(flush_fd);
    } else {
      // render_file is using stdout: skip ASCII output to avoid mixing with video
      if (GET_OPTION(snapshot_mode) && !g_snapshot_first_frame_rendered) {
        g_snapshot_first_frame_rendered = true;
        g_snapshot_first_frame_rendered_ns = time_get_ns();
        log_info("SNAPSHOT: FIRST FRAME RENDERED (render_file piped) - Timer started");
      }
    }
  }

  STOP_TIMER_AND_LOG_EVERY(dev, 3 * NS_PER_SEC_INT, 5 * NS_PER_MS_INT, "frame_write",
                           "FRAME_WRITE: Write and flush complete (%.2f ms)");

  // Clean up digital rain result if allocated
  if (rain_result) {
    SAFE_FREE(rain_result);
  }
}

void session_display_encode_frame(session_display_ctx_t *ctx, const image_t *image, uint64_t captured_ns) {
  static int call_count = 0;
  if (call_count++ < 5) {
    log_info("session_display_encode_frame: CALLED (ctx=%p, image=%p, captured_ns=%llu, ctx->render_file=%p)",
             (void *)ctx, (void *)image, (unsigned long long)captured_ns, ctx ? (void *)ctx->render_file : NULL);
  }

  if (!ctx || !ctx->initialized) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Display context is NULL or uninitialized");
    return;
  }

  if (!image) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Image is NULL");
    return;
  }

  // Only encode if render_file is active
  if (!ctx->render_file) {
    log_warn_every(10 * NS_PER_SEC_INT, "session_display_encode_frame: render_file is NULL, skipping encode");
    return;
  }

  // Convert image to ASCII
  char *ascii = session_display_convert_to_ascii(ctx, image);
  if (!ascii) {
    log_warn("session_display_encode_frame: Failed to convert image to ASCII");
    return;
  }

  // Write ASCII to render-file encoder
#ifndef _WIN32
  asciichat_error_t fe = render_file_write_frame(ctx->render_file, ascii, captured_ns);
  if (fe != ASCIICHAT_OK) {
    log_warn_every(5 * NS_PER_SEC_INT, "render-file: encode failed (%s)", asciichat_error_string(fe));
  }
#endif

  SAFE_FREE(ascii);
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

void session_display_render_fps_overlay(session_display_ctx_t *ctx) {
  if (!ctx || !ctx->fps_counter || !ctx->initialized) {
    return;
  }

  // Only render overlay in TTY mode
  if (!ctx->has_tty) {
    return;
  }

  // Get current FPS value from counter
  float fps = fps_counter_get(ctx->fps_counter);

  // Get terminal width for right-alignment
  int term_cols = (int)terminal_get_effective_width();

  // Position at row 1, right side of screen
  // Column = term_cols - 7 (to fit "FPS:144" which is 7 chars)
  // Use reverse video (\033[7m) for visibility, then reset (\033[0m)
  char overlay[64];
  int overlay_len = snprintf(overlay, sizeof(overlay), "\033[1;%dH\033[7mFPS:%3.0f\033[0m", term_cols - 6, fps);

  if (overlay_len > 0 && overlay_len < (int)sizeof(overlay)) {
    // Write overlay to TTY
    int fd = (ctx->has_tty && ctx->tty_info.fd >= 0) ? ctx->tty_info.fd : STDOUT_FILENO;
    (void)platform_write_all(fd, overlay, overlay_len);

    // Flush to ensure overlay appears immediately
    (void)terminal_flush(fd);
  }
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
    (void)terminal_cursor_show();
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
    uint32_t write_idx = (uint32_t)atomic_load_u64(&rb->write_index);
    uint32_t read_idx = (uint32_t)atomic_load_u64(&rb->read_index);

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
    atomic_store_u64(&rb->write_index, (write_idx + num_samples) % AUDIO_RING_BUFFER_SIZE);

    return ASCIICHAT_OK;
  }

  return ASCIICHAT_OK;
}

/* ============================================================================
 * Keyboard Help Functions (from keyboard_help module)
 * ============================================================================ */

/**
 * @brief Toggle keyboard help on/off (implemented in display.c for struct access)
 */
void keyboard_help_toggle(session_display_ctx_t *ctx) {
  if (!ctx) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Session display context is NULL");
    return;
  }

  // Disable help in snapshot mode only - allow help toggle even if stdin/stdout aren't TTYs
  // (the display context tracks has_tty independently)
  if (GET_OPTION(snapshot_mode)) {
    return;
  }

  bool current = atomic_load_bool(&ctx->keyboard_help_active);
  atomic_store_bool(&ctx->keyboard_help_active, !current);
}

/**
 * @brief Check if keyboard help is currently active (implemented in display.c for struct access)
 */
bool keyboard_help_is_active(session_display_ctx_t *ctx) {
  if (!ctx) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Session display context is NULL");
    return false;
  }

  // Disable help in snapshot mode only - allow help state check even if stdin/stdout aren't TTYs
  if (GET_OPTION(snapshot_mode)) {
    return false;
  }

  return atomic_load_bool(&ctx->keyboard_help_active);
}

void session_display_set_snapshot_actual_duration(session_display_ctx_t *ctx, double actual_duration_sec) {
  log_info("session_display_set_snapshot_actual_duration: CALLED with duration=%.3f, ctx=%p, render_file=%p",
           actual_duration_sec, (void *)ctx, ctx ? (void *)ctx->render_file : NULL);

  if (!ctx) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Session display context is NULL");
    return;
  }

  if (!ctx->render_file) {
    log_warn("  -> render_file is NULL, cannot set duration");
    return; // No render_file context, nothing to do
  }

#ifndef _WIN32
  // Pass the actual duration to the render_file module, which passes it to the encoder
  if (ctx->render_file) {
    log_info("  -> Passing duration %.3f to render_file encoder", actual_duration_sec);
    render_file_set_snapshot_actual_duration(ctx->render_file, actual_duration_sec);
  }
#endif
}
