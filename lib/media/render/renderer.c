#include <ascii-chat/util/time.h>
#include <ascii-chat/options/colorscheme.h>
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
#include <ascii-chat/tui/render_progress.h>
#include <ascii-chat/tui/controller.h>
#include <ascii-chat/audio/recording.h>
#include <ascii-chat/media/kitty/image.h>

#define RENDER_AUTO_FONT_SIZE_PT 12.0
#define RENDER_AUTO_VIDEO_MAX_DIMENSION 1280
#define RENDER_AUTO_MIN_FONT_SIZE_PT 3.0

/**
 * @brief For render file ctx, this is the live record that ties `progress`, `error`, `renderer`, `encoder` together.
 * The owning code creates it, updates it while work is active, and releases its handles and pointed-to storage before
 * freeing the record.
 */
struct render_file_ctx_s {
  render_progress_t *progress;
  asciichat_error_t error;
  terminal_renderer_t *renderer;
  ffmpeg_encoder_t *encoder;
  audio_recording_t *recording;
  uint64_t frames_written;
  uint64_t audio_samples_written;
  uint64_t first_frame_ns;
  uint64_t last_frame_index;
  uint64_t audio_target;
  bool live_timing;
  bool kitty;
  bool kitty_only;
  bool discard;
  uint32_t audio_sample_rate; // 48000 Hz
  int fps;
  float *audio_read_buf; // Temporary buffer for reading audio samples
  int audio_buf_size;    // Size of audio_read_buf
};

asciichat_error_t render_file_create(const char *output_path, int cols, int rows, int fps, int theme,
                                     render_file_ctx_t **out) {
  if (!output_path || !out || cols <= 0 || rows <= 0)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing render output, grid, or context");
  *out = NULL;
  log_info("output_path=%s, cols=%d, rows=%d, fps=%d, theme=%d, out=%p", output_path ? output_path : "(null)", cols,
           rows, fps, theme, (void *)out);
  render_file_ctx_t *ctx = SAFE_CALLOC(1, sizeof(*ctx), render_file_ctx_t *);
  if (strcmp(output_path, "/dev/null") == 0) {
    ctx->discard = true;
    ctx->error = ASCIICHAT_OK;
    *out = ctx;
    return ASCIICHAT_OK;
  }
  ctx->kitty_only = strcmp(output_path, "--kitty") == 0;
  ctx->kitty = ctx->kitty_only || GET_OPTION(render_kitty);
  if (ctx->kitty && !GET_OPTION(snapshot_mode)) {
    SAFE_FREE(ctx);
    return SET_ERRNO(ERROR_INVALID_PARAM, "Kitty graphics output requires --snapshot");
  }
  if (ctx->kitty && (strcmp(output_path, "-") == 0 || strcmp(output_path, "pipe:") == 0)) {
    SAFE_FREE(ctx);
    return SET_ERRNO(ERROR_INVALID_PARAM, "Kitty graphics and binary media cannot share stdout");
  }
  const char *term = SAFE_GETENV("TERM");
  if (ctx->kitty && terminal_is_stdout_tty() &&
      (!term || strcmp(term, "dumb") == 0 || strcmp(term, "linux") == 0 || strcmp(term, "vt100") == 0)) {
    SAFE_FREE(ctx);
    return SET_ERRNO(ERROR_NOT_SUPPORTED,
                     "Kitty graphics require a capable terminal; redirect stdout to export the protocol");
  }

  // Resolve font spec → platform-appropriate file path, family name, or bundled data.
  char font_spec[PLATFORM_MAX_PATH_LENGTH] = {0};
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
    log_debug("[MATRIX] Using matrix font (enabled by --matrix flag)");
  }

  asciichat_error_t fe =
      platform_font_resolve(raw_font, font_spec, sizeof(font_spec), &font_is_path, &font_data, &font_data_size);
  if (fe != ASCIICHAT_OK) {
    if (raw_font && raw_font[0]) {
      SAFE_FREE(ctx);
      return fe;
    }
    log_warn("font resolution failed for '%s' — using bundled default", raw_font ? raw_font : "(explicit system)");
    fe = platform_font_resolve("default", font_spec, sizeof(font_spec), &font_is_path, &font_data, &font_data_size);
    if (fe != ASCIICHAT_OK) {
      SAFE_FREE(ctx);
      return fe;
    }
  }

  log_debug("Font resolved: font_spec='%s', font_is_path=%d, font_data=%p (size=%zu)", font_spec, font_is_path,
            (void *)font_data, font_data_size);

  // A zero size selects a compact default for video; explicit font sizes keep
  // their exact dimensions. Images and Kitty snapshots retain the 12 pt default.
  double font_size_pt = GET_OPTION(render_font_size);
  const char *extension = strrchr(output_path, '.');
  bool is_image =
      extension && (!strcmp(extension, ".png") || !strcmp(extension, ".jpg") || !strcmp(extension, ".jpeg") ||
                    !strcmp(extension, ".bmp") || !strcmp(extension, ".webp"));
  bool automatic_video_size = font_size_pt == 0.0 && !is_image && !ctx->kitty;
  if (font_size_pt == 0.0) {
    font_size_pt = RENDER_AUTO_FONT_SIZE_PT;
    if (automatic_video_size) {
      double width_scale = (double)RENDER_AUTO_VIDEO_MAX_DIMENSION / ((double)cols * 10.0);
      double height_scale = (double)RENDER_AUTO_VIDEO_MAX_DIMENSION / ((double)rows * 20.0);
      double scale = width_scale < height_scale ? width_scale : height_scale;
      if (scale < 1.0)
        font_size_pt *= scale;
      if (font_size_pt < RENDER_AUTO_MIN_FONT_SIZE_PT)
        font_size_pt = RENDER_AUTO_MIN_FONT_SIZE_PT;
    }
  }
  term_renderer_theme_t resolved_theme = (term_renderer_theme_t)theme;
  if (resolved_theme == TERM_RENDERER_THEME_AUTO) {
    resolved_theme =
        colorscheme_current_background() == TERM_BACKGROUND_DARK ? TERM_RENDERER_THEME_DARK : TERM_RENDERER_THEME_LIGHT;
    log_debug("auto theme resolved to %s", resolved_theme == TERM_RENDERER_THEME_DARK ? "dark" : "light");
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
    log_error("term_renderer_create failed: %s", asciichat_error_format(NULL, err));
    SAFE_FREE(ctx);
    return err;
  }
  int actual_width_px = term_renderer_width_px(ctx->renderer);
  int actual_height_px = term_renderer_height_px(ctx->renderer);
  // Hinting and font metrics vary. Recalculate once if the initial estimate
  // exceeds the target, retaining the original renderer if a bitmap font
  // cannot be scaled down.
  if (automatic_video_size &&
      (actual_width_px > RENDER_AUTO_VIDEO_MAX_DIMENSION || actual_height_px > RENDER_AUTO_VIDEO_MAX_DIMENSION) &&
      font_size_pt > RENDER_AUTO_MIN_FONT_SIZE_PT) {
    double width_scale = (double)RENDER_AUTO_VIDEO_MAX_DIMENSION / actual_width_px;
    double height_scale = (double)RENDER_AUTO_VIDEO_MAX_DIMENSION / actual_height_px;
    double scale = width_scale < height_scale ? width_scale : height_scale;
    double smaller = font_size_pt * scale * 0.98;
    if (smaller < RENDER_AUTO_MIN_FONT_SIZE_PT)
      smaller = RENDER_AUTO_MIN_FONT_SIZE_PT;
    tr_cfg.font_size_pt = smaller;
    terminal_renderer_t *candidate = NULL;
    err = term_renderer_create(&tr_cfg, &candidate);
    if (err == ASCIICHAT_OK) {
      int candidate_width = term_renderer_width_px(candidate);
      int candidate_height = term_renderer_height_px(candidate);
      if (candidate_width <= actual_width_px && candidate_height <= actual_height_px &&
          (candidate_width < actual_width_px || candidate_height < actual_height_px)) {
        term_renderer_destroy(ctx->renderer);
        ctx->renderer = candidate;
        actual_width_px = candidate_width;
        actual_height_px = candidate_height;
      } else {
        term_renderer_destroy(candidate);
      }
    } else {
      term_renderer_destroy(ctx->renderer);
      SAFE_FREE(ctx);
      return err;
    }
  }
  log_info("GRID DIMENSIONS: %ux%u cells -> PIXEL DIMENSIONS: %dx%d px", cols, rows, actual_width_px, actual_height_px);
  log_debug("term_renderer created (%dx%d cells, %dx%d px)", cols, rows, actual_width_px, actual_height_px);

  if (ctx->kitty_only) {
    ctx->fps = fps;
    ctx->progress = render_progress_create(true, "terminal");
    NAMED_REGISTER_CONTEXT(ctx, "render_file_ctx", output_path, NULL);
    ui_controller_title_render(UI_TITLE_RENDERING);
    *out = ctx;
    return ASCIICHAT_OK;
  }
  int output_width_px = actual_width_px;
  int output_height_px = actual_height_px;
  if (automatic_video_size &&
      (output_width_px > RENDER_AUTO_VIDEO_MAX_DIMENSION || output_height_px > RENDER_AUTO_VIDEO_MAX_DIMENSION)) {
    double scale_x = (double)RENDER_AUTO_VIDEO_MAX_DIMENSION / output_width_px;
    double scale_y = (double)RENDER_AUTO_VIDEO_MAX_DIMENSION / output_height_px;
    double scale = scale_x < scale_y ? scale_x : scale_y;
    output_width_px = ((int)(output_width_px * scale)) & ~1;
    output_height_px = ((int)(output_height_px * scale)) & ~1;
    if (output_width_px < 2)
      output_width_px = 2;
    if (output_height_px < 2)
      output_height_px = 2;
  }
  err = ffmpeg_encoder_create_scaled(output_path, actual_width_px, actual_height_px, output_width_px, output_height_px,
                                     fps, &ctx->encoder);
  if (err != ASCIICHAT_OK) {
    log_error("ffmpeg_encoder_create failed: %s", asciichat_error_format(NULL, err));
    term_renderer_destroy(ctx->renderer);
    SAFE_FREE(ctx);
    return err;
  }
  log_debug("ffmpeg_encoder created successfully");

  // Initialize audio fields
  ctx->audio_sample_rate = 48000; // Audio pipeline is 48kHz
  ctx->fps = fps;
  // Allocate large buffer for snapshot mode (5 seconds at 48kHz with ~14 FPS = 3,428 samples per frame)
  // Use 8192 to comfortably handle snapshot reads without excessive overhead
  ctx->audio_buf_size = 8192; // Temporary buffer for reading audio (in floats)
  ctx->audio_read_buf = SAFE_MALLOC(ctx->audio_buf_size * sizeof(float), float *);

  err = audio_recording_create_channels(&ctx->recording, 2);
  if (err != ASCIICHAT_OK) {
    ffmpeg_encoder_destroy(ctx->encoder);
    term_renderer_destroy(ctx->renderer);
    SAFE_FREE(ctx->audio_read_buf);
    SAFE_FREE(ctx);
    return err;
  }
  log_info("initialized encoder for %s", output_path);

  // Register render file context for debugging
  NAMED_REGISTER_CONTEXT(ctx, "render_file_ctx", output_path, NULL);

  bool binary_stdout = strcmp(output_path, "-") == 0 || strcmp(output_path, "pipe:") == 0;
  ctx->progress = render_progress_create(!binary_stdout, output_path);
  ui_controller_title_render(UI_TITLE_RENDERING);
  *out = ctx;
  return ASCIICHAT_OK;
}

void render_file_set_live_timing(render_file_ctx_t *ctx) {
  if (ctx) {
    ctx->live_timing = true;
    if (ctx->encoder)
      ffmpeg_encoder_set_live_timing(ctx->encoder);
  }
}

static asciichat_error_t render_file_write_frame_internal(render_file_ctx_t *ctx, const char *ansi_frame,
                                                          uint64_t captured_ns) {

  if (!ctx) {
    log_warn("ctx is NULL");
    return ASCIICHAT_OK;
  }

  if (!ansi_frame) {
    log_warn("ansi_frame is NULL");
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
  uint64_t render_started_ns = time_get_ns();
  asciichat_error_t err = term_renderer_feed(ctx->renderer, ansi_frame, frame_len);
  uint64_t rasterized_ns = time_get_ns();
  if (err != ASCIICHAT_OK) {
    log_warn("term_renderer_feed failed: %s", asciichat_error_format(NULL, err));
    return err;
  }
  if (ctx->kitty_only) {
    ctx->frames_written++;
    return ASCIICHAT_OK;
  }

  const uint8_t *pixels = term_renderer_pixels(ctx->renderer);
  int pitch = term_renderer_pitch(ctx->renderer);
  // sws_scale consumes the renderer's pixels synchronously before returning.
  // The renderer is owned by this encoder thread and is not updated until the
  // next call, so copying the entire RGBA frame is unnecessary.
  err = ffmpeg_encoder_write_frame(ctx->encoder, pixels, pitch, captured_ns);
  uint64_t encoded_ns = time_get_ns();
  log_debug_every(US_PER_SEC_INT, "RECORD_STAGE: raster_ms=%.2f encode_ms=%.2f bytes=%zu",
                  (double)(rasterized_ns - render_started_ns) / NS_PER_MS_INT,
                  (double)(encoded_ns - rasterized_ns) / NS_PER_MS_INT, frame_len);
  if (err != ASCIICHAT_OK) {
    log_warn("ffmpeg_encoder_write_frame failed: %s", asciichat_error_format(NULL, err));
  }

  if (err == ASCIICHAT_OK) {
    if (ctx->frames_written == 0) {
      ctx->first_frame_ns = captured_ns;
      audio_recording_start(ctx->recording, captured_ns);
    }
    ctx->frames_written++;
    ctx->last_frame_index = frame_index;
    uint64_t target =
        ((ctx->live_timing ? frame_index + 1 : ctx->frames_written) * ctx->audio_sample_rate / ctx->fps) * 2;
    ctx->audio_target = target;
    /* Keep 100 ms pending so audio callbacks can deliver samples before encoding. */
    uint64_t ready = target > 9600 ? target - 9600 : 0;
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

asciichat_error_t render_file_write_frame(render_file_ctx_t *ctx, const char *frame, uint64_t captured_ns) {
  if (!ctx || !frame)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing render context or frame");
  if (ctx->error != ASCIICHAT_OK)
    return ctx->error;
  if (ctx->discard)
    return ASCIICHAT_OK;
  asciichat_error_t err = render_file_write_frame_internal(ctx, frame, captured_ns);
  if (err != ASCIICHAT_OK)
    ui_controller_title_render(UI_TITLE_FAILED);
  render_progress_frame(ctx->progress, frame, err == ASCIICHAT_OK);
  if (err != ASCIICHAT_OK)
    ctx->error = err;
  return err;
}

void render_file_begin_drain(render_file_ctx_t *ctx, uint64_t total, bool total_known) {
  if (ctx)
    render_progress_begin(ctx->progress, total, total_known);
}

void render_file_set_snapshot_actual_duration(render_file_ctx_t *ctx, double actual_duration_sec) {
  if (!ctx || !ctx->encoder)
    return;
  ffmpeg_encoder_set_snapshot_actual_duration(ctx->encoder, actual_duration_sec);
}

asciichat_error_t render_file_destroy(render_file_ctx_t *ctx) {
  if (!ctx)
    return ASCIICHAT_OK;
  if (ctx->discard) {
    SAFE_FREE(ctx);
    return ASCIICHAT_OK;
  }
  NAMED_UNREGISTER(ctx);
  ui_controller_title_render(ctx->error == ASCIICHAT_OK ? UI_TITLE_FINALIZING : UI_TITLE_FAILED);
  render_progress_finalize(ctx->progress);
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
  if (ctx->error != ASCIICHAT_OK)
    err = ctx->error;
  render_progress_destroy(ctx->progress);
  if (err == ASCIICHAT_OK && ctx->kitty && ctx->frames_written == 0)
    err = SET_ERRNO(ERROR_MEDIA_INIT, "No snapshot was rendered for Kitty output");
  if (err == ASCIICHAT_OK && ctx->kitty && ctx->frames_written > 0)
    err = kitty_image_write(stdout, term_renderer_pixels(ctx->renderer), term_renderer_width_px(ctx->renderer),
                            term_renderer_height_px(ctx->renderer), term_renderer_pitch(ctx->renderer));
  ui_controller_title_render(err == ASCIICHAT_OK ? UI_TITLE_IDLE : UI_TITLE_FAILED);
  term_renderer_destroy(ctx->renderer);
  SAFE_FREE(ctx->audio_read_buf);
  SAFE_FREE(ctx);
  return err;
}
