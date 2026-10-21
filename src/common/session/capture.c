#include <ascii-chat/debug/named.h>
#include <ascii-chat/stats/runtime.h>
#include <ascii-chat/atomic.h>
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

#include "session/capture.h"
#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/media/source.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/video/rgba/image.h>
#include <ascii-chat/video/face_follow_image.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/fps.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/asciichat_errno.h>

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/ui/controller.h>
#include <ascii-chat/ui/frame_buffer.h>
#include <ascii-chat/ui/prompt.h>
#include <ascii-chat/media/playlist.h>
#include <ascii-chat/ui/notice.h>
#include <ascii-chat/common/shutdown.h>

/* ============================================================================
 * Session Capture Constants
 * ============================================================================ */

/** @brief Maximum frame width for network transmission (bandwidth optimization) */
#define SESSION_MAX_FRAME_WIDTH 320

/** @brief Maximum frame height for network transmission (bandwidth optimization) */
#define SESSION_MAX_FRAME_HEIGHT 180

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
  mutex_t source_mutex, load_mutex;
  bool media_locks_ready, loader_created, loader_done;
  asciichat_thread_t loader;
  atomic_t load_cancel;
  atomic_t playlist_skip;
  media_playlist_t playlist, load_playlist;
  bool advance_failed, load_is_advance;
  atomic_t source_epoch, frame_serial, awaiting_presentation;
  media_source_t *loaded_source;
  image_t *loaded_frame, *initial_frame;
  bool initial_frame_pending;
  char load_path[8192], load_error[512];

  face_follow_image_t *face_follow;
  double face_media_position, face_media_tick;
  bool face_media_valid;
  media_source_t *webcam_source;
  image_t *last_media_frame;
  image_t *empty_frame;
  bool webcam_attempted;
  atomic_t toggle_media;
  atomic_t redraw_media;
  bool retained_frame_shown;
  atomic_t non_media_active;
  bool media_was_paused;
  bool saved_matrix, saved_waveform, saved_fft;
  bool non_media_requested;
  media_source_t *saved_audio_source;
  bool saved_playback_only, saved_monitor_media;

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

  /** @brief Next capture deadline on the monotonic clock (nanoseconds) */
  uint64_t next_frame_deadline_ns;

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

static void capture_sync_entries(session_capture_ctx_t *ctx, bool add) {
  if (add)
    NAMED_REGISTER_ATOMIC(&ctx->load_cancel, "capture.load_cancel", NULL);
  else
    NAMED_UNREGISTER(&ctx->load_cancel);
  if (add)
    NAMED_REGISTER_ATOMIC(&ctx->playlist_skip, "capture.playlist_skip", NULL);
  else
    NAMED_UNREGISTER(&ctx->playlist_skip);
  if (add)
    NAMED_REGISTER_ATOMIC(&ctx->source_epoch, "capture.source_epoch", NULL);
  else
    NAMED_UNREGISTER(&ctx->source_epoch);
  if (add)
    NAMED_REGISTER_ATOMIC(&ctx->frame_serial, "capture.frame_serial", NULL);
  else
    NAMED_UNREGISTER(&ctx->frame_serial);
  if (add)
    NAMED_REGISTER_ATOMIC(&ctx->awaiting_presentation, "capture.awaiting_presentation", NULL);
  else
    NAMED_UNREGISTER(&ctx->awaiting_presentation);
  if (add)
    NAMED_REGISTER_ATOMIC(&ctx->toggle_media, "capture.toggle_media", NULL);
  else
    NAMED_UNREGISTER(&ctx->toggle_media);
  if (add)
    NAMED_REGISTER_ATOMIC(&ctx->redraw_media, "capture.redraw_media", NULL);
  else
    NAMED_UNREGISTER(&ctx->redraw_media);
  if (add)
    NAMED_REGISTER_ATOMIC(&ctx->non_media_active, "capture.non_media_active", NULL);
  else
    NAMED_UNREGISTER(&ctx->non_media_active);
}

session_capture_ctx_t *session_mirror_capture_create(const session_capture_config_t *config) {
  // Mirror capture requires a config with media source info
  if (!config) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Mirror capture requires explicit config"), NULL;
  }

  // Delegate to the main implementation
  return session_capture_create(config);
}

session_capture_ctx_t *session_network_capture_create(uint32_t target_fps) {
  // Network modes don't capture media - create minimal context for keyboard/audio only
  session_capture_ctx_t *ctx = SAFE_CALLOC(1, sizeof(session_capture_ctx_t), session_capture_ctx_t *);
  if (!ctx) {
    return NULL;
  }

  // Set FPS (use provided value or default to 60)
  ctx->target_fps = target_fps > 0 ? target_fps : 60;
  ctx->resize_for_network = false;

  // Audio and keyboard will be set up separately by callers
  ctx->audio_enabled = false;
  ctx->source = NULL;

  // Initialize minimal FPS tracker (won't be used but keep structure consistent)
  char *tracker_name = SAFE_MALLOC(32, char *);
  if (!tracker_name) {
    SAFE_FREE(ctx);
    return NULL;
  }
  safe_snprintf(tracker_name, 32, "NETWORK_CAPTURE");
  fps_init(&ctx->fps_tracker, 60, tracker_name);

  ctx->start_time_ns = time_get_ns();
  ctx->initialized = true;
  capture_sync_entries(ctx, true);

  log_debug("Created network capture context (no local media source)");
  return ctx;
}

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
      unsigned int idx = GET_OPTION(webcam_index);
      safe_snprintf(webcam_index_str, sizeof(webcam_index_str), "%u", idx);
      auto_config.type = MEDIA_SOURCE_WEBCAM;
      auto_config.path = webcam_index_str;
      log_debug("Webcam mode: using device index %u", idx);
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

  if (!ctx)
    return NULL;
  if (mutex_init(&ctx->source_mutex, "capture_source") != 0) {
    SAFE_FREE(ctx);
    return NULL;
  }
  if (mutex_init(&ctx->load_mutex, "capture_load") != 0) {
    mutex_destroy(&ctx->source_mutex);
    SAFE_FREE(ctx);
    return NULL;
  }
  ctx->media_locks_ready = true;
  if (config->type == MEDIA_SOURCE_FILE && config->path) {
    media_playlist_t configured = GET_OPTION(media_playlist);
    if (configured.count && strcmp(configured.paths[0], config->path) == 0)
      ctx->playlist = configured;
    else if (media_playlist_append(&ctx->playlist, config->path, strstr(config->path, "://") != NULL) != ASCIICHAT_OK)
      CLEAR_ERRNO();
  }

  // Store configuration
  ctx->target_fps = config->target_fps > 0 ? config->target_fps : 60;
  ctx->resize_for_network = config->resize_for_network;

  // Store audio configuration
  ctx->audio_enabled = config->enable_audio;
  ctx->audio_fallback_enabled = config->audio_fallback_to_mic;
  ctx->mic_audio_ctx = config->mic_audio_ctx;
  ctx->using_file_audio = false;
  ctx->file_has_audio = false;

  // Create media source from config type and path
  asciichat_errno_scope_t create_scope = asciichat_errno_checkpoint();
  ctx->source = media_source_create(config->type, config->path);

  if (!ctx->source) {
    // Preserve existing error if set, otherwise set generic error
    asciichat_error_context_t failure;
    asciichat_error_t existing_error = asciichat_errno_peek_since(create_scope, &failure) ? failure.code : ASCIICHAT_OK;
    if (existing_error == ASCIICHAT_OK) {
      SET_ERRNO(ERROR_MEDIA_INIT, "Failed to create media source");
    }
    if (ctx->media_locks_ready) {
      mutex_destroy(&ctx->load_mutex);
      mutex_destroy(&ctx->source_mutex);
    }
    SAFE_FREE(ctx);
    return NULL;
  }

  if (media_source_start_video(ctx->source) != ASCIICHAT_OK) {
    media_source_destroy(ctx->source);
    if (ctx->media_locks_ready) {
      mutex_destroy(&ctx->load_mutex);
      mutex_destroy(&ctx->source_mutex);
    }
    SAFE_FREE(ctx);
    return NULL;
  }

  if (config->type == MEDIA_SOURCE_FILE) {
    ctx->saved_matrix = GET_OPTION(matrix_rain);
    ctx->saved_waveform = GET_OPTION(waveform);
    ctx->saved_fft = GET_OPTION(fft);
    // Explicit visualization flags also apply to the initial media source.
  }

  // Set up exit callback on media source for graceful shutdown during I/O
  // This allows Ctrl+C to abort blocking FFmpeg calls like YouTube HTTP requests
  if (config->should_exit_callback && ctx->source) {
    media_source_set_exit_callback(ctx->source, config->should_exit_callback, config->callback_data);
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
  if (config->loop && config->type == MEDIA_SOURCE_FILE && ctx->playlist.count <= 1) {
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
    }
  }

  // Initialize FPS tracker
  // Note: Must allocate tracker_name on heap since fps_init stores a pointer to it
  char *tracker_name = SAFE_MALLOC(32, char *);
  if (!tracker_name) {
    media_source_destroy(ctx->source);
    if (ctx->media_locks_ready) {
      mutex_destroy(&ctx->load_mutex);
      mutex_destroy(&ctx->source_mutex);
    }
    SAFE_FREE(ctx);
    return NULL;
  }
  safe_snprintf(tracker_name, 32, "CAPTURE_%u", ctx->target_fps);
  fps_init(&ctx->fps_tracker, (int)ctx->target_fps, tracker_name);

  // Record start time for FPS calculation (nanoseconds)
  ctx->start_time_ns = time_get_ns();

  ctx->initialized = true;
  capture_sync_entries(ctx, true);
  return ctx;
}

void session_capture_destroy(session_capture_ctx_t *ctx) {
  if (!ctx) {
    return;
  }

  if (ctx->loader_created) {
    atomic_store_bool(&ctx->load_cancel, true);
    if (asciichat_thread_join(&ctx->loader, NULL) != 0) {
      log_error("Cannot join media loader; retaining capture resources");
      return;
    }
    ui_controller_remove(UI_SCREEN_RENDER_PROGRESS);
  }
  if (ctx->loaded_frame)
    image_destroy(ctx->loaded_frame);
  if (ctx->initial_frame)
    image_destroy(ctx->initial_frame);
  if (ctx->loaded_source) {
    media_source_destroy(ctx->loaded_source);
    ctx->loaded_source = NULL;
  }
  if (ctx->media_locks_ready) {
    mutex_destroy(&ctx->load_mutex);
    mutex_destroy(&ctx->source_mutex);
  }
  if (ctx->webcam_source)
    media_source_destroy(ctx->webcam_source);
  if (ctx->last_media_frame)
    image_destroy(ctx->last_media_frame);
  if (ctx->empty_frame)
    image_destroy(ctx->empty_frame);
  face_follow_image_destroy(ctx->face_follow);
  ctx->face_follow = NULL;
  // Destroy media source
  if (ctx->source) {
    media_source_destroy(ctx->source);
    ctx->source = NULL;
  }

  // Free FPS tracker name (allocated in session_capture_create)
  if (ctx->fps_tracker.tracker_name) {
    char *temp = (char *)ctx->fps_tracker.tracker_name;
    SAFE_FREE(temp);
  }

  ctx->initialized = false;
  capture_sync_entries(ctx, false);
  SAFE_FREE(ctx);
}

/* ============================================================================
 * Session Capture Operations
 * ============================================================================ */

static bool load_cancelled(void *data) {
  session_capture_ctx_t *ctx = data;
  return shutdown_is_requested() || atomic_load_bool(&ctx->load_cancel);
}

static bool playback_cancelled(void *data) {
  (void)data;
  return shutdown_is_requested();
}

static void render_media_loading(terminal_size_t size, const void *data) {
  (void)data;
  frame_buffer_t *buffer = frame_buffer_create(size.rows, size.cols);
  if (!buffer)
    return;
  ui_media_panel_render(buffer, size, "Loading media...", "Esc cancels; current playback continues.", NULL, 0);
  ui_controller_write(STDERR_FILENO, frame_buffer_get_content(buffer), frame_buffer_get_length(buffer));
  frame_buffer_destroy(buffer);
}

static void *load_media_worker(void *data) {
  session_capture_ctx_t *ctx = data;
  asciichat_errno_scope_t scope = asciichat_errno_scope_begin();
  media_source_t *source = media_source_create_interruptible(MEDIA_SOURCE_FILE, ctx->load_path, load_cancelled, ctx);
  if (source) {
    media_source_set_exit_callback(source, load_cancelled, ctx);
    media_source_set_loop(source, GET_OPTION(media_loop) && ctx->load_playlist.count <= 1);
    if (media_source_start_video(source) != ASCIICHAT_OK) {
      media_source_destroy(source);
      source = NULL;
    }
  }
  image_t *first = NULL;
  if (source && media_source_has_video(source)) {
    uint64_t deadline = time_get_ns() + 15 * NS_PER_SEC_INT;
    while (!load_cancelled(ctx) && time_get_ns() < deadline) {
      asciichat_errno_scope_t read_scope = asciichat_errno_checkpoint();
      image_t *frame = media_source_read_video(source);
      if (frame) {
        first = image_new(frame->w, frame->h);
        if (first)
          memcpy(first->pixels, frame->pixels, (size_t)frame->w * frame->h * sizeof(rgb_pixel_t));
        break;
      }
      asciichat_error_context_t read_failure;
      if (asciichat_errno_peek_since(read_scope, &read_failure) || media_source_at_end(source))
        break;
      platform_sleep_ms(1);
    }
    if (!first) {
      media_source_destroy(source);
      source = NULL;
    }
  }
  asciichat_error_context_t failure;
  char error[512] = "Could not open media";
  if (!source && asciichat_errno_peek_since(scope, &failure))
    snprintf(error, sizeof(error), "%s", failure.context_message);
  asciichat_errno_scope_end(scope, ASCIICHAT_ERRNO_DISMISSED);
  mutex_lock(&ctx->load_mutex);
  ctx->loaded_source = source;
  ctx->loaded_frame = first;
  snprintf(ctx->load_error, sizeof(ctx->load_error), "%s", error);
  ctx->loader_done = true;
  mutex_unlock(&ctx->load_mutex);
  return NULL;
}

bool session_capture_cancel_open(session_capture_ctx_t *ctx) {
  if (!ctx || !ctx->media_locks_ready)
    return false;
  mutex_lock(&ctx->load_mutex);
  bool busy = ctx->loader_created;
  if (busy)
    atomic_store_bool(&ctx->load_cancel, true);
  mutex_unlock(&ctx->load_mutex);
  if (busy)
    ui_controller_remove(UI_SCREEN_RENDER_PROGRESS);
  return busy;
}

static asciichat_error_t capture_load_playlist(session_capture_ctx_t *ctx, const media_playlist_t *list, size_t index,
                                               bool automatic) {
  const char *path = list->paths[index];
  if (!ctx || !ctx->media_locks_ready || !path || strlen(path) >= sizeof(ctx->load_path))
    return SET_ERRNO(ERROR_INVALID_STATE, "Local capture is unavailable");
  size_t limit = strstr(path, "://") ? sizeof(((options_t *)0)->media_url) : sizeof(((options_t *)0)->media_file);
  if (strlen(path) >= limit)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Media path or URL exceeds the configured source limit");
  mutex_lock(&ctx->load_mutex);
  if (ctx->loader_created || ctx->loaded_source) {
    mutex_unlock(&ctx->load_mutex);
    return SET_ERRNO(ERROR_INVALID_STATE, "Previous media load is still active; Escape cancels loading");
  }
  ctx->load_playlist = *list;
  ctx->load_playlist.index = index;
  ctx->load_is_advance = automatic;
  snprintf(ctx->load_path, sizeof(ctx->load_path), "%s", path);
  ctx->loader_done = false;
  atomic_store_bool(&ctx->awaiting_presentation, false);
  atomic_store_bool(&ctx->load_cancel, false);
  asciichat_error_t progress = ui_controller_submit(
      UI_SCREEN_RENDER_PROGRESS, STDERR_FILENO, (terminal_size_t){.cols = 40, .rows = 8}, render_media_loading, "", 1);
  if (progress != ASCIICHAT_OK) {
    mutex_unlock(&ctx->load_mutex);
    return progress;
  }
  if (asciichat_thread_create(&ctx->loader, "media_open", load_media_worker, ctx) != 0) {
    ui_controller_remove(UI_SCREEN_RENDER_PROGRESS);
    mutex_unlock(&ctx->load_mutex);
    return SET_ERRNO(ERROR_THREAD, "Cannot start media loader");
  }
  ctx->loader_created = true;
  mutex_unlock(&ctx->load_mutex);
  return ASCIICHAT_OK;
}

asciichat_error_t session_capture_open_media(session_capture_ctx_t *ctx, const char *path) {
  media_playlist_t list = {0};
  asciichat_error_t result = media_playlist_append(&list, path, path && strstr(path, "://") != NULL);
  return result == ASCIICHAT_OK ? capture_load_playlist(ctx, &list, 0, false) : result;
}
asciichat_error_t session_capture_open_playlist(session_capture_ctx_t *ctx, const char *text) {
  media_playlist_t list = {0};
  asciichat_error_t result = media_playlist_parse(text, &list);
  return result == ASCIICHAT_OK ? capture_load_playlist(ctx, &list, 0, false) : result;
}
void session_capture_skip_media(session_capture_ctx_t *ctx, int direction) {
  if (ctx && (direction == -1 || direction == 1))
    atomic_store_u64(&ctx->playlist_skip, direction > 0 ? 1 : 2);
}

/* Only the capture owner commits replacements, between frame reads. */
static void capture_commit_media(session_capture_ctx_t *ctx) {
  if (!ctx->media_locks_ready)
    return;
  mutex_lock(&ctx->load_mutex);
  if (!ctx->loader_created || !ctx->loader_done) {
    mutex_unlock(&ctx->load_mutex);
    return;
  }
  if (asciichat_thread_join(&ctx->loader, NULL) != 0) {
    mutex_unlock(&ctx->load_mutex);
    log_error("Cannot join media loader; deferring replacement");
    return;
  }
  char path[8192], failure[512];
  snprintf(path, sizeof(path), "%s", ctx->load_path);
  snprintf(failure, sizeof(failure), "%s", ctx->load_error);
  ctx->loader_created = false;
  media_source_t *replacement = ctx->loaded_source;
  ctx->loaded_source = NULL;
  image_t *first = ctx->loaded_frame;
  ctx->loaded_frame = NULL;
  media_playlist_t loaded_playlist = ctx->load_playlist;
  bool cancelled = atomic_load_bool(&ctx->load_cancel);
  bool automatic = ctx->load_is_advance;
  mutex_unlock(&ctx->load_mutex);
  if (cancelled || !replacement) {
    ctx->advance_failed |= automatic;
    ui_controller_remove(UI_SCREEN_RENDER_PROGRESS);
    if (first)
      image_destroy(first);
    if (replacement)
      media_source_destroy(replacement);
    if (!cancelled)
      NOTICE(WARNING, "MEDIA COULD NOT BE OPENED", "%s", failure);
    CLEAR_ERRNO();
    return;
  }
  audio_context_t *audio = ctx->audio_ctx;
  if (audio && audio_stop_duplex(audio) != ASCIICHAT_OK) {
    ctx->advance_failed |= automatic;
    ui_controller_remove(UI_SCREEN_RENDER_PROGRESS);
    if (first)
      image_destroy(first);
    media_source_destroy(replacement);
    LOG_ERRNO_IF_SET("Cannot stop audio for media replacement");
    CLEAR_ERRNO();
    return;
  }
  bool has_audio = media_source_has_audio(replacement);
  media_source_set_exit_callback(replacement, playback_cancelled, ctx);
  if (audio) {
    media_source_t *old_audio_source = audio->capture_media_source;
    bool old_playback_only = audio->playback_only;
    bool use_media = has_audio && GET_OPTION(audio_capture_source) != AUDIO_CAPTURE_SOURCE_MIC;
    audio->capture_media_source = use_media ? replacement : NULL;
    audio->playback_only = !audio_should_enable_microphone(GET_OPTION(audio_capture_source), has_audio);
    media_source_set_audio_context(replacement, audio);
    audio_flush_playback_buffers(audio);
    if (audio->capture_buffer)
      audio_ring_buffer_clear(audio->capture_buffer);
    if (audio_start_duplex(audio) != ASCIICHAT_OK) {
      ctx->advance_failed |= automatic;
      ui_controller_remove(UI_SCREEN_RENDER_PROGRESS);
      LOG_ERRNO_IF_SET("Cannot start replacement audio; restoring previous source");
      CLEAR_ERRNO();
      if (first)
        image_destroy(first);
      if (audio_stop_duplex(audio) != ASCIICHAT_OK) {
        /* A live worker may still borrow replacement; retain it until teardown. */
        mutex_lock(&ctx->load_mutex);
        ctx->loaded_source = replacement;
        mutex_unlock(&ctx->load_mutex);
        NOTICE(WARNING, "MEDIA AUDIO", "Could not stop replacement audio");
        CLEAR_ERRNO();
        return;
      }
      audio->capture_media_source = old_audio_source;
      audio->playback_only = old_playback_only;
      media_source_destroy(replacement);
      if (audio_start_duplex(audio) != ASCIICHAT_OK) {
        LOG_ERRNO_IF_SET("Cannot restore previous audio");
        CLEAR_ERRNO();
      }
      NOTICE(WARNING, "MEDIA AUDIO", "Could not start replacement audio; previous video retained");
      CLEAR_ERRNO();
      return;
    }
  }
  media_source_t *previous = ctx->source;
  bool was_file = media_source_get_type(previous) == MEDIA_SOURCE_FILE;
  if (!was_file && !ctx->webcam_source && media_source_uses_webcam(previous)) {
    ctx->webcam_source = previous;
    ctx->webcam_attempted = true;
    previous = NULL;
  }
  ctx->source = replacement;
  if (ctx->initial_frame)
    image_destroy(ctx->initial_frame);
  ctx->initial_frame = first;
  ctx->initial_frame_pending = first != NULL;
  ctx->file_has_audio = has_audio;
  ctx->using_file_audio = ctx->file_has_audio;
  ctx->audio_enabled = audio != NULL;
  ctx->saved_audio_source = replacement;
  ctx->non_media_requested = false;
  atomic_store_bool(&ctx->non_media_active, false);
  atomic_store_u64(&ctx->toggle_media, 0);
  if (!was_file) {
    ctx->saved_matrix = GET_OPTION(matrix_rain);
    ctx->saved_waveform = GET_OPTION(waveform);
    ctx->saved_fft = GET_OPTION(fft);
  }
  options_set_bool("matrix_rain", false);
  options_set_bool("waveform", false);
  options_set_bool("fft", false);
  ctx->retained_frame_shown = false;
  if (ctx->last_media_frame)
    image_destroy(ctx->last_media_frame);
  ctx->last_media_frame = NULL;
  face_follow_image_destroy(ctx->face_follow);
  ctx->face_follow = NULL;
  ctx->face_media_valid = false;
  ctx->next_frame_deadline_ns = 0;
  ctx->frame_count = 0;
  ctx->start_time_ns = time_get_ns();
  if (!GET_OPTION(fps_explicitly_set)) {
    double fps = media_source_get_video_fps(replacement);
    if (fps >= 0.5 && fps <= UINT32_MAX - 0.5) {
      ctx->target_fps = (uint32_t)(fps + 0.5);
      fps_init(&ctx->fps_tracker, (int)ctx->target_fps, ctx->fps_tracker.tracker_name);
    }
  }
  ctx->should_pause_after_first_frame = false;
  ctx->paused_after_first_frame = false;
  if (audio) {
    ctx->saved_audio_source = audio->capture_media_source;
    ctx->saved_playback_only = audio->playback_only;
    ctx->saved_monitor_media = audio->monitor_local_media;
  }
  if (previous)
    media_source_destroy(previous);
  bool url = strstr(path, "://") != NULL;
  options_set_bool("media_input", true);
  options_set_string("media_url", url ? path : "");
  options_set_string("media_file", url ? "" : path);
  options_set_bool("media_from_stdin", false);
  mutex_lock(&ctx->load_mutex);
  ctx->playlist = loaded_playlist;
  ctx->advance_failed = false;
  atomic_fetch_add_u64(&ctx->source_epoch, 1);
  atomic_store_bool(&ctx->awaiting_presentation, !ctx->loader_created);
  mutex_unlock(&ctx->load_mutex);
  log_info("Opened media: %s", path);
  CLEAR_ERRNO();
}

void session_capture_lock_source(session_capture_ctx_t *ctx) {
  if (ctx && ctx->media_locks_ready)
    mutex_lock(&ctx->source_mutex);
}
void session_capture_unlock_source(session_capture_ctx_t *ctx) {
  if (ctx && ctx->media_locks_ready)
    mutex_unlock(&ctx->source_mutex);
}

static image_t *capture_read_frame(session_capture_ctx_t *ctx) {
  capture_commit_media(ctx);
  mutex_lock(&ctx->load_mutex);
  bool loading = ctx->loader_created;
  mutex_unlock(&ctx->load_mutex);
  if (!loading) {
    uint64_t skip = atomic_exchange_u64(&ctx->playlist_skip, 0);
    bool ended =
        ctx->source && media_source_get_type(ctx->source) == MEDIA_SOURCE_FILE && media_source_at_end(ctx->source);
    size_t next;
    bool adjacent = media_playlist_adjacent(&ctx->playlist, skip == 2 ? -1 : 1, &next);
    if (!skip && ended && !adjacent && GET_OPTION(media_loop) && ctx->playlist.count > 1) {
      next = 0;
      adjacent = true;
    }
    if ((skip || (ended && !ctx->advance_failed)) && adjacent) {
      if (capture_load_playlist(ctx, &ctx->playlist, next, !skip) != ASCIICHAT_OK) {
        ctx->advance_failed |= !skip;
        LOG_ERRNO_IF_SET("Cannot advance media playlist");
        CLEAR_ERRNO();
      }
    }
  }
  if (!ctx || !ctx->initialized || !ctx->source) {
    return NULL;
  }

  if (ctx->initial_frame && !ctx->initial_frame_pending) {
    image_destroy(ctx->initial_frame);
    ctx->initial_frame = NULL;
  }
  static uint64_t last_frame_time_ns = 0;
  uint64_t frame_request_time_ns = time_get_ns();

  bool is_file = media_source_get_type(ctx->source) == MEDIA_SOURCE_FILE;
  if (is_file && (atomic_exchange_u64(&ctx->toggle_media, 0) & 1)) {
    bool leaving = !atomic_load_bool(&ctx->non_media_active);
    if (leaving) {
      ctx->media_was_paused = media_source_is_paused(ctx->source);
      media_source_pause(ctx->source);
    }
    audio_context_t *audio = ctx->audio_ctx;
    if (audio && audio_stop_duplex(audio) == ASCIICHAT_OK) {
      if (leaving) {
        ctx->saved_audio_source = audio->capture_media_source;
        ctx->saved_playback_only = audio->playback_only;
        ctx->saved_monitor_media = audio->monitor_local_media;
        audio->capture_media_source = NULL;
        audio->playback_only = !audio_should_enable_microphone(GET_OPTION(audio_capture_source), false);
        audio->monitor_local_media = false;
      } else {
        audio->capture_media_source = ctx->saved_audio_source;
        audio->playback_only = ctx->saved_playback_only;
        audio->monitor_local_media = ctx->saved_monitor_media;
      }
      if (audio->capture_buffer)
        audio_ring_buffer_clear(audio->capture_buffer);
      if (audio_start_duplex(audio) != ASCIICHAT_OK) {
        LOG_ERRNO_IF_SET("Failed to switch audio input");
        CLEAR_ERRNO();
      }
    }
    if (!leaving && !ctx->media_was_paused)
      media_source_resume(ctx->source);
    atomic_store_bool(&ctx->non_media_active, leaving);
    ctx->face_media_valid = false;
    face_follow_image_destroy(ctx->face_follow);
    ctx->face_follow = NULL;
    atomic_store_bool(&ctx->redraw_media, true);
    ctx->next_frame_deadline_ns = 0;
    log_info("Media playback %s at %.3fs (%s)", leaving ? "hidden" : "selected", media_source_get_position(ctx->source),
             ctx->media_was_paused ? "paused" : "playing");
  }
  bool alternate = atomic_load_bool(&ctx->non_media_active);
  if (alternate && !ctx->webcam_attempted) {
    ctx->webcam_attempted = true;
    ctx->webcam_source = media_source_create(MEDIA_SOURCE_WEBCAM, NULL);
    if (ctx->webcam_source && media_source_start_video(ctx->webcam_source) != ASCIICHAT_OK) {
      media_source_destroy(ctx->webcam_source);
      ctx->webcam_source = NULL;
    }
    if (!ctx->webcam_source) {
      LOG_ERRNO_IF_SET("Webcam unavailable; animations remain available");
      CLEAR_ERRNO();
      ctx->empty_frame = image_new(320, 180);
      if (ctx->empty_frame)
        memset(ctx->empty_frame->pixels, 0, 320 * 180 * sizeof(rgb_pixel_t));
    }
  }
  image_t *frame;
  if (!alternate && ctx->initial_frame_pending) {
    frame = ctx->initial_frame;
    ctx->initial_frame_pending = false;
  } else {
    frame = alternate ? (ctx->webcam_source ? media_source_read_video(ctx->webcam_source) : ctx->empty_frame)
                      : media_source_read_video(ctx->source);
  }
  if (frame && is_file && !alternate) {
    double now = (double)time_get_ns() / 1e9;
    double position = media_source_get_position(ctx->source);
    if (ctx->face_media_valid && (position < ctx->face_media_position - 0.1 ||
                                  position > ctx->face_media_position + (now - ctx->face_media_tick) + 0.75)) {
      face_follow_image_destroy(ctx->face_follow);
      ctx->face_follow = NULL;
    }
    ctx->face_media_valid = true;
    ctx->face_media_position = position;
    ctx->face_media_tick = now;
  }
  if (is_file && !alternate && !GET_OPTION(snapshot_mode) && !GET_OPTION(render_file)[0]) {
    if (frame) {
      ctx->retained_frame_shown = false;
      if (!ctx->last_media_frame || ctx->last_media_frame->w != frame->w || ctx->last_media_frame->h != frame->h) {
        if (ctx->last_media_frame)
          image_destroy(ctx->last_media_frame);
        ctx->last_media_frame = image_new(frame->w, frame->h);
      }
      if (ctx->last_media_frame)
        memcpy(ctx->last_media_frame->pixels, frame->pixels, (size_t)frame->w * frame->h * sizeof(rgb_pixel_t));
    } else if (media_source_at_end(ctx->source) || media_source_is_paused(ctx->source)) {
      if (media_source_at_end(ctx->source))
        media_source_pause(ctx->source);
      bool redraw = atomic_exchange_bool(&ctx->redraw_media, false);
      if (!ctx->retained_frame_shown || redraw) {
        ctx->retained_frame_shown = true;
        bool debug = false;
#ifndef NDEBUG
        debug = GET_OPTION(face_follow_debug);
#endif
        return face_follow_image_process(&ctx->face_follow, ctx->last_media_frame, (double)time_get_ns() / 1e9,
                                         GET_OPTION(face_follow), debug,
                                         ctx->resize_for_network ? SESSION_MAX_FRAME_WIDTH : 960,
                                         ctx->resize_for_network ? SESSION_MAX_FRAME_HEIGHT : 720);
      }
      return NULL;
    }
  }
  stats_duration_record(stats_runtime_scope(), STATS_DURATION_CAPTURE, time_get_ns() - frame_request_time_ns);
  if (frame) {
    stats_counter_add(stats_runtime_scope(), STATS_COUNTER_FRAMES_CAPTURED, 1);
    stats_runtime_media(frame->w, frame->h, media_source_get_position(ctx->source),
                        media_source_get_duration(ctx->source));
  }

  if (frame) {
    uint64_t frame_available_time_ns = time_get_ns();

    // Track frame for FPS reporting
    fps_frame_ns(&ctx->fps_tracker, frame_available_time_ns, "frame captured");
    ctx->frame_count++;
    atomic_fetch_add_u64(&ctx->frame_serial, 1);

    // Log frame-to-frame timing
    if (last_frame_time_ns > 0) {
      uint64_t time_since_last_frame_ns = time_elapsed_ns(last_frame_time_ns, frame_request_time_ns);
      uint64_t time_to_get_frame_ns = time_elapsed_ns(frame_request_time_ns, frame_available_time_ns);
      double since_last_ms = (double)time_since_last_frame_ns / NS_PER_MS;
      double to_get_ms = (double)time_to_get_frame_ns / NS_PER_MS;

      if (ctx->frame_count % 30 == 0) {
        log_dev_every(3 * US_PER_SEC_INT, "FRAME_TIMING[%lu]: since_last=%.1f ms, to_get=%.1f ms", ctx->frame_count,
                      since_last_ms, to_get_ms);
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

  bool debug = false;
#ifndef NDEBUG
  debug = GET_OPTION(face_follow_debug);
#endif
  return face_follow_image_process(&ctx->face_follow, frame, (double)time_get_ns() / 1e9, GET_OPTION(face_follow),
                                   debug, ctx->resize_for_network ? SESSION_MAX_FRAME_WIDTH : 960,
                                   ctx->resize_for_network ? SESSION_MAX_FRAME_HEIGHT : 720);
}

image_t *session_capture_read_frame(session_capture_ctx_t *ctx) {
  if (!ctx)
    return NULL;
  session_capture_lock_source(ctx);
  image_t *frame = capture_read_frame(ctx);
  session_capture_unlock_source(ctx);
  return frame;
}

asciichat_error_t session_capture_process_for_transmission_into(session_capture_ctx_t *ctx, image_t *frame,
                                                                image_t **output) {
  if (!ctx || !frame || !output)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing transmission frame or workspace");
  ssize_t width = frame->w, height = frame->h;
  if (ctx->resize_for_network)
    calculate_optimal_dimensions(frame->w, frame->h, SESSION_MAX_FRAME_WIDTH, SESSION_MAX_FRAME_HEIGHT, &width,
                                 &height);
  if (!*output || (*output)->w != width || (*output)->h != height) {
    image_t *replacement = image_new(width, height);
    if (!replacement)
      return SET_ERRNO(ERROR_MEMORY, "Cannot reserve transmission image workspace");
    image_destroy(*output);
    *output = replacement;
  }
  if (frame != *output) {
    if (frame->w == width && frame->h == height)
      memcpy((*output)->pixels, frame->pixels, (size_t)width * (size_t)height * sizeof(rgb_pixel_t));
    else
      image_resize(frame, *output);
  }
  return ASCIICHAT_OK;
}

image_t *session_capture_process_for_transmission(session_capture_ctx_t *ctx, image_t *frame) {
  image_t *output = NULL;
  if (session_capture_process_for_transmission_into(ctx, frame, &output) != ASCIICHAT_OK)
    return NULL;
  return output;
}

void session_capture_sleep_for_fps(session_capture_ctx_t *ctx) {
  if (!ctx || !ctx->initialized || !ctx->source) {
    return;
  }

  uint64_t frame_interval_ns = NS_PER_SEC_INT / ctx->target_fps;
  if (frame_interval_ns == 0) {
    return;
  }

  uint64_t now_ns = time_get_ns();
  if (ctx->next_frame_deadline_ns == 0) {
    ctx->next_frame_deadline_ns = now_ns + frame_interval_ns;
  } else {
    ctx->next_frame_deadline_ns += frame_interval_ns;
  }

  if (now_ns >= ctx->next_frame_deadline_ns) {
    // Skip missed slots instead of adding another full frame interval to the
    // work time or attempting a catch-up burst.
    uint64_t missed_intervals = (now_ns - ctx->next_frame_deadline_ns) / frame_interval_ns + 1;
    ctx->next_frame_deadline_ns += missed_intervals * frame_interval_ns;
    return;
  }

  platform_sleep_ns(ctx->next_frame_deadline_ns - now_ns);
}

bool session_capture_at_end(session_capture_ctx_t *ctx) {
  if (!ctx || !ctx->initialized || !ctx->source) {
    return true;
  }

  mutex_lock(&ctx->load_mutex);
  bool pending = ctx->loader_created;
  mutex_unlock(&ctx->load_mutex);
  size_t next;
  if (pending || (!ctx->advance_failed && (media_playlist_adjacent(&ctx->playlist, 1, &next) ||
                                           (GET_OPTION(media_loop) && ctx->playlist.count > 1))))
    return false;
  if (media_source_get_type(ctx->source) == MEDIA_SOURCE_FILE && !GET_OPTION(snapshot_mode) &&
      !GET_OPTION(render_file)[0])
    return false;
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

static size_t capture_read_audio(session_capture_ctx_t *ctx, float *buffer, size_t num_samples) {
  if (!ctx || !ctx->initialized || !buffer || num_samples == 0) {
    return 0;
  }

  if (!ctx->audio_enabled) {
    return 0;
  }

  // If using file audio, read from media source
  if (ctx->using_file_audio && ctx->source && !atomic_load_bool(&ctx->non_media_active)) {
    return media_source_read_audio(ctx->source, buffer, num_samples);
  }

  // If using microphone fallback, read from mic context
  if (ctx->mic_audio_ctx || (ctx->audio_ctx && atomic_load_bool(&ctx->non_media_active))) {
    audio_context_t *audio_ctx = ctx->mic_audio_ctx ? ctx->mic_audio_ctx : ctx->audio_ctx;
    if (audio_ctx && audio_ctx->capture_buffer) {
      return audio_ring_buffer_read(audio_ctx->capture_buffer, buffer, num_samples);
    }
  }

  return 0;
}

size_t session_capture_read_audio(session_capture_ctx_t *ctx, float *buffer, size_t num_samples) {
  session_capture_lock_source(ctx);
  size_t count = capture_read_audio(ctx, buffer, num_samples);
  session_capture_unlock_source(ctx);
  return count;
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
    session_capture_lock_source(ctx);
    ctx->audio_ctx = audio_ctx;
    session_capture_unlock_source(ctx);
  }
}

void session_capture_toggle_media(session_capture_ctx_t *ctx) {
  if (!ctx || !ctx->source || media_source_get_type(ctx->source) != MEDIA_SOURCE_FILE)
    return;
  // Swap UI preferences in input order so a following animation key applies to
  // the newly selected mode even before the capture thread switches devices.
  ctx->non_media_requested = !ctx->non_media_requested;
  options_set_bool("media_input", !ctx->non_media_requested);
  if (ctx->non_media_requested) {
    options_set_bool("matrix_rain", ctx->saved_matrix);
    options_set_bool("waveform", ctx->saved_waveform);
    options_set_bool("fft", ctx->saved_fft);
  } else {
    ctx->saved_matrix = GET_OPTION(matrix_rain);
    ctx->saved_waveform = GET_OPTION(waveform);
    ctx->saved_fft = GET_OPTION(fft);
    options_set_bool("matrix_rain", false);
    options_set_bool("waveform", false);
    options_set_bool("fft", false);
  }
  atomic_fetch_add_u64(&ctx->toggle_media, 1);
}

bool session_capture_media_active(session_capture_ctx_t *ctx) {
  return ctx && !atomic_load_bool(&ctx->non_media_active);
}

void session_capture_redraw_media(session_capture_ctx_t *ctx) {
  if (ctx)
    atomic_store_bool(&ctx->redraw_media, true);
}

uint64_t session_capture_source_epoch(session_capture_ctx_t *ctx) {
  return ctx ? atomic_load_u64(&ctx->source_epoch) : 0;
}
uint64_t session_capture_frame_serial(session_capture_ctx_t *ctx) {
  return ctx ? atomic_load_u64(&ctx->frame_serial) : 0;
}
void session_capture_complete_open(session_capture_ctx_t *ctx, uint64_t epoch) {
  if (!ctx)
    return;
  mutex_lock(&ctx->load_mutex);
  if (epoch == session_capture_source_epoch(ctx) && atomic_exchange_bool(&ctx->awaiting_presentation, false))
    ui_controller_remove(UI_SCREEN_RENDER_PROGRESS);
  mutex_unlock(&ctx->load_mutex);
}
