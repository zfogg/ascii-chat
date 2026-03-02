/**
 * @file video/h265/encoder.c
 * @brief FFmpeg HEVC encoder for ASCII art frames
 */

#include <ascii-chat/video/h265/encoder.h>
#include <ascii-chat/common.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/platform/system.h>

#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>

#include <string.h>
#include <stdlib.h>

typedef struct h265_encoder {
  AVCodecContext *codec_ctx;
  AVFrame *frame;
  AVPacket *packet;

  uint16_t current_width;
  uint16_t current_height;

  uint8_t *yuv_buf;
  size_t yuv_buf_size;

  bool request_keyframe;

  uint64_t total_frames;
  uint64_t keyframes;
  uint32_t avg_bitrate;
} h265_encoder_t;

h265_encoder_t *h265_encoder_create(uint16_t initial_width, uint16_t initial_height) {
  if (initial_width == 0 || initial_height == 0) {
    SET_ERRNO(ERROR_MEDIA_INIT, "Frame dimensions must be non-zero");
    return NULL;
  }

  h265_encoder_t *enc = SAFE_CALLOC(1, sizeof(h265_encoder_t), h265_encoder_t *);
  if (!enc) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate encoder structure");
    return NULL;
  }

  // Find best available HEVC encoder (hevc_vaapi, hevc_nvenc, libx265, or software fallback)
  const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_HEVC);
  if (!codec) {
    SET_ERRNO(ERROR_MEDIA_INIT, "HEVC encoder not found");
    SAFE_FREE(enc);
    return NULL;
  }

  enc->codec_ctx = avcodec_alloc_context3(codec);
  if (!enc->codec_ctx) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate codec context");
    SAFE_FREE(enc);
    return NULL;
  }

  // Configure codec context
  enc->codec_ctx->width = initial_width;
  enc->codec_ctx->height = initial_height;
  enc->codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  enc->codec_ctx->time_base = (AVRational){1, 30};
  enc->codec_ctx->framerate = (AVRational){30, 1};

  // Set ultrafast preset for low-latency encoding
  if (av_opt_set(enc->codec_ctx->priv_data, "preset", "ultrafast", 0) < 0) {
    log_warn("Failed to set preset to ultrafast, using default");
  }
  if (av_opt_set(enc->codec_ctx->priv_data, "tune", "zerolatency", 0) < 0) {
    log_warn("Failed to set tune to zerolatency, using default");
  }

  // Open codec (suppress x265 library stderr output during initialization)
  platform_stderr_redirect_handle_t h265_stderr = platform_stdout_stderr_redirect_to_null();
  int codec_open_result = avcodec_open2(enc->codec_ctx, codec, NULL);
  platform_stdout_stderr_restore(h265_stderr);

  if (codec_open_result < 0) {
    SET_ERRNO(ERROR_MEDIA_INIT, "Failed to open HEVC encoder");
    avcodec_free_context(&enc->codec_ctx);
    SAFE_FREE(enc);
    return NULL;
  }

  // Allocate frame and packet
  enc->frame = av_frame_alloc();
  enc->packet = av_packet_alloc();

  if (!enc->frame || !enc->packet) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate frame/packet");
    if (enc->frame)
      av_frame_free(&enc->frame);
    if (enc->packet)
      av_packet_free(&enc->packet);
    avcodec_free_context(&enc->codec_ctx);
    SAFE_FREE(enc);
    return NULL;
  }

  enc->current_width = initial_width;
  enc->current_height = initial_height;

  // Allocate YUV420P buffer (Y plane + U/V planes)
  size_t stride = initial_width;
  enc->yuv_buf_size = stride * initial_height * 3 / 2;
  enc->yuv_buf = SAFE_MALLOC(enc->yuv_buf_size, uint8_t *);
  if (!enc->yuv_buf) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate YUV buffer");
    av_frame_free(&enc->frame);
    av_packet_free(&enc->packet);
    avcodec_free_context(&enc->codec_ctx);
    SAFE_FREE(enc);
    return NULL;
  }

  log_info("FFmpeg HEVC encoder created: %ux%u (codec: %s)", initial_width, initial_height, codec->name);
  return enc;
}

void h265_encoder_destroy(h265_encoder_t *encoder) {
  if (!encoder)
    return;

  if (encoder->frame) {
    av_frame_free(&encoder->frame);
  }
  if (encoder->packet) {
    av_packet_free(&encoder->packet);
  }
  if (encoder->codec_ctx) {
    avcodec_free_context(&encoder->codec_ctx);
  }
  if (encoder->yuv_buf) {
    SAFE_FREE(encoder->yuv_buf);
  }
  SAFE_FREE(encoder);
}

static asciichat_error_t h265_encoder_reconfigure(h265_encoder_t *encoder, uint16_t new_width, uint16_t new_height) {
  if (encoder->current_width == new_width && encoder->current_height == new_height) {
    return ASCIICHAT_OK;
  }

  // Free and reallocate codec context with new dimensions
  avcodec_free_context(&encoder->codec_ctx);

  const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_HEVC);
  encoder->codec_ctx = avcodec_alloc_context3(codec);
  if (!encoder->codec_ctx) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate codec context for %ux%u", new_width, new_height);
  }

  encoder->codec_ctx->width = new_width;
  encoder->codec_ctx->height = new_height;
  encoder->codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  encoder->codec_ctx->time_base = (AVRational){1, 30};
  encoder->codec_ctx->framerate = (AVRational){30, 1};

  if (av_opt_set(encoder->codec_ctx->priv_data, "preset", "ultrafast", 0) < 0) {
    log_warn("Failed to set preset on reconfigure");
  }

  if (avcodec_open2(encoder->codec_ctx, codec, NULL) < 0) {
    return SET_ERRNO(ERROR_MEDIA_INIT, "Failed to open HEVC encoder for %ux%u", new_width, new_height);
  }

  size_t new_yuv_size = new_width * new_height * 3 / 2;
  if (new_yuv_size > encoder->yuv_buf_size) {
    uint8_t *new_buf = SAFE_MALLOC(new_yuv_size, uint8_t *);
    if (!new_buf) {
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate larger YUV buffer");
    }
    SAFE_FREE(encoder->yuv_buf);
    encoder->yuv_buf = new_buf;
    encoder->yuv_buf_size = new_yuv_size;
  }

  encoder->current_width = new_width;
  encoder->current_height = new_height;
  encoder->request_keyframe = true;

  log_info("HEVC encoder reconfigured to %ux%u", new_width, new_height);
  return ASCIICHAT_OK;
}

static void h265_encoder_ascii_to_yuv420(const uint8_t *rgb_data, uint16_t width, uint16_t height, uint8_t *yuv_buf) {
  uint8_t *y_plane = yuv_buf;
  uint8_t *u_plane = y_plane + width * height;
  uint8_t *v_plane = u_plane + (width / 2) * (height / 2);

  // Convert RGB24 to YUV420p
  // Y = 0.299*R + 0.587*G + 0.114*B (standard luma formula)
  // U = -0.14713*R - 0.28886*G + 0.436*B (offset by 128)
  // V = 0.615*R - 0.51499*G - 0.10001*B (offset by 128)

  // Process Y plane (each pixel)
  for (uint32_t i = 0; i < (uint32_t)width * (uint32_t)height; i++) {
    uint8_t r = rgb_data[i * 3];
    uint8_t g = rgb_data[i * 3 + 1];
    uint8_t b = rgb_data[i * 3 + 2];

    // Calculate Y (luma)
    y_plane[i] = (uint8_t)(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
  }

  // Process U and V planes (subsampled 2x2)
  for (uint32_t y = 0; y < (uint32_t)height; y += 2) {
    for (uint32_t x = 0; x < (uint32_t)width; x += 2) {
      // Average 2x2 RGB block for chroma
      uint32_t idx00 = ((y) * width + x) * 3;
      uint32_t idx10 = ((y) * width + x + 1) * 3;
      uint32_t idx01 = ((y + 1) * width + x) * 3;
      uint32_t idx11 = ((y + 1) * width + x + 1) * 3;

      uint16_t r_avg = (rgb_data[idx00] + rgb_data[idx10] + rgb_data[idx01] + rgb_data[idx11]) / 4;
      uint16_t g_avg = (rgb_data[idx00 + 1] + rgb_data[idx10 + 1] + rgb_data[idx01 + 1] + rgb_data[idx11 + 1]) / 4;
      uint16_t b_avg = (rgb_data[idx00 + 2] + rgb_data[idx10 + 2] + rgb_data[idx01 + 2] + rgb_data[idx11 + 2]) / 4;

      // Calculate U (Cb)
      int16_t u = (int16_t)((((-38 * r_avg - 74 * g_avg + 112 * b_avg + 128) >> 8) + 128));
      u_plane[(y / 2) * (width / 2) + (x / 2)] = (uint8_t)(u < 0 ? 0 : (u > 255 ? 255 : u));

      // Calculate V (Cr)
      int16_t v = (int16_t)((((112 * r_avg - 94 * g_avg - 18 * b_avg + 128) >> 8) + 128));
      v_plane[(y / 2) * (width / 2) + (x / 2)] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
    }
  }
}

asciichat_error_t h265_encode(h265_encoder_t *encoder, uint16_t width, uint16_t height, const uint8_t *ascii_data,
                              uint8_t *output_buf, size_t *output_size) {
  if (!encoder || !ascii_data || !output_buf || !output_size) {
    return SET_ERRNO(ERROR_INTERNAL, "Invalid encoder arguments");
  }

  if (*output_size < 5) {
    return SET_ERRNO(ERROR_NETWORK_SIZE, "Output buffer too small (minimum 5 bytes)");
  }

  asciichat_error_t result = h265_encoder_reconfigure(encoder, width, height);
  if (result != ASCIICHAT_OK) {
    return result;
  }

  h265_encoder_ascii_to_yuv420(ascii_data, width, height, encoder->yuv_buf);

  // Setup frame data pointers
  encoder->frame->data[0] = encoder->yuv_buf;
  encoder->frame->data[1] = encoder->yuv_buf + width * height;
  encoder->frame->data[2] = encoder->yuv_buf + width * height + (width / 2) * (height / 2);
  encoder->frame->linesize[0] = width;
  encoder->frame->linesize[1] = width / 2;
  encoder->frame->linesize[2] = width / 2;
  encoder->frame->width = width;
  encoder->frame->height = height;
  encoder->frame->format = AV_PIX_FMT_YUV420P;

  // Request keyframe if needed
  if (encoder->request_keyframe) {
    encoder->frame->pict_type = AV_PICTURE_TYPE_I;
    encoder->request_keyframe = false;
  }

  // Encode frame
  log_debug("H265_ENCODE: Sending frame to encoder: %ux%u format=%d", width, height, encoder->frame->format);
  int send_ret = avcodec_send_frame(encoder->codec_ctx, encoder->frame);
  if (send_ret < 0) {
    log_error("H265_ENCODE: avcodec_send_frame failed: %d", send_ret);
    return SET_ERRNO(ERROR_MEDIA_DECODE, "Failed to send frame to encoder");
  }

  log_debug("H265_ENCODE: Receiving encoded packet...");
  int recv_ret = avcodec_receive_packet(encoder->codec_ctx, encoder->packet);
  if (recv_ret == AVERROR(EAGAIN)) {
    // EAGAIN is normal - encoder needs more frames before outputting a packet
    log_dev("H265_ENCODE: EAGAIN - encoder buffering frames (normal), waiting for next frame");
    *output_size = 0; // No output this time
    return ASCIICHAT_OK;
  }
  if (recv_ret < 0) {
    log_error("H265_ENCODE: avcodec_receive_packet failed: %d (EAGAIN=%d)", recv_ret, AVERROR(EAGAIN));
    return SET_ERRNO(ERROR_MEDIA_DECODE, "Failed to receive encoded packet");
  }
  log_debug("H265_ENCODE: Got packet: size=%d", (int)encoder->packet->size);

  uint8_t flags = 0;
  if (encoder->frame->pict_type == AV_PICTURE_TYPE_I) {
    flags |= H265_ENCODER_FLAG_KEYFRAME;
    encoder->keyframes++;
  }

  if (width != encoder->current_width || height != encoder->current_height) {
    flags |= H265_ENCODER_FLAG_SIZE_CHANGE;
  }

  size_t header_size = 5;
  size_t packet_size = encoder->packet->size;
  size_t required_size = header_size + packet_size;

  if (required_size > *output_size) {
    return SET_ERRNO(ERROR_NETWORK_SIZE, "Output buffer too small: need %zu, have %zu", required_size, *output_size);
  }

  // Write header: [flags][width:u16][height:u16]
  output_buf[0] = flags;
  output_buf[1] = (width >> 8) & 0xFF;
  output_buf[2] = width & 0xFF;
  output_buf[3] = (height >> 8) & 0xFF;
  output_buf[4] = height & 0xFF;

  // Write encoded data
  if (packet_size > 0) {
    memcpy(output_buf + header_size, encoder->packet->data, packet_size);
  }

  *output_size = required_size;
  encoder->total_frames++;

  return ASCIICHAT_OK;
}

void h265_encoder_request_keyframe(h265_encoder_t *encoder) {
  if (encoder) {
    encoder->request_keyframe = true;
  }
}

asciichat_error_t h265_encoder_flush(h265_encoder_t *encoder, uint8_t *output_buf, size_t *output_size) {
  if (!encoder || !output_buf || !output_size) {
    return SET_ERRNO(ERROR_INTERNAL, "Invalid encoder or output parameters for flush");
  }

  if (*output_size < 5) {
    return SET_ERRNO(ERROR_NETWORK_SIZE, "Output buffer too small (minimum 5 bytes)");
  }

  // Send NULL frame to signal end of stream (flushes encoder)
  log_debug("H265_FLUSH: Sending NULL frame to encoder to flush buffered data");
  int send_ret = avcodec_send_frame(encoder->codec_ctx, NULL);
  if (send_ret < 0) {
    log_error("H265_FLUSH: avcodec_send_frame(NULL) failed: %d", send_ret);
    return SET_ERRNO(ERROR_MEDIA_DECODE, "Failed to flush encoder");
  }

  // Try to receive a packet
  log_debug("H265_FLUSH: Attempting to receive flushed packet...");
  int recv_ret = avcodec_receive_packet(encoder->codec_ctx, encoder->packet);
  if (recv_ret == AVERROR(EAGAIN)) {
    log_dev("H265_FLUSH: EAGAIN - no more frames available");
    *output_size = 0;
    return ASCIICHAT_OK;
  }
  if (recv_ret == AVERROR_EOF) {
    log_debug("H265_FLUSH: EOF - encoder is flushed");
    *output_size = 0;
    return ASCIICHAT_OK;
  }
  if (recv_ret < 0) {
    log_error("H265_FLUSH: avcodec_receive_packet failed: %d", recv_ret);
    return SET_ERRNO(ERROR_MEDIA_DECODE, "Failed to receive flushed packet");
  }

  log_debug("H265_FLUSH: Got flushed packet: size=%d", (int)encoder->packet->size);

  // Check if packet size exceeds output buffer
  size_t header_size = 5;
  size_t packet_size = encoder->packet->size;
  size_t required_size = header_size + packet_size;

  if (required_size > *output_size) {
    return SET_ERRNO(ERROR_NETWORK_SIZE, "Output buffer too small: need %zu, have %zu", required_size, *output_size);
  }

  // Write header: [flags][width:u16][height:u16]
  uint8_t flags = 0;
  if (encoder->frame->pict_type == AV_PICTURE_TYPE_I) {
    flags |= H265_ENCODER_FLAG_KEYFRAME;
    encoder->keyframes++;
  }

  output_buf[0] = flags;
  output_buf[1] = (encoder->current_width >> 8) & 0xFF;
  output_buf[2] = encoder->current_width & 0xFF;
  output_buf[3] = (encoder->current_height >> 8) & 0xFF;
  output_buf[4] = encoder->current_height & 0xFF;

  // Write encoded data
  if (packet_size > 0) {
    memcpy(output_buf + header_size, encoder->packet->data, packet_size);
  }

  *output_size = required_size;
  encoder->total_frames++;

  return ASCIICHAT_OK;
}

void h265_encoder_get_stats(h265_encoder_t *encoder, uint64_t *total_frames, uint64_t *keyframes,
                            uint32_t *avg_bitrate) {
  if (!encoder)
    return;

  if (total_frames)
    *total_frames = encoder->total_frames;
  if (keyframes)
    *keyframes = encoder->keyframes;
  if (avg_bitrate)
    *avg_bitrate = encoder->avg_bitrate;
}
