/**
 * @file media/source.c
 * @brief Unified media source implementation
 */

#include "source.h"
#include "ffmpeg_decoder.h"
#include "video/webcam/webcam.h"
#include "log/logging.h"
#include "asciichat_errno.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Media Source Structure
 * ============================================================================ */

struct media_source_t {
  media_source_type_t type;
  bool loop_enabled;

  // Webcam context (for WEBCAM and TEST types)
  webcam_context_t *webcam_ctx;
  unsigned short int webcam_index;

  // FFmpeg decoder (for FILE and STDIN types)
  ffmpeg_decoder_t *decoder;

  // Cached path (for FILE type)
  char *file_path;
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

    log_info("Media source: Webcam device %u", index);
    break;
  }

  case MEDIA_SOURCE_FILE: {
    if (!path || path[0] == '\0') {
      SET_ERRNO(ERROR_INVALID_PARAM, "File path is required for FILE source");
      SAFE_FREE(source);
      return NULL;
    }

    // Cache file path for potential reopen on loop
    source->file_path = strdup(path);
    if (!source->file_path) {
      SET_ERRNO(ERROR_MEMORY, "Failed to duplicate file path");
      SAFE_FREE(source);
      return NULL;
    }

    source->decoder = ffmpeg_decoder_create(path);
    if (!source->decoder) {
      log_error("Failed to open media file: %s", path);
      SAFE_FREE(source->file_path);
      SAFE_FREE(source);
      return NULL;
    }

    log_info("Media source: File '%s'", path);
    break;
  }

  case MEDIA_SOURCE_STDIN: {
    source->decoder = ffmpeg_decoder_create_stdin();
    if (!source->decoder) {
      log_error("Failed to open stdin for media input");
      SAFE_FREE(source);
      return NULL;
    }

    log_info("Media source: stdin");
    break;
  }

  case MEDIA_SOURCE_TEST: {
    // Test pattern doesn't need webcam context - it's handled in webcam_read()
    // which checks GET_OPTION(test_pattern) and generates a pattern directly
    source->webcam_index = 0;
    source->webcam_ctx = NULL; // No context needed for test pattern

    log_info("Media source: Test pattern");
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

  if (source->decoder) {
    ffmpeg_decoder_destroy(source->decoder);
    source->decoder = NULL;
  }

  if (source->file_path) {
    free(source->file_path);
    source->file_path = NULL;
  }

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
  case MEDIA_SOURCE_TEST:
    // Read from webcam
    if (source->webcam_ctx) {
      return webcam_read_context(source->webcam_ctx);
    }
    return NULL;

  case MEDIA_SOURCE_FILE:
  case MEDIA_SOURCE_STDIN: {
    if (!source->decoder) {
      return NULL;
    }

    image_t *frame = ffmpeg_decoder_read_video_frame(source->decoder);

    // Handle EOF with loop
    if (!frame && ffmpeg_decoder_at_end(source->decoder)) {
      if (source->loop_enabled && source->type == MEDIA_SOURCE_FILE) {
        log_debug("End of file reached, rewinding for loop");
        if (media_source_rewind(source) == ASCIICHAT_OK) {
          // Try reading again after rewind
          frame = ffmpeg_decoder_read_video_frame(source->decoder);
        }
      }
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
    return source->decoder && ffmpeg_decoder_has_video(source->decoder);

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
    if (!source->decoder) {
      return 0;
    }

    size_t samples_read = ffmpeg_decoder_read_audio_samples(source->decoder, buffer, num_samples);

    // Handle EOF with loop
    if (samples_read == 0 && ffmpeg_decoder_at_end(source->decoder)) {
      if (source->loop_enabled && source->type == MEDIA_SOURCE_FILE) {
        log_debug("End of file reached (audio), rewinding for loop");
        if (media_source_rewind(source) == ASCIICHAT_OK) {
          // Try reading again after rewind
          samples_read = ffmpeg_decoder_read_audio_samples(source->decoder, buffer, num_samples);
        }
      }
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
    return source->decoder && ffmpeg_decoder_has_audio(source->decoder);

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
    if (!source->decoder) {
      return true;
    }
    // If loop is enabled, we never truly reach end
    if (source->loop_enabled && source->type == MEDIA_SOURCE_FILE) {
      return false;
    }
    return ffmpeg_decoder_at_end(source->decoder);

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
    if (!source->decoder) {
      return ERROR_INVALID_PARAM;
    }
    return ffmpeg_decoder_rewind(source->decoder);

  case MEDIA_SOURCE_STDIN:
    return ERROR_NOT_SUPPORTED; // Cannot seek stdin

  default:
    return ERROR_INVALID_PARAM;
  }
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
    if (!source->decoder) {
      return -1.0;
    }
    return ffmpeg_decoder_get_duration(source->decoder);

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
    if (!source->decoder) {
      return -1.0;
    }
    return ffmpeg_decoder_get_position(source->decoder);

  default:
    return -1.0;
  }
}
