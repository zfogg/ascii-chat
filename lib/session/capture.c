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

#include "capture.h"
#include "common.h"
#include "log/logging.h"
#include "media/source.h"
#include "options/options.h"
#include "video/image.h"
#include "util/time.h"
#include "util/fps.h"
#include "asciichat_errno.h"

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
      snprintf(webcam_index_str, sizeof(webcam_index_str), "%u", GET_OPTION(webcam_index));
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

  // Create media source
  ctx->source = media_source_create(config->type, config->path);
  if (!ctx->source) {
    // Preserve existing error if set, otherwise set generic error
    asciichat_error_t existing_error = GET_ERRNO();
    if (existing_error == ASCIICHAT_OK) {
      SET_ERRNO(ERROR_MEDIA_INIT, "Failed to create media source");
    }
    SAFE_FREE(ctx);
    return NULL;
  }

  // Enable loop if requested (only for file sources)
  if (config->loop && config->type == MEDIA_SOURCE_FILE) {
    media_source_set_loop(ctx->source, true);
  }

  // Initialize adaptive sleep for frame rate limiting
  // Calculate baseline sleep time in nanoseconds from target FPS
  uint64_t baseline_sleep_ns = NS_PER_SEC_INT / ctx->target_fps;
  adaptive_sleep_config_t sleep_config = {
      .baseline_sleep_ns = baseline_sleep_ns,
      .min_speed_multiplier = 1.0, // Constant rate (no slowdown)
      .max_speed_multiplier = 1.0, // Constant rate (no speedup)
      .speedup_rate = 0.0,         // No adaptive behavior (constant FPS)
      .slowdown_rate = 0.0         // No adaptive behavior (constant FPS)
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
  snprintf(tracker_name, 32, "CAPTURE_%u", ctx->target_fps);
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
  SAFE_FREE(ctx);
}

/* ============================================================================
 * Session Capture Operations
 * ============================================================================ */

image_t *session_capture_read_frame(session_capture_ctx_t *ctx) {
  if (!ctx || !ctx->initialized || !ctx->source) {
    return NULL;
  }

  image_t *frame = media_source_read_video(ctx->source);

  if (frame) {
    // Track frame for FPS reporting
    fps_frame_ns(&ctx->fps_tracker, time_get_ns(), "frame captured");
    ctx->frame_count++;
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
