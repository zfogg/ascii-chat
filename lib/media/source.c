/**
 * @file media/source.c
 * @brief Unified media source implementation
 */

#include <ascii-chat/media/source.h>
#include <ascii-chat/media/ffmpeg_decoder.h>
#include <ascii-chat/media/yt_dlp.h>
#include <ascii-chat/video/webcam/webcam.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/util.h>
#include <ascii-chat/common/buffer_sizes.h>
#include <ascii-chat/util/time.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Media Source Structure
 * ============================================================================ */

/**
 * @brief Media source for video and audio capture
 *
 * Abstracts multiple media input types (webcam, files, stdin, test patterns)
 * with unified interface for frame/audio reading.
 */
struct media_source_t {
  media_source_type_t type; ///< Type of media source (webcam, file, stdin, test)
  bool loop_enabled;        ///< Whether to loop playback (for files)
  bool is_paused;           ///< Whether playback is paused

  // Webcam context (for WEBCAM and TEST types)
  webcam_context_t *webcam_ctx;    ///< Webcam context (NULL for non-webcam types)
  unsigned short int webcam_index; ///< Webcam device index

  // FFmpeg decoders (for FILE and STDIN types)
  ffmpeg_decoder_t *video_decoder; ///< Video decoder (separate or shared with audio)
  ffmpeg_decoder_t *audio_decoder; ///< Audio decoder (separate or shared with video)
  bool is_shared_decoder;          ///< True if both streams share same decoder (YouTube URLs)

  // Thread synchronization
  mutex_t decoder_mutex;     ///< Protects shared decoder access (YouTube URLs)
  mutex_t seek_access_mutex; ///< Protects decoder access during seeks
  mutex_t pause_mutex;       ///< Protects pause state

  // Cached paths
  char *file_path;            ///< File path (for FILE type)
  char *original_youtube_url; ///< Original YouTube URL for re-extraction

  // Audio integration
  void *audio_ctx; ///< Audio context for clearing buffers on seek (opaque)
};

/* ============================================================================
 * Stream Type Detection
 * ============================================================================ */

/**
 * @brief Check if URL has FFmpeg-native file extension
 */
static bool url_has_ffmpeg_native_extension(const char *url) {
  if (!url)
    return false;

  // Extract extension from URL (ignore query params)
  const char *question = strchr(url, '?');
  size_t url_len = question ? (size_t)(question - url) : strlen(url);

  // Find last dot
  const char *dot = NULL;
  for (const char *p = url + url_len - 1; p >= url; p--) {
    if (*p == '.') {
      dot = p;
      break;
    }
    if (*p == '/')
      break;
  }

  if (!dot)
    return false;

  const char *ext = dot + 1;

  // Video containers
  if (strcasecmp(ext, "mp4") == 0 || strcasecmp(ext, "mkv") == 0 || strcasecmp(ext, "webm") == 0 ||
      strcasecmp(ext, "mov") == 0 || strcasecmp(ext, "avi") == 0 || strcasecmp(ext, "flv") == 0 ||
      strcasecmp(ext, "ogv") == 0 || strcasecmp(ext, "ts") == 0 || strcasecmp(ext, "m2ts") == 0 ||
      strcasecmp(ext, "mts") == 0 || strcasecmp(ext, "3gp") == 0 || strcasecmp(ext, "3g2") == 0 ||
      strcasecmp(ext, "f4v") == 0 || strcasecmp(ext, "asf") == 0 || strcasecmp(ext, "wmv") == 0) {
    return true;
  }

  // Audio containers
  if (strcasecmp(ext, "ogg") == 0 || strcasecmp(ext, "oga") == 0 || strcasecmp(ext, "wma") == 0 ||
      strcasecmp(ext, "wav") == 0 || strcasecmp(ext, "flac") == 0 || strcasecmp(ext, "aac") == 0 ||
      strcasecmp(ext, "m4a") == 0 || strcasecmp(ext, "m4b") == 0 || strcasecmp(ext, "mp3") == 0 ||
      strcasecmp(ext, "aiff") == 0 || strcasecmp(ext, "au") == 0) {
    return true;
  }

  // Streaming
  if (strcasecmp(ext, "m3u8") == 0 || strcasecmp(ext, "mpd") == 0) {
    return true;
  }

  return false;
}

/**
 * @brief Check if URL is a direct stream
 */
static bool url_is_direct_stream(const char *url) {
  if (!url)
    return false;

  // RTSP and RTMP are streaming protocols, always direct
  if (strncmp(url, "rtsp://", 7) == 0 || strncmp(url, "rtmp://", 7) == 0) {
    return true;
  }

  // Check for FFmpeg-native file extension
  return url_has_ffmpeg_native_extension(url);
}

/**
 * @brief Resolve URL to playable stream URL using smart routing
 */
static asciichat_error_t media_source_resolve_url(const char *url, const char *yt_dlp_options, char *output_url,
                                                  size_t output_size) {
  if (!url || !output_url || output_size < 256) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for URL resolution");
    return ERROR_INVALID_PARAM;
  }

  bool is_direct = url_is_direct_stream(url);

  if (is_direct) {
    log_debug("URL is direct stream, passing to FFmpeg directly: %s", url);
    SAFE_STRNCPY(output_url, url, output_size - 1);
    output_url[output_size - 1] = '\0';
    return ASCIICHAT_OK;
  }

  log_debug("URL is complex site, attempting yt-dlp extraction: %s", url);

  // Try yt-dlp extraction (for complex sites like YouTube, Twitch, etc.)
  asciichat_error_t yt_dlp_result = yt_dlp_extract_stream_url(url, yt_dlp_options, output_url, output_size);
  if (yt_dlp_result == ASCIICHAT_OK) {
    log_debug("yt-dlp successfully extracted stream URL");
    return ASCIICHAT_OK;
  }

  log_error("yt-dlp extraction failed for URL: %s", url);

  // For complex sites, try FFmpeg as last resort
  log_debug("yt-dlp failed, trying FFmpeg as fallback for: %s", url);
  SAFE_STRNCPY(output_url, url, output_size - 1);
  output_url[output_size - 1] = '\0';
  log_info("FFmpeg will attempt to handle URL (yt-dlp extraction failed)");
  return ASCIICHAT_OK; // Let FFmpeg try - if it fails, that's ok too
}

/* ============================================================================
 * Media Source Lifecycle
 * ============================================================================ */

media_source_t *media_source_create(media_source_type_t type, const char *path) {
  media_source_t *source = SAFE_MALLOC(sizeof(media_source_t), media_source_t *);
  if (!source) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate media source");
    return NULL;
  }

  memset(source, 0, sizeof(*source));
  source->type = type;
  source->loop_enabled = false;
  source->is_paused = false;

  // Initialize mutex for protecting shared decoder access (for YouTube URLs)
  if (mutex_init(&source->decoder_mutex) != 0) {
    SET_ERRNO(ERROR_MEMORY, "Failed to initialize mutex");
    SAFE_FREE(source);
    return NULL;
  }

  // Initialize mutex for protecting pause state (accessed from keyboard and video threads)
  if (mutex_init(&source->pause_mutex) != 0) {
    SET_ERRNO(ERROR_MEMORY, "Failed to initialize pause mutex");
    mutex_destroy(&source->decoder_mutex);
    SAFE_FREE(source);
    return NULL;
  }

  // Initialize mutex for protecting decoder access during seeks (prevents race conditions)
  if (mutex_init(&source->seek_access_mutex) != 0) {
    SET_ERRNO(ERROR_MEMORY, "Failed to initialize seek access mutex");
    mutex_destroy(&source->decoder_mutex);
    mutex_destroy(&source->pause_mutex);
    SAFE_FREE(source);
    return NULL;
  }

  switch (type) {
  case MEDIA_SOURCE_WEBCAM: {
    // Parse webcam index from path (if provided)
    unsigned short int index = 0;
    if (path) {
      int parsed = atoi(path);
      if (parsed >= 0 && parsed <= USHRT_MAX) {
        index = (unsigned short int)parsed;
      }
    }

    source->webcam_index = index;
    asciichat_error_t webcam_error = webcam_init_context(&source->webcam_ctx, index);
    if (webcam_error != ASCIICHAT_OK) {
      // Webcam init failed - log and cleanup
      log_error("Failed to initialize webcam device %u (error code: %d)", index, webcam_error);
      SAFE_FREE(source);

      // Explicitly re-set errno to preserve the specific error code for the caller
      // (log_error or other calls may have cleared the thread-local errno)
      if (webcam_error == ERROR_WEBCAM_IN_USE) {
        SET_ERRNO(ERROR_WEBCAM_IN_USE, "Webcam device %u is in use", index);
      } else {
        SET_ERRNO(ERROR_WEBCAM, "Failed to initialize webcam device %u", index);
      }
      return NULL;
    }

    log_debug("Media source: Webcam device %u", index);
    break;
  }

  case MEDIA_SOURCE_FILE: {
    if (!path || path[0] == '\0') {
      SET_ERRNO(ERROR_INVALID_PARAM, "File path is required for FILE source");
      SAFE_FREE(source);
      return NULL;
    }

    // Resolve URL using smart FFmpeg/yt-dlp routing
    const char *effective_path = path;
    char resolved_url[BUFFER_SIZE_XLARGE] = {0};
    const char *yt_dlp_options = GET_OPTION(yt_dlp_options);

    log_debug("Resolving URL: %s", path);
    asciichat_error_t resolve_err = media_source_resolve_url(path, yt_dlp_options, resolved_url, sizeof(resolved_url));
    if (resolve_err != ASCIICHAT_OK) {
      log_debug("Failed to resolve URL (error: %d)", resolve_err);
      SAFE_FREE(source);
      return NULL;
    }
    effective_path = resolved_url;
    log_debug("Using resolved URL for playback");

    // Store original URL for potential re-extraction if needed
    source->original_youtube_url = platform_strdup(path);
    if (!source->original_youtube_url) {
      log_warn("Failed to cache original URL");
    }

    // Cache file path for potential reopen on loop
    source->file_path = platform_strdup(effective_path);
    if (!source->file_path) {
      SET_ERRNO(ERROR_MEMORY, "Failed to duplicate file path");
      SAFE_FREE(source->original_youtube_url);
      SAFE_FREE(source);
      return NULL;
    }

    // Always use separate decoders for video and audio
    // This allows independent read rates and avoids lock contention
    source->video_decoder = ffmpeg_decoder_create(effective_path);
    if (!source->video_decoder) {
      log_error("Failed to open media file for video: %s", effective_path);
      SAFE_FREE(source->file_path);
      SAFE_FREE(source->original_youtube_url);
      SAFE_FREE(source);
      return NULL;
    }

    // Start prefetch thread for video frames (critical for HTTP performance)
    // This thread continuously reads frames into a buffer so the render loop never blocks
    asciichat_error_t prefetch_err = ffmpeg_decoder_start_prefetch(source->video_decoder);
    if (prefetch_err != ASCIICHAT_OK) {
      log_error("Failed to start video prefetch thread: %s", asciichat_error_string(prefetch_err));
      // Don't fail on prefetch error - continue with frame skipping as fallback
    }

    source->audio_decoder = ffmpeg_decoder_create(effective_path);
    if (!source->audio_decoder) {
      log_error("Failed to open media file for audio: %s", effective_path);
      ffmpeg_decoder_destroy(source->video_decoder);
      source->video_decoder = NULL;
      SAFE_FREE(source->file_path);
      SAFE_FREE(source->original_youtube_url);
      SAFE_FREE(source);
      return NULL;
    }
    source->is_shared_decoder = false;

    log_debug("Media source: URL resolved to stream (separate video/audio decoders)");
    break;
  }

  case MEDIA_SOURCE_STDIN: {
    // Create separate decoders for video and audio from stdin
    source->video_decoder = ffmpeg_decoder_create_stdin();
    if (!source->video_decoder) {
      log_error("Failed to open stdin for video input");
      SAFE_FREE(source);
      return NULL;
    }

    // Start prefetch thread for stdin video frames
    asciichat_error_t prefetch_err = ffmpeg_decoder_start_prefetch(source->video_decoder);
    if (prefetch_err != ASCIICHAT_OK) {
      log_error("Failed to start stdin video prefetch thread: %s", asciichat_error_string(prefetch_err));
      // Don't fail on prefetch error - continue with frame skipping as fallback
    }

    source->audio_decoder = ffmpeg_decoder_create_stdin();
    if (!source->audio_decoder) {
      log_error("Failed to open stdin for audio input");
      ffmpeg_decoder_destroy(source->video_decoder);
      source->video_decoder = NULL;
      SAFE_FREE(source);
      return NULL;
    }

    log_debug("Media source: stdin (separate video/audio decoders)");
    break;
  }

  case MEDIA_SOURCE_TEST: {
    // Test pattern doesn't need webcam context - it's handled in webcam_read()
    // which checks GET_OPTION(test_pattern) and generates a pattern directly
    source->webcam_index = 0;
    source->webcam_ctx = NULL; // No context needed for test pattern

    log_debug("Media source: Test pattern");
    break;
  }

  default:
    SET_ERRNO(ERROR_INVALID_PARAM, "Unknown media source type: %d", type);
    SAFE_FREE(source);
    return NULL;
  }

  return source;
}

void media_source_destroy(media_source_t *source) {
  if (!source) {
    return;
  }

  // Cleanup based on type
  if (source->webcam_ctx) {
    webcam_cleanup_context(source->webcam_ctx);
    source->webcam_ctx = NULL;
  }

  // For YouTube (shared decoder), only destroy once
  // For local files (separate decoders), destroy both
  if (source->video_decoder) {
    ffmpeg_decoder_destroy(source->video_decoder);
    source->video_decoder = NULL;
  }

  if (source->audio_decoder) {
    // Don't double-free if audio and video share the same decoder
    if (!source->is_shared_decoder) {
      ffmpeg_decoder_destroy(source->audio_decoder);
    }
    source->audio_decoder = NULL;
  }

  if (source->file_path) {
    free(source->file_path);
    source->file_path = NULL;
  }

  if (source->original_youtube_url) {
    free(source->original_youtube_url);
    source->original_youtube_url = NULL;
  }

  // Destroy mutexes
  mutex_destroy(&source->decoder_mutex);
  mutex_destroy(&source->pause_mutex);
  mutex_destroy(&source->seek_access_mutex);

  SAFE_FREE(source);
}

/* ============================================================================
 * Video Operations
 * ============================================================================ */

image_t *media_source_read_video(media_source_t *source) {
  if (!source) {
    return NULL;
  }

  // Check pause state (thread-safe)
  mutex_lock(&source->pause_mutex);
  bool is_paused = source->is_paused;
  mutex_unlock(&source->pause_mutex);

  // Return NULL immediately if paused (maintaining position)
  if (is_paused) {
    return NULL;
  }

  switch (source->type) {
  case MEDIA_SOURCE_WEBCAM:
    // Read from webcam
    if (source->webcam_ctx) {
      return webcam_read_context(source->webcam_ctx);
    }
    return NULL;

  case MEDIA_SOURCE_TEST:
    // Test pattern uses global webcam_read() which checks GET_OPTION(test_pattern)
    return webcam_read();

  case MEDIA_SOURCE_FILE:
  case MEDIA_SOURCE_STDIN: {
    if (!source->video_decoder) {
      return NULL;
    }

    // Lock shared decoder if YouTube URL (protect against concurrent audio thread access)
    if (source->is_shared_decoder) {
      mutex_lock(&source->decoder_mutex);
    }

    uint64_t frame_read_start_ns = time_get_ns();
    image_t *frame = ffmpeg_decoder_read_video_frame(source->video_decoder);
    uint64_t frame_read_ns = time_elapsed_ns(frame_read_start_ns, time_get_ns());

    // Track frame reading statistics for FPS diagnosis
    static uint64_t total_attempts = 0;
    static uint64_t successful_frames = 0;
    static uint64_t null_frame_count = 0;
    static uint64_t total_read_time_ns = 0;
    static uint64_t max_read_time_ns = 0;

    total_attempts++;
    if (frame) {
      successful_frames++;
      total_read_time_ns += frame_read_ns;
      if (frame_read_ns > max_read_time_ns) {
        max_read_time_ns = frame_read_ns;
      }

      // Log statistics every 30 successful frames
      if (successful_frames % 30 == 0) {
        double avg_read_ms = (double)total_read_time_ns / (double)successful_frames / NS_PER_MS;
        double max_read_ms = (double)max_read_time_ns / NS_PER_MS;
        double null_rate = (double)null_frame_count * 100.0 / (double)total_attempts;
        log_info_every(3000000,
                       "FRAME_STATS[%lu]: avg_read=%.2f ms, max_read=%.2f ms, null_rate=%.1f%% "
                       "(%lu null/%lu attempts)",
                       successful_frames, avg_read_ms, max_read_ms, null_rate, null_frame_count, total_attempts);
      }
    } else {
      null_frame_count++;
    }

    // Handle EOF with loop
    if (!frame && ffmpeg_decoder_at_end(source->video_decoder)) {
      if (source->loop_enabled && source->type == MEDIA_SOURCE_FILE) {
        log_debug("End of file reached, rewinding for loop");
        if (media_source_rewind(source) == ASCIICHAT_OK) {
          // Try reading again after rewind
          frame = ffmpeg_decoder_read_video_frame(source->video_decoder);
        }
      }
    }

    // Unlock shared decoder
    if (source->is_shared_decoder) {
      mutex_unlock(&source->decoder_mutex);
    }

    return frame;
  }

  default:
    return NULL;
  }
}

bool media_source_has_video(media_source_t *source) {
  if (!source) {
    return false;
  }

  switch (source->type) {
  case MEDIA_SOURCE_WEBCAM:
  case MEDIA_SOURCE_TEST:
    return true; // Webcam/test always has video

  case MEDIA_SOURCE_FILE:
  case MEDIA_SOURCE_STDIN:
    return source->video_decoder && ffmpeg_decoder_has_video(source->video_decoder);

  default:
    return false;
  }
}

/* ============================================================================
 * Audio Operations
 * ============================================================================ */

size_t media_source_read_audio(media_source_t *source, float *buffer, size_t num_samples) {
  if (!source || !buffer || num_samples == 0) {
    return 0;
  }

  static uint64_t call_count = 0;
  call_count++;
  if (call_count <= 5 || call_count % 1000 == 0) {
    log_info("media_source_read_audio #%lu: source_type=%d num_samples=%zu", call_count, source->type, num_samples);
  }

  // Check pause state (thread-safe)
  mutex_lock(&source->pause_mutex);
  bool is_paused = source->is_paused;
  mutex_unlock(&source->pause_mutex);

  // Return silence immediately if paused (maintaining position)
  if (is_paused) {
    memset(buffer, 0, num_samples * sizeof(float));
    return num_samples;
  }

  switch (source->type) {
  case MEDIA_SOURCE_WEBCAM:
  case MEDIA_SOURCE_TEST:
    // Webcam/test pattern don't provide audio
    return 0;

  case MEDIA_SOURCE_FILE:
  case MEDIA_SOURCE_STDIN: {
    if (!source->audio_decoder) {
      log_warn("media_source_read_audio: audio_decoder is NULL!");
      return 0;
    }

    // Lock seek_access_mutex to prevent audio callback from reading during seek
    mutex_lock(&source->seek_access_mutex);

    // Lock shared decoder if YouTube URL (protect against concurrent video thread access)
    if (source->is_shared_decoder) {
      mutex_lock(&source->decoder_mutex);
    }

    double audio_pos_before_read = ffmpeg_decoder_get_position(source->audio_decoder);
    size_t samples_read = ffmpeg_decoder_read_audio_samples(source->audio_decoder, buffer, num_samples);
    double audio_pos_after_read = ffmpeg_decoder_get_position(source->audio_decoder);

    static double last_audio_pos = 0;
    if (audio_pos_after_read >= 0 && last_audio_pos >= 0 && audio_pos_after_read < last_audio_pos) {
      log_warn("AUDIO POSITION WENT BACKWARD: %.2f → %.2f (LOOPING!)", last_audio_pos, audio_pos_after_read);
    }
    if (audio_pos_after_read >= 0) {
      last_audio_pos = audio_pos_after_read;
    }

    log_info_every(100000, "Audio: read %zu samples, pos %.2f → %.2f", samples_read, audio_pos_before_read,
                   audio_pos_after_read);

    // Handle EOF with loop
    if (samples_read == 0 && ffmpeg_decoder_at_end(source->audio_decoder)) {
      if (source->loop_enabled && source->type == MEDIA_SOURCE_FILE) {
        log_debug("End of file reached (audio), rewinding for loop");
        if (media_source_rewind(source) == ASCIICHAT_OK) {
          // Try reading again after rewind
          samples_read = ffmpeg_decoder_read_audio_samples(source->audio_decoder, buffer, num_samples);
        }
      }
    }

    // Unlock shared decoder
    if (source->is_shared_decoder) {
      mutex_unlock(&source->decoder_mutex);
    }

    // Release seek_access_mutex
    mutex_unlock(&source->seek_access_mutex);

    return samples_read;
  }

  default:
    return 0;
  }
}

bool media_source_has_audio(media_source_t *source) {
  if (!source) {
    return false;
  }

  switch (source->type) {
  case MEDIA_SOURCE_WEBCAM:
  case MEDIA_SOURCE_TEST:
    return false; // Webcam/test don't have audio

  case MEDIA_SOURCE_FILE:
  case MEDIA_SOURCE_STDIN:
    return source->audio_decoder && ffmpeg_decoder_has_audio(source->audio_decoder);

  default:
    return false;
  }
}

/* ============================================================================
 * Playback Control
 * ============================================================================ */

void media_source_set_loop(media_source_t *source, bool loop) {
  if (!source) {
    return;
  }

  source->loop_enabled = loop;

  if (loop && source->type == MEDIA_SOURCE_STDIN) {
    log_warn("Loop mode not supported for stdin input (cannot seek)");
  }
}

bool media_source_at_end(media_source_t *source) {
  if (!source) {
    return true;
  }

  switch (source->type) {
  case MEDIA_SOURCE_WEBCAM:
  case MEDIA_SOURCE_TEST:
    return false; // Webcam never ends

  case MEDIA_SOURCE_FILE:
  case MEDIA_SOURCE_STDIN:
    if (!source->video_decoder) {
      return true;
    }
    // If loop is enabled, we never truly reach end
    if (source->loop_enabled && source->type == MEDIA_SOURCE_FILE) {
      return false;
    }
    return ffmpeg_decoder_at_end(source->video_decoder);

  default:
    return true;
  }
}

asciichat_error_t media_source_rewind(media_source_t *source) {
  if (!source) {
    return ERROR_INVALID_PARAM;
  }

  switch (source->type) {
  case MEDIA_SOURCE_WEBCAM:
  case MEDIA_SOURCE_TEST:
    return ASCIICHAT_OK; // No-op for webcam

  case MEDIA_SOURCE_FILE:
    if (!source->video_decoder || !source->audio_decoder) {
      return ERROR_INVALID_PARAM;
    }

    // Lock shared decoder if YouTube URL (protect against concurrent thread access)
    if (source->is_shared_decoder) {
      mutex_lock(&source->decoder_mutex);
    }

    // Rewind video decoder
    asciichat_error_t video_result = ffmpeg_decoder_rewind(source->video_decoder);
    if (video_result != ASCIICHAT_OK) {
      if (source->is_shared_decoder) {
        mutex_unlock(&source->decoder_mutex);
      }
      return video_result;
    }

    // For YouTube (shared decoder), don't rewind audio separately
    // For local files (separate decoders), rewind audio too
    asciichat_error_t result = ASCIICHAT_OK;
    if (!source->is_shared_decoder) {
      result = ffmpeg_decoder_rewind(source->audio_decoder);
    }

    // Unlock shared decoder
    if (source->is_shared_decoder) {
      mutex_unlock(&source->decoder_mutex);
    }

    return result;

  case MEDIA_SOURCE_STDIN:
    return ERROR_NOT_SUPPORTED; // Cannot seek stdin

  default:
    return ERROR_INVALID_PARAM;
  }
}

asciichat_error_t media_source_sync_audio_to_video(media_source_t *source) {
  // DEPRECATED: This function is deprecated and causes audio playback issues.
  // Seeking the audio decoder to match video position every ~1 second causes
  // audio skips and loops. Audio and video naturally stay synchronized when
  // decoding independently from the same source.

  log_warn("DEPRECATED: media_source_sync_audio_to_video() called - this function causes audio playback issues. "
           "Use natural decode rates instead.");

  if (!source) {
    return ERROR_INVALID_PARAM;
  }

  // Only applicable to FILE and STDIN types
  if (source->type != MEDIA_SOURCE_FILE && source->type != MEDIA_SOURCE_STDIN) {
    return ASCIICHAT_OK; // No-op for WEBCAM/TEST
  }

  // For shared decoders (YouTube URLs), no sync needed (same decoder for both)
  if (source->is_shared_decoder) {
    return ASCIICHAT_OK;
  }

  // NOTE: The actual sync code is disabled because it causes problems.
  // Get video decoder's current PTS
  // double video_pts = ffmpeg_decoder_get_position(source->video_decoder);
  // If we have a valid PTS, seek audio decoder to that position
  // if (video_pts >= 0.0) {
  //   return ffmpeg_decoder_seek_to_timestamp(source->audio_decoder, video_pts);
  // }

  return ASCIICHAT_OK;
}

asciichat_error_t media_source_seek(media_source_t *source, double timestamp_sec) {
  if (!source) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Media source is NULL");
    return ERROR_INVALID_PARAM;
  }

  if (timestamp_sec < 0.0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Timestamp must be >= 0.0");
    return ERROR_INVALID_PARAM;
  }

  // WEBCAM and TEST sources: no-op (always return OK)
  if (source->type == MEDIA_SOURCE_WEBCAM || source->type == MEDIA_SOURCE_TEST) {
    return ASCIICHAT_OK;
  }

  // STDIN: not supported
  if (source->type == MEDIA_SOURCE_STDIN) {
    SET_ERRNO(ERROR_NOT_SUPPORTED, "Cannot seek stdin");
    return ERROR_NOT_SUPPORTED;
  }

  asciichat_error_t result = ASCIICHAT_OK;

  // Clear audio playback buffer BEFORE seeking to prevent old audio from being queued
  // This ensures fresh audio starts playing immediately after seek completes
  if (source->audio_ctx) {
    audio_context_t *audio_ctx = (audio_context_t *)source->audio_ctx;
    if (audio_ctx->playback_buffer) {
      audio_ring_buffer_clear(audio_ctx->playback_buffer);
    }
  }

  // The seeking_in_progress flag in decoders blocks prefetch thread
  // seeking_in_progress coordinates with audio callback via condition variable

  uint64_t seek_start_ns = time_get_ns();
  if (source->video_decoder) {
    double video_pos_before = ffmpeg_decoder_get_position(source->video_decoder);
    asciichat_error_t video_err = ffmpeg_decoder_seek_to_timestamp(source->video_decoder, timestamp_sec);
    double video_pos_after = ffmpeg_decoder_get_position(source->video_decoder);
    uint64_t video_seek_ns = time_elapsed_ns(seek_start_ns, time_get_ns());
    if (video_err != ASCIICHAT_OK) {
      log_warn("Video seek to %.2f failed: error code %d (took %.1fms)", timestamp_sec, video_err,
               (double)video_seek_ns / 1000000.0);
      result = video_err;
    } else {
      log_info("Video SEEK: %.2f → %.2f sec (target %.2f, took %.1fms)", video_pos_before, video_pos_after,
               timestamp_sec, (double)video_seek_ns / 1000000.0);
    }
  }

  // Seek audio decoder (if separate from video)
  if (source->audio_decoder && !source->is_shared_decoder) {
    log_info("=== Starting audio seek to %.2f sec ===", timestamp_sec);
    uint64_t audio_seek_start_ns = time_get_ns();
    double audio_pos_before = ffmpeg_decoder_get_position(source->audio_decoder);
    log_info("Audio position before seek: %.2f", audio_pos_before);
    asciichat_error_t audio_err = ffmpeg_decoder_seek_to_timestamp(source->audio_decoder, timestamp_sec);
    double audio_pos_after = ffmpeg_decoder_get_position(source->audio_decoder);
    uint64_t audio_seek_ns = time_elapsed_ns(audio_seek_start_ns, time_get_ns());
    log_info("Audio position after seek: %.2f", audio_pos_after);
    if (audio_err != ASCIICHAT_OK) {
      log_warn("Audio seek to %.2f failed: error code %d (took %.1fms)", timestamp_sec, audio_err,
               (double)audio_seek_ns / 1000000.0);
      result = audio_err;
    } else {
      log_info("Audio SEEK COMPLETE: %.2f → %.2f sec (target %.2f, took %.1fms)", audio_pos_before, audio_pos_after,
               timestamp_sec, (double)audio_seek_ns / 1000000.0);
    }
  }

  // Prefetch thread automatically resumes when seeking_in_progress is cleared and signaled

  return result;
}

media_source_type_t media_source_get_type(media_source_t *source) {
  return source ? source->type : MEDIA_SOURCE_TEST;
}

double media_source_get_duration(media_source_t *source) {
  if (!source) {
    return -1.0;
  }

  switch (source->type) {
  case MEDIA_SOURCE_WEBCAM:
  case MEDIA_SOURCE_TEST:
    return -1.0; // Infinite

  case MEDIA_SOURCE_FILE:
  case MEDIA_SOURCE_STDIN:
    if (!source->video_decoder) {
      return -1.0;
    }
    return ffmpeg_decoder_get_duration(source->video_decoder);

  default:
    return -1.0;
  }
}

double media_source_get_position(media_source_t *source) {
  if (!source) {
    return -1.0;
  }

  switch (source->type) {
  case MEDIA_SOURCE_WEBCAM:
  case MEDIA_SOURCE_TEST:
    return -1.0; // No position concept

  case MEDIA_SOURCE_FILE:
  case MEDIA_SOURCE_STDIN:
    if (!source->video_decoder) {
      return -1.0;
    }
    return ffmpeg_decoder_get_position(source->video_decoder);

  default:
    return -1.0;
  }
}

double media_source_get_video_fps(media_source_t *source) {
  if (!source) {
    return 0.0;
  }

  switch (source->type) {
  case MEDIA_SOURCE_WEBCAM:
  case MEDIA_SOURCE_TEST:
    return 0.0; // Variable rate, no fixed FPS

  case MEDIA_SOURCE_FILE:
  case MEDIA_SOURCE_STDIN:
    if (!source->video_decoder) {
      return 0.0;
    }
    return ffmpeg_decoder_get_video_fps(source->video_decoder);

  default:
    return 0.0;
  }
}

/* ============================================================================
 * Pause/Resume Control
 * ============================================================================ */

void media_source_pause(media_source_t *source) {
  if (!source) {
    return;
  }
  mutex_lock(&source->pause_mutex);
  source->is_paused = true;
  mutex_unlock(&source->pause_mutex);
}

void media_source_resume(media_source_t *source) {
  if (!source) {
    return;
  }
  mutex_lock(&source->pause_mutex);
  source->is_paused = false;
  mutex_unlock(&source->pause_mutex);
}

bool media_source_is_paused(media_source_t *source) {
  if (!source) {
    return false;
  }
  mutex_lock(&source->pause_mutex);
  bool paused = source->is_paused;
  mutex_unlock(&source->pause_mutex);
  return paused;
}

void media_source_toggle_pause(media_source_t *source) {
  if (!source) {
    return;
  }
  mutex_lock(&source->pause_mutex);
  source->is_paused = !source->is_paused;
  mutex_unlock(&source->pause_mutex);
}

void media_source_set_audio_context(media_source_t *source, void *audio_ctx) {
  if (source) {
    source->audio_ctx = audio_ctx;
  }
}
