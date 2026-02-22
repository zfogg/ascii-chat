/**
 * @file video/renderer.c
 * @ingroup video
 * @brief render_file_* — ties together font resolution, platform renderer, FFmpeg encoder
 */
#ifndef _WIN32
#include <ascii-chat/video/renderer.h>
#include <ascii-chat/media/ffmpeg_encoder.h>
#include <ascii-chat/platform/font.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/platform/terminal.h>
#include <string.h>

struct render_file_ctx_s {
  terminal_renderer_t *renderer;
  ffmpeg_encoder_t *encoder;
};

asciichat_error_t render_file_create(const char *output_path, int cols, int rows, int fps, int theme,
                                     render_file_ctx_t **out) {
  render_file_ctx_t *ctx = SAFE_CALLOC(1, sizeof(*ctx), render_file_ctx_t *);

  // Resolve font spec → platform-appropriate file path, family name, or bundled data.
  char font_spec[512] = {0};
  bool font_is_path = false;
  const uint8_t *font_data = NULL;
  size_t font_data_size = 0;
  const char *raw_font = GET_OPTION(render_font);
  asciichat_error_t fe =
      platform_font_resolve(raw_font, font_spec, sizeof(font_spec), &font_is_path, &font_data, &font_data_size);
  if (fe != ASCIICHAT_OK) {
    log_warn("renderer: font resolution failed for '%s' — using system default", raw_font[0] ? raw_font : "(default)");
  }

  term_renderer_config_t tr_cfg = {
      .cols = cols,
      .rows = rows,
      .font_size_pt = GET_OPTION(render_font_size),
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
  log_debug("render_file_create: term_renderer created successfully (%dx%d px)", term_renderer_width_px(ctx->renderer),
            term_renderer_height_px(ctx->renderer));

  err = ffmpeg_encoder_create(output_path, term_renderer_width_px(ctx->renderer),
                              term_renderer_height_px(ctx->renderer), fps, &ctx->encoder);
  if (err != ASCIICHAT_OK) {
    log_error("render_file_create: ffmpeg_encoder_create failed: %s", asciichat_error_string(err));
    term_renderer_destroy(ctx->renderer);
    SAFE_FREE(ctx);
    return err;
  }
  log_debug("render_file_create: ffmpeg_encoder created successfully");

  log_info("renderer: initialized encoder for %s", output_path);
  *out = ctx;
  return ASCIICHAT_OK;
}

asciichat_error_t render_file_write_frame(render_file_ctx_t *ctx, const char *ansi_frame) {
  if (!ctx)
    return ASCIICHAT_OK;
  if (!ansi_frame) {
    log_warn("render_file_write_frame: ansi_frame is NULL");
    return ASCIICHAT_OK;
  }

  size_t frame_len = strlen(ansi_frame);
  log_info("render_file_write_frame: processing frame (len=%zu, first 100 chars: %.100s)", frame_len, ansi_frame);

  asciichat_error_t err = term_renderer_feed(ctx->renderer, ansi_frame, frame_len);
  if (err != ASCIICHAT_OK) {
    log_warn("render_file_write_frame: term_renderer_feed failed: %s", asciichat_error_string(err));
    return err;
  }

  err =
      ffmpeg_encoder_write_frame(ctx->encoder, term_renderer_pixels(ctx->renderer), term_renderer_pitch(ctx->renderer));
  if (err != ASCIICHAT_OK) {
    log_warn("render_file_write_frame: ffmpeg_encoder_write_frame failed: %s", asciichat_error_string(err));
  }
  return err;
}

asciichat_error_t render_file_destroy(render_file_ctx_t *ctx) {
  if (!ctx)
    return ASCIICHAT_OK;
  asciichat_error_t err = ffmpeg_encoder_destroy(ctx->encoder);
  term_renderer_destroy(ctx->renderer);
  SAFE_FREE(ctx);
  return err;
}
#endif
