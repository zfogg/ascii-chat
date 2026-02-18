/**
 * @file media/ffmpeg_decoder.c
 * @brief FFmpeg-based media decoder implementation
 */

#include <ascii-chat/media/ffmpeg_decoder.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/video/image.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/url.h>
#include <ascii-chat/options/options.h>

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
 * FFmpeg Logging Suppression
 * ============================================================================ */

/**
 * Silent logging callback for FFmpeg - discards all log messages
 * This prevents FFmpeg from printing "stdout: " prefixed messages
 */
static void ffmpeg_silent_log_callback(void *avcl, int level, const char *fmt, va_list vl) {
  (void)avcl;  // unused
  (void)level; // unused
  (void)fmt;   // unused
  (void)vl;    // unused
  // Do nothing - silently discard all FFmpeg logs
}

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

/**
 * @brief FFmpeg decoder state for video and audio decoding
 *
 * Handles video/audio decoding from files, stdin, and URLs using FFmpeg.
 * Supports double-buffered prefetching for smooth playback.
 */
struct ffmpeg_decoder_t {
  // Format context
  AVFormatContext *format_ctx; ///< FFmpeg format/container context

  // Video stream
  AVCodecContext *video_codec_ctx; ///< Video codec context
  int video_stream_idx;            ///< Video stream index (-1 if none)
  struct SwsContext *sws_ctx;      ///< Software scaler for format conversion

  // Audio stream
  AVCodecContext *audio_codec_ctx; ///< Audio codec context
  int audio_stream_idx;            ///< Audio stream index (-1 if none)
  struct SwrContext *swr_ctx;      ///< Software resampler for format conversion

  // Frame and packet buffers
  AVFrame *frame;   ///< Reusable frame for decoding
  AVPacket *packet; ///< Reusable packet for reading

  // Decoded image cache - double-buffered for prefetching
  image_t *current_image; ///< Working buffer for decoding

  // Audio sample buffer (for partial frame handling)
  float *audio_buffer;        ///< Buffer for partial audio frames
  size_t audio_buffer_size;   ///< Total size of audio buffer
  size_t audio_buffer_offset; ///< Current offset in audio buffer

  // Background thread for frame prefetching (reduces YouTube HTTP blocking)
  asciichat_thread_t prefetch_thread; ///< Prefetch thread handle
  image_t *prefetch_image_a;          ///< First prefetch buffer
  image_t *prefetch_image_b;          ///< Second prefetch buffer
  image_t *current_prefetch_image;    ///< Currently available prefetched frame
  bool prefetch_frame_ready;          ///< Whether current_prefetch_image has valid data
  bool prefetch_thread_running;       ///< Whether prefetch thread is active
  bool prefetch_should_stop;          ///< Signal to stop prefetch thread
  bool seeking_in_progress;           ///< Signal to pause prefetch thread during seek
  cond_t prefetch_cond;               ///< Condition variable for pausing during seek
  mutex_t prefetch_mutex;             ///< Protect prefetch state and FFmpeg decoder access

  // Track which buffer is being read by main thread
  image_t *current_read_buffer; ///< Buffer main thread is currently reading/rendering
  bool buffer_a_in_use;         ///< Whether prefetch_image_a is being read by main thread
  bool buffer_b_in_use;         ///< Whether prefetch_image_b is being read by main thread

  // State flags
  bool eof_reached; ///< Whether end of file was reached
  bool is_stdin;    ///< Whether reading from stdin

  // Stdin I/O context
  AVIOContext *avio_ctx;      ///< Custom I/O context for stdin
  unsigned char *avio_buffer; ///< Buffer for custom I/O

  // Position tracking
  double last_video_pts; ///< Last video presentation timestamp
  double last_audio_pts; ///< Last audio presentation timestamp

  // Sample-based position tracking
  uint64_t audio_samples_read; ///< Total audio samples decoded and output
  int audio_sample_rate;       ///< Audio sample rate (Hz)
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
 * @brief FFmpeg interrupt callback - allows seeking to interrupt long-running av_read_frame() calls
 *
 * This callback is called periodically by FFmpeg during blocking operations.
 * Returning 1 tells FFmpeg to abort the current operation.
 */
static int ffmpeg_interrupt_callback(void *opaque) {
  ffmpeg_decoder_t *decoder = (ffmpeg_decoder_t *)opaque;
  if (!decoder) {
    return 0;
  }
  // Interrupt av_read_frame() if a seek is in progress
  return decoder->seeking_in_progress ? 1 : 0;
}

/**
 * @brief Background thread function for prefetching video frames
 *
 * Continuously reads frames from the decoder and decodes them into prefetch buffers.
 * This allows the main render thread to pull pre-decoded frames without blocking
 * on YouTube HTTP requests.
 */
static void *ffmpeg_decoder_prefetch_thread_func(void *arg) {
  ffmpeg_decoder_t *decoder = (ffmpeg_decoder_t *)arg;
  if (!decoder || !decoder->prefetch_image_a || !decoder->prefetch_image_b) {
    return NULL;
  }

  log_debug("Video prefetch thread started");
  bool use_image_a = true; // Track which buffer we're using (critical for correct buffer swapping)

  while (true) {
    mutex_lock(&decoder->prefetch_mutex);

    // Check if thread should stop
    bool should_stop = decoder->prefetch_should_stop || decoder->eof_reached;
    if (should_stop) {
      mutex_unlock(&decoder->prefetch_mutex);
      break;
    }

    // Pause if seek is in progress - wait for signal to continue
    // cond_wait automatically releases and re-acquires mutex
    while (decoder->seeking_in_progress) {
      cond_wait(&decoder->prefetch_cond, &decoder->prefetch_mutex);
    }

    // Mutex is re-acquired after cond_wait - KEEP IT HELD

    // Check if the buffer we want to use is still in use by the main thread
    // If so, skip this iteration and try again later
    bool buffer_in_use = use_image_a ? decoder->buffer_a_in_use : decoder->buffer_b_in_use;
    if (buffer_in_use) {
      // Can't use this buffer yet - main thread is still rendering it
      mutex_unlock(&decoder->prefetch_mutex);
      platform_sleep_us(1000); // 1ms - brief sleep before retry
      continue;
    }

    uint64_t read_start_ns = time_get_ns();
    bool frame_decoded = false;

    // Release mutex before blocking av_read_frame() call
    // The seeking_in_progress flag prevents av_seek_frame() races
    mutex_unlock(&decoder->prefetch_mutex);

    // Read packets until we get a video frame - MUTEX RELEASED
    while (true) {
      int ret = av_read_frame(decoder->format_ctx, decoder->packet);
      if (ret < 0) {
        if (ret == AVERROR_EOF) {
          decoder->eof_reached = true;
        }
        break;
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
        continue;
      }

      // Receive decoded frame
      ret = avcodec_receive_frame(decoder->video_codec_ctx, decoder->frame);
      if (ret == AVERROR(EAGAIN)) {
        continue; // Need more packets
      } else if (ret < 0) {
        break;
      }

      // Frame decoded - mutex still held
      // Update position tracking
      decoder->last_video_pts =
          get_frame_pts_seconds(decoder->frame, decoder->format_ctx->streams[decoder->video_stream_idx]->time_base);

      // Convert frame to RGB24
      int width = decoder->video_codec_ctx->width;
      int height = decoder->video_codec_ctx->height;

      // Get current decode buffer based on which one we're using
      // (Buffer availability was already checked before entering the decode loop)
      image_t *decode_buffer = use_image_a ? decoder->prefetch_image_a : decoder->prefetch_image_b;

      // Reallocate if needed (width/height changed)
      if (decode_buffer->w != width || decode_buffer->h != height) {
        image_destroy(decode_buffer);
        decode_buffer = image_new((size_t)width, (size_t)height);
        if (!decode_buffer) {
          log_error("Failed to allocate prefetch image buffer");
          break;
        }
        // Update decoder's pointer to the reallocated buffer
        if (use_image_a) {
          decoder->prefetch_image_a = decode_buffer;
        } else {
          decoder->prefetch_image_b = decode_buffer;
        }
      }

      // Convert pixel format (rgb_pixel_t is 3 bytes: r, g, b)
      uint8_t *dst_data[1] = {(uint8_t *)decode_buffer->pixels};
      int dst_linesize[1] = {width * 3};

      // Lazy initialize swscale context if not done at startup (happens with HTTP/stdin streams)
      if (!decoder->sws_ctx) {
        if (width > 0 && height > 0 && decoder->video_codec_ctx->pix_fmt != AV_PIX_FMT_NONE) {
          decoder->sws_ctx = sws_getContext(width, height, decoder->video_codec_ctx->pix_fmt, width, height,
                                            AV_PIX_FMT_RGB24, SWS_BILINEAR, NULL, NULL, NULL);
          if (!decoder->sws_ctx) {
            log_error("Failed to create swscale context on first frame");
            break;
          }
          log_debug("Lazy initialized swscale context with %dx%d", width, height);
        } else {
          log_error("Cannot initialize swscale: invalid dimensions or pixel format");
          break;
        }
      }

      sws_scale(decoder->sws_ctx, (const uint8_t *const *)decoder->frame->data, decoder->frame->linesize, 0, height,
                dst_data, dst_linesize);

      frame_decoded = true;
      break; // Exit while loop
    }

    // Re-acquire mutex to update prefetch state
    mutex_lock(&decoder->prefetch_mutex);

    // Mutex now held - update prefetch state
    if (frame_decoded) {
      // Update shared prefetch state while mutex is held
    }

    mutex_unlock(&decoder->prefetch_mutex); // Release at end of main loop iteration

    if (frame_decoded) {
      uint64_t read_time_ns = time_elapsed_ns(read_start_ns, time_get_ns());
      double read_ms = (double)read_time_ns / NS_PER_MS;

      // Get current decode buffer
      image_t *decode_buffer = use_image_a ? decoder->prefetch_image_a : decoder->prefetch_image_b;

      // Update the current prefetch image (main thread will pull from this)
      mutex_lock(&decoder->prefetch_mutex);
      decoder->current_prefetch_image = decode_buffer;
      decoder->prefetch_frame_ready = true;
      mutex_unlock(&decoder->prefetch_mutex);

      log_dev_every(5 * US_PER_SEC_INT, "PREFETCH: decoded frame in %.2f ms", read_ms);

      // Switch to the other buffer for next iteration (MUST use boolean flag, not pointer comparison)
      use_image_a = !use_image_a;
    } else {
      // EOF or error - exit thread
      break;
    }
  }

  log_debug("Video prefetch thread stopped");
  return NULL;
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
    av_log_set_level(AV_LOG_QUIET);                  // Suppress all FFmpeg logging
    av_log_set_callback(ffmpeg_silent_log_callback); // Install silent callback to discard all output
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

  // Suppress FFmpeg's probing output by redirecting both stdout and stderr to /dev/null
  // FFmpeg may write directly to either stream, so we suppress both
  platform_stderr_redirect_handle_t stdio_handle = platform_stdout_stderr_redirect_to_null();

  // Configure FFmpeg options for HTTP streaming performance
  AVDictionary *options = NULL;

  // For HTTP/HTTPS streams: enable fast probing and reconnection (validated via production-grade URL regex)
  if (path && url_is_valid(path)) {
    // Limit probing to 32KB for faster format detection
    av_dict_set(&options, "probesize", "32768", 0);
    // Analyze for 100ms max to determine streams quickly
    av_dict_set(&options, "analyzeduration", "100000", 0);
    // Enable auto-reconnection for interrupted connections
    av_dict_set(&options, "reconnect", "1", 0);
    // Allow reconnection for streamed protocols
    av_dict_set(&options, "reconnect_streamed", "1", 0);
    // Set reasonable I/O timeout (10 seconds)
    av_dict_set(&options, "rw_timeout", "10000000", 0);
    // Enable HTTP persistent connection (keep-alive) for better performance
    av_dict_set(&options, "http_persistent", "1", 0);
    // Reduce connect timeout to fail faster if server is unreachable
    av_dict_set(&options, "connect_timeout", "5000000", 0);
  }

  // Open input file
  int ret = avformat_open_input(&decoder->format_ctx, path, NULL, &options);
  av_dict_free(&options); // Free options dictionary

  if (ret < 0) {
    platform_stdout_stderr_restore(stdio_handle);
    SET_ERRNO(ERROR_MEDIA_OPEN, "Failed to open media file: %s", path);
    SAFE_FREE(decoder);
    return NULL;
  }

  // Find stream info
  if (avformat_find_stream_info(decoder->format_ctx, NULL) < 0) {
    platform_stdout_stderr_restore(stdio_handle);
    SET_ERRNO(ERROR_MEDIA_DECODE, "Failed to find stream info");
    avformat_close_input(&decoder->format_ctx);
    SAFE_FREE(decoder);
    return NULL;
  }

  // Install interrupt callback to allow seeking to interrupt long av_read_frame() calls
  // Do this AFTER finding stream info to ensure format context is fully initialized
  if (decoder->format_ctx) {
    decoder->format_ctx->interrupt_callback.callback = ffmpeg_interrupt_callback;
    decoder->format_ctx->interrupt_callback.opaque = decoder;
  }

  // Restore stdout and stderr after FFmpeg initialization
  platform_stdout_stderr_restore(stdio_handle);

  // Open video codec
  asciichat_error_t err = open_codec_context(decoder->format_ctx, AVMEDIA_TYPE_VIDEO, &decoder->video_stream_idx,
                                             &decoder->video_codec_ctx);
  if (err != ASCIICHAT_OK) {
    log_warn("Failed to open video codec (file may be audio-only)");
  }

  // Open audio codec - audio is enabled by default (no option needed)
  // Always try to open audio codec, don't rely on GET_OPTION(audio_enabled) which has a default issue
  err = open_codec_context(decoder->format_ctx, AVMEDIA_TYPE_AUDIO, &decoder->audio_stream_idx,
                           &decoder->audio_codec_ctx);
  if (err != ASCIICHAT_OK) {
    log_debug("No audio codec found (file may be video-only or audio codec not available)");
    decoder->audio_stream_idx = -1;
    decoder->audio_codec_ctx = NULL;
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
    // Validate codec context has valid dimensions and pixel format
    // For HTTP streams, these might not be valid until first frame is read
    if (decoder->video_codec_ctx->width <= 0 || decoder->video_codec_ctx->height <= 0) {
      log_warn("Video codec has invalid dimensions (%dx%d), will initialize swscale on first frame",
               decoder->video_codec_ctx->width, decoder->video_codec_ctx->height);
      // Don't create swscale context yet - will create it lazily on first frame read
    } else if (decoder->video_codec_ctx->pix_fmt == AV_PIX_FMT_NONE) {
      log_warn("Video codec has invalid pixel format, will initialize swscale on first frame");
      // Don't create swscale context yet - will create it lazily on first frame read
    } else {
      // Create swscale context with valid parameters
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
  }

  // Initialize swresample context for audio if present
  if (decoder->audio_codec_ctx) {
    // Store output sample rate for position tracking
    decoder->audio_sample_rate = TARGET_SAMPLE_RATE;

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

  // Initialize video frame prefetching system (for YouTube streaming)
  if (decoder->video_stream_idx >= 0) {
    int width = decoder->video_codec_ctx->width;
    int height = decoder->video_codec_ctx->height;

    // Create two prefetch image buffers for double-buffering
    decoder->prefetch_image_a = image_new((size_t)width, (size_t)height);
    decoder->prefetch_image_b = image_new((size_t)width, (size_t)height);
    if (!decoder->prefetch_image_a || !decoder->prefetch_image_b) {
      SET_ERRNO(ERROR_MEMORY, "Failed to allocate prefetch image buffers");
      ffmpeg_decoder_destroy(decoder);
      return NULL;
    }

    decoder->current_prefetch_image = decoder->prefetch_image_a;
    decoder->prefetch_frame_ready = false;

    // Initialize prefetch mutex
    if (mutex_init(&decoder->prefetch_mutex) != 0) {
      SET_ERRNO(ERROR_MEMORY, "Failed to initialize prefetch mutex");
      ffmpeg_decoder_destroy(decoder);
      return NULL;
    }

    if (cond_init(&decoder->prefetch_cond) != 0) {
      SET_ERRNO(ERROR_MEMORY, "Failed to initialize prefetch condition variable");
      mutex_destroy(&decoder->prefetch_mutex);
      ffmpeg_decoder_destroy(decoder);
      return NULL;
    }

    decoder->prefetch_thread_running = false;
    decoder->prefetch_should_stop = false;
  }

  log_debug("FFmpeg decoder opened: %s (video=%s, audio=%s)", path, decoder->video_stream_idx >= 0 ? "yes" : "no",
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

  // Suppress FFmpeg's probing output by redirecting both stdout and stderr to /dev/null
  platform_stderr_redirect_handle_t stdio_handle = platform_stdout_stderr_redirect_to_null();

  // Open input from stdin
  if (avformat_open_input(&decoder->format_ctx, NULL, NULL, NULL) < 0) {
    platform_stdout_stderr_restore(stdio_handle);
    SET_ERRNO(ERROR_MEDIA_OPEN, "Failed to open stdin");
    av_freep(&decoder->avio_ctx->buffer);
    avio_context_free(&decoder->avio_ctx);
    avformat_free_context(decoder->format_ctx);
    SAFE_FREE(decoder);
    return NULL;
  }

  // Find stream info
  if (avformat_find_stream_info(decoder->format_ctx, NULL) < 0) {
    platform_stdout_stderr_restore(stdio_handle);
    SET_ERRNO(ERROR_MEDIA_DECODE, "Failed to find stream info from stdin");
    ffmpeg_decoder_destroy(decoder);
    return NULL;
  }

  platform_stdout_stderr_restore(stdio_handle);

  // Open codecs (same as file-based decoder)
  asciichat_error_t err = open_codec_context(decoder->format_ctx, AVMEDIA_TYPE_VIDEO, &decoder->video_stream_idx,
                                             &decoder->video_codec_ctx);
  if (err != ASCIICHAT_OK) {
    log_warn("Failed to open video codec from stdin");
  }

  if (GET_OPTION(audio_enabled)) {
    err = open_codec_context(decoder->format_ctx, AVMEDIA_TYPE_AUDIO, &decoder->audio_stream_idx,
                             &decoder->audio_codec_ctx);
    if (err != ASCIICHAT_OK) {
      log_warn("Failed to open audio codec from stdin");
    }
  } else {
    decoder->audio_stream_idx = -1;
    decoder->audio_codec_ctx = NULL;
    log_debug("Audio decoding disabled by user option");
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
    // Validate codec context has valid dimensions and pixel format
    // For stdin/HTTP streams, these might not be valid until first frame is read
    if (decoder->video_codec_ctx->width <= 0 || decoder->video_codec_ctx->height <= 0) {
      log_warn("Video codec has invalid dimensions (%dx%d), will initialize swscale on first frame",
               decoder->video_codec_ctx->width, decoder->video_codec_ctx->height);
      // Don't create swscale context yet - will create it lazily on first frame read
    } else if (decoder->video_codec_ctx->pix_fmt == AV_PIX_FMT_NONE) {
      log_warn("Video codec has invalid pixel format, will initialize swscale on first frame");
      // Don't create swscale context yet - will create it lazily on first frame read
    } else {
      // Create swscale context with valid parameters
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

  log_debug("FFmpeg decoder opened from stdin (video=%s, audio=%s)", decoder->video_stream_idx >= 0 ? "yes" : "no",
            decoder->audio_stream_idx >= 0 ? "yes" : "no");

  return decoder;
}

void ffmpeg_decoder_destroy(ffmpeg_decoder_t *decoder) {
  if (!decoder) {
    return;
  }

  // Stop prefetch thread (signal it to stop and wait for it to finish)
  if (decoder->prefetch_thread_running) {
    mutex_lock(&decoder->prefetch_mutex);
    decoder->prefetch_should_stop = true;
    mutex_unlock(&decoder->prefetch_mutex);

    // Wait for thread to finish
    asciichat_thread_join(&decoder->prefetch_thread, NULL);
    decoder->prefetch_thread_running = false;
  }

  // Clean up prefetch state
  cond_destroy(&decoder->prefetch_cond);
  mutex_destroy(&decoder->prefetch_mutex);

  // Free prefetch image buffers
  if (decoder->prefetch_image_a) {
    image_destroy(decoder->prefetch_image_a);
    decoder->prefetch_image_a = NULL;
  }
  if (decoder->prefetch_image_b) {
    image_destroy(decoder->prefetch_image_b);
    decoder->prefetch_image_b = NULL;
  }

  // Don't destroy current_image - it points to one of the prefetch buffers
  // which have already been destroyed above
  decoder->current_image = NULL;

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

  // Try to get a prefetched frame from the background thread (preferred path)
  mutex_lock(&decoder->prefetch_mutex);
  if (decoder->prefetch_frame_ready && decoder->current_prefetch_image) {
    // Release the previous buffer (rendering is now complete)
    if (decoder->current_read_buffer == decoder->prefetch_image_a) {
      decoder->buffer_a_in_use = false;
    } else if (decoder->current_read_buffer == decoder->prefetch_image_b) {
      decoder->buffer_b_in_use = false;
    }

    image_t *frame = decoder->current_prefetch_image;
    decoder->prefetch_frame_ready = false;

    // Mark the new buffer as in use (prevent prefetch thread from overwriting it during rendering)
    if (frame == decoder->prefetch_image_a) {
      decoder->buffer_a_in_use = true;
    } else if (frame == decoder->prefetch_image_b) {
      decoder->buffer_b_in_use = true;
    }

    decoder->current_read_buffer = frame;
    mutex_unlock(&decoder->prefetch_mutex);

    // Use the prefetched frame
    decoder->current_image = frame;
    log_dev_every(5 * US_PER_SEC_INT, "Using prefetched frame");
    return frame;
  }
  mutex_unlock(&decoder->prefetch_mutex);

  // No fallback synchronous decode - rely on background prefetch thread
  // Skipping frames when prefetch not ready allows audio timing to advance
  // This is critical for proper audio-video sync when prefetch is active
  log_dev_every(5 * US_PER_SEC_INT,
                "Prefetch frame not ready, skipping to next iteration (allow prefetch to catch up)");
  return NULL;
}

/**
 * @brief Start the background frame prefetching thread
 *
 * Starts a background thread that continuously reads and decodes video frames,
 * storing them in double-buffer structures. The render loop pulls frames from
 * these buffers. This prevents the render thread from blocking on YouTube HTTP requests.
 */
asciichat_error_t ffmpeg_decoder_start_prefetch(ffmpeg_decoder_t *decoder) {
  if (!decoder || decoder->video_stream_idx < 0) {
    return ERROR_INVALID_PARAM;
  }

  if (!decoder->prefetch_image_a || !decoder->prefetch_image_b) {
    return ERROR_INVALID_PARAM;
  }

  // Already running
  if (decoder->prefetch_thread_running) {
    return ASCIICHAT_OK;
  }

  // Reset stop flag and create thread
  decoder->prefetch_should_stop = false;

  int thread_err = asciichat_thread_create(&decoder->prefetch_thread, ffmpeg_decoder_prefetch_thread_func, decoder);
  if (thread_err != 0) {
    return SET_ERRNO(ERROR_THREAD, "Failed to create video prefetch thread");
  }

  decoder->prefetch_thread_running = true;
  return ASCIICHAT_OK;
}

/**
 * @brief Stop the background frame prefetching thread
 */
void ffmpeg_decoder_stop_prefetch(ffmpeg_decoder_t *decoder) {
  if (!decoder || !decoder->prefetch_thread_running) {
    return;
  }

  decoder->prefetch_should_stop = true;
  // Wait up to 2 seconds for thread to stop
  // The interrupt callback should cause av_read_frame to abort quickly
  int join_result = asciichat_thread_join_timeout(&decoder->prefetch_thread, NULL, 2000 * NS_PER_MS_INT);

  if (join_result == 0) {
    // Thread exited successfully
    decoder->prefetch_thread_running = false;
  } else {
    // Timeout: thread is still running (blocked on I/O)
    // Mark as stopped anyway - we'll create a new thread on restart
    // The old thread will eventually finish and exit
    decoder->prefetch_thread_running = false;
  }
}

bool ffmpeg_decoder_is_prefetch_running(ffmpeg_decoder_t *decoder) {
  if (!decoder) {
    return false;
  }
  return decoder->prefetch_thread_running;
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

  // Try avg_frame_rate first (average frame rate from entire stream)
  double fps = av_q2d_safe(stream->avg_frame_rate);

  // Fallback to r_frame_rate if avg_frame_rate is invalid or zero
  // r_frame_rate is the "real" frame rate based on codec parameters
  // This is more reliable for YouTube videos and some video codecs
  if (fps <= 0.0) {
    fps = av_q2d_safe(stream->r_frame_rate);
  }

  return fps;
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
  static uint64_t packet_count = 0;
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

    log_info_every(50 * US_PER_MS_INT, "Audio packet #%lu: pts=%ld dts=%ld duration=%d size=%d", packet_count++,
                   decoder->packet->pts, decoder->packet->dts, decoder->packet->duration, decoder->packet->size);

    // Send packet to decoder
    ret = avcodec_send_packet(decoder->audio_codec_ctx, decoder->packet);
    av_packet_unref(decoder->packet);

    if (ret < 0) {
      log_warn("Error sending audio packet to decoder");
      continue;
    }

    // Receive all decoded frames from this packet
    // Important: a single packet can produce multiple frames. Must drain all before next packet.
    while (1) {
      ret = avcodec_receive_frame(decoder->audio_codec_ctx, decoder->frame);
      if (ret == AVERROR(EAGAIN)) {
        break; // No more frames from this packet, get next packet
      } else if (ret < 0) {
        log_warn("Error receiving audio frame from decoder");
        goto audio_read_done;
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

      if (samples_written >= num_samples) {
        goto audio_read_done;
      }
    }
  }

audio_read_done:
  // Flush resampler buffer if we haven't filled the full request
  // The resampler may have buffered samples that need to be output
  if (samples_written < num_samples) {
    int remaining_space = (int)(num_samples - samples_written);
    uint8_t *out_ptr = (uint8_t *)(buffer + samples_written);
    int flushed = swr_convert(decoder->swr_ctx, &out_ptr, remaining_space, NULL, 0);
    if (flushed > 0) {
      samples_written += (size_t)flushed;
    }
  }

  // Update sample-based position tracking
  decoder->audio_samples_read += samples_written;

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
  decoder->audio_samples_read = 0; // Reset sample counter to 0

  return ASCIICHAT_OK;
}

asciichat_error_t ffmpeg_decoder_seek_to_timestamp(ffmpeg_decoder_t *decoder, double timestamp_sec) {
  if (!decoder) {
    return ERROR_INVALID_PARAM;
  }

  if (decoder->is_stdin) {
    return ERROR_NOT_SUPPORTED; // Cannot seek stdin
  }

  // Hold mutex during entire seek operation to prevent race with prefetch thread
  mutex_lock(&decoder->prefetch_mutex);

  // Set flag to pause prefetch thread via condition variable
  decoder->seeking_in_progress = true;

  // Convert seconds to FFmpeg time base units (AV_TIME_BASE = 1,000,000)
  int64_t target_ts = (int64_t)(timestamp_sec * AV_TIME_BASE);

  // For HTTP streams, use simple keyframe seeking (faster than frame-accurate seeking)
  // HTTP seeking is expensive and can break stream state, so prefer speed over precision
  int seek_ret = av_seek_frame(decoder->format_ctx, -1, target_ts, AVSEEK_FLAG_BACKWARD);
  if (seek_ret < 0) {
    // Fallback: try without backward flag
    seek_ret = av_seek_frame(decoder->format_ctx, -1, target_ts, 0);
  }

  if (seek_ret < 0) {
    decoder->seeking_in_progress = false;
    cond_signal(&decoder->prefetch_cond);
    mutex_unlock(&decoder->prefetch_mutex);
    return SET_ERRNO(ERROR_MEDIA_SEEK, "Failed to seek to timestamp %.2f seconds", timestamp_sec);
  }

  // Flush codec buffers AFTER seeking
  if (decoder->video_codec_ctx) {
    avcodec_flush_buffers(decoder->video_codec_ctx);
  }
  if (decoder->audio_codec_ctx) {
    avcodec_flush_buffers(decoder->audio_codec_ctx);
  }

  // Reset state
  decoder->eof_reached = false;
  decoder->audio_buffer_offset = 0;
  // Clear any stale audio data in buffer
  if (decoder->audio_buffer) {
    memset(decoder->audio_buffer, 0, decoder->audio_buffer_size * sizeof(float));
  }
  decoder->last_video_pts = -1.0;
  decoder->last_audio_pts = -1.0;
  decoder->prefetch_frame_ready = false;
  // Set audio_samples_read to match the seek target so position tracking works correctly
  decoder->audio_samples_read = (uint64_t)(timestamp_sec * decoder->audio_sample_rate);

  // Reset current_read_buffer and mark both buffers as not in use
  // After seeking, the prefetch thread may reallocate buffers, so current_read_buffer
  // could point to freed memory. Clear it so the next read doesn't try to release stale pointers.
  decoder->current_read_buffer = NULL;
  decoder->buffer_a_in_use = false;
  decoder->buffer_b_in_use = false;

  // Resume prefetch thread
  decoder->seeking_in_progress = false;
  cond_signal(&decoder->prefetch_cond);

  // Release mutex - prefetch thread can resume
  mutex_unlock(&decoder->prefetch_mutex);

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

  // Prefer sample-based position tracking (continuous, works before frames are decoded)
  if (decoder->audio_sample_rate > 0 && decoder->audio_samples_read >= 0) {
    return (double)decoder->audio_samples_read / (double)decoder->audio_sample_rate;
  }

  // Fallback to frame-based position if available
  if (decoder->last_video_pts >= 0.0) {
    return decoder->last_video_pts;
  } else if (decoder->last_audio_pts >= 0.0) {
    return decoder->last_audio_pts;
  }

  return -1.0;
}
