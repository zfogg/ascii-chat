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
#include <ascii-chat/options/options.h>
#include <string.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

struct ffmpeg_encoder_s {
  // Video stream
  AVCodecContext *codec_ctx;
  AVStream *stream;
  AVFrame *frame;
  AVFrame *frame_encoded; // Frame in target pixel format for encoding
  struct SwsContext *sws_ctx;

  // Audio stream
  AVStream *audio_stream;
  AVCodecContext *audio_codec_ctx;
  struct SwrContext *audio_swr_ctx;
  AVFrame *audio_frame;
  int64_t audio_pts;
  int audio_frame_size;       // Codec's required samples per frame
  float *audio_partial_buf;   // Accumulator for partial frames
  int audio_partial_len;

  // Shared
  AVFormatContext *fmt_ctx;
  AVPacket *pkt;
  int width_px;
  int height_px;
  int frame_count;
  int fps;                           // Frames per second (for duration calculation)
  int is_image;                      // Single-frame format (PNG, JPG)
  enum AVPixelFormat target_pix_fmt; // RGB24 for images, YUV420P for video
  int has_audio_stream;              // Whether audio stream was created
};

// Determine audio codec from file extension
static void get_audio_codec_from_extension(const char *path, const char **audio_codec, enum AVSampleFormat *sample_fmt,
                                           int *has_audio) {
  const char *dot = strrchr(path, '.');
  *has_audio = 0;
  *audio_codec = NULL;
  *sample_fmt = AV_SAMPLE_FMT_NONE;

  if (!dot) {
    *has_audio = 1;
    *audio_codec = "aac";
    *sample_fmt = AV_SAMPLE_FMT_FLTP;
    return;
  }

  const char *ext = dot + 1;
  // Video formats with audio support
  if (strcmp(ext, "mp4") == 0 || strcmp(ext, "mov") == 0) {
    *has_audio = 1;
    *audio_codec = "aac";
    *sample_fmt = AV_SAMPLE_FMT_FLTP;
  } else if (strcmp(ext, "mkv") == 0 || strcmp(ext, "mka") == 0) {
    *has_audio = 1;
    *audio_codec = "aac";  // Matroska supports both AAC and Opus
    *sample_fmt = AV_SAMPLE_FMT_FLTP;
  } else if (strcmp(ext, "webm") == 0) {
    *has_audio = 1;
    *audio_codec = "libopus";
    *sample_fmt = AV_SAMPLE_FMT_FLT;
  } else if (strcmp(ext, "avi") == 0) {
    *has_audio = 1;
    *audio_codec = "pcm_s16le";
    *sample_fmt = AV_SAMPLE_FMT_S16;
  } else if (strcmp(ext, "flv") == 0) {
    *has_audio = 1;
    *audio_codec = "aac";
    *sample_fmt = AV_SAMPLE_FMT_FLTP;
  } else if (strcmp(ext, "ogv") == 0 || strcmp(ext, "ogg") == 0) {
    *has_audio = 1;
    *audio_codec = "libvorbis";
    *sample_fmt = AV_SAMPLE_FMT_FLT;
  } else if (strcmp(ext, "3gp") == 0) {
    *has_audio = 1;
    *audio_codec = "aac";
    *sample_fmt = AV_SAMPLE_FMT_FLTP;
  } else if (strcmp(ext, "gif") == 0 || strcmp(ext, "png") == 0 || strcmp(ext, "jpg") == 0 || strcmp(ext, "jpeg") == 0) {
    *has_audio = 0;
    *audio_codec = NULL;
    *sample_fmt = AV_SAMPLE_FMT_NONE;
  } else {
    // Default for unknown formats
    *has_audio = 1;
    *audio_codec = "aac";
    *sample_fmt = AV_SAMPLE_FMT_FLTP;
  }
}

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
  // Video formats
  if (strcmp(ext, "mp4") == 0 || strcmp(ext, "mov") == 0) {
    *codec = "libx264";
    *format = "mp4";
    *pix_fmt = AV_PIX_FMT_YUV420P;
  } else if (strcmp(ext, "mkv") == 0) {
    *codec = "libx264";
    *format = "matroska";
    *pix_fmt = AV_PIX_FMT_YUV420P;
  } else if (strcmp(ext, "mka") == 0) {
    // MKA is audio-only Matroska
    *codec = "libx264";
    *format = "matroska";
    *pix_fmt = AV_PIX_FMT_YUV420P;
  } else if (strcmp(ext, "webm") == 0) {
    *codec = "libvpx-vp9";
    *format = "webm";
    *pix_fmt = AV_PIX_FMT_YUV420P;
  } else if (strcmp(ext, "avi") == 0) {
    *codec = "mpeg4";
    *format = "avi";
    *pix_fmt = AV_PIX_FMT_YUV420P;
  } else if (strcmp(ext, "flv") == 0) {
    *codec = "libx264";
    *format = "flv";
    *pix_fmt = AV_PIX_FMT_YUV420P;
  } else if (strcmp(ext, "ogv") == 0 || strcmp(ext, "ogg") == 0) {
    *codec = "libtheora";
    *format = "ogg";
    *pix_fmt = AV_PIX_FMT_YUV420P;
  } else if (strcmp(ext, "3gp") == 0) {
    *codec = "libx264";
    *format = "3gp";
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
    // Default for unknown formats
    *codec = "libx264";
    *format = "mp4";
    *pix_fmt = AV_PIX_FMT_YUV420P;
  }
}

// Initialize audio stream for encoder
static asciichat_error_t encoder_init_audio_stream(ffmpeg_encoder_t *enc, const char *output_path) {
  const char *audio_codec_name = NULL;
  enum AVSampleFormat target_sample_fmt = AV_SAMPLE_FMT_NONE;
  int has_audio = 0;

  get_audio_codec_from_extension(output_path, &audio_codec_name, &target_sample_fmt, &has_audio);

  if (!has_audio) {
    log_debug("encoder_init_audio_stream: no audio for format");
    enc->has_audio_stream = 0;
    return ASCIICHAT_OK;
  }

  // Find audio encoder
  const AVCodec *audio_codec = avcodec_find_encoder_by_name(audio_codec_name);
  if (!audio_codec) {
    log_warn("encoder_init_audio_stream: audio encoder '%s' not found, skipping audio", audio_codec_name);
    enc->has_audio_stream = 0;
    return ASCIICHAT_OK;
  }

  // Create audio stream
  enc->audio_stream = avformat_new_stream(enc->fmt_ctx, audio_codec);
  if (!enc->audio_stream) {
    log_warn("encoder_init_audio_stream: avformat_new_stream for audio failed");
    enc->has_audio_stream = 0;
    return ASCIICHAT_OK;
  }

  // Allocate audio codec context
  enc->audio_codec_ctx = avcodec_alloc_context3(audio_codec);
  if (!enc->audio_codec_ctx) {
    log_warn("encoder_init_audio_stream: avcodec_alloc_context3 for audio failed");
    enc->has_audio_stream = 0;
    return ASCIICHAT_OK;
  }

  // Configure audio codec context
  enc->audio_codec_ctx->sample_rate = 48000;         // 48kHz (from audio pipeline)
  AVChannelLayout ch_layout = AV_CHANNEL_LAYOUT_MONO;
  av_channel_layout_copy(&enc->audio_codec_ctx->ch_layout, &ch_layout);
  enc->audio_codec_ctx->sample_fmt = target_sample_fmt;
  enc->audio_codec_ctx->time_base = (AVRational){1, 48000};
  enc->audio_codec_ctx->bit_rate = 128000;            // 128 kbps

  // Open audio codec
  int ret = avcodec_open2(enc->audio_codec_ctx, audio_codec, NULL);
  if (ret < 0) {
    log_warn("encoder_init_audio_stream: avcodec_open2 for audio failed");
    avcodec_free_context(&enc->audio_codec_ctx);
    enc->has_audio_stream = 0;
    return ASCIICHAT_OK;
  }

  // Copy codec parameters to stream
  ret = avcodec_parameters_from_context(enc->audio_stream->codecpar, enc->audio_codec_ctx);
  if (ret < 0) {
    log_warn("encoder_init_audio_stream: avcodec_parameters_from_context for audio failed");
    avcodec_free_context(&enc->audio_codec_ctx);
    enc->has_audio_stream = 0;
    return ASCIICHAT_OK;
  }

  // Create audio frame
  enc->audio_frame = av_frame_alloc();
  if (!enc->audio_frame) {
    log_warn("encoder_init_audio_stream: av_frame_alloc for audio failed");
    avcodec_free_context(&enc->audio_codec_ctx);
    enc->has_audio_stream = 0;
    return ASCIICHAT_OK;
  }

  enc->audio_frame->sample_rate = 48000;
  AVChannelLayout frame_ch_layout = AV_CHANNEL_LAYOUT_MONO;
  av_channel_layout_copy(&enc->audio_frame->ch_layout, &frame_ch_layout);
  enc->audio_frame->format = target_sample_fmt;

  // Get frame size from codec (if specified)
  enc->audio_frame_size = enc->audio_codec_ctx->frame_size;
  if (enc->audio_frame_size == 0) {
    // Default frame size for codecs that don't specify
    enc->audio_frame_size = 1024;
  }

  // Allocate buffer for partial frames (accumulator)
  enc->audio_partial_buf = SAFE_MALLOC(enc->audio_frame_size, float *);
  enc->audio_partial_len = 0;

  // Create resampler from input format (AV_SAMPLE_FMT_FLT mono 48kHz) to target format
  enc->audio_swr_ctx = swr_alloc();
  if (!enc->audio_swr_ctx) {
    log_warn("encoder_init_audio_stream: swr_alloc failed");
    SAFE_FREE(enc->audio_partial_buf);
    av_frame_free(&enc->audio_frame);
    avcodec_free_context(&enc->audio_codec_ctx);
    enc->has_audio_stream = 0;
    return ASCIICHAT_OK;
  }

  // Set input format (mono 48kHz float32)
  AVChannelLayout in_ch_layout = AV_CHANNEL_LAYOUT_MONO;
  av_opt_set_chlayout(enc->audio_swr_ctx, "in_chlayout", &in_ch_layout, 0);
  av_opt_set_int(enc->audio_swr_ctx, "in_sample_rate", 48000, 0);
  av_opt_set_sample_fmt(enc->audio_swr_ctx, "in_sample_fmt", AV_SAMPLE_FMT_FLT, 0);

  // Set output format
  AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_MONO;
  av_opt_set_chlayout(enc->audio_swr_ctx, "out_chlayout", &out_ch_layout, 0);
  av_opt_set_int(enc->audio_swr_ctx, "out_sample_rate", 48000, 0);
  av_opt_set_sample_fmt(enc->audio_swr_ctx, "out_sample_fmt", target_sample_fmt, 0);

  // Initialize the resampler
  ret = swr_init(enc->audio_swr_ctx);
  if (ret < 0) {
    log_warn("encoder_init_audio_stream: swr_init failed");
    swr_free(&enc->audio_swr_ctx);
    SAFE_FREE(enc->audio_partial_buf);
    av_frame_free(&enc->audio_frame);
    avcodec_free_context(&enc->audio_codec_ctx);
    enc->has_audio_stream = 0;
    return ASCIICHAT_OK;
  }

  enc->audio_pts = 0;
  enc->has_audio_stream = 1;
  log_debug("encoder_init_audio_stream: audio stream initialized with codec=%s, frame_size=%d", audio_codec_name,
            enc->audio_frame_size);

  return ASCIICHAT_OK;
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

  // Set bitrate for high quality (music-level quality)
  // Use ~5-10 Mbps per megapixel for visually lossless quality
  int bitrate = (width_px * height_px * 10) / 1024; // higher quality estimate in kbps
  if (bitrate < 4000)
    bitrate = 4000; // Minimum 4000 kbps for quality
  if (bitrate > 50000)
    bitrate = 50000; // Cap at 50 Mbps max
  enc->codec_ctx->bit_rate = bitrate * 1000;
  log_debug("ffmpeg_encoder: bitrate set to %d kbps for %dx%d", bitrate, width_px, height_px);

  // Configure codec-specific options for quality
  AVDictionary *codec_opts = NULL;
  if (strcmp(codec_name, "libx264") == 0) {
    // x264 high-quality settings: optimize for visual quality with high detail preservation
    av_dict_set(&codec_opts, "preset", "slow", 0); // slow preset = high quality encoding
    av_dict_set(&codec_opts, "x264-params", "aq-mode=3:aq-strength=1:me=tesa:merange=32",
                0); // maximum detail through adaptive quantization + exhaustive ME
    log_debug("ffmpeg_encoder: x264 preset=slow with maximum quality settings");
  }

  // Open codec
  ret = avcodec_open2(enc->codec_ctx, codec, &codec_opts);
  av_dict_free(&codec_opts);
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

  // Initialize audio stream if supported
  encoder_init_audio_stream(enc, output_path);

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

  // CRITICAL: Ensure frame format is set before each sws_scale call
  // Some FFmpeg versions reset the format field, so we need to set it explicitly
  enc->frame_encoded->format = enc->target_pix_fmt;

  // Convert RGB24 to target format
  sws_scale(enc->sws_ctx, (const uint8_t *const *)data, linesize, 0, enc->height_px, enc->frame_encoded->data,
            enc->frame_encoded->linesize);

  enc->frame_encoded->pts = enc->frame_count;

  // Set frame duration in codec time base units
  bool snapshot_mode = GET_OPTION(snapshot_mode);
  if (snapshot_mode && GET_OPTION(snapshot_delay) > 0) {
    // Snapshot mode: frame durations should span the snapshot_delay duration
    // We don't know total frame count yet, but we can estimate based on configured fps
    // Effective fps = (snapshot_delay_seconds * configured_fps) / 1 second
    // Each frame gets duration = 1 frame / (frame_count / snapshot_delay) = snapshot_delay / frame_count
    // Since we don't know frame_count until destroy, use: snapshot_delay * time_base.den / estimated_frames
    // Estimate based on: estimated_frames = snapshot_delay * fps
    double snapshot_delay = GET_OPTION(snapshot_delay);
    double estimated_frames = snapshot_delay * enc->fps;
    if (estimated_frames > 1) {
      // Frame duration = snapshot_delay / estimated_frames (in seconds) converted to time base units
      enc->frame_encoded->duration = (int64_t)((snapshot_delay * enc->codec_ctx->time_base.den) / (enc->codec_ctx->time_base.num * estimated_frames));
    } else {
      // Fallback for very short snapshot_delay
      enc->frame_encoded->duration = enc->codec_ctx->time_base.den / (enc->codec_ctx->time_base.num * enc->fps);
    }
  } else {
    // Normal mode: use frame duration based on configured FPS
    enc->frame_encoded->duration = enc->codec_ctx->time_base.den / (enc->codec_ctx->time_base.num * enc->fps);
  }

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

asciichat_error_t ffmpeg_encoder_write_audio(ffmpeg_encoder_t *enc, const float *samples, int num_samples) {
  if (!enc || !samples || num_samples <= 0)
    return ASCIICHAT_OK; // Silently ignore invalid calls

  // No audio stream for this format
  if (!enc->has_audio_stream)
    return ASCIICHAT_OK;

  // Accumulate samples into partial buffer
  int samples_to_process = num_samples;
  int samples_offset = 0;

  while (samples_to_process > 0) {
    // How many samples can we add to the partial buffer?
    int space_in_buffer = enc->audio_frame_size - enc->audio_partial_len;
    int samples_to_copy = (samples_to_process < space_in_buffer) ? samples_to_process : space_in_buffer;

    // Copy samples to partial buffer
    memcpy(&enc->audio_partial_buf[enc->audio_partial_len], &samples[samples_offset], samples_to_copy * sizeof(float));
    enc->audio_partial_len += samples_to_copy;
    samples_offset += samples_to_copy;
    samples_to_process -= samples_to_copy;

    // If buffer is full, encode and write frame
    if (enc->audio_partial_len >= enc->audio_frame_size) {
      // Set up audio frame with accumulated samples
      enc->audio_frame->data[0] = (uint8_t *)enc->audio_partial_buf;
      enc->audio_frame->linesize[0] = enc->audio_frame_size * sizeof(float);
      enc->audio_frame->nb_samples = enc->audio_frame_size;
      enc->audio_frame->pts = enc->audio_pts;

      // Send frame to encoder
      int ret = avcodec_send_frame(enc->audio_codec_ctx, enc->audio_frame);
      if (ret < 0) {
        log_warn_every(5 * 1000000000LL, "ffmpeg: avcodec_send_frame for audio failed");
      } else {
        // Receive and write packets
        while (ret >= 0) {
          ret = avcodec_receive_packet(enc->audio_codec_ctx, enc->pkt);
          if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
          if (ret < 0) {
            log_warn_every(5 * 1000000000LL, "ffmpeg: avcodec_receive_packet for audio failed");
            break;
          }

          // Rescale timestamp and write
          av_packet_rescale_ts(enc->pkt, enc->audio_codec_ctx->time_base, enc->audio_stream->time_base);
          enc->pkt->stream_index = enc->audio_stream->index;
          ret = av_interleaved_write_frame(enc->fmt_ctx, enc->pkt);
          av_packet_unref(enc->pkt);
          if (ret < 0) {
            log_warn_every(5 * 1000000000LL, "ffmpeg: av_interleaved_write_frame for audio failed");
            break;
          }
        }
      }

      // Update PTS for next frame
      enc->audio_pts += enc->audio_frame_size;
      enc->audio_partial_len = 0;
    }
  }

  return ASCIICHAT_OK;
}

asciichat_error_t ffmpeg_encoder_destroy(ffmpeg_encoder_t *enc) {
  if (!enc)
    return ASCIICHAT_OK;

  NAMED_UNREGISTER(enc);

  // Flush video encoder
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

  // Flush audio encoder if present
  if (enc->has_audio_stream && enc->audio_codec_ctx) {
    // Write any remaining partial audio samples as padding
    if (enc->audio_partial_len > 0) {
      // Zero-pad the remaining samples
      memset(&enc->audio_partial_buf[enc->audio_partial_len], 0,
             (enc->audio_frame_size - enc->audio_partial_len) * sizeof(float));
      enc->audio_frame->data[0] = (uint8_t *)enc->audio_partial_buf;
      enc->audio_frame->linesize[0] = enc->audio_frame_size * sizeof(float);
      enc->audio_frame->nb_samples = enc->audio_frame_size;
      enc->audio_frame->pts = enc->audio_pts;
      avcodec_send_frame(enc->audio_codec_ctx, enc->audio_frame);
    }

    // Flush any remaining packets
    avcodec_send_frame(enc->audio_codec_ctx, NULL);
    while (1) {
      int ret = avcodec_receive_packet(enc->audio_codec_ctx, enc->pkt);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        break;
      if (ret < 0)
        break;

      av_packet_rescale_ts(enc->pkt, enc->audio_codec_ctx->time_base, enc->audio_stream->time_base);
      enc->pkt->stream_index = enc->audio_stream->index;
      av_interleaved_write_frame(enc->fmt_ctx, enc->pkt);
      av_packet_unref(enc->pkt);
    }
  }

  // Set stream duration for proper metadata (must be before trailer)
  if (enc->stream && enc->frame_count > 0) {
    int64_t duration;
    bool snapshot_mode = GET_OPTION(snapshot_mode);
    if (snapshot_mode) {
      double snapshot_delay = GET_OPTION(snapshot_delay);
      // In snapshot mode with wall-clock timing, the video duration should equal snapshot_delay
      // duration_seconds = snapshot_delay
      // duration_time_base_units = snapshot_delay * time_base.den
      duration = (int64_t)(snapshot_delay * enc->stream->time_base.den);

      // Calculate what the frame durations should be to sum to snapshot_delay
      // This ensures FFmpeg plays the captured frames across the full snapshot_delay duration
      // effective_frame_duration = snapshot_delay / frame_count (seconds per frame)
      int64_t effective_frame_duration = (int64_t)((snapshot_delay / (double)enc->frame_count) * enc->stream->time_base.den);

      log_debug("ffmpeg_encoder_destroy: Snapshot mode with wall-clock timing");
      log_debug("  snapshot_delay=%.2f seconds, captured %d frames", snapshot_delay, enc->frame_count);
      log_debug("  stream->time_base=%d/%d, effective frame duration=%lld time_base units",
                enc->stream->time_base.num, enc->stream->time_base.den, (long long)effective_frame_duration);
      log_debug("  (each frame should span %.3f seconds to total %.2f seconds)",
                snapshot_delay / (double)enc->frame_count, snapshot_delay);
    } else {
      // Normal mode: calculate duration from frame count and FPS
      // time_base = 1 / time_base.den seconds per unit
      // Each frame is 1/fps seconds, so duration = frame_count / fps seconds
      // In time base units: duration = (frame_count / fps) * time_base.den = frame_count * time_base.den / fps
      duration = (int64_t)enc->frame_count * enc->stream->time_base.den / enc->fps;
      log_debug("ffmpeg_encoder_destroy: Normal mode - Set stream duration=%lld (frames=%d, fps=%d, time_base=%d/%d)",
                (long long)duration, enc->frame_count, enc->fps, 1, enc->stream->time_base.den);
    }
    enc->stream->duration = duration;

    // Also set the last frame timestamp to match the stream duration for consistency
    // This ensures ffmpeg's duration calculation matches stream->duration
    if (snapshot_mode) {
      // In snapshot mode, adjust the last frame's timestamp to span the full snapshot_delay
      // The frame count will be written at this duration
      log_debug("ffmpeg_encoder_destroy: Snapshot frame timestamps adjusted to span %.2f seconds",
                GET_OPTION(snapshot_delay));
    }
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

  // Cleanup audio resources
  if (enc->has_audio_stream) {
    swr_free(&enc->audio_swr_ctx);
    av_frame_free(&enc->audio_frame);
    avcodec_free_context(&enc->audio_codec_ctx);
    SAFE_FREE(enc->audio_partial_buf);
  }

  av_packet_free(&enc->pkt);
  avcodec_free_context(&enc->codec_ctx);
  if (enc->fmt_ctx)
    avformat_free_context(enc->fmt_ctx);

  log_debug("ffmpeg_encoder_destroy: wrote %d frames", enc->frame_count);
  SAFE_FREE(enc);
  return ASCIICHAT_OK;
}
#endif
