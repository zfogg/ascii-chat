/**
 * @file media/render/renderer.c
 * @ingroup media
 * @brief render_file_* — ties together font resolution, platform renderer, FFmpeg encoder
 */
#include <ascii-chat/media/render/renderer.h>
#include <ascii-chat/media/ffmpeg_encoder.h>
#include <ascii-chat/platform/font.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/memory.h>
#include <ascii-chat/debug/named.h>
#include <string.h>
#include <ascii-chat/audio/recording.h>

struct render_file_ctx_s {
  terminal_renderer_t *renderer;
  ffmpeg_encoder_t *encoder;
  audio_recording_t *recording;
  uint64_t frames_written;
  uint64_t audio_samples_written;
  uint64_t first_frame_ns;
  uint64_t last_frame_index;
  uint64_t audio_target;
  bool live_timing;
  uint32_t audio_sample_rate; // 48000 Hz
  int fps;
  float *audio_read_buf; // Temporary buffer for reading audio samples
  int audio_buf_size;    // Size of audio_read_buf
};

asciichat_error_t render_file_create(const char *output_path, int cols, int rows, int fps, int theme,
                                     render_file_ctx_t **out) {
  log_info("[RENDER_FILE_CREATE] CALLED with output_path=%s, cols=%d, rows=%d, fps=%d, theme=%d, out=%p",
           output_path ? output_path : "(null)", cols, rows, fps, theme, (void *)out);
  render_file_ctx_t *ctx = SAFE_CALLOC(1, sizeof(*ctx), render_file_ctx_t *);

  // Resolve font spec → platform-appropriate file path, family name, or bundled data.
  char font_spec[512] = {0};
  bool font_is_path = false;
  const uint8_t *font_data = NULL;
  size_t font_data_size = 0;
  const char *raw_font = GET_OPTION(render_font);

  // Log the font option value from GET_OPTION (for WASM font debugging)
  log_info("[WASM_FONT_GETTER] GET_OPTION(render_font) returned: '%s' (empty=%d), GET_OPTION(matrix_rain)=%d",
           raw_font ? raw_font : "(null)", (!raw_font || raw_font[0] == '\0') ? 1 : 0, GET_OPTION(matrix_rain));

  // Auto-select matrix font when --matrix flag is set, unless user explicitly overrides with --render-font
  log_debug("raw_font=%s, matrix_rain=%d", raw_font, GET_OPTION(matrix_rain));
  if ((!raw_font || raw_font[0] == '\0') && GET_OPTION(matrix_rain)) {
    raw_font = "matrix";
    log_debug("render_file_create: [MATRIX] Using matrix font (enabled by --matrix flag)");
  }

  asciichat_error_t fe =
      platform_font_resolve(raw_font, font_spec, sizeof(font_spec), &font_is_path, &font_data, &font_data_size);
  if (fe != ASCIICHAT_OK) {
    log_warn("renderer: font resolution failed for '%s' — using bundled default",
             raw_font ? raw_font : "(explicit system)");
    fe = platform_font_resolve("default", font_spec, sizeof(font_spec), &font_is_path, &font_data, &font_data_size);
    if (fe != ASCIICHAT_OK) {
      SAFE_FREE(ctx);
      return fe;
    }
  }

  log_debug("render_file_create: Font resolved: font_spec='%s', font_is_path=%d, font_data=%p (size=%zu)", font_spec,
            font_is_path, (void *)font_data, font_data_size);

  // Use reasonable default font size for render-file quality
  double font_size_pt = GET_OPTION(render_font_size);
  term_renderer_theme_t resolved_theme = (term_renderer_theme_t)theme;
  if (resolved_theme == TERM_RENDERER_THEME_AUTO) {
    resolved_theme = terminal_has_dark_background() ? TERM_RENDERER_THEME_DARK : TERM_RENDERER_THEME_LIGHT;
    log_debug("render_file_create: auto theme resolved to %s",
              resolved_theme == TERM_RENDERER_THEME_DARK ? "dark" : "light");
  }

  term_renderer_config_t tr_cfg = {
      .cols = cols,
      .rows = rows,
      .font_size_pt = font_size_pt,
      .theme = resolved_theme,
      .font_is_path = font_is_path,
      .font_data = font_data,
      .font_data_size = font_data_size,
  };
  SAFE_STRNCPY(tr_cfg.font_spec, font_spec, sizeof(tr_cfg.font_spec));

  asciichat_error_t err = term_renderer_create(&tr_cfg, &ctx->renderer);
  if (err != ASCIICHAT_OK) {
    log_error("render_file_create: term_renderer_create failed: %s", asciichat_error_string(err));
    SAFE_FREE(ctx);
    return err;
  }
  int actual_width_px = term_renderer_width_px(ctx->renderer);
  int actual_height_px = term_renderer_height_px(ctx->renderer);
  log_info("render_file_create: GRID DIMENSIONS: %ux%u cells -> PIXEL DIMENSIONS: %dx%d px", cols, rows,
           actual_width_px, actual_height_px);
  log_debug("render_file_create: term_renderer created (%dx%d cells, %dx%d px)", cols, rows, actual_width_px,
            actual_height_px);

  err = ffmpeg_encoder_create(output_path, actual_width_px, actual_height_px, fps, &ctx->encoder);
  if (err != ASCIICHAT_OK) {
    log_error("render_file_create: ffmpeg_encoder_create failed: %s", asciichat_error_string(err));
    term_renderer_destroy(ctx->renderer);
    SAFE_FREE(ctx);
    return err;
  }
  log_debug("render_file_create: ffmpeg_encoder created successfully");

  // Initialize audio fields
  ctx->audio_sample_rate = 48000; // Audio pipeline is 48kHz
  ctx->fps = fps;
  // Allocate large buffer for snapshot mode (5 seconds at 48kHz with ~14 FPS = 3,428 samples per frame)
  // Use 8192 to comfortably handle snapshot reads without excessive overhead
  ctx->audio_buf_size = 8192; // Temporary buffer for reading audio (in floats)
  ctx->audio_read_buf = SAFE_MALLOC(ctx->audio_buf_size * sizeof(float), float *);

  err = audio_recording_create(&ctx->recording);
  if (err != ASCIICHAT_OK) {
    ffmpeg_encoder_destroy(ctx->encoder);
    term_renderer_destroy(ctx->renderer);
    SAFE_FREE(ctx->audio_read_buf);
    SAFE_FREE(ctx);
    return err;
  }
  log_info("renderer: initialized encoder for %s", output_path);

  // Register render file context for debugging
  NAMED_REGISTER_CONTEXT(ctx, "render_file_ctx", output_path, NULL);

  *out = ctx;
  return ASCIICHAT_OK;
}

void render_file_set_live_timing(render_file_ctx_t *ctx) {
  if (ctx) {
    ctx->live_timing = true;
    ffmpeg_encoder_set_live_timing(ctx->encoder);
  }
}

asciichat_error_t render_file_write_frame(render_file_ctx_t *ctx, const char *ansi_frame, uint64_t captured_ns) {

  log_info("render_file_write_frame: CALLED - ctx=%p, captured_ns=%llu", (void *)ctx, (unsigned long long)captured_ns);

  if (!ctx) {
    log_warn("render_file_write_frame: ctx is NULL");
    return ASCIICHAT_OK;
  }

  if (!ansi_frame) {
    log_warn("render_file_write_frame: ansi_frame is NULL");
    return ASCIICHAT_OK;
  }

  size_t frame_len = strlen(ansi_frame);
  uint64_t frame_index = ctx->frames_written;
  if (ctx->live_timing && ctx->frames_written > 0) {
    uint64_t elapsed = captured_ns > ctx->first_frame_ns ? captured_ns - ctx->first_frame_ns : 0;
    frame_index = elapsed / 1000000000ULL * ctx->fps + elapsed % 1000000000ULL * ctx->fps / 1000000000ULL;
    if (frame_index <= ctx->last_frame_index)
      return ASCIICHAT_OK;
  }
  log_info("render_file_write_frame: processing frame (len=%zu)", frame_len);
  if (frame_len > 0) {
    log_info("  first 100 chars: %.100s", ansi_frame);
  }

  asciichat_error_t err = term_renderer_feed(ctx->renderer, ansi_frame, frame_len);
  if (err != ASCIICHAT_OK) {
    log_warn("render_file_write_frame: term_renderer_feed failed: %s", asciichat_error_string(err));
    return err;
  }

  const uint8_t *pixels = term_renderer_pixels(ctx->renderer);
  int pitch = term_renderer_pitch(ctx->renderer);
  int width_px = term_renderer_width_px(ctx->renderer);
  int height_px = term_renderer_height_px(ctx->renderer);

  log_info("render_file_write_frame: pixels=%p pitch=%d dims=%dx%d", (void *)pixels, pitch, width_px, height_px);

  if (pixels) {
    uint8_t sample_r = pixels[0], sample_g = pixels[1], sample_b = pixels[2], sample_a = pixels[3];
    size_t mid_offset = (size_t)(height_px / 2) * (size_t)pitch + (size_t)(width_px / 2) * 4;
    uint8_t sample_r_mid = pixels[mid_offset], sample_g_mid = pixels[mid_offset + 1],
            sample_b_mid = pixels[mid_offset + 2], sample_a_mid = pixels[mid_offset + 3];

    log_info("  pixel[0,0]: RGBA(%u,%u,%u,%u), pixel[%d,%d]: RGBA(%u,%u,%u,%u)", sample_r, sample_g, sample_b, sample_a,
             width_px / 2, height_px / 2, sample_r_mid, sample_g_mid, sample_b_mid, sample_a_mid);
  }

  // term_renderer_pixels() returns a pointer to the renderer's internal buffer which gets
  // overwritten on the next frame. We must copy the data before passing to the encoder.
  uint8_t *pixels_copy = NULL;
  if (pixels) {
    size_t pixel_buffer_size = (size_t)pitch * (size_t)height_px;
    pixels_copy = SAFE_MALLOC(pixel_buffer_size, uint8_t *);
    memcpy(pixels_copy, pixels, pixel_buffer_size);
    log_info("render_file_write_frame: copied %zu bytes of pixel data (was at %p, now at %p)", pixel_buffer_size,
             (void *)pixels, (void *)pixels_copy);
  }

  err = ffmpeg_encoder_write_frame(ctx->encoder, pixels_copy, pitch, captured_ns);
  if (err != ASCIICHAT_OK) {
    log_warn("render_file_write_frame: ffmpeg_encoder_write_frame failed: %s", asciichat_error_string(err));
  }

  // Free the pixel copy (ffmpeg_encoder_write_frame reads and converts it immediately)
  SAFE_FREE(pixels_copy);

  if (err == ASCIICHAT_OK) {
    if (ctx->frames_written == 0) {
      ctx->first_frame_ns = captured_ns;
      audio_recording_start(ctx->recording, captured_ns);
    }
    ctx->frames_written++;
    ctx->last_frame_index = frame_index;
    uint64_t target = (ctx->live_timing ? frame_index + 1 : ctx->frames_written) * ctx->audio_sample_rate / ctx->fps;
    ctx->audio_target = target;
    /* Keep 100 ms pending so audio callbacks can deliver samples before encoding. */
    uint64_t ready = target > 4800 ? target - 4800 : 0;
    while (ctx->audio_samples_written < ready) {
      int count = (int)((ready - ctx->audio_samples_written) > (uint64_t)ctx->audio_buf_size
                            ? (uint64_t)ctx->audio_buf_size
                            : ready - ctx->audio_samples_written);
      audio_recording_read(ctx->recording, ctx->audio_read_buf, count);
      err = ffmpeg_encoder_write_audio(ctx->encoder, ctx->audio_read_buf, count);
      if (err != ASCIICHAT_OK)
        break;
      ctx->audio_samples_written += count;
    }
  }

  return err;
}

void render_file_set_snapshot_actual_duration(render_file_ctx_t *ctx, double actual_duration_sec) {
  if (!ctx || !ctx->encoder)
    return;
  ffmpeg_encoder_set_snapshot_actual_duration(ctx->encoder, actual_duration_sec);
}

asciichat_error_t render_file_destroy(render_file_ctx_t *ctx) {
  if (!ctx)
    return ASCIICHAT_OK;
  uint64_t target = ctx->audio_target;
  asciichat_error_t err = ASCIICHAT_OK;
  while (ctx->audio_samples_written < target) {
    int count = (int)((target - ctx->audio_samples_written) > (uint64_t)ctx->audio_buf_size
                          ? (uint64_t)ctx->audio_buf_size
                          : target - ctx->audio_samples_written);
    audio_recording_read(ctx->recording, ctx->audio_read_buf, count);
    err = ffmpeg_encoder_write_audio(ctx->encoder, ctx->audio_read_buf, count);
    if (err != ASCIICHAT_OK)
      break;
    ctx->audio_samples_written += count;
  }
  audio_recording_destroy(ctx->recording);
  asciichat_error_t close_err = ffmpeg_encoder_destroy(ctx->encoder);
  if (err == ASCIICHAT_OK)
    err = close_err;
  term_renderer_destroy(ctx->renderer);
  SAFE_FREE(ctx->audio_read_buf);
  SAFE_FREE(ctx);
  return err;
}
