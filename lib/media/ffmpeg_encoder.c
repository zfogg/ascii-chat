/**
 * @file media/ffmpeg_encoder.c
 * @ingroup media
 * @brief FFmpeg video/image file encoder for render-file output
 */
#ifndef _WIN32
#include <ascii-chat/media/ffmpeg_encoder.h>
#include <ascii-chat/platform/memory.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/log/io.h>
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
  int64_t video_pts;                 // Cumulative PTS for proper frame timestamps
  int fps;                           // Frames per second (for duration calculation)
  int is_image;                      // Single-frame format (PNG, JPG)
  enum AVPixelFormat target_pix_fmt; // RGB24 for images, YUV420P for video
  int has_audio_stream;              // Whether audio stream was created
  int is_stdout_pipe;                // Whether writing to stdout (non-seekable)
  uint64_t previous_captured_ns;     // Previous frame's capture timestamp (for snapshot mode)
  double snapshot_actual_duration;   // Actual wall-clock duration in snapshot mode (seconds)
  uint64_t snapshot_elapsed_ns;      // Current elapsed time in snapshot mode for dynamic frame duration

  // Frame timing from capture timestamps
  uint64_t first_frame_captured_ns;  // Wall-clock timestamp of first frame (0 = not set)

  // Snapshot mode frame distribution
  int estimated_frame_count;         // Expected frame count for snapshot mode (fps * snapshot_delay)
  int64_t estimated_frame_duration;  // Duration each frame should have in stream time_base units
};

// Determine audio codec from file extension
static void get_audio_codec_from_extension(const char *path, const char **audio_codec, enum AVSampleFormat *sample_fmt,
                                           int *has_audio) {
  const char *dot = strrchr(path, '.');
  *has_audio = 0;
  *audio_codec = NULL;
  *sample_fmt = AV_SAMPLE_FMT_NONE;

  if (!dot) {
    // Use Opus for stdout (pipe/non-seekable output, part of WebM), otherwise default to AAC
    if (path && strcmp(path, "-") == 0) {
      *has_audio = 1;
      *audio_codec = "libopus";
      *sample_fmt = AV_SAMPLE_FMT_FLT;
    } else {
      *has_audio = 1;
      *audio_codec = "aac";
      *sample_fmt = AV_SAMPLE_FMT_FLTP;
    }
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
    // Default to MP4 for any output without extension (including stdout piping)
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

  // Open audio codec (capture FFmpeg logs)
  int ret = 0;
  LOG_IO("ffmpeg", {
    ret = avcodec_open2(enc->audio_codec_ctx, audio_codec, NULL);
  });
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
  enc->audio_partial_buf = SAFE_MALLOC(enc->audio_frame_size * sizeof(float), float *);
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
  log_info("[FFMPEG_ENCODER_CREATE] START: output=%s, %dx%d @ %dfps", output_path, width_px, height_px, fps);

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

  // For snapshot mode, calculate output FPS based on input FPS and snapshot duration
  // If we capture for N seconds at input_fps, we'll get ~(N * input_fps) frames
  // Output duration should be snapshot_delay seconds
  // So output_fps = (input_fps * snapshot_delay) / snapshot_delay = input_fps
  // EXCEPT: if the input was probed, fps parameter is the actual input fps
  // Then output_fps should equal input_fps to make frame durations sum correctly
  bool snapshot_mode = GET_OPTION(snapshot_mode);
  double snapshot_delay = GET_OPTION(snapshot_delay);
  log_debug("ffmpeg_encoder_create: snapshot_mode=%d, snapshot_delay=%.1f, input_fps=%d",
            snapshot_mode, snapshot_delay, fps);

  if (snapshot_mode && snapshot_delay > 0) {
    // For snapshot mode: if we capture at input_fps for snapshot_delay seconds,
    // we get approximately (input_fps * snapshot_delay) frames
    // To make output span exactly snapshot_delay seconds:
    // output_fps = frame_count / snapshot_delay = (input_fps * snapshot_delay) / snapshot_delay = input_fps
    // So we should use input_fps as output_fps!
    // Example: 30fps input for 3s → ~90 frames → output at 30fps → 90/30 = 3 seconds ✓

    log_info("ffmpeg_encoder_create: Snapshot mode snapshot_delay=%.1f, input_fps=%d",
             snapshot_delay, fps);
    log_info("  → Will estimate ~%.0f frames, output FPS=%d makes durations sum to %.1f seconds",
             fps * snapshot_delay, fps, snapshot_delay);
  }

  enc->fps = fps;

  // Snapshot mode frame distribution setup
  enc->estimated_frame_count = 0;
  enc->estimated_frame_duration = 0;
  if (snapshot_mode && snapshot_delay > 0 && fps > 0) {
    // Estimate how many frames we'll capture
    enc->estimated_frame_count = (int)(fps * snapshot_delay + 0.5);
    // Each frame should have uniform duration in stream time_base (1/90000 for MP4)
    // frame_duration = total_duration / estimated_frame_count
    //                = snapshot_delay seconds * time_base.den / estimated_frame_count
    // We'll set this to stream->time_base later after it's initialized
    log_debug("ffmpeg_encoder_create: Snapshot mode - estimated_frame_count=%d, snapshot_delay=%.2f",
              enc->estimated_frame_count, snapshot_delay);
  }
  enc->is_stdout_pipe = (output_path && strcmp(output_path, "-") == 0) ? 1 : 0;

  // For codec/format detection, use output_path unless it's "-" (stdout)
  // For stdout, use "fake.mp4" to default to MP4 format
  const char *detection_path = output_path;
  if (output_path && strcmp(output_path, "-") == 0) {
    detection_path = "fake.mp4"; // Fake path for format detection when piping to stdout
    log_debug("ffmpeg_encoder_create: Using MP4 format for stdout piping");
  }

  const char *codec_name = NULL, *format_name = NULL;
  get_codec_from_extension(detection_path, &codec_name, &format_name, &enc->is_image, &enc->target_pix_fmt);

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
    // x264 fast settings for real-time rendering (optimize for speed over quality)
    av_dict_set(&codec_opts, "preset", "ultrafast", 0); // ultrafast preset for real-time encoding
    av_dict_set(&codec_opts, "crf", "28", 0); // Lower quality (crf 28 = reasonable quality, much faster)
    log_debug("ffmpeg_encoder: x264 preset=ultrafast with crf=28 for real-time encoding");
  }

  // Open codec (capture libx264 startup logs)
  LOG_IO("ffmpeg", {
    ret = avcodec_open2(enc->codec_ctx, codec, &codec_opts);
  });
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

  // Muxer flags configuration
  // For stdout/non-seekable output, use fragmented MP4 (frag_keyframe)
  // For regular files, use faststart (moov at front for streaming)
  bool is_stdout = (output_path && strcmp(output_path, "-") == 0);
  if (is_stdout) {
    // Use fragmented MP4 for stdout - starts a new fragment at each video keyframe
    // This allows streaming without requiring file seeking (FFmpeg docs recommended)
    av_dict_set(&opts, "movflags", "+frag_keyframe", 0);
    log_debug("ffmpeg_encoder_create: Using fragmented MP4 (+frag_keyframe) for stdout output");
  } else {
    // Move moov atom to the front (faststart) - helps with streaming and player compatibility
    av_dict_set(&opts, "movflags", "faststart", 0);
  }

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

  // Open output file or stdout
  if (!(enc->fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
    // For stdout output, use "pipe:1" (FFmpeg's standard for writing to stdout)
    const char *open_path = output_path;
    if (output_path && strcmp(output_path, "-") == 0) {
      open_path = "pipe:1"; // Use pipe:1 for stdout
      log_debug("ffmpeg: Redirecting '-' to 'pipe:1' for stdout output");
    }
    ret = avio_open(&enc->fmt_ctx->pb, open_path, AVIO_FLAG_WRITE);
    if (ret < 0) {
      sws_freeContext(enc->sws_ctx);
      av_frame_free(&enc->frame);
      av_frame_free(&enc->frame_encoded);
      av_packet_free(&enc->pkt);
      avcodec_free_context(&enc->codec_ctx);
      avformat_free_context(enc->fmt_ctx);
      SAFE_FREE(enc);
      return SET_ERRNO(ERROR_INIT, "ffmpeg: avio_open failed for '%s'", open_path);
    }
  }

  // Write header (capture FFmpeg format muxer logs)
  LOG_IO("ffmpeg", {
    ret = avformat_write_header(enc->fmt_ctx, &opts);
  });
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

  // Now that stream->time_base is set, calculate estimated frame duration for snapshot mode
  bool snapshot_mode_after_header = GET_OPTION(snapshot_mode);
  double snapshot_delay_after_header = GET_OPTION(snapshot_delay);
  if (snapshot_mode_after_header && enc->estimated_frame_count > 0 && snapshot_delay_after_header > 0) {
    // Each frame should span: snapshot_delay / estimated_frame_count seconds
    // In stream time_base units: (snapshot_delay * time_base.den) / estimated_frame_count
    enc->estimated_frame_duration =
        (int64_t)((snapshot_delay_after_header * (double)enc->stream->time_base.den) / enc->estimated_frame_count);
    log_debug("ffmpeg_encoder_create: Calculated estimated_frame_duration=%lld time_base units "
              "for snapshot mode (snapshot_delay=%.2f, estimated_frames=%d, time_base=%d/%d)",
              (long long)enc->estimated_frame_duration, snapshot_delay_after_header, enc->estimated_frame_count,
              enc->stream->time_base.num, enc->stream->time_base.den);
  }

  enc->frame_count = 0;
  enc->video_pts = 0;
  enc->snapshot_actual_duration = 0.0;
  enc->snapshot_elapsed_ns = 0;
  enc->first_frame_captured_ns = 0;  // Initialize first frame timestamp
  *out = enc;

  /* Register encoder with named registry */
  NAMED_REGISTER_FFMPEG_ENCODER(enc, output_path, NULL);

  log_info("[FFMPEG_ENCODER_CREATE] SUCCESS: encoder initialized, frame_count=0, fps=%d, dims=%dx%d", enc->fps, enc->width_px, enc->height_px);
  return ASCIICHAT_OK;
}

asciichat_error_t ffmpeg_encoder_write_frame(ffmpeg_encoder_t *enc, const uint8_t *rgb, int pitch,
                                             uint64_t captured_ns) {
  if (!enc || !rgb)
    return SET_ERRNO(ERROR_INVALID_PARAM, "ffmpeg_encoder_write_frame: NULL input");

  // Initialize first frame capture time on first call
  if (enc->first_frame_captured_ns == 0) {
    enc->first_frame_captured_ns = captured_ns;
    log_debug("ffmpeg_encoder_write_frame: first frame captured at %llu ns", (unsigned long long)captured_ns);
  }

  log_info("[FFMPEG_ENCODER_WRITE] frame %d, pitch=%d, dims=%dx%d", enc->frame_count, pitch, enc->width_px, enc->height_px);
  log_debug("ffmpeg_encoder_write_frame: frame %d, pitch=%d, dims=%dx%d, captured_ns=%llu", enc->frame_count, pitch, enc->width_px,
            enc->height_px, (unsigned long long)captured_ns);

  // Set up RGB frame for conversion
  uint8_t *data[1] = {(uint8_t *)rgb};
  int linesize[1] = {pitch};

  // CRITICAL: Ensure frame format is set before each sws_scale call
  // Some FFmpeg versions reset the format field, so we need to set it explicitly
  enc->frame_encoded->format = enc->target_pix_fmt;

  // Convert RGB24 to target format
  sws_scale(enc->sws_ctx, (const uint8_t *const *)data, linesize, 0, enc->height_px, enc->frame_encoded->data,
            enc->frame_encoded->linesize);

  // Set frame duration based on mode
  int64_t frame_duration;
  bool snapshot_mode = GET_OPTION(snapshot_mode);

  if (enc->frame_count == 0) {
    log_info("ffmpeg_encoder_write_frame: frame 0, snapshot_mode=%d, estimated_frame_count=%d, "
             "estimated_frame_duration=%lld", snapshot_mode, enc->estimated_frame_count,
             (long long)enc->estimated_frame_duration);
  }

  if (snapshot_mode && enc->estimated_frame_count > 0 && enc->estimated_frame_duration > 0) {
    // Snapshot mode with pre-estimated frame count: use uniform duration distribution
    // Each frame gets the same duration so they're evenly distributed across snapshot_delay seconds
    // Convert from stream time_base to codec time_base for FFmpeg encoding
    // frame_duration_codec = frame_duration_stream * (stream_time_base.den / codec_time_base.den)
    frame_duration = (int64_t)(((double)enc->estimated_frame_duration * enc->codec_ctx->time_base.den) /
                               (double)enc->stream->time_base.den);
    if (frame_duration == 0) {
      frame_duration = 1;
    }
    if (enc->frame_count <= 2) {
      log_debug("ffmpeg: frame %d SET uniform duration=%lld codec units (%.3f ms, estimated for %d frames over %.1f sec)",
                enc->frame_count, (long long)frame_duration,
                (double)frame_duration * enc->codec_ctx->time_base.num / enc->codec_ctx->time_base.den * 1000,
                enc->estimated_frame_count, GET_OPTION(snapshot_delay));
    }
  } else if (snapshot_mode) {
    // Snapshot mode without pre-estimation: use ACTUAL elapsed time between frames
    // Calculate duration from actual capture timestamp differences
    // IMPORTANT: Use codec's time_base here; FFmpeg will rescale to stream time_base later
    if (enc->frame_count > 0 && enc->previous_captured_ns > 0) {
      // Frame duration = time since last frame, converted to codec time_base units with high precision
      uint64_t elapsed_ns = captured_ns - enc->previous_captured_ns;
      // Convert nanoseconds to codec time_base units using exact calculation
      // formula: duration = elapsed_ns * time_base.den / (time_base.num * 1e9)
      frame_duration = (int64_t)(elapsed_ns * enc->codec_ctx->time_base.den /
                                 ((uint64_t)enc->codec_ctx->time_base.num * 1000000000ULL));
      if (frame_duration == 0) {
        // Frames too close together, use minimum 1 unit
        frame_duration = 1;
      }
    } else {
      // First frame: use a reasonable default (1 frame at output fps)
      frame_duration = 1;  // 1 unit at codec time_base (1/fps)
    }
  } else {
    // Normal mode: use FPS-based frame duration
    frame_duration = enc->codec_ctx->time_base.den / (enc->codec_ctx->time_base.num * enc->fps);
  }

  enc->frame_encoded->duration = frame_duration;

  // Track this frame's capture timestamp for duration calculation of next frame
  if (snapshot_mode) {
    uint64_t frame_dur_ms = (enc->previous_captured_ns > 0) ? (captured_ns - enc->previous_captured_ns) / 1000000 : 0;
    if (enc->frame_count <= 2) {
      log_info("ffmpeg: frame %d SET duration=%lld units (%.3fms since last, codec_base=1/%d)",
               enc->frame_count, (long long)frame_duration, (double)frame_dur_ms,
               enc->codec_ctx->time_base.den);
    }
    enc->previous_captured_ns = captured_ns;
  }

  // Calculate PTS from actual capture timestamp
  // time_base is {1, fps}, so 1 PTS unit = 1/fps seconds
  // Convert elapsed nanoseconds to PTS units: elapsed_ns * fps / 1_000_000_000
  uint64_t elapsed_ns = captured_ns - enc->first_frame_captured_ns;
  int64_t pts_from_timestamp = (int64_t)((elapsed_ns * (uint64_t)enc->fps) / 1000000000ULL);

  // In snapshot mode, distribute frames linearly across the actual duration
  if (snapshot_mode) {
    extern uint64_t g_snapshot_actual_duration_ms;
    extern uint64_t g_snapshot_last_capture_elapsed_ns;
    if (g_snapshot_actual_duration_ms > 0) {
      // Distribute frames linearly based on their capture timestamp relative to total duration
      // pts = (frame_elapsed_ns / total_elapsed_ns) * actual_duration_sec * fps
      // This ensures frame 0 is at 0s and last frame is at actual_duration_sec
      double actual_duration_sec = (double)g_snapshot_actual_duration_ms / 1000.0;
      double total_elapsed_sec = (double)g_snapshot_last_capture_elapsed_ns / (double)NS_PER_SEC_INT;

      if (total_elapsed_sec > 0.001) {
        double frame_fraction = (double)elapsed_ns / (double)g_snapshot_last_capture_elapsed_ns;
        double target_pts_sec = frame_fraction * actual_duration_sec;
        int64_t pts_before = pts_from_timestamp;
        // Convert target seconds to time_base units: pts = target_sec * time_base.den / time_base.num
        pts_from_timestamp = (int64_t)(target_pts_sec * (double)enc->codec_ctx->time_base.den / (double)enc->codec_ctx->time_base.num);

        if (enc->frame_count < 3 || enc->frame_count % 10 == 0) {
          log_debug("ffmpeg: snapshot frame %d: PTS distributed (before=%lld, after=%lld, sec=%lld->%lld, frac=%.3f)",
                    enc->frame_count, (long long)pts_before, (long long)pts_from_timestamp,
                    (long long)((double)pts_before * enc->codec_ctx->time_base.num / enc->codec_ctx->time_base.den),
                    (long long)target_pts_sec, frame_fraction);
        }
      }
    }
  }

  // Use timestamp-based (or scaled) PTS
  enc->frame_encoded->pts = pts_from_timestamp;
  enc->video_pts += frame_duration;

  if (!snapshot_mode) {
    log_debug("ffmpeg_encoder_write_frame: PTS calculation - elapsed_ns=%llu, pts_from_timestamp=%lld, fps=%d",
              (unsigned long long)elapsed_ns, (long long)pts_from_timestamp, enc->fps);
  }

  // Send frame to encoder
  int ret = avcodec_send_frame(enc->codec_ctx, enc->frame_encoded);
  if (ret < 0) {
    log_warn_every(5 * 1000000000LL, "ffmpeg: avcodec_send_frame failed");
    return ASCIICHAT_OK; // Continue anyway
  }

  // Receive packets from encoder and write to file
  static int write_frame_pkt_count = 0;

  while (ret >= 0) {
    ret = avcodec_receive_packet(enc->codec_ctx, enc->pkt);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
      break;
    if (ret < 0) {
      log_warn_every(5 * 1000000000LL, "ffmpeg: avcodec_receive_packet failed");
      break;
    }

    write_frame_pkt_count++;

    // Set packet duration in codec time base (duration of one frame at target FPS)
    // This ensures proper stream duration calculation
    if (enc->pkt->duration == 0) {
      enc->pkt->duration = enc->codec_ctx->time_base.den / (enc->codec_ctx->time_base.num * enc->fps);
    }

    av_packet_rescale_ts(enc->pkt, enc->codec_ctx->time_base, enc->stream->time_base);
    enc->pkt->stream_index = enc->stream->index;

    // In snapshot mode, apply adjusted duration if snapshot_actual_duration is set
    // This happens when we get the actual duration from the capture thread
    bool is_snapshot_mode = GET_OPTION(snapshot_mode);
    if (is_snapshot_mode && enc->snapshot_actual_duration > 0 && enc->frame_count > 0) {
      int64_t adjusted_dur = (int64_t)((enc->snapshot_actual_duration * enc->stream->time_base.den) / enc->frame_count);
      // Only apply if it's significantly different from the default
      if (adjusted_dur > enc->pkt->duration * 2) {
        enc->pkt->duration = adjusted_dur;
        log_debug("ffmpeg: write_frame packet %d duration adjusted to %lld units based on actual duration",
                  write_frame_pkt_count, (long long)enc->pkt->duration);
      }
    } else if (!is_snapshot_mode && write_frame_pkt_count < 3) {
      log_debug("ffmpeg: write_frame pkt %d, duration=%lld, snapshot_mode=%d, actual_dur=%.2f, frame_count=%d",
                write_frame_pkt_count, (long long)enc->pkt->duration, is_snapshot_mode, enc->snapshot_actual_duration,
                enc->frame_count);
    }

    if (write_frame_pkt_count <= 2 || write_frame_pkt_count >= 16) {
      log_debug("ffmpeg: write_frame pkt %d with final duration=%lld (%.6f sec in stream time_base), total frames=%d",
                write_frame_pkt_count, (long long)enc->pkt->duration,
                (double)enc->pkt->duration / enc->stream->time_base.den, enc->frame_count);
    }

    ret = av_interleaved_write_frame(enc->fmt_ctx, enc->pkt);
    av_packet_unref(enc->pkt);
    if (ret < 0) {
      log_warn_every(5 * 1000000000LL, "ffmpeg: av_interleaved_write_frame failed");
      break;
    }

    // Flush output buffer for stdout to ensure frames are written immediately
    if (enc->is_stdout_pipe) {
      avio_flush(enc->fmt_ctx->pb);
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

void ffmpeg_encoder_set_snapshot_actual_duration(ffmpeg_encoder_t *enc, double actual_duration_sec) {
  if (!enc)
    return;
  enc->snapshot_actual_duration = actual_duration_sec;

  // If we already have frame count, we can calculate the adjusted packet duration now
  // This will be used for any remaining frames and during flush
  if (enc->frame_count > 0 && actual_duration_sec > 0) {
    // Calculate the packet duration that would make all frames fit in actual_duration_sec
    // packet_duration_units = (actual_duration_sec * time_base.den) / frame_count
    // This is only a starting point - the actual duration might be different if more frames are captured
    log_debug("ffmpeg_encoder_set_snapshot_actual_duration: %.3f sec with %d frames so far",
              actual_duration_sec, enc->frame_count);
  } else {
    log_info("ffmpeg_encoder_set_snapshot_actual_duration: Setting duration to %.3f seconds (already encoded %d frames)",
            actual_duration_sec, enc->frame_count);
  }
}

asciichat_error_t ffmpeg_encoder_destroy(ffmpeg_encoder_t *enc) {
  if (!enc)
    return ASCIICHAT_OK;

  NAMED_UNREGISTER(enc);

  // In snapshot mode, calculate the FPS needed to match the target duration
  bool snapshot_mode = GET_OPTION(snapshot_mode);
  int64_t adjusted_packet_duration = 0;
  double snapshot_delay = GET_OPTION(snapshot_delay);

  if (snapshot_mode && enc->frame_count > 0 && snapshot_delay > 0) {
    // Calculate output FPS that distributes frames across snapshot_delay seconds
    // output_fps = frame_count / snapshot_delay
    // packet_duration (in stream time_base) = stream_time_base.den / output_fps
    //                                        = (stream_time_base.den * snapshot_delay) / frame_count
    adjusted_packet_duration = (int64_t)((snapshot_delay * enc->stream->time_base.den) / enc->frame_count);
    log_info("ffmpeg_encoder_destroy: Snapshot mode - adjusting packet durations for %d frames over %.2f seconds",
             enc->frame_count, snapshot_delay);
    log_debug("  Adjusted packet duration: %lld units (%.6f seconds per frame)",
              (long long)adjusted_packet_duration,
              (double)adjusted_packet_duration / enc->stream->time_base.den);
  }

  // Flush video encoder (capture libx264 final statistics)
  LOG_IO("ffmpeg", {
    avcodec_send_frame(enc->codec_ctx, NULL);
    int flush_pkt_count = 0;
    while (1) {
      int ret = avcodec_receive_packet(enc->codec_ctx, enc->pkt);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        break;
      if (ret < 0)
        break;

      flush_pkt_count++;

      // Set packet duration in codec time base if not already set
      if (enc->pkt->duration == 0) {
        enc->pkt->duration = enc->codec_ctx->time_base.den / (enc->codec_ctx->time_base.num * enc->fps);
      }

      // In snapshot mode, rescale to stream time_base and apply adjusted duration if set
      int64_t duration_before = enc->pkt->duration;
      av_packet_rescale_ts(enc->pkt, enc->codec_ctx->time_base, enc->stream->time_base);
      if (snapshot_mode && adjusted_packet_duration > 0) {
        enc->pkt->duration = adjusted_packet_duration;
        log_debug("ffmpeg_encoder_destroy: Flush packet %d duration adjusted from %lld to %lld",
                  flush_pkt_count, (long long)duration_before, (long long)adjusted_packet_duration);
      }
      enc->pkt->stream_index = enc->stream->index;
      av_interleaved_write_frame(enc->fmt_ctx, enc->pkt);
      av_packet_unref(enc->pkt);
    }
  });

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

    // Flush any remaining packets (capture FFmpeg audio codec logs)
    LOG_IO("ffmpeg", {
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
    });
  }

  // Set stream duration for proper metadata (must be before trailer)
  if (enc->stream && enc->frame_count > 0) {
    int64_t duration;
    bool snapshot_mode = GET_OPTION(snapshot_mode);
    if (snapshot_mode) {
      extern uint64_t g_snapshot_actual_duration_ms;
      double snapshot_delay = GET_OPTION(snapshot_delay);
      // Use requested snapshot_delay for output duration, not actual capture time
      // This ensures -D N produces an N-second video regardless of source duration
      // Frames will be distributed linearly across the requested duration
      double effective_duration = snapshot_delay;

      // In snapshot mode with wall-clock timing, the video duration should equal the actual elapsed time
      // duration_seconds = effective_duration
      // duration_time_base_units = effective_duration * time_base.den
      duration = (int64_t)(effective_duration * enc->stream->time_base.den);

      // Calculate what the frame durations should be to sum to effective_duration
      // This ensures FFmpeg plays the captured frames across the full effective_duration
      // effective_frame_duration = effective_duration / frame_count (seconds per frame)
      int64_t effective_frame_duration = (int64_t)((effective_duration / (double)enc->frame_count) * enc->stream->time_base.den);

      log_info("ffmpeg_encoder_destroy: Snapshot mode - snapshot_delay=%.2f, frames=%d, output_duration=%.2f sec",
               snapshot_delay, enc->frame_count, effective_duration);
      log_debug("  Using effective_duration=%.2f seconds for output", effective_duration);
      log_debug("  stream->time_base=%d/%d, effective frame duration=%lld time_base units",
                enc->stream->time_base.num, enc->stream->time_base.den, (long long)effective_frame_duration);
      log_debug("  (each frame should span %.3f seconds to total %.2f seconds)",
                effective_duration / (double)enc->frame_count, effective_duration);

      log_info("ffmpeg_encoder_destroy: Setting stream->duration=%lld (effective_duration=%.3f, time_base=%d/%d, equiv=%.3f sec)",
               (long long)duration, effective_duration,
               enc->stream->time_base.num, enc->stream->time_base.den,
               (double)duration * enc->stream->time_base.num / enc->stream->time_base.den);
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

    // CRITICAL: Adjust last frame duration to ensure total duration matches stream->duration
    // Each frame was encoded with FPS-based duration, but snapshot mode needs them distributed
    // across the actual capture duration. Since we can't change past frame durations, we extend
    // the last frame's duration to make the sum equal the desired total.
    if (snapshot_mode) {
      // Calculate sum of all frame durations (each frame currently has the same FPS-based duration)
      int64_t current_total_duration = enc->video_pts;  // This is the sum of all frame durations so far
      int64_t desired_duration = duration;

      // The last frame needs to be extended to: desired - (sum of all other frames)
      // This ensures the video plays for exactly the snapshot_delay duration
      int64_t last_frame_extension = desired_duration - current_total_duration;

      if (last_frame_extension > 0) {
        // We need to extend the last frame. But we can't modify it directly after encoding.
        // Instead, the muxer will use stream->duration as the authoritative source.
        log_debug("ffmpeg_encoder_destroy: Last frame would need extension of %lld units to reach total %lld (currently %lld)",
                  (long long)last_frame_extension, (long long)desired_duration, (long long)current_total_duration);
      }
    }
  }

  // Write trailer (capture FFmpeg muxer logs and final frame statistics)
  LOG_IO("ffmpeg", {
    av_write_trailer(enc->fmt_ctx);
  });

  // Close output file
  if (enc->fmt_ctx && !(enc->fmt_ctx->oformat->flags & AVFMT_NOFILE))
    avio_closep(&enc->fmt_ctx->pb);

  // Cleanup
  sws_freeContext(enc->sws_ctx);
  av_frame_free(&enc->frame);
  av_frame_free(&enc->frame_encoded);

  // Cleanup audio resources (capture any remaining FFmpeg logs from codec cleanup)
  if (enc->has_audio_stream) {
    swr_free(&enc->audio_swr_ctx);
    av_frame_free(&enc->audio_frame);
    LOG_IO("ffmpeg", {
      avcodec_free_context(&enc->audio_codec_ctx);
    });
    SAFE_FREE(enc->audio_partial_buf);
  }

  av_packet_free(&enc->pkt);
  LOG_IO("ffmpeg", {
    avcodec_free_context(&enc->codec_ctx);
  });
  if (enc->fmt_ctx)
    avformat_free_context(enc->fmt_ctx);

  log_debug("ffmpeg_encoder_destroy: wrote %d frames", enc->frame_count);
  SAFE_FREE(enc);
  return ASCIICHAT_OK;
}
#endif
