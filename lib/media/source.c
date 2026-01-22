/**
 * @file media/source.c
 * @brief Unified media source implementation
 */

#include "source.h"
#include "ffmpeg_decoder.h"
#include "youtube.h"
#include "video/webcam/webcam.h"
#include "log/logging.h"
#include "asciichat_errno.h"
#include "platform/abstraction.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* ============================================================================
 * Media Source Structure
 * ============================================================================ */

struct media_source_t {
  media_source_type_t type;
  bool loop_enabled;

  // Webcam context (for WEBCAM and TEST types)
  webcam_context_t *webcam_ctx;
  unsigned short int webcam_index;

  // FFmpeg decoders (for FILE and STDIN types)
  // STRATEGY:
  // - Local files: Use SEPARATE decoders for video and audio (allows concurrent reads at different rates)
  // - YouTube URLs: Use SHARED decoder for both (YouTube rejects concurrent connections to same URL)
  // - is_shared_decoder: true if both streams use same decoder object
  ffmpeg_decoder_t *video_decoder;
  ffmpeg_decoder_t *audio_decoder;
  bool is_shared_decoder;

  // Mutex to protect shared decoder access from concurrent video/audio threads
  // Only used when is_shared_decoder is true (YouTube URLs)
  pthread_mutex_t decoder_mutex;

  // Cached path (for FILE type) and original YouTube URL for potential re-extraction
  char *file_path;
  char *original_youtube_url;
};

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

  // Initialize mutex for protecting shared decoder access (for YouTube URLs)
  if (pthread_mutex_init(&source->decoder_mutex, NULL) != 0) {
    SET_ERRNO(ERROR_MEMORY, "Failed to initialize mutex");
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

    // Check if this is a YouTube URL and extract direct stream URL
    const char *effective_path = path;
    char extracted_url[2048] = {0};
    bool is_youtube = youtube_is_youtube_url(path);

    if (is_youtube) {
      log_info("Detected YouTube URL, extracting stream URL");
      asciichat_error_t extract_err = youtube_extract_stream_url(path, extracted_url, sizeof(extracted_url));
      if (extract_err != ASCIICHAT_OK) {
        // Note: youtube_extract_stream_url already logs the error via SET_ERRNO on first attempt
        // On cached failures, it returns silently. Only log here if it's not a cached failure.
        // We detect this by checking if there's already an error context (from youtube_extract_stream_url's SET_ERRNO)
        log_debug("Failed to extract YouTube stream URL (error: %d) - error logged by extraction function",
                  extract_err);
        SAFE_FREE(source);
        return NULL;
      }
      effective_path = extracted_url;
      log_debug("Using extracted YouTube stream URL");

      // Store original YouTube URL for potential re-extraction if stream expires
      source->original_youtube_url = strdup(path);
      if (!source->original_youtube_url) {
        log_warn("Failed to cache original YouTube URL");
      }
    }

    // Cache file path for potential reopen on loop
    source->file_path = strdup(effective_path);
    if (!source->file_path) {
      SET_ERRNO(ERROR_MEMORY, "Failed to duplicate file path");
      SAFE_FREE(source->original_youtube_url);
      SAFE_FREE(source);
      return NULL;
    }

    // Create separate decoders for video and audio
    // Both YouTube URLs and local files use separate decoders to allow independent read rates
    // This prevents oscillation issues when video and audio threads read at different times

    source->video_decoder = ffmpeg_decoder_create(effective_path);
    if (!source->video_decoder) {
      log_error("Failed to open media file for video: %s", effective_path);
      SAFE_FREE(source->file_path);
      SAFE_FREE(source->original_youtube_url);
      SAFE_FREE(source);
      return NULL;
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

    if (is_youtube) {
      log_debug("Media source: YouTube (separate video/audio decoders)");
    } else {
      log_debug("Media source: File '%s' (separate video/audio decoders)", effective_path);
    }
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

  // Destroy mutex
  pthread_mutex_destroy(&source->decoder_mutex);

  SAFE_FREE(source);
}

/* ============================================================================
 * Video Operations
 * ============================================================================ */

image_t *media_source_read_video(media_source_t *source) {
  if (!source) {
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
      pthread_mutex_lock(&source->decoder_mutex);
    }

    double pos_before = ffmpeg_decoder_get_position(source->video_decoder);
    image_t *frame = ffmpeg_decoder_read_video_frame(source->video_decoder);
    double pos_after = ffmpeg_decoder_get_position(source->video_decoder);

    if (source->is_shared_decoder && pos_before >= 0 && pos_after >= 0 &&
        (pos_after < pos_before - 1.0 || pos_after > pos_before + 5.0)) {
      log_warn("VIDEO POSITION JUMP: %.2f -> %.2f (delta: %.2f sec)", pos_before, pos_after, pos_after - pos_before);
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
      pthread_mutex_unlock(&source->decoder_mutex);
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

  switch (source->type) {
  case MEDIA_SOURCE_WEBCAM:
  case MEDIA_SOURCE_TEST:
    // Webcam/test pattern don't provide audio
    return 0;

  case MEDIA_SOURCE_FILE:
  case MEDIA_SOURCE_STDIN: {
    if (!source->audio_decoder) {
      return 0;
    }

    // Lock shared decoder if YouTube URL (protect against concurrent video thread access)
    if (source->is_shared_decoder) {
      pthread_mutex_lock(&source->decoder_mutex);
    }

    size_t samples_read = ffmpeg_decoder_read_audio_samples(source->audio_decoder, buffer, num_samples);

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
      pthread_mutex_unlock(&source->decoder_mutex);
    }

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
      pthread_mutex_lock(&source->decoder_mutex);
    }

    // Rewind video decoder
    asciichat_error_t video_result = ffmpeg_decoder_rewind(source->video_decoder);
    if (video_result != ASCIICHAT_OK) {
      if (source->is_shared_decoder) {
        pthread_mutex_unlock(&source->decoder_mutex);
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
      pthread_mutex_unlock(&source->decoder_mutex);
    }

    return result;

  case MEDIA_SOURCE_STDIN:
    return ERROR_NOT_SUPPORTED; // Cannot seek stdin

  default:
    return ERROR_INVALID_PARAM;
  }
}

asciichat_error_t media_source_sync_audio_to_video(media_source_t *source) {
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

  // Get video decoder's current PTS
  double video_pts = ffmpeg_decoder_get_position(source->video_decoder);

  // If we have a valid PTS, seek audio decoder to that position
  if (video_pts >= 0.0) {
    return ffmpeg_decoder_seek_to_timestamp(source->audio_decoder, video_pts);
  }

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

  // For YouTube URLs with shared decoder, lock during seek
  if (source->is_shared_decoder) {
    pthread_mutex_lock(&source->decoder_mutex);
  }

  // Seek video decoder
  if (source->video_decoder) {
    asciichat_error_t video_err = ffmpeg_decoder_seek_to_timestamp(source->video_decoder, timestamp_sec);
    if (video_err != ASCIICHAT_OK) {
      log_warn("Video seek to %.2f failed: error code %d", timestamp_sec, video_err);
      result = video_err;
    }
  }

  // Seek audio decoder (if separate from video)
  if (source->audio_decoder && !source->is_shared_decoder) {
    asciichat_error_t audio_err = ffmpeg_decoder_seek_to_timestamp(source->audio_decoder, timestamp_sec);
    if (audio_err != ASCIICHAT_OK) {
      log_warn("Audio seek to %.2f failed: error code %d", timestamp_sec, audio_err);
      result = audio_err;
    }
  }

  if (source->is_shared_decoder) {
    pthread_mutex_unlock(&source->decoder_mutex);
  }
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
