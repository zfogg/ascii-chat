/**
 * @file session/capture.h
 * @brief 📹 Unified media capture abstraction for session-based video sources
 * @ingroup session
 * @addtogroup session
 * @{
 *
 * This header provides a unified interface for media capture that abstracts
 * over webcam, file, stdin, and test pattern sources. It is designed to be
 * reusable across different modes (client, mirror, and discovery mode).
 *
 * CORE FEATURES:
 * ==============
 * - Unified API for all media source types (webcam, file, stdin, test)
 * - Configurable frame rate with adaptive sleep
 * - Optional frame resizing for network transmission
 * - FPS tracking and reporting
 * - Loop support for file sources
 *
 * USAGE:
 * ======
 * @code{.c}
 * // Create capture context for webcam at 60 FPS
 * session_capture_config_t config = {
 *     .type = MEDIA_SOURCE_WEBCAM,
 *     .path = "0",
 *     .target_fps = 60,
 *     .loop = false,
 *     .resize_for_network = false
 * };
 * session_capture_ctx_t *ctx = session_capture_create(&config);
 *
 * // Read and process frames
 * while (!done) {
 *     image_t *frame = session_capture_read_frame(ctx);
 *     if (frame) {
 *         // Process frame...
 *     }
 *     session_capture_sleep_for_fps(ctx);
 * }
 *
 * // Cleanup
 * session_capture_destroy(ctx);
 * @endcode
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "media/source.h"
#include "video/image.h"

/* ============================================================================
 * Session Capture Configuration
 * ============================================================================ */

/**
 * @brief Callback type to check if initialization should be cancelled
 *
 * Called periodically during initialization to allow graceful cancellation.
 * Should return true if initialization should stop immediately.
 *
 * @param user_data Opaque pointer provided by caller
 * @return true to cancel initialization, false to continue
 */
typedef bool (*session_capture_should_exit_fn)(void *user_data);

/**
 * @brief Configuration for session capture context
 *
 * Specifies the media source type, path, and capture parameters.
 *
 * @ingroup session
 */
typedef struct {
  /** @brief Media source type (WEBCAM, FILE, STDIN, TEST) */
  media_source_type_t type;

  /** @brief Device index (for webcam) or file path (for file sources) */
  const char *path;

  /** @brief Target frame rate in FPS (e.g., 60 for display, 144 for network) */
  uint32_t target_fps;

  /** @brief Enable loop playback for file sources */
  bool loop;

  /** @brief Resize frames to network-optimal dimensions (MAX_FRAME_WIDTH x MAX_FRAME_HEIGHT) */
  bool resize_for_network;

  /** @brief Enable audio capture from media source */
  bool enable_audio;

  /** @brief Fall back to microphone if file audio is not available */
  bool audio_fallback_to_mic;

  /** @brief Microphone audio context for fallback (borrowed, not owned) */
  void *mic_audio_ctx;

  /** @brief Optional: callback to check if initialization should be cancelled (e.g., shutdown signal) */
  session_capture_should_exit_fn should_exit_callback;

  /** @brief Opaque data passed to should_exit_callback */
  void *callback_data;
} session_capture_config_t;

/* ============================================================================
 * Session Capture Context
 * ============================================================================ */

/**
 * @brief Opaque session capture context handle
 *
 * Manages media source, FPS tracking, and adaptive sleep state.
 * Created via session_capture_create(), destroyed via session_capture_destroy().
 *
 * @ingroup session
 */
typedef struct session_capture_ctx session_capture_ctx_t;

/* ============================================================================
 * Session Capture Lifecycle Functions
 * @{
 */

/**
 * @brief Create a new session capture context
 * @param config Capture configuration (must not be NULL)
 * @return Pointer to capture context, or NULL on failure
 *
 * Creates and initializes a session capture context with the specified
 * configuration. The media source is opened and ready for reading.
 *
 * @note Call session_capture_destroy() to free resources when done.
 * @note On failure, sets asciichat_errno with error details.
 *
 * @ingroup session
 */
session_capture_ctx_t *session_capture_create(const session_capture_config_t *config);

/**
 * @brief Destroy session capture context and free resources
 * @param ctx Capture context to destroy (can be NULL)
 *
 * Cleans up the capture context and releases all resources including
 * the media source. Safe to call with NULL.
 *
 * @ingroup session
 */
void session_capture_destroy(session_capture_ctx_t *ctx);

/** @} */

/* ============================================================================
 * Session Capture Operations
 * @{
 */

/**
 * @brief Read the next video frame from the capture source
 * @param ctx Capture context (must not be NULL)
 * @return Pointer to image frame, or NULL on error/end
 *
 * Reads the next video frame from the media source. The returned image
 * is owned by the media source and should NOT be freed by the caller.
 *
 * @note Frame is valid until next call or context destruction.
 * @note Check session_capture_at_end() to distinguish error from EOF.
 *
 * @ingroup session
 */
image_t *session_capture_read_frame(session_capture_ctx_t *ctx);

/**
 * @brief Process a frame for network transmission (resize if needed)
 * @param ctx Capture context (must not be NULL)
 * @param frame Input frame from session_capture_read_frame() (must not be NULL)
 * @return Processed image ready for transmission, or NULL on error
 *
 * Resizes the frame to network-optimal dimensions if resize_for_network
 * was enabled in the configuration. The returned image is a new allocation
 * owned by the caller and must be freed with image_destroy().
 *
 * @note Returns a copy if no resizing is needed.
 * @note Caller owns the returned image and must call image_destroy().
 *
 * @ingroup session
 */
image_t *session_capture_process_for_transmission(session_capture_ctx_t *ctx, image_t *frame);

/**
 * @brief Sleep to maintain target frame rate
 * @param ctx Capture context (must not be NULL)
 *
 * Implements adaptive sleep to maintain the configured target frame rate.
 * Call this once per frame capture iteration.
 *
 * @note Uses adaptive sleep for smooth frame rate limiting.
 *
 * @ingroup session
 */
void session_capture_sleep_for_fps(session_capture_ctx_t *ctx);

/**
 * @brief Check if capture source has reached end of stream
 * @param ctx Capture context (must not be NULL)
 * @return true if end of stream reached, false otherwise
 *
 * Useful for detecting end of file sources. Webcam and test pattern
 * sources never reach end.
 *
 * @ingroup session
 */
bool session_capture_at_end(session_capture_ctx_t *ctx);

/**
 * @brief Check if capture context is initialized and valid
 * @param ctx Capture context (can be NULL)
 * @return true if context is valid and initialized, false otherwise
 *
 * @ingroup session
 */
bool session_capture_is_valid(session_capture_ctx_t *ctx);

/**
 * @brief Get the current FPS being achieved by the capture source
 * @param ctx Capture context (must not be NULL)
 * @return Current FPS, or 0.0 if not enough frames captured yet
 *
 * @ingroup session
 */
double session_capture_get_current_fps(session_capture_ctx_t *ctx);

/**
 * @brief Get the target FPS configured for this capture context
 * @param ctx Capture context (must not be NULL)
 * @return Target FPS from configuration
 *
 * @ingroup session
 */
uint32_t session_capture_get_target_fps(session_capture_ctx_t *ctx);

/**
 * @brief Check if capture source has audio available
 * @param ctx Capture context (must not be NULL)
 * @return true if audio is available, false otherwise
 *
 * Returns whether the capture source has audio and it is enabled.
 *
 * @ingroup session
 */
bool session_capture_has_audio(session_capture_ctx_t *ctx);

/**
 * @brief Read audio samples from capture source
 * @param ctx Capture context (must not be NULL)
 * @param buffer Output buffer for audio samples (must not be NULL)
 * @param num_samples Number of samples to read
 * @return Number of samples actually read (0 if no audio available)
 *
 * Reads audio samples from the media source. Audio is read from either:
 * - File if available and enabled
 * - Microphone if using fallback
 *
 * Returns 0 if audio is not enabled or available.
 *
 * @ingroup session
 */
size_t session_capture_read_audio(session_capture_ctx_t *ctx, float *buffer, size_t num_samples);

/**
 * @brief Check if currently using file audio vs microphone fallback
 * @param ctx Capture context (must not be NULL)
 * @return true if using file audio, false if using microphone or no audio
 *
 * Useful for logging and debugging which audio source is active.
 *
 * @ingroup session
 */
bool session_capture_using_file_audio(session_capture_ctx_t *ctx);

/**
 * @brief Get the underlying media source from capture context
 * @param ctx Capture context (must not be NULL)
 * @return Pointer to media source, or NULL if not available
 *
 * Used by audio playback to read audio directly from media source at callback time.
 *
 * @ingroup session
 */
void *session_capture_get_media_source(session_capture_ctx_t *ctx);

/**
 * @brief Synchronize audio decoder to video position for frame-locked playback
 * @param ctx Capture context (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Keeps the audio decoder synchronized with the video decoder's current position.
 * Used in mirror mode to ensure audio and video frames stay in sync.
 *
 * **Usage:**
 * Call this after each session_capture_read_frame() to sync the audio decoder
 * to the video frame's timestamp before the PortAudio callback reads audio.
 *
 * @ingroup session
 */
asciichat_error_t session_capture_sync_audio_to_video(session_capture_ctx_t *ctx);

/** @} */

/** @} */
