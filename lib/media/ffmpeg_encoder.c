/**
 * @file media/ffmpeg_encoder.c
 * @ingroup media
 * @brief FFmpeg video/image file encoder for render-file output
 */
#ifndef _WIN32
#include <ascii-chat/media/ffmpeg_encoder.h>
#include <ascii-chat/platform/memory.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/debug/named.h>
#include <string.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>

struct ffmpeg_encoder_s {
  AVFormatContext *fmt_ctx;
  AVCodecContext *codec_ctx;
  AVStream *stream;
  AVFrame *frame;
  AVFrame *frame_encoded; // Frame in target pixel format for encoding
  struct SwsContext *sws_ctx;
  AVPacket *pkt;
  int width_px;
  int height_px;
  int frame_count;
  int fps;                           // Frames per second (for duration calculation)
  int is_image;                      // Single-frame format (PNG, JPG)
  enum AVPixelFormat target_pix_fmt; // RGB24 for images, YUV420P for video
};

// Determine codec, format, and pixel format from file extension
static void get_codec_from_extension(const char *path, const char **codec, const char **format, int *is_image,
                                     enum AVPixelFormat *pix_fmt) {
  const char *dot = strrchr(path, '.');
  *is_image = 0;
  *pix_fmt = AV_PIX_FMT_YUV420P; // Default

  if (!dot) {
    *codec = "libx264";
    *format = "mp4";
    return;
  }

  const char *ext = dot + 1;
  if (strcmp(ext, "mp4") == 0 || strcmp(ext, "mov") == 0) {
    *codec = "libx264";
    *format = "mp4";
    *pix_fmt = AV_PIX_FMT_YUV420P;
  } else if (strcmp(ext, "webm") == 0) {
    *codec = "libvpx-vp9";
    *format = "webm";
    *pix_fmt = AV_PIX_FMT_YUV420P;
  } else if (strcmp(ext, "avi") == 0) {
    *codec = "mpeg4";
    *format = "avi";
    *pix_fmt = AV_PIX_FMT_YUV420P;
  } else if (strcmp(ext, "gif") == 0) {
    *codec = "gif";
    *format = "gif";
    *pix_fmt = AV_PIX_FMT_RGB24; // GIF from RGB24 (FFmpeg handles palette conversion)
  } else if (strcmp(ext, "png") == 0) {
    *codec = "png";
    *format = "image2";
    *is_image = 1;
    *pix_fmt = AV_PIX_FMT_RGB24; // PNG uses RGB24
  } else if (strcmp(ext, "jpg") == 0 || strcmp(ext, "jpeg") == 0) {
    *codec = "mjpeg";
    *format = "image2";
    *is_image = 1;
    *pix_fmt = AV_PIX_FMT_YUVJ420P; // MJPEG uses YUVJ420P
  } else {
    *codec = "libx264";
    *format = "mp4";
    *pix_fmt = AV_PIX_FMT_YUV420P;
  }
}

asciichat_error_t ffmpeg_encoder_create(const char *output_path, int width_px, int height_px, int fps,
                                        ffmpeg_encoder_t **out) {
  if (!output_path || !out || width_px <= 0 || height_px <= 0)
    return SET_ERRNO(ERROR_INVALID_PARAM, "ffmpeg_encoder_create: invalid parameters");

  // Use default FPS if not specified (0 from config means "use default")
  if (fps <= 0) {
    fps = 60;
    log_debug("ffmpeg_encoder_create: fps was 0, using default 60");
  }

  ffmpeg_encoder_t *enc = SAFE_CALLOC(1, sizeof(*enc), ffmpeg_encoder_t *);
  enc->width_px = width_px;
  enc->height_px = height_px;
  enc->fps = fps;

  const char *codec_name = NULL, *format_name = NULL;
  get_codec_from_extension(output_path, &codec_name, &format_name, &enc->is_image, &enc->target_pix_fmt);

  log_debug("ffmpeg_encoder_create: %s (%s, %dx%d @ %dfps, %s)", output_path, codec_name, width_px, height_px, fps,
            enc->is_image ? "image" : "video");

  // Allocate output media context
  int ret = avformat_alloc_output_context2(&enc->fmt_ctx, NULL, format_name, output_path);
  if (ret < 0) {
    SAFE_FREE(enc);
    return SET_ERRNO(ERROR_INIT, "ffmpeg: avformat_alloc_output_context2 failed");
  }

  // Find encoder
  const AVCodec *codec = avcodec_find_encoder_by_name(codec_name);
  if (!codec) {
    avformat_free_context(enc->fmt_ctx);
    SAFE_FREE(enc);
    return SET_ERRNO(ERROR_INIT, "ffmpeg: encoder '%s' not found", codec_name);
  }

  // Create new stream
  enc->stream = avformat_new_stream(enc->fmt_ctx, codec);
  if (!enc->stream) {
    avformat_free_context(enc->fmt_ctx);
    SAFE_FREE(enc);
    return SET_ERRNO(ERROR_INIT, "ffmpeg: avformat_new_stream failed");
  }

  // Allocate codec context
  enc->codec_ctx = avcodec_alloc_context3(codec);
  if (!enc->codec_ctx) {
    avformat_free_context(enc->fmt_ctx);
    SAFE_FREE(enc);
    return SET_ERRNO(ERROR_INIT, "ffmpeg: avcodec_alloc_context3 failed");
  }

  // Set codec parameters
  enc->codec_ctx->width = width_px;
  enc->codec_ctx->height = height_px;
  enc->codec_ctx->pix_fmt = enc->target_pix_fmt;
  enc->codec_ctx->time_base = (AVRational){1, fps};
  enc->codec_ctx->framerate = (AVRational){fps, 1};

  // Set bitrate (reasonable default: ~1Mbps per megapixel)
  int bitrate = (width_px * height_px) / 1024; // rough estimate in kbps
  if (bitrate < 500)
    bitrate = 500;
  if (bitrate > 5000)
    bitrate = 5000;
  enc->codec_ctx->bit_rate = bitrate * 1000;

  // Open codec
  ret = avcodec_open2(enc->codec_ctx, codec, NULL);
  if (ret < 0) {
    avcodec_free_context(&enc->codec_ctx);
    avformat_free_context(enc->fmt_ctx);
    SAFE_FREE(enc);
    return SET_ERRNO(ERROR_INIT, "ffmpeg: avcodec_open2 failed");
  }

  // Copy codec parameters to stream
  ret = avcodec_parameters_from_context(enc->stream->codecpar, enc->codec_ctx);
  if (ret < 0) {
    avcodec_free_context(&enc->codec_ctx);
    avformat_free_context(enc->fmt_ctx);
    SAFE_FREE(enc);
    return SET_ERRNO(ERROR_INIT, "ffmpeg: avcodec_parameters_from_context failed");
  }

  // Muxer options for proper MP4 container structure
  AVDictionary *opts = NULL;
  // Move moov atom to the front (faststart) - helps with streaming and player compatibility
  av_dict_set(&opts, "movflags", "faststart", 0);

  if (enc->is_image) {
    // Use -update flag for single frame output
    av_dict_set(&opts, "update", "1", 0);
  }

  // Allocate frames
  enc->frame = av_frame_alloc();
  enc->frame_encoded = av_frame_alloc();
  enc->pkt = av_packet_alloc();
  if (!enc->frame || !enc->frame_encoded || !enc->pkt) {
    av_frame_free(&enc->frame);
    av_frame_free(&enc->frame_encoded);
    av_packet_free(&enc->pkt);
    avcodec_free_context(&enc->codec_ctx);
    avformat_free_context(enc->fmt_ctx);
    SAFE_FREE(enc);
    return SET_ERRNO(ERROR_INIT, "ffmpeg: frame allocation failed");
  }

  // Set frame properties for target pixel format
  enc->frame_encoded->format = enc->target_pix_fmt;
  enc->frame_encoded->width = width_px;
  enc->frame_encoded->height = height_px;
  ret = av_frame_get_buffer(enc->frame_encoded, 32);
  if (ret < 0) {
    av_frame_free(&enc->frame);
    av_frame_free(&enc->frame_encoded);
    av_packet_free(&enc->pkt);
    avcodec_free_context(&enc->codec_ctx);
    avformat_free_context(enc->fmt_ctx);
    SAFE_FREE(enc);
    return SET_ERRNO(ERROR_INIT, "ffmpeg: av_frame_get_buffer failed");
  }

  // Create SWS context for RGB → target format conversion
  enc->sws_ctx = sws_getContext(width_px, height_px, AV_PIX_FMT_RGB24, width_px, height_px, enc->target_pix_fmt,
                                SWS_BICUBIC, NULL, NULL, NULL);
  if (!enc->sws_ctx) {
    av_frame_free(&enc->frame);
    av_frame_free(&enc->frame_encoded);
    av_packet_free(&enc->pkt);
    avcodec_free_context(&enc->codec_ctx);
    avformat_free_context(enc->fmt_ctx);
    SAFE_FREE(enc);
    return SET_ERRNO(ERROR_INIT, "ffmpeg: sws_getContext failed");
  }

  // Open output file
  if (!(enc->fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
    ret = avio_open(&enc->fmt_ctx->pb, output_path, AVIO_FLAG_WRITE);
    if (ret < 0) {
      sws_freeContext(enc->sws_ctx);
      av_frame_free(&enc->frame);
      av_frame_free(&enc->frame_encoded);
      av_packet_free(&enc->pkt);
      avcodec_free_context(&enc->codec_ctx);
      avformat_free_context(enc->fmt_ctx);
      SAFE_FREE(enc);
      return SET_ERRNO(ERROR_INIT, "ffmpeg: avio_open failed for '%s'", output_path);
    }
  }

  // Write header
  ret = avformat_write_header(enc->fmt_ctx, &opts);
  av_dict_free(&opts); // Free options after use
  if (ret < 0) {
    avio_closep(&enc->fmt_ctx->pb);
    sws_freeContext(enc->sws_ctx);
    av_frame_free(&enc->frame);
    av_frame_free(&enc->frame_encoded);
    av_packet_free(&enc->pkt);
    avcodec_free_context(&enc->codec_ctx);
    avformat_free_context(enc->fmt_ctx);
    SAFE_FREE(enc);
    return SET_ERRNO(ERROR_INIT, "ffmpeg: avformat_write_header failed");
  }

  enc->frame_count = 0;
  *out = enc;

  /* Register encoder with named registry */
  NAMED_REGISTER(enc, output_path, "ffmpeg_encoder", "0x%tx");

  return ASCIICHAT_OK;
}

asciichat_error_t ffmpeg_encoder_write_frame(ffmpeg_encoder_t *enc, const uint8_t *rgb, int pitch) {
  if (!enc || !rgb)
    return SET_ERRNO(ERROR_INVALID_PARAM, "ffmpeg_encoder_write_frame: NULL input");

  log_debug("ffmpeg_encoder_write_frame: frame %d, pitch=%d, dims=%dx%d", enc->frame_count, pitch, enc->width_px,
            enc->height_px);

  // Set up RGB frame for conversion
  uint8_t *data[1] = {(uint8_t *)rgb};
  int linesize[1] = {pitch};

  // Convert RGB24 to target format
  sws_scale(enc->sws_ctx, (const uint8_t *const *)data, linesize, 0, enc->height_px, enc->frame_encoded->data,
            enc->frame_encoded->linesize);

  enc->frame_encoded->pts = enc->frame_count;
  // Set frame duration in codec time base units (one frame at target FPS)
  enc->frame_encoded->duration = enc->codec_ctx->time_base.den / (enc->codec_ctx->time_base.num * enc->fps);

  // Send frame to encoder
  int ret = avcodec_send_frame(enc->codec_ctx, enc->frame_encoded);
  if (ret < 0) {
    log_warn_every(5 * 1000000000LL, "ffmpeg: avcodec_send_frame failed");
    return ASCIICHAT_OK; // Continue anyway
  }

  // Receive packets from encoder and write to file
  while (ret >= 0) {
    ret = avcodec_receive_packet(enc->codec_ctx, enc->pkt);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
      break;
    if (ret < 0) {
      log_warn_every(5 * 1000000000LL, "ffmpeg: avcodec_receive_packet failed");
      break;
    }

    // Set packet duration in codec time base (duration of one frame at target FPS)
    // This ensures proper stream duration calculation
    if (enc->pkt->duration == 0) {
      enc->pkt->duration = enc->codec_ctx->time_base.den / (enc->codec_ctx->time_base.num * enc->fps);
    }

    av_packet_rescale_ts(enc->pkt, enc->codec_ctx->time_base, enc->stream->time_base);
    enc->pkt->stream_index = enc->stream->index;

    ret = av_interleaved_write_frame(enc->fmt_ctx, enc->pkt);
    av_packet_unref(enc->pkt);
    if (ret < 0) {
      log_warn_every(5 * 1000000000LL, "ffmpeg: av_interleaved_write_frame failed");
      break;
    }
  }

  enc->frame_count++;
  return ASCIICHAT_OK;
}

asciichat_error_t ffmpeg_encoder_destroy(ffmpeg_encoder_t *enc) {
  if (!enc)
    return ASCIICHAT_OK;

  NAMED_UNREGISTER(enc);

  // Flush encoder
  avcodec_send_frame(enc->codec_ctx, NULL);
  while (1) {
    int ret = avcodec_receive_packet(enc->codec_ctx, enc->pkt);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
      break;
    if (ret < 0)
      break;

    // Set packet duration in codec time base if not already set
    if (enc->pkt->duration == 0) {
      enc->pkt->duration = enc->codec_ctx->time_base.den / (enc->codec_ctx->time_base.num * enc->fps);
    }

    av_packet_rescale_ts(enc->pkt, enc->codec_ctx->time_base, enc->stream->time_base);
    enc->pkt->stream_index = enc->stream->index;
    av_interleaved_write_frame(enc->fmt_ctx, enc->pkt);
    av_packet_unref(enc->pkt);
  }

  // Set stream duration for proper metadata (must be before trailer)
  if (enc->stream && enc->frame_count > 0) {
    // Calculate duration in stream time base units
    // time_base = 1 / time_base.den seconds per unit
    // Each frame is 1/fps seconds, so duration = frame_count / fps seconds
    // In time base units: duration = (frame_count / fps) * time_base.den = frame_count * time_base.den / fps
    int64_t duration = (int64_t)enc->frame_count * enc->stream->time_base.den / enc->fps;
    enc->stream->duration = duration;
    log_debug("ffmpeg_encoder_destroy: Set stream duration=%lld (frames=%d, fps=%d, time_base=%d/%d)",
              (long long)duration, enc->frame_count, enc->fps, 1, enc->stream->time_base.den);
  }

  // Write trailer
  av_write_trailer(enc->fmt_ctx);

  // Close output file
  if (enc->fmt_ctx && !(enc->fmt_ctx->oformat->flags & AVFMT_NOFILE))
    avio_closep(&enc->fmt_ctx->pb);

  // Cleanup
  sws_freeContext(enc->sws_ctx);
  av_frame_free(&enc->frame);
  av_frame_free(&enc->frame_encoded);
  av_packet_free(&enc->pkt);
  avcodec_free_context(&enc->codec_ctx);
  if (enc->fmt_ctx)
    avformat_free_context(enc->fmt_ctx);

  log_debug("ffmpeg_encoder_destroy: wrote %d frames", enc->frame_count);
  SAFE_FREE(enc);
  return ASCIICHAT_OK;
}
#endif
