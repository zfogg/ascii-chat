/**
 * @file media/render/renderer.c
 * @ingroup media
 * @brief render_file_* — ties together font resolution, platform renderer, FFmpeg encoder
 */
#ifndef _WIN32
#include <ascii-chat/media/render/renderer.h>
#include <ascii-chat/media/ffmpeg_encoder.h>
#include <ascii-chat/media/source.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/platform/font.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/memory.h>
#include <ascii-chat/debug/named.h>
#include <string.h>

struct render_file_ctx_s {
  terminal_renderer_t *renderer;
  ffmpeg_encoder_t *encoder;
  media_source_t *audio_media_source;    // for --file/--url audio
  audio_ring_buffer_t *audio_capture_rb; // for live mic capture
  uint32_t audio_sample_rate;            // 48000 Hz
  float *audio_read_buf;                 // Temporary buffer for reading audio samples
  int audio_buf_size;                    // Size of audio_read_buf
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

  // Auto-select matrix font when --matrix flag is set, unless user explicitly overrides with --render-font
  if ((!raw_font || raw_font[0] == '\0') && GET_OPTION(matrix_rain)) {
    raw_font = "matrix";
    log_debug("render_file_create: [MATRIX] Using matrix font (enabled by --matrix flag)");
  }

  // BEFORE resolving fonts, check if matrix font and disable it for render-file (causes glyph height=0)
  if (raw_font && strcmp(raw_font, "matrix") == 0) {
    log_warn("render_file_create: [MATRIX] Matrix font not supported for render-file output, using system font instead");
    raw_font = NULL;  // Force fallback to system font
  }

  asciichat_error_t fe =
      platform_font_resolve(raw_font, font_spec, sizeof(font_spec), &font_is_path, &font_data, &font_data_size);
  if (fe != ASCIICHAT_OK) {
    log_warn("renderer: font resolution failed for '%s' — using system default", raw_font ? raw_font : "(explicit system)");
  }

  log_debug("render_file_create: Font resolved: font_spec='%s', font_is_path=%d, font_data=%p (size=%zu)", font_spec,
            font_is_path, (void *)font_data, font_data_size);

  // Use reasonable default font size for render-file quality
  double font_size_pt = GET_OPTION(render_font_size);

  term_renderer_config_t tr_cfg = {
      .cols = cols,
      .rows = rows,
      .font_size_pt = font_size_pt,
      .theme = (term_renderer_theme_t)theme,
      .font_is_path = font_is_path,
      .font_data = font_data,
      .font_data_size = font_data_size,
  };
  strncpy(tr_cfg.font_spec, font_spec, sizeof(tr_cfg.font_spec) - 1);

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
  ctx->audio_sample_rate = 48000;  // Audio pipeline is 48kHz
  ctx->audio_buf_size = 1024;      // Temporary buffer for reading audio (in floats)
  ctx->audio_read_buf = SAFE_MALLOC(ctx->audio_buf_size * sizeof(float), float *);
  ctx->audio_media_source = NULL;
  ctx->audio_capture_rb = NULL;

  log_info("renderer: initialized encoder for %s", output_path);

  // Register render file context for debugging
  NAMED_REGISTER_CONTEXT(ctx, "render_file_ctx", output_path, NULL);

  *out = ctx;
  return ASCIICHAT_OK;
}

void render_file_set_audio_source(render_file_ctx_t *ctx, void *audio_media_source, void *audio_capture_rb) {
  if (!ctx) return;
  ctx->audio_media_source = (media_source_t *)audio_media_source;
  ctx->audio_capture_rb = (audio_ring_buffer_t *)audio_capture_rb;
  log_debug("render_file_set_audio_source: media_source=%p, capture_rb=%p", audio_media_source, audio_capture_rb);
}

asciichat_error_t render_file_write_frame(render_file_ctx_t *ctx, const char *ansi_frame) {
  // Write to debug file directly to bypass logging system
  FILE *dbg = fopen("/tmp/render-debug.txt", "a");
  if (dbg) {
    fprintf(dbg, "[RENDER_WRITE_FRAME] Called with ctx=%p, frame_len=%zu\n", (void *)ctx,
            ansi_frame ? strlen(ansi_frame) : 0);
    fclose(dbg);
  }
  log_info("render_file_write_frame: CALLED - ctx=%p", (void *)ctx);

  if (!ctx) {
    log_warn("render_file_write_frame: ctx is NULL");
    return ASCIICHAT_OK;
  }

  if (!ansi_frame) {
    log_warn("render_file_write_frame: ansi_frame is NULL");
    return ASCIICHAT_OK;
  }

  size_t frame_len = strlen(ansi_frame);
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

  // Check first few pixels to verify content
  if (pixels) {
    uint8_t sample_r = pixels[0], sample_g = pixels[1], sample_b = pixels[2];
    uint8_t sample_r_mid = pixels[(height_px / 2) * pitch], sample_g_mid = pixels[(height_px / 2) * pitch + 1],
            sample_b_mid = pixels[(height_px / 2) * pitch + 2];
    log_info("  pixel[0]: RGB(%u,%u,%u),  pixel[mid]: RGB(%u,%u,%u)", sample_r, sample_g, sample_b, sample_r_mid,
             sample_g_mid, sample_b_mid);
  }

  err = ffmpeg_encoder_write_frame(ctx->encoder, pixels, pitch);
  if (err != ASCIICHAT_OK) {
    log_warn("render_file_write_frame: ffmpeg_encoder_write_frame failed: %s", asciichat_error_string(err));
  }

  // Write synchronized audio for this frame if available
  // Calculate how many samples correspond to one video frame
  // Note: Audio samples per frame = 48000 Hz / FPS  (e.g., 1600 samples @ 30fps)
  if (ctx && ctx->audio_read_buf && ctx->encoder) {
    // Assume default 60 FPS if we can't determine it (typically matches capture rate)
    int fps = 60;
    int samples_per_frame = ctx->audio_sample_rate / fps;
    if (samples_per_frame > ctx->audio_buf_size) {
      samples_per_frame = ctx->audio_buf_size;
    }

    int samples_read = 0;
    static int audio_debug_once = 0;

    // Read from whichever audio source is available (prefer media source, fallback to capture)
    if (ctx->audio_media_source) {
      // Read from media source (file/URL audio)
      samples_read = media_source_read_audio(ctx->audio_media_source, ctx->audio_read_buf, samples_per_frame);
      if (samples_read < 0) {
        samples_read = 0;  // On error, write silence
      }
      if (!audio_debug_once++) {
        log_debug("render_file_write_frame: reading audio from media_source (samples_per_frame=%d, read=%d)",
                  samples_per_frame, samples_read);
      }
    } else if (ctx->audio_capture_rb) {
      // Read from ring buffer (live mic capture)
      samples_read =
          audio_ring_buffer_read(ctx->audio_capture_rb, ctx->audio_read_buf, samples_per_frame);
      if (samples_read < 0) {
        samples_read = 0;  // On error, write silence
      }
      if (!audio_debug_once++) {
        log_debug("render_file_write_frame: reading audio from capture_rb (samples_per_frame=%d, read=%d)",
                  samples_per_frame, samples_read);
      }
    } else {
      if (!audio_debug_once++) {
        log_debug("render_file_write_frame: no audio source available (media=%p, capture=%p)",
                  ctx->audio_media_source, ctx->audio_capture_rb);
      }
    }

    // Write audio samples if any were read
    if (samples_read > 0) {
      ffmpeg_encoder_write_audio(ctx->encoder, ctx->audio_read_buf, samples_read);
    } else if (samples_read == 0 && (ctx->audio_media_source || ctx->audio_capture_rb)) {
      // Source exists but no samples available - still need to write something for sync
      // Zero-fill the buffer (silence) to maintain timing
      memset(ctx->audio_read_buf, 0, samples_per_frame * sizeof(float));
      ffmpeg_encoder_write_audio(ctx->encoder, ctx->audio_read_buf, samples_per_frame);
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
  asciichat_error_t err = ffmpeg_encoder_destroy(ctx->encoder);
  term_renderer_destroy(ctx->renderer);
  SAFE_FREE(ctx->audio_read_buf);
  SAFE_FREE(ctx);
  return err;
}
#endif
