/**
 * @file session/capture.c
 * @brief 📹 Unified media capture implementation
 * @ingroup session
 *
 * Implements the session capture abstraction layer for unified media source
 * handling across client, mirror, and discovery modes.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <ascii-chat/session/capture.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/media/source.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/video/image.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/fps.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/asciichat_errno.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * Session Capture Constants
 * ============================================================================ */

/** @brief Maximum frame width for network transmission (bandwidth optimization) */
#define SESSION_MAX_FRAME_WIDTH 480

/** @brief Maximum frame height for network transmission (bandwidth optimization) */
#define SESSION_MAX_FRAME_HEIGHT 270

/* ============================================================================
 * Session Capture Context Structure
 * ============================================================================ */

/**
 * @brief Internal session capture context structure
 *
 * Contains all state for media capture including source, timing, and FPS tracking.
 */
struct session_capture_ctx {
  /** @brief Underlying media source (webcam, file, stdin, test) */
  media_source_t *source;

  /** @brief Whether we own the media source (true) or it was provided externally (false) */
  bool source_owned;

  /** @brief Adaptive sleep state for frame rate limiting */
  adaptive_sleep_state_t sleep_state;

  /** @brief FPS tracker for monitoring capture rate */
  fps_t fps_tracker;

  /** @brief Target frames per second */
  uint32_t target_fps;

  /** @brief Whether to resize frames for network transmission */
  bool resize_for_network;

  /** @brief Context has been successfully initialized */
  bool initialized;

  /** @brief Frame count for FPS calculation */
  uint64_t frame_count;

  /** @brief Start time for FPS calculation (nanoseconds) */
  uint64_t start_time_ns;

  /** @brief Audio is enabled for capture */
  bool audio_enabled;

  /** @brief File has audio stream available */
  bool file_has_audio;

  /** @brief Using file audio (true) or microphone fallback (false) */
  bool using_file_audio;

  /** @brief Fall back to microphone if file has no audio */
  bool audio_fallback_enabled;

  /** @brief Microphone audio context for fallback (borrowed, not owned) */
  void *mic_audio_ctx;

  /** @brief Main audio context for playback (borrowed, not owned) */
  void *audio_ctx;

  /** @brief Pause media source after first frame is read (--pause flag) */
  bool should_pause_after_first_frame;

  /** @brief Whether we've already paused after the first frame */
  bool paused_after_first_frame;
};

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * @brief Calculate optimal frame dimensions for network transmission
 *
 * Implements fit-to-bounds scaling algorithm that maintains original
 * aspect ratio while ensuring frame fits within network size limits.
 *
 * @param original_width Original frame width from source
 * @param original_height Original frame height from source
 * @param max_width Maximum allowed width for transmission
 * @param max_height Maximum allowed height for transmission
 * @param result_width Output parameter for calculated width
 * @param result_height Output parameter for calculated height
 */
static void calculate_optimal_dimensions(ssize_t original_width, ssize_t original_height, ssize_t max_width,
                                         ssize_t max_height, ssize_t *result_width, ssize_t *result_height) {
  // Calculate original aspect ratio
  float img_aspect = (float)original_width / (float)original_height;

  // Check if image needs resizing
  if (original_width <= max_width && original_height <= max_height) {
    // Image is already within bounds - use as-is
    *result_width = original_width;
    *result_height = original_height;
    return;
  }

  // Determine scaling factor based on which dimension is the limiting factor
  if ((float)max_width / (float)max_height > img_aspect) {
    // Max box is wider than image aspect - scale by height
    *result_height = max_height;
    *result_width = (ssize_t)(max_height * img_aspect);
  } else {
    // Max box is taller than image aspect - scale by width
    *result_width = max_width;
    *result_height = (ssize_t)(max_width / img_aspect);
  }
}

/* ============================================================================
 * Session Capture Lifecycle Functions
 * ============================================================================ */

session_capture_ctx_t *session_capture_create(const session_capture_config_t *config) {
  // Auto-create config from command-line options if NULL
  session_capture_config_t auto_config = {0};
  if (!config) {
    // Auto-initialization from GET_OPTION() only works when options have been parsed
    // Verify by checking if we can access options (would SET_ERRNO if not)
    const char *media_file = GET_OPTION(media_file);
    bool media_from_stdin = GET_OPTION(media_from_stdin);

    if (media_file[0] != '\0') {
      // File or stdin streaming
      auto_config.type = media_from_stdin ? MEDIA_SOURCE_STDIN : MEDIA_SOURCE_FILE;
      auto_config.path = media_file;
      auto_config.loop = GET_OPTION(media_loop) && !media_from_stdin;
    } else if (GET_OPTION(test_pattern)) {
      // Test pattern mode
      auto_config.type = MEDIA_SOURCE_TEST;
      auto_config.path = NULL;
    } else {
      // Webcam mode (default)
      static char webcam_index_str[32];
      safe_snprintf(webcam_index_str, sizeof(webcam_index_str), "%u", GET_OPTION(webcam_index));
      auto_config.type = MEDIA_SOURCE_WEBCAM;
      auto_config.path = webcam_index_str;
    }

    // Default settings suitable for local display
    auto_config.target_fps = 60;
    auto_config.resize_for_network = false;
    config = &auto_config;
  }

  // Check if we should exit before starting initialization
  if (config->should_exit_callback && config->should_exit_callback(config->callback_data)) {
    return NULL;
  }

  // Allocate context
  session_capture_ctx_t *ctx = SAFE_CALLOC(1, sizeof(session_capture_ctx_t), session_capture_ctx_t *);

  // Store configuration
  ctx->target_fps = config->target_fps > 0 ? config->target_fps : 60;
  ctx->resize_for_network = config->resize_for_network;

  // Store audio configuration
  ctx->audio_enabled = config->enable_audio;
  ctx->audio_fallback_enabled = config->audio_fallback_to_mic;
  ctx->mic_audio_ctx = config->mic_audio_ctx;
  ctx->using_file_audio = false;
  ctx->file_has_audio = false;

  // Use pre-created media source if provided, otherwise create a new one
  // (Allows reusing media source from probing phase to avoid redundant yt-dlp calls)
  if (config->media_source) {
    ctx->source = config->media_source;
    ctx->source_owned = false; // Caller owns this source
    log_debug("Using pre-created media source (avoids redundant YouTube extraction)");
  } else {
    ctx->source = media_source_create(config->type, config->path);
    ctx->source_owned = true; // We own this source and must destroy it
  }

  if (!ctx->source) {
    // Preserve existing error if set, otherwise set generic error
    asciichat_error_t existing_error = GET_ERRNO();
    if (existing_error == ASCIICHAT_OK) {
      SET_ERRNO(ERROR_MEDIA_INIT, "Failed to create media source");
    }
    SAFE_FREE(ctx);
    return NULL;
  }

  // Detect if media source has audio
  if (ctx->audio_enabled && ctx->source) {
    ctx->file_has_audio = media_source_has_audio(ctx->source);
    if (ctx->file_has_audio) {
      ctx->using_file_audio = true;
      log_info("Audio capture enabled: using file audio");
    } else if (ctx->audio_fallback_enabled && ctx->mic_audio_ctx) {
      ctx->using_file_audio = false;
      log_info("Audio capture enabled: file has no audio, using microphone fallback");
    } else {
      ctx->audio_enabled = false;
      log_debug("Audio capture disabled: no file audio and no fallback configured");
    }
  }

  // Enable loop if requested (only for file sources)
  if (config->loop && config->type == MEDIA_SOURCE_FILE) {
    media_source_set_loop(ctx->source, true);
  }

  // Set initial pause state if requested (only for file sources)
  // Note: We don't pause yet - we'll pause AFTER reading the first frame in the render loop
  // This is because paused sources return NULL from read_video()
  if (config->type == MEDIA_SOURCE_FILE && GET_OPTION(pause)) {
    ctx->should_pause_after_first_frame = true;
    log_debug("Will pause after first frame (--pause flag)");
  }

  // Perform initial seek if requested
  if (config->initial_seek_timestamp > 0.0) {
    log_debug("Seeking to %.2f seconds", config->initial_seek_timestamp);
    asciichat_error_t seek_err = media_source_seek(ctx->source, config->initial_seek_timestamp);
    if (seek_err != ASCIICHAT_OK) {
      log_warn("Failed to seek to %.2f seconds: %d", config->initial_seek_timestamp, seek_err);
      // Continue anyway - seeking is best-effort
    } else {
      log_debug("Successfully seeked to %.2f seconds", config->initial_seek_timestamp);
      // Codec buffers are flushed during seek, so next frame read will use correct position

      // Reset timing state after seek to prevent FPS calculation confusion
      // (frame_count and elapsed_time must match the new playback position)
      ctx->frame_count = 0;
      ctx->start_time_ns = time_get_ns();

      // For snapshot mode with immediate snapshot (delay=0), we need to wait for prefetch thread to
      // decode the seeked frame. This is a workaround for a timing issue where the prefetch thread
      // needs time to decode the frame at the new position after seeking.
      // The sleep duration must be long enough for the prefetch thread to decode one frame.
      float snapshot_delay = GET_OPTION(snapshot_delay);
      if (GET_OPTION(snapshot_mode) && snapshot_delay == 0.0f) {
        // HTTP streams are slow - need 1+ second for first frame decode after seek
        // Local files are faster - 100-200ms is usually sufficient
        // Use 1 second as a safe default to handle both cases
        log_debug("Waiting for prefetch thread after seek (snapshot_delay=0, HTTP streams need ~1 second)");
        platform_sleep_usec(1000000); // 1 second - ensures prefetch thread has delivered seeked frame
      }
    }
  }

  // Initialize adaptive sleep for frame rate limiting
  // Calculate baseline sleep time in nanoseconds from target FPS
  uint64_t baseline_sleep_ns = NS_PER_SEC_INT / ctx->target_fps;
  adaptive_sleep_config_t sleep_config = {
      .baseline_sleep_ns = baseline_sleep_ns,
      .min_speed_multiplier = 0.5, // Allow slowing down to 50% of baseline
      .max_speed_multiplier = 2.0, // Allow speeding up to 200% of baseline
      .speedup_rate = 0.1,         // Adapt by 10% per frame if possible
      .slowdown_rate = 0.1         // Adapt by 10% per frame if possible
  };
  adaptive_sleep_init(&ctx->sleep_state, &sleep_config);

  // Initialize FPS tracker
  // Note: Must allocate tracker_name on heap since fps_init stores a pointer to it
  char *tracker_name = SAFE_MALLOC(32, char *);
  if (!tracker_name) {
    media_source_destroy(ctx->source);
    SAFE_FREE(ctx);
    return NULL;
  }
  safe_snprintf(tracker_name, 32, "CAPTURE_%u", ctx->target_fps);
  fps_init(&ctx->fps_tracker, (int)ctx->target_fps, tracker_name);

  // Record start time for FPS calculation (nanoseconds)
  ctx->start_time_ns = time_get_ns();

  ctx->initialized = true;
  return ctx;
}

void session_capture_destroy(session_capture_ctx_t *ctx) {
  if (!ctx) {
    return;
  }

  // Destroy media source only if we own it (not if provided by caller)
  if (ctx->source && ctx->source_owned) {
    media_source_destroy(ctx->source);
    ctx->source = NULL;
  }

  // Free FPS tracker name (allocated in session_capture_create)
  if (ctx->fps_tracker.tracker_name) {
    char *temp = (char *)ctx->fps_tracker.tracker_name;
    SAFE_FREE(temp);
  }

  ctx->initialized = false;
  SAFE_FREE(ctx);
}

/* ============================================================================
 * Session Capture Operations
 * ============================================================================ */

image_t *session_capture_read_frame(session_capture_ctx_t *ctx) {
  if (!ctx || !ctx->initialized || !ctx->source) {
    return NULL;
  }

  static uint64_t last_frame_time_ns = 0;
  uint64_t frame_request_time_ns = time_get_ns();

  image_t *frame = media_source_read_video(ctx->source);

  if (frame) {
    uint64_t frame_available_time_ns = time_get_ns();

    // Track frame for FPS reporting
    fps_frame_ns(&ctx->fps_tracker, frame_available_time_ns, "frame captured");
    ctx->frame_count++;

    // Log frame-to-frame timing
    if (last_frame_time_ns > 0) {
      uint64_t time_since_last_frame_ns = time_elapsed_ns(last_frame_time_ns, frame_request_time_ns);
      uint64_t time_to_get_frame_ns = time_elapsed_ns(frame_request_time_ns, frame_available_time_ns);
      double since_last_ms = (double)time_since_last_frame_ns / 1000000.0;
      double to_get_ms = (double)time_to_get_frame_ns / 1000000.0;

      if (ctx->frame_count % 30 == 0) {
        log_dev_every(3000000, "FRAME_TIMING[%lu]: since_last=%.1f ms, to_get=%.1f ms", ctx->frame_count, since_last_ms,
                      to_get_ms);
      }
    }
    last_frame_time_ns = frame_available_time_ns;

    // Handle --pause flag: pause after first frame is read
    if (ctx->should_pause_after_first_frame && !ctx->paused_after_first_frame) {
      media_source_pause(ctx->source);
      ctx->paused_after_first_frame = true;
      log_info("Paused (--pause flag)");
    }
  }

  return frame;
}

image_t *session_capture_process_for_transmission(session_capture_ctx_t *ctx, image_t *frame) {
  if (!ctx || !frame) {
    SET_ERRNO(ERROR_INVALID_PARAM, "session_capture_process_for_transmission: NULL parameter");
    return NULL;
  }

  // If resize is not enabled, just create a copy
  if (!ctx->resize_for_network) {
    image_t *copy = image_new(frame->w, frame->h);
    if (!copy) {
      SET_ERRNO(ERROR_MEMORY, "Failed to allocate image copy");
      return NULL;
    }
    memcpy(copy->pixels, frame->pixels, (size_t)frame->w * (size_t)frame->h * sizeof(rgb_pixel_t));
    return copy;
  }

  // Calculate optimal dimensions for network transmission
  ssize_t resized_width, resized_height;
  calculate_optimal_dimensions(frame->w, frame->h, SESSION_MAX_FRAME_WIDTH, SESSION_MAX_FRAME_HEIGHT, &resized_width,
                               &resized_height);

  // Check if resizing is actually needed
  if (frame->w == resized_width && frame->h == resized_height) {
    // No resizing needed - create a copy
    image_t *copy = image_new(frame->w, frame->h);
    if (!copy) {
      SET_ERRNO(ERROR_MEMORY, "Failed to allocate image copy");
      return NULL;
    }
    memcpy(copy->pixels, frame->pixels, (size_t)frame->w * (size_t)frame->h * sizeof(rgb_pixel_t));
    return copy;
  }

  // Create new image for resized frame
  image_t *resized = image_new(resized_width, resized_height);
  if (!resized) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate resized image buffer");
    return NULL;
  }

  // Perform resizing operation
  image_resize(frame, resized);

  return resized;
}

void session_capture_sleep_for_fps(session_capture_ctx_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return;
  }

  // Use adaptive sleep with queue_depth=0, target_depth=0 for constant rate
  adaptive_sleep_do(&ctx->sleep_state, 0, 0);
}

bool session_capture_at_end(session_capture_ctx_t *ctx) {
  if (!ctx || !ctx->initialized || !ctx->source) {
    return true;
  }

  return media_source_at_end(ctx->source);
}

bool session_capture_is_valid(session_capture_ctx_t *ctx) {
  return ctx != NULL && ctx->initialized && ctx->source != NULL;
}

double session_capture_get_current_fps(session_capture_ctx_t *ctx) {
  if (!ctx || !ctx->initialized || ctx->frame_count == 0) {
    return 0.0;
  }

  // Calculate elapsed time in nanoseconds then convert to seconds
  uint64_t elapsed_ns = time_elapsed_ns(ctx->start_time_ns, time_get_ns());
  double elapsed_sec = time_ns_to_s(elapsed_ns);

  if (elapsed_sec <= 0.0) {
    return 0.0;
  }

  return (double)ctx->frame_count / elapsed_sec;
}

uint32_t session_capture_get_target_fps(session_capture_ctx_t *ctx) {
  if (!ctx) {
    return 0;
  }
  return ctx->target_fps;
}

bool session_capture_has_audio(session_capture_ctx_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return false;
  }
  return ctx->audio_enabled;
}

size_t session_capture_read_audio(session_capture_ctx_t *ctx, float *buffer, size_t num_samples) {
  if (!ctx || !ctx->initialized || !buffer || num_samples == 0) {
    return 0;
  }

  if (!ctx->audio_enabled) {
    return 0;
  }

  // If using file audio, read from media source
  if (ctx->using_file_audio && ctx->source) {
    return media_source_read_audio(ctx->source, buffer, num_samples);
  }

  // If using microphone fallback, read from mic context
  if (ctx->mic_audio_ctx) {
    // mic_audio_ctx is actually an audio_context_t pointer
    // We need to read from its capture_buffer audio ring buffer
    audio_context_t *audio_ctx = (audio_context_t *)ctx->mic_audio_ctx;
    if (audio_ctx && audio_ctx->capture_buffer) {
      return audio_ring_buffer_read(audio_ctx->capture_buffer, buffer, num_samples);
    }
  }

  return 0;
}

bool session_capture_using_file_audio(session_capture_ctx_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return false;
  }
  return ctx->using_file_audio;
}

void *session_capture_get_media_source(session_capture_ctx_t *ctx) {
  if (!ctx || !ctx->initialized || !ctx->source) {
    return NULL;
  }
  return (void *)ctx->source;
}

void *session_capture_get_audio_context(session_capture_ctx_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return NULL;
  }
  return ctx->audio_ctx;
}

void session_capture_set_audio_context(session_capture_ctx_t *ctx, void *audio_ctx) {
  if (ctx) {
    ctx->audio_ctx = audio_ctx;
  }
}

asciichat_error_t session_capture_sync_audio_to_video(session_capture_ctx_t *ctx) {
  // DEPRECATED: This function is deprecated. Seeking audio to match video causes
  // audio playback interruptions. Audio and video stay naturally synchronized when
  // decoding independently from the same source.

  if (!ctx || !ctx->initialized || !ctx->source) {
    return ERROR_INVALID_PARAM;
  }
  return media_source_sync_audio_to_video(ctx->source);
}
