/**
 * @file session/audio.h
 * @brief 🔊 Session-level audio coordination wrapper
 * @ingroup session
 * @addtogroup session
 * @{
 *
 * This header provides a thin wrapper around the lib/audio/ system for
 * session-level audio coordination. It handles the differences between
 * host (mixing) and participant (simple capture/playback) modes.
 *
 * CORE FEATURES:
 * ==============
 * - Unified API for both host and participant audio handling
 * - Host-only audio mixing for multi-participant sessions
 * - Source management for tracking participant audio streams
 * - Clean startup/shutdown coordination
 *
 * USAGE:
 * ======
 * @code{.c}
 * // Create audio context for a participant
 * session_audio_ctx_t *ctx = session_audio_create(false);
 *
 * // Start capture and playback
 * session_audio_start_capture(ctx);
 * session_audio_start_playback(ctx);
 *
 * // Cleanup
 * session_audio_destroy(ctx);
 * @endcode
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../asciichat_errno.h"

/* ============================================================================
 * Session Audio Context
 * ============================================================================ */

/**
 * @brief Opaque session audio context handle
 *
 * Manages audio capture, playback, and optional mixing state.
 * Created via session_audio_create(), destroyed via session_audio_destroy().
 *
 * @ingroup session
 */
typedef struct session_audio_ctx session_audio_ctx_t;

/* ============================================================================
 * Session Audio Lifecycle Functions
 * @{
 */

/**
 * @brief Create a new session audio context
 * @param is_host true if this context is for a session host (enables mixing)
 * @return Pointer to audio context, or NULL on failure
 *
 * Creates and initializes a session audio context. Hosts get audio mixing
 * capabilities while participants only get basic capture/playback.
 *
 * @note Call session_audio_destroy() to free resources when done.
 * @note On failure, sets asciichat_errno with error details.
 *
 * @ingroup session
 */
session_audio_ctx_t *session_audio_create(bool is_host);

/**
 * @brief Destroy session audio context and free resources
 * @param ctx Audio context to destroy (can be NULL)
 *
 * Stops all audio streams, cleans up mixers (if host), and releases
 * all resources. Safe to call with NULL.
 *
 * @ingroup session
 */
void session_audio_destroy(session_audio_ctx_t *ctx);

/** @} */

/* ============================================================================
 * Session Audio Control Functions
 * @{
 */

/**
 * @brief Start audio capture (microphone input)
 * @param ctx Audio context (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Starts capturing audio from the default input device. Captured audio
 * is available for transmission via session_audio_read_captured().
 *
 * @ingroup session
 */
asciichat_error_t session_audio_start_capture(session_audio_ctx_t *ctx);

/**
 * @brief Start audio playback (speaker output)
 * @param ctx Audio context (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Starts playback on the default output device. Audio for playback
 * should be provided via session_audio_write_playback().
 *
 * @ingroup session
 */
asciichat_error_t session_audio_start_playback(session_audio_ctx_t *ctx);

/**
 * @brief Start full-duplex audio (simultaneous capture and playback)
 * @param ctx Audio context (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Starts both capture and playback in a single full-duplex stream.
 * This is preferred over separate start_capture/start_playback calls
 * as it provides better echo cancellation support.
 *
 * @ingroup session
 */
asciichat_error_t session_audio_start_duplex(session_audio_ctx_t *ctx);

/**
 * @brief Stop all audio streams
 * @param ctx Audio context (must not be NULL)
 *
 * Stops capture, playback, and any mixing operations.
 *
 * @ingroup session
 */
void session_audio_stop(session_audio_ctx_t *ctx);

/**
 * @brief Check if audio is currently running
 * @param ctx Audio context (can be NULL)
 * @return true if audio is active, false otherwise
 *
 * @ingroup session
 */
bool session_audio_is_running(session_audio_ctx_t *ctx);

/** @} */

/* ============================================================================
 * Session Audio I/O Functions
 * @{
 */

/**
 * @brief Read captured audio samples
 * @param ctx Audio context (must not be NULL)
 * @param buffer Output buffer for audio samples (must not be NULL)
 * @param num_samples Maximum number of samples to read
 * @return Number of samples actually read
 *
 * Reads captured audio samples from the capture ring buffer.
 * Samples are in float format (-1.0 to 1.0 range).
 *
 * @ingroup session
 */
size_t session_audio_read_captured(session_audio_ctx_t *ctx, float *buffer, size_t num_samples);

/**
 * @brief Write audio samples for playback
 * @param ctx Audio context (must not be NULL)
 * @param buffer Input buffer with audio samples (must not be NULL)
 * @param num_samples Number of samples to write
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Writes audio samples to the playback ring buffer. Samples should
 * be in float format (-1.0 to 1.0 range).
 *
 * @ingroup session
 */
asciichat_error_t session_audio_write_playback(session_audio_ctx_t *ctx, const float *buffer, size_t num_samples);

/** @} */

/* ============================================================================
 * Session Audio Host-Only Functions (Mixing)
 * @{
 */

/**
 * @brief Add an audio source for mixing (host only)
 * @param ctx Audio context (must not be NULL, must be host context)
 * @param source_id Unique identifier for this source
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Registers a new audio source for the mixer. Each participant
 * should have a unique source_id.
 *
 * @note Only available when context was created with is_host=true.
 *
 * @ingroup session
 */
asciichat_error_t session_audio_add_source(session_audio_ctx_t *ctx, uint32_t source_id);

/**
 * @brief Remove an audio source from mixing (host only)
 * @param ctx Audio context (must not be NULL, must be host context)
 * @param source_id Source identifier to remove
 *
 * Unregisters an audio source from the mixer.
 *
 * @note Only available when context was created with is_host=true.
 *
 * @ingroup session
 */
void session_audio_remove_source(session_audio_ctx_t *ctx, uint32_t source_id);

/**
 * @brief Write audio samples from a specific source (host only)
 * @param ctx Audio context (must not be NULL, must be host context)
 * @param source_id Source identifier
 * @param samples Audio samples from this source
 * @param num_samples Number of samples
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Provides audio data from a specific participant to the mixer.
 *
 * @note Only available when context was created with is_host=true.
 *
 * @ingroup session
 */
asciichat_error_t session_audio_write_source(session_audio_ctx_t *ctx, uint32_t source_id, const float *samples,
                                             size_t num_samples);

/**
 * @brief Mix all sources except one and return the result (host only)
 * @param ctx Audio context (must not be NULL, must be host context)
 * @param exclude_id Source ID to exclude from mix (for echo prevention)
 * @param output Output buffer for mixed audio (must not be NULL)
 * @param num_samples Number of samples to mix
 * @return Number of samples actually mixed
 *
 * Mixes audio from all registered sources except exclude_id. This allows
 * sending each participant a mix of all other participants without their
 * own audio (preventing echo).
 *
 * @note Only available when context was created with is_host=true.
 *
 * @ingroup session
 */
size_t session_audio_mix_excluding(session_audio_ctx_t *ctx, uint32_t exclude_id, float *output, size_t num_samples);

/** @} */

/** @} */
