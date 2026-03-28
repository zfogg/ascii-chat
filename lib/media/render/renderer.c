/**
 * @file media/render/renderer.c
 * @ingroup media
 * @brief render_file_* — ties together font resolution, platform renderer, FFmpeg encoder
 */
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
  // Allocate large buffer for snapshot mode (5 seconds at 48kHz with ~14 FPS = 3,428 samples per frame)
  // Use 8192 to comfortably handle snapshot reads without excessive overhead
  ctx->audio_buf_size = 8192;      // Temporary buffer for reading audio (in floats)
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

asciichat_error_t render_file_write_frame(render_file_ctx_t *ctx, const char *ansi_frame, uint64_t captured_ns) {
  // Write to debug file directly to bypass logging system
  FILE *dbg = fopen("/tmp/render-debug.txt", "a");
  if (dbg) {
    fprintf(dbg, "[RENDER_WRITE_FRAME] Called with ctx=%p, frame_len=%zu, captured_ns=%llu\n", (void *)ctx,
            ansi_frame ? strlen(ansi_frame) : 0, (unsigned long long)captured_ns);
    fclose(dbg);
  }
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

  // Check pixels in multiple locations to verify content
  if (pixels) {
    // Check pixel at (0,0) - top left corner
    uint8_t sample_r = pixels[0], sample_g = pixels[1], sample_b = pixels[2];

    // Check pixel at (500, 100) where we injected a test red stripe
    size_t test_offset = 100 * pitch + 500 * 3;
    uint8_t test_r = pixels[test_offset], test_g = pixels[test_offset + 1], test_b = pixels[test_offset + 2];

    // Check pixel at middle of screen
    uint8_t sample_r_mid = pixels[(height_px / 2) * pitch], sample_g_mid = pixels[(height_px / 2) * pitch + 1],
            sample_b_mid = pixels[(height_px / 2) * pitch + 2];

    log_info("  pixel[0,0]: RGB(%u,%u,%u), test_stripe[500,100]: RGB(%u,%u,%u), pixel[0,%d]: RGB(%u,%u,%u)",
             sample_r, sample_g, sample_b, test_r, test_g, test_b, height_px/2, sample_r_mid, sample_g_mid, sample_b_mid);
  }

  // CRITICAL: Copy pixel buffer before encoding
  // term_renderer_pixels() returns a pointer to the renderer's internal buffer which gets
  // overwritten on the next frame. We must copy the data before passing to the encoder.
  uint8_t *pixels_copy = NULL;
  if (pixels) {
    size_t pixel_buffer_size = (size_t)pitch * (size_t)height_px;
    pixels_copy = SAFE_MALLOC(pixel_buffer_size, uint8_t *);
    memcpy(pixels_copy, pixels, pixel_buffer_size);
    log_info("render_file_write_frame: copied %zu bytes of pixel data (was at %p, now at %p)",
             pixel_buffer_size, (void *)pixels, (void *)pixels_copy);

    // DEBUG: Inject test red stripe to verify encoder is working
    static int frame_num = 0;
    if (frame_num++ < 1) {
      log_info("DEBUG: Injecting test RED vertical stripe into pixel buffer for verification");
      // Fill a 100-pixel-wide vertical stripe with bright red at x=500
      for (int y = 0; y < height_px; y++) {
        for (int x = 500; x < 600 && x < width_px; x++) {
          uint8_t *pixel = pixels_copy + y * pitch + x * 3;
          pixel[0] = 255;  // R
          pixel[1] = 0;    // G
          pixel[2] = 0;    // B
        }
      }
    }
  }

  err = ffmpeg_encoder_write_frame(ctx->encoder, pixels_copy, pitch, captured_ns);
  if (err != ASCIICHAT_OK) {
    log_warn("render_file_write_frame: ffmpeg_encoder_write_frame failed: %s", asciichat_error_string(err));
  }

  // Free the pixel copy (ffmpeg_encoder_write_frame reads and converts it immediately)
  SAFE_FREE(pixels_copy);

  // Write synchronized audio for this frame if available
  // In snapshot mode, track actual FPS and calculate samples_per_frame dynamically
  if (ctx && ctx->audio_read_buf && ctx->encoder) {
    // Detect if we're in snapshot mode
    bool snapshot_mode = GET_OPTION(snapshot_mode);
    double snapshot_delay = GET_OPTION(snapshot_delay);

    int samples_per_frame;

    // Track frame timing for dynamic FPS calculation in snapshot mode
    static uint64_t first_frame_ns = 0;
    static int log_once = 0;

    if (first_frame_ns == 0) {
      first_frame_ns = captured_ns;
    }

    if (snapshot_mode && snapshot_delay > 0) {
      // Simple approach: distribute snapshot_delay * sample_rate evenly across expected ~70 frames
      // This ensures total audio duration = snapshot_delay seconds
      int total_target_samples = (int)(ctx->audio_sample_rate * snapshot_delay);
      int expected_frame_count = (int)(13.6 * snapshot_delay);  // 13.6 FPS is typical capture rate
      samples_per_frame = total_target_samples / expected_frame_count;

      // Ensure minimum to avoid rounding to 0
      if (samples_per_frame < 100) {
        samples_per_frame = 100;
      }

    } else {
      // Normal/live mode: use 60 FPS baseline
      samples_per_frame = ctx->audio_sample_rate / 60;
      if (!log_once++) {
        log_info("[AUDIO_CALC] Normal mode: samples_per_frame=%d", samples_per_frame);
      }
    }

    if (samples_per_frame > ctx->audio_buf_size) {
      samples_per_frame = ctx->audio_buf_size;
      if (!log_once++) {
        log_warn("[AUDIO_CALC] samples_per_frame clamped to buffer size %d", ctx->audio_buf_size);
      }
    }
    if (samples_per_frame <= 0) {
      samples_per_frame = 800;  // Fallback minimum
    }

    int samples_read = 0;
    static int audio_eof_reached = 0;

    // Read from whichever audio source is available (prefer media source, fallback to capture)
    if (!audio_eof_reached && ctx->audio_media_source) {
      // Read from media source (file/URL audio) - try to get samples
      samples_read = media_source_read_audio(ctx->audio_media_source, ctx->audio_read_buf, samples_per_frame);

      if (samples_read <= 0) {
        // EOF reached or error - no more audio samples available from source
        audio_eof_reached = 1;
        samples_read = 0;
      }
    } else if (!audio_eof_reached && ctx->audio_capture_rb) {
      // Read from ring buffer (live mic capture)
      samples_read =
          audio_ring_buffer_read(ctx->audio_capture_rb, ctx->audio_read_buf, samples_per_frame);
      if (samples_read < 0) {
        samples_read = 0;  // On error, write silence
        audio_eof_reached = 1;
      } else if (samples_read == 0) {
        audio_eof_reached = 1;
      }
      log_debug("render_file_write_frame: reading audio from capture_rb (samples_per_frame=%d, read=%d)",
                samples_per_frame, samples_read);
    }

    // Write audio: either from source or silence to fill remaining time
    if (samples_read > 0) {
      // We got samples from the source - write them
      ffmpeg_encoder_write_audio(ctx->encoder, ctx->audio_read_buf, samples_read);
    } else {
      // No samples available (EOF reached or not available) - write silence to maintain sync
      // This ensures audio track duration matches video track duration
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
