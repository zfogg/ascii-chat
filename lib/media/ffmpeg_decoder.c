/**
 * @file media/ffmpeg_decoder.c
 * @brief FFmpeg-based media decoder implementation
 */

#include "ffmpeg_decoder.h"
#include "common.h"
#include "log/logging.h"
#include "asciichat_errno.h"
#include "video/image.h"

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <string.h>
#include <inttypes.h>

/* ============================================================================
 * Constants
 * ============================================================================ */

/** Target audio sample rate (48kHz for Opus compatibility) */
#define TARGET_SAMPLE_RATE 48000

/** Target audio channels (mono) */
#define TARGET_CHANNELS 1

/** AVIO buffer size for stdin reading (64KB) */
#define AVIO_BUFFER_SIZE (64 * 1024)

/* ============================================================================
 * FFmpeg Decoder Structure
 * ============================================================================ */

struct ffmpeg_decoder_t {
  // Format context
  AVFormatContext *format_ctx;

  // Video stream
  AVCodecContext *video_codec_ctx;
  int video_stream_idx;
  struct SwsContext *sws_ctx;

  // Audio stream
  AVCodecContext *audio_codec_ctx;
  int audio_stream_idx;
  struct SwrContext *swr_ctx;

  // Frame and packet buffers
  AVFrame *frame;
  AVPacket *packet;

  // Decoded image cache
  image_t *current_image;

  // Audio sample buffer (for partial frame handling)
  float *audio_buffer;
  size_t audio_buffer_size;
  size_t audio_buffer_offset;

  // State flags
  bool eof_reached;
  bool is_stdin;

  // Stdin I/O context
  AVIOContext *avio_ctx;
  unsigned char *avio_buffer;

  // Position tracking
  double last_video_pts;
  double last_audio_pts;
};

/* ============================================================================
 * Stdin I/O Callbacks
 * ============================================================================ */

/**
 * @brief Read callback for stdin AVIOContext
 */
static int stdin_read_packet(void *opaque, uint8_t *buf, int buf_size) {
  (void)opaque; // Unused

  size_t bytes_read = fread(buf, 1, (size_t)buf_size, stdin);
  if (bytes_read == 0) {
    if (feof(stdin)) {
      return AVERROR_EOF;
    }
    return AVERROR(EIO);
  }

  return (int)bytes_read;
}

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Convert AVRational to double
 */
static inline double av_q2d_safe(AVRational r) {
  return (r.den != 0) ? ((double)r.num / (double)r.den) : 0.0;
}

/**
 * @brief Get timestamp in seconds from AVFrame
 */
static double get_frame_pts_seconds(AVFrame *frame, AVRational time_base) {
  if (frame->pts == AV_NOPTS_VALUE) {
    return -1.0;
  }
  return (double)frame->pts * av_q2d_safe(time_base);
}

/**
 * @brief Open codec for stream
 */
static asciichat_error_t open_codec_context(AVFormatContext *fmt_ctx, enum AVMediaType type, int *stream_idx,
                                            AVCodecContext **codec_ctx) {
  int ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
  if (ret < 0) {
    // Stream not found - not an error, just means no stream of this type
    *stream_idx = -1;
    *codec_ctx = NULL;
    return ASCIICHAT_OK;
  }

  *stream_idx = ret;
  AVStream *stream = fmt_ctx->streams[ret];

  // Find decoder
  const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
  if (!codec) {
    return SET_ERRNO(ERROR_MEDIA_DECODE, "Codec not found for stream %d", ret);
  }

  // Allocate codec context
  *codec_ctx = avcodec_alloc_context3(codec);
  if (!*codec_ctx) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate codec context");
  }

  // Copy codec parameters
  if (avcodec_parameters_to_context(*codec_ctx, stream->codecpar) < 0) {
    avcodec_free_context(codec_ctx);
    return SET_ERRNO(ERROR_MEDIA_DECODE, "Failed to copy codec parameters");
  }

  // Open codec
  if (avcodec_open2(*codec_ctx, codec, NULL) < 0) {
    avcodec_free_context(codec_ctx);
    return SET_ERRNO(ERROR_MEDIA_DECODE, "Failed to open codec");
  }

  return ASCIICHAT_OK;
}

/* ============================================================================
 * Decoder Lifecycle
 * ============================================================================ */

ffmpeg_decoder_t *ffmpeg_decoder_create(const char *path) {
  if (!path) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Path is NULL");
    return NULL;
  }

  // Suppress FFmpeg's verbose debug logging (H.264 codec warnings, etc.)
  // Only set this once, it's a global setting
  static bool ffmpeg_log_level_set = false;
  if (!ffmpeg_log_level_set) {
    av_log_set_level(AV_LOG_QUIET); // Suppress all FFmpeg logging
    ffmpeg_log_level_set = true;
  }

  ffmpeg_decoder_t *decoder = SAFE_MALLOC(sizeof(ffmpeg_decoder_t), ffmpeg_decoder_t *);
  if (!decoder) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate decoder");
    return NULL;
  }

  memset(decoder, 0, sizeof(*decoder));
  decoder->video_stream_idx = -1;
  decoder->audio_stream_idx = -1;
  decoder->last_video_pts = -1.0;
  decoder->last_audio_pts = -1.0;

  // Open input file
  if (avformat_open_input(&decoder->format_ctx, path, NULL, NULL) < 0) {
    SET_ERRNO(ERROR_MEDIA_OPEN, "Failed to open media file: %s", path);
    SAFE_FREE(decoder);
    return NULL;
  }

  // Find stream info
  if (avformat_find_stream_info(decoder->format_ctx, NULL) < 0) {
    SET_ERRNO(ERROR_MEDIA_DECODE, "Failed to find stream info");
    avformat_close_input(&decoder->format_ctx);
    SAFE_FREE(decoder);
    return NULL;
  }

  // Open video codec
  asciichat_error_t err = open_codec_context(decoder->format_ctx, AVMEDIA_TYPE_VIDEO, &decoder->video_stream_idx,
                                             &decoder->video_codec_ctx);
  if (err != ASCIICHAT_OK) {
    log_warn("Failed to open video codec (file may be audio-only)");
  }

  // Open audio codec
  err = open_codec_context(decoder->format_ctx, AVMEDIA_TYPE_AUDIO, &decoder->audio_stream_idx,
                           &decoder->audio_codec_ctx);
  if (err != ASCIICHAT_OK) {
    log_warn("Failed to open audio codec (file may be video-only)");
  }

  // Require at least one stream
  if (decoder->video_stream_idx < 0 && decoder->audio_stream_idx < 0) {
    SET_ERRNO(ERROR_MEDIA_DECODE, "No video or audio streams found");
    ffmpeg_decoder_destroy(decoder);
    return NULL;
  }

  // Allocate frame and packet
  decoder->frame = av_frame_alloc();
  decoder->packet = av_packet_alloc();
  if (!decoder->frame || !decoder->packet) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate frame/packet");
    ffmpeg_decoder_destroy(decoder);
    return NULL;
  }

  // Initialize swscale context for video if present
  if (decoder->video_codec_ctx) {
    decoder->sws_ctx =
        sws_getContext(decoder->video_codec_ctx->width, decoder->video_codec_ctx->height,
                       decoder->video_codec_ctx->pix_fmt, decoder->video_codec_ctx->width,
                       decoder->video_codec_ctx->height, AV_PIX_FMT_RGB24, SWS_BILINEAR, NULL, NULL, NULL);
    if (!decoder->sws_ctx) {
      SET_ERRNO(ERROR_MEDIA_DECODE, "Failed to create swscale context");
      ffmpeg_decoder_destroy(decoder);
      return NULL;
    }
  }

  // Initialize swresample context for audio if present
  if (decoder->audio_codec_ctx) {
    // Allocate resampler context
    decoder->swr_ctx = swr_alloc();
    if (!decoder->swr_ctx) {
      SET_ERRNO(ERROR_MEMORY, "Failed to allocate swresample context");
      ffmpeg_decoder_destroy(decoder);
      return NULL;
    }

    // Set options
    av_opt_set_chlayout(decoder->swr_ctx, "in_chlayout", &decoder->audio_codec_ctx->ch_layout, 0);
    av_opt_set_int(decoder->swr_ctx, "in_sample_rate", decoder->audio_codec_ctx->sample_rate, 0);
    av_opt_set_sample_fmt(decoder->swr_ctx, "in_sample_fmt", decoder->audio_codec_ctx->sample_fmt, 0);

    AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_MONO;
    av_opt_set_chlayout(decoder->swr_ctx, "out_chlayout", &out_ch_layout, 0);
    av_opt_set_int(decoder->swr_ctx, "out_sample_rate", TARGET_SAMPLE_RATE, 0);
    av_opt_set_sample_fmt(decoder->swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);

    // Initialize
    if (swr_init(decoder->swr_ctx) < 0) {
      SET_ERRNO(ERROR_MEDIA_DECODE, "Failed to initialize swresample context");
      ffmpeg_decoder_destroy(decoder);
      return NULL;
    }

    // Allocate audio buffer (10 seconds worth)
    decoder->audio_buffer_size = TARGET_SAMPLE_RATE * 10;
    decoder->audio_buffer = SAFE_MALLOC(decoder->audio_buffer_size * sizeof(float), float *);
    if (!decoder->audio_buffer) {
      SET_ERRNO(ERROR_MEMORY, "Failed to allocate audio buffer");
      ffmpeg_decoder_destroy(decoder);
      return NULL;
    }
  }

  log_info("FFmpeg decoder opened: %s (video=%s, audio=%s)", path, decoder->video_stream_idx >= 0 ? "yes" : "no",
           decoder->audio_stream_idx >= 0 ? "yes" : "no");

  return decoder;
}

ffmpeg_decoder_t *ffmpeg_decoder_create_stdin(void) {
  ffmpeg_decoder_t *decoder = SAFE_MALLOC(sizeof(ffmpeg_decoder_t), ffmpeg_decoder_t *);
  if (!decoder) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate decoder");
    return NULL;
  }

  memset(decoder, 0, sizeof(*decoder));
  decoder->video_stream_idx = -1;
  decoder->audio_stream_idx = -1;
  decoder->is_stdin = true;
  decoder->last_video_pts = -1.0;
  decoder->last_audio_pts = -1.0;

  // Allocate AVIO buffer
  decoder->avio_buffer = SAFE_MALLOC(AVIO_BUFFER_SIZE, unsigned char *);
  if (!decoder->avio_buffer) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate AVIO buffer");
    SAFE_FREE(decoder);
    return NULL;
  }

  // Create AVIO context for stdin
  decoder->avio_ctx = avio_alloc_context(decoder->avio_buffer, AVIO_BUFFER_SIZE,
                                         0,    // write_flag
                                         NULL, // opaque
                                         stdin_read_packet,
                                         NULL, // write_packet
                                         NULL  // seek (stdin is not seekable)
  );

  if (!decoder->avio_ctx) {
    SET_ERRNO(ERROR_MEMORY, "Failed to create AVIO context");
    SAFE_FREE(decoder->avio_buffer);
    SAFE_FREE(decoder);
    return NULL;
  }

  // Allocate format context
  decoder->format_ctx = avformat_alloc_context();
  if (!decoder->format_ctx) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate format context");
    av_freep(&decoder->avio_ctx->buffer);
    avio_context_free(&decoder->avio_ctx);
    SAFE_FREE(decoder);
    return NULL;
  }

  decoder->format_ctx->pb = decoder->avio_ctx;

  // Open input from stdin
  if (avformat_open_input(&decoder->format_ctx, NULL, NULL, NULL) < 0) {
    SET_ERRNO(ERROR_MEDIA_OPEN, "Failed to open stdin");
    av_freep(&decoder->avio_ctx->buffer);
    avio_context_free(&decoder->avio_ctx);
    avformat_free_context(decoder->format_ctx);
    SAFE_FREE(decoder);
    return NULL;
  }

  // Find stream info
  if (avformat_find_stream_info(decoder->format_ctx, NULL) < 0) {
    SET_ERRNO(ERROR_MEDIA_DECODE, "Failed to find stream info from stdin");
    ffmpeg_decoder_destroy(decoder);
    return NULL;
  }

  // Open codecs (same as file-based decoder)
  asciichat_error_t err = open_codec_context(decoder->format_ctx, AVMEDIA_TYPE_VIDEO, &decoder->video_stream_idx,
                                             &decoder->video_codec_ctx);
  if (err != ASCIICHAT_OK) {
    log_warn("Failed to open video codec from stdin");
  }

  err = open_codec_context(decoder->format_ctx, AVMEDIA_TYPE_AUDIO, &decoder->audio_stream_idx,
                           &decoder->audio_codec_ctx);
  if (err != ASCIICHAT_OK) {
    log_warn("Failed to open audio codec from stdin");
  }

  if (decoder->video_stream_idx < 0 && decoder->audio_stream_idx < 0) {
    SET_ERRNO(ERROR_MEDIA_DECODE, "No video or audio streams found in stdin");
    ffmpeg_decoder_destroy(decoder);
    return NULL;
  }

  // Allocate frame and packet
  decoder->frame = av_frame_alloc();
  decoder->packet = av_packet_alloc();
  if (!decoder->frame || !decoder->packet) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate frame/packet");
    ffmpeg_decoder_destroy(decoder);
    return NULL;
  }

  // Initialize swscale/swresample (same as file-based)
  if (decoder->video_codec_ctx) {
    decoder->sws_ctx =
        sws_getContext(decoder->video_codec_ctx->width, decoder->video_codec_ctx->height,
                       decoder->video_codec_ctx->pix_fmt, decoder->video_codec_ctx->width,
                       decoder->video_codec_ctx->height, AV_PIX_FMT_RGB24, SWS_BILINEAR, NULL, NULL, NULL);
    if (!decoder->sws_ctx) {
      SET_ERRNO(ERROR_MEDIA_DECODE, "Failed to create swscale context");
      ffmpeg_decoder_destroy(decoder);
      return NULL;
    }
  }

  if (decoder->audio_codec_ctx) {
    decoder->swr_ctx = swr_alloc();
    if (!decoder->swr_ctx) {
      SET_ERRNO(ERROR_MEMORY, "Failed to allocate swresample context");
      ffmpeg_decoder_destroy(decoder);
      return NULL;
    }

    av_opt_set_chlayout(decoder->swr_ctx, "in_chlayout", &decoder->audio_codec_ctx->ch_layout, 0);
    av_opt_set_int(decoder->swr_ctx, "in_sample_rate", decoder->audio_codec_ctx->sample_rate, 0);
    av_opt_set_sample_fmt(decoder->swr_ctx, "in_sample_fmt", decoder->audio_codec_ctx->sample_fmt, 0);

    AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_MONO;
    av_opt_set_chlayout(decoder->swr_ctx, "out_chlayout", &out_ch_layout, 0);
    av_opt_set_int(decoder->swr_ctx, "out_sample_rate", TARGET_SAMPLE_RATE, 0);
    av_opt_set_sample_fmt(decoder->swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);

    if (swr_init(decoder->swr_ctx) < 0) {
      SET_ERRNO(ERROR_MEDIA_DECODE, "Failed to initialize swresample context");
      ffmpeg_decoder_destroy(decoder);
      return NULL;
    }

    decoder->audio_buffer_size = TARGET_SAMPLE_RATE * 10;
    decoder->audio_buffer = SAFE_MALLOC(decoder->audio_buffer_size * sizeof(float), float *);
    if (!decoder->audio_buffer) {
      SET_ERRNO(ERROR_MEMORY, "Failed to allocate audio buffer");
      ffmpeg_decoder_destroy(decoder);
      return NULL;
    }
  }

  log_info("FFmpeg decoder opened from stdin (video=%s, audio=%s)", decoder->video_stream_idx >= 0 ? "yes" : "no",
           decoder->audio_stream_idx >= 0 ? "yes" : "no");

  return decoder;
}

void ffmpeg_decoder_destroy(ffmpeg_decoder_t *decoder) {
  if (!decoder) {
    return;
  }

  // Free image cache
  if (decoder->current_image) {
    image_destroy(decoder->current_image);
    decoder->current_image = NULL;
  }

  // Free audio buffer
  SAFE_FREE(decoder->audio_buffer);

  // Free swscale context
  if (decoder->sws_ctx) {
    sws_freeContext(decoder->sws_ctx);
    decoder->sws_ctx = NULL;
  }

  // Free swresample context
  if (decoder->swr_ctx) {
    swr_free(&decoder->swr_ctx);
  }

  // Free frame and packet
  if (decoder->frame) {
    av_frame_free(&decoder->frame);
  }
  if (decoder->packet) {
    av_packet_free(&decoder->packet);
  }

  // Free codec contexts
  if (decoder->video_codec_ctx) {
    avcodec_free_context(&decoder->video_codec_ctx);
  }
  if (decoder->audio_codec_ctx) {
    avcodec_free_context(&decoder->audio_codec_ctx);
  }

  // Free format context
  if (decoder->format_ctx) {
    avformat_close_input(&decoder->format_ctx);
  }

  // Free AVIO context (stdin only)
  if (decoder->avio_ctx) {
    av_freep(&decoder->avio_ctx->buffer);
    avio_context_free(&decoder->avio_ctx);
  }

  SAFE_FREE(decoder);
}

/* ============================================================================
 * Video Operations
 * ============================================================================ */

image_t *ffmpeg_decoder_read_video_frame(ffmpeg_decoder_t *decoder) {
  if (!decoder || decoder->video_stream_idx < 0) {
    return NULL;
  }

  // Read packets until we get a video frame
  while (true) {
    // Read next packet
    int ret = av_read_frame(decoder->format_ctx, decoder->packet);
    if (ret < 0) {
      if (ret == AVERROR_EOF) {
        decoder->eof_reached = true;
      }
      return NULL;
    }

    // Check if this is a video packet
    if (decoder->packet->stream_index != decoder->video_stream_idx) {
      av_packet_unref(decoder->packet);
      continue;
    }

    // Send packet to decoder
    ret = avcodec_send_packet(decoder->video_codec_ctx, decoder->packet);
    av_packet_unref(decoder->packet);

    if (ret < 0) {
      log_warn("Error sending video packet to decoder");
      continue;
    }

    // Receive decoded frame
    ret = avcodec_receive_frame(decoder->video_codec_ctx, decoder->frame);
    if (ret == AVERROR(EAGAIN)) {
      continue; // Need more packets
    } else if (ret < 0) {
      log_warn("Error receiving video frame from decoder");
      return NULL;
    }

    // Update position tracking
    decoder->last_video_pts =
        get_frame_pts_seconds(decoder->frame, decoder->format_ctx->streams[decoder->video_stream_idx]->time_base);

    // Convert frame to RGB24
    int width = decoder->video_codec_ctx->width;
    int height = decoder->video_codec_ctx->height;

    // Reallocate image if needed
    if (!decoder->current_image || decoder->current_image->w != width || decoder->current_image->h != height) {

      if (decoder->current_image) {
        image_destroy(decoder->current_image);
      }

      decoder->current_image = image_new((size_t)width, (size_t)height);
      if (!decoder->current_image) {
        log_error("Failed to allocate image");
        return NULL;
      }
    }

    // Convert pixel format (rgb_pixel_t is 3 bytes: r, g, b)
    uint8_t *dst_data[1] = {(uint8_t *)decoder->current_image->pixels};
    int dst_linesize[1] = {width * 3};

    sws_scale(decoder->sws_ctx, (const uint8_t *const *)decoder->frame->data, decoder->frame->linesize, 0, height,
              dst_data, dst_linesize);

    return decoder->current_image;
  }
}

bool ffmpeg_decoder_has_video(ffmpeg_decoder_t *decoder) {
  return decoder && decoder->video_stream_idx >= 0;
}

asciichat_error_t ffmpeg_decoder_get_video_dimensions(ffmpeg_decoder_t *decoder, int *width, int *height) {
  if (!decoder || decoder->video_stream_idx < 0) {
    return ERROR_INVALID_PARAM;
  }

  if (width) {
    *width = decoder->video_codec_ctx->width;
  }
  if (height) {
    *height = decoder->video_codec_ctx->height;
  }

  return ASCIICHAT_OK;
}

double ffmpeg_decoder_get_video_fps(ffmpeg_decoder_t *decoder) {
  if (!decoder || decoder->video_stream_idx < 0) {
    return -1.0;
  }

  AVStream *stream = decoder->format_ctx->streams[decoder->video_stream_idx];
  return av_q2d_safe(stream->avg_frame_rate);
}

/* ============================================================================
 * Audio Operations
 * ============================================================================ */

size_t ffmpeg_decoder_read_audio_samples(ffmpeg_decoder_t *decoder, float *buffer, size_t num_samples) {
  if (!decoder || decoder->audio_stream_idx < 0 || !buffer || num_samples == 0) {
    return 0;
  }

  size_t samples_written = 0;

  // First, drain any buffered samples
  if (decoder->audio_buffer_offset > 0) {
    size_t available = decoder->audio_buffer_offset;
    size_t to_copy = (available < num_samples) ? available : num_samples;

    memcpy(buffer, decoder->audio_buffer, to_copy * sizeof(float));
    samples_written += to_copy;

    // Shift buffer
    if (to_copy < available) {
      memmove(decoder->audio_buffer, decoder->audio_buffer + to_copy, (available - to_copy) * sizeof(float));
    }
    decoder->audio_buffer_offset -= to_copy;

    if (samples_written >= num_samples) {
      return samples_written;
    }
  }

  // Read more packets to fill the request
  while (samples_written < num_samples) {
    int ret = av_read_frame(decoder->format_ctx, decoder->packet);
    if (ret < 0) {
      if (ret == AVERROR_EOF) {
        decoder->eof_reached = true;
      }
      break;
    }

    // Check if this is an audio packet
    if (decoder->packet->stream_index != decoder->audio_stream_idx) {
      av_packet_unref(decoder->packet);
      continue;
    }

    // Send packet to decoder
    ret = avcodec_send_packet(decoder->audio_codec_ctx, decoder->packet);
    av_packet_unref(decoder->packet);

    if (ret < 0) {
      log_warn("Error sending audio packet to decoder");
      continue;
    }

    // Receive decoded frame
    ret = avcodec_receive_frame(decoder->audio_codec_ctx, decoder->frame);
    if (ret == AVERROR(EAGAIN)) {
      continue;
    } else if (ret < 0) {
      log_warn("Error receiving audio frame from decoder");
      break;
    }

    // Update position tracking
    decoder->last_audio_pts =
        get_frame_pts_seconds(decoder->frame, decoder->format_ctx->streams[decoder->audio_stream_idx]->time_base);

    // Resample to target format
    float *out_buf = buffer + samples_written;
    int out_samples = (int)(num_samples - samples_written);

    uint8_t *out_ptr = (uint8_t *)out_buf;
    int converted = swr_convert(decoder->swr_ctx, &out_ptr, out_samples, (const uint8_t **)decoder->frame->data,
                                decoder->frame->nb_samples);

    if (converted > 0) {
      samples_written += (size_t)converted;
    }
  }

  return samples_written;
}

bool ffmpeg_decoder_has_audio(ffmpeg_decoder_t *decoder) {
  return decoder && decoder->audio_stream_idx >= 0;
}

/* ============================================================================
 * Playback Control
 * ============================================================================ */

asciichat_error_t ffmpeg_decoder_rewind(ffmpeg_decoder_t *decoder) {
  if (!decoder) {
    return ERROR_INVALID_PARAM;
  }

  if (decoder->is_stdin) {
    return ERROR_NOT_SUPPORTED; // Cannot seek stdin
  }

  // Flush codec buffers
  if (decoder->video_codec_ctx) {
    avcodec_flush_buffers(decoder->video_codec_ctx);
  }
  if (decoder->audio_codec_ctx) {
    avcodec_flush_buffers(decoder->audio_codec_ctx);
  }

  // Seek to beginning
  if (av_seek_frame(decoder->format_ctx, -1, 0, AVSEEK_FLAG_BACKWARD) < 0) {
    return SET_ERRNO(ERROR_MEDIA_SEEK, "Failed to seek to beginning");
  }

  decoder->eof_reached = false;
  decoder->audio_buffer_offset = 0;
  decoder->last_video_pts = -1.0;
  decoder->last_audio_pts = -1.0;

  return ASCIICHAT_OK;
}

asciichat_error_t ffmpeg_decoder_seek_to_timestamp(ffmpeg_decoder_t *decoder, double timestamp_sec) {
  if (!decoder) {
    return ERROR_INVALID_PARAM;
  }

  if (decoder->is_stdin) {
    return ERROR_NOT_SUPPORTED; // Cannot seek stdin
  }

  // Convert seconds to FFmpeg time base units (AV_TIME_BASE = 1,000,000)
  int64_t target_ts = (int64_t)(timestamp_sec * AV_TIME_BASE);

  // Seek to timestamp with frame-based seeking to nearest keyframe
  // Note: av_seek_frame can only seek to keyframes in most video codecs
  int seek_ret = av_seek_frame(decoder->format_ctx, -1, target_ts, AVSEEK_FLAG_FRAME | AVSEEK_FLAG_BACKWARD);
  if (seek_ret < 0) {
    // Fallback: try without AVSEEK_FLAG_FRAME
    seek_ret = av_seek_frame(decoder->format_ctx, -1, target_ts, AVSEEK_FLAG_BACKWARD);
  }

  if (seek_ret < 0) {
    return SET_ERRNO(ERROR_MEDIA_SEEK, "Failed to seek to timestamp %.2f seconds", timestamp_sec);
  }

  // Flush codec buffers AFTER seeking to discard any buffered frames from before the seek
  if (decoder->video_codec_ctx) {
    avcodec_flush_buffers(decoder->video_codec_ctx);
  }
  if (decoder->audio_codec_ctx) {
    avcodec_flush_buffers(decoder->audio_codec_ctx);
  }

  // Reset state to indicate no current position is known until next frame is read
  decoder->eof_reached = false;
  decoder->audio_buffer_offset = 0;  // Discard any buffered audio samples from before seek
  decoder->last_video_pts = -1.0;
  decoder->last_audio_pts = -1.0;

  return ASCIICHAT_OK;
}

bool ffmpeg_decoder_at_end(ffmpeg_decoder_t *decoder) {
  return decoder && decoder->eof_reached;
}

double ffmpeg_decoder_get_duration(ffmpeg_decoder_t *decoder) {
  if (!decoder || !decoder->format_ctx) {
    return -1.0;
  }

  if (decoder->format_ctx->duration == AV_NOPTS_VALUE) {
    return -1.0;
  }

  return (double)decoder->format_ctx->duration / AV_TIME_BASE;
}

double ffmpeg_decoder_get_position(ffmpeg_decoder_t *decoder) {
  if (!decoder) {
    return -1.0;
  }

  // Return video position if available, otherwise audio position
  if (decoder->last_video_pts >= 0.0) {
    return decoder->last_video_pts;
  } else if (decoder->last_audio_pts >= 0.0) {
    return decoder->last_audio_pts;
  }

  return -1.0;
}
