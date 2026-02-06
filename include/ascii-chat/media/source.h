#pragma once

/**
 * @file media/source.h
 * @brief 🎬 Unified media source abstraction for webcam, files, and stdin
 * @ingroup media
 * @addtogroup media
 * @{
 *
 * This header provides a unified interface for media sources in ascii-chat,
 * abstracting over webcams, media files, and stdin input. This allows the
 * capture thread to work with any media source transparently.
 *
 * CORE FEATURES:
 * ==============
 * - Unified API for webcam, files, and stdin
 * - FFmpeg-based decoding for all common formats
 * - Video frame extraction (RGB format)
 * - Audio sample extraction (48kHz float mono)
 * - Loop support for file playback
 * - Automatic format detection
 *
 * SUPPORTED SOURCES:
 * ==================
 * - MEDIA_SOURCE_WEBCAM: Hardware webcam device
 * - MEDIA_SOURCE_FILE: Media file (mp4, avi, mkv, gif, mp3, etc.)
 * - MEDIA_SOURCE_STDIN: Piped or redirected media data
 * - MEDIA_SOURCE_TEST: Test pattern (existing)
 *
 * SUPPORTED FORMATS:
 * ==================
 * - Video: mp4, avi, mkv, webm, mov, flv, wmv, gif (animated)
 * - Audio: mp3, aac, opus, flac, wav, ogg, m4a
 * - Images: gif, png, jpg (static - single frame)
 *
 * USAGE:
 * ======
 * @code
 * // Create media source from file
 * media_source_t *source = media_source_create(MEDIA_SOURCE_FILE, "video.mp4");
 * media_source_set_loop(source, true);
 *
 * // Read video frames
 * while (!done) {
 *     image_t *frame = media_source_read_video(source);
 *     if (!frame) {
 *         if (media_source_at_end(source)) break;
 *         continue; // Error, try next frame
 *     }
 *     // Process frame...
 * }
 *
 * // Read audio samples
 * float buffer[480];
 * size_t samples = media_source_read_audio(source, buffer, 480);
 *
 * // Cleanup
 * media_source_destroy(source);
 * @endcode
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <stdbool.h>
#include <stddef.h>
#include "../common.h"
#include "../video/image.h"

/* ============================================================================
 * Media Source Types
 * ============================================================================ */

/**
 * @brief Media source type enumeration
 *
 * Identifies the type of media source being used.
 *
 * @ingroup media
 */
typedef enum {
  MEDIA_SOURCE_WEBCAM, ///< Hardware webcam device
  MEDIA_SOURCE_FILE,   ///< Media file (video/audio)
  MEDIA_SOURCE_STDIN,  ///< Piped or redirected input
  MEDIA_SOURCE_TEST    ///< Test pattern generator
} media_source_type_t;

/**
 * @brief Opaque media source handle
 *
 * Forward declaration of media source structure. Use media_source_create()
 * to create and media_source_destroy() to cleanup.
 *
 * THREAD SAFETY:
 * ==============
 * - media_source_read_video() and media_source_read_audio() may be called
 *   from different threads (e.g., video render thread + audio callback thread)
 * - Both operations access the shared FFmpeg decoder
 * - FFmpeg decoders are NOT thread-safe for concurrent operations
 * - media_source_t uses internal synchronization to protect decoder access
 * - Callers do NOT need external locking
 *
 * @ingroup media
 */
typedef struct media_source_t media_source_t;

/* ============================================================================
 * Media Source Lifecycle
 * ============================================================================ */

/**
 * @brief Create a new media source
 * @param type Media source type
 * @param path File path (for FILE type), device index string (for WEBCAM), or NULL
 * @return Pointer to media source, or NULL on failure
 *
 * Creates and initializes a media source of the specified type.
 *
 * **Path parameter usage:**
 * - MEDIA_SOURCE_FILE: File path (e.g., "video.mp4")
 * - MEDIA_SOURCE_STDIN: Use "-" or NULL
 * - MEDIA_SOURCE_WEBCAM: Device index as string (e.g., "0") or NULL for default
 * - MEDIA_SOURCE_TEST: Ignored (use NULL)
 *
 * @note For WEBCAM type, delegates to webcam_init()
 * @note For FILE/STDIN types, uses FFmpeg decoder
 * @note Call media_source_destroy() to cleanup
 *
 * @ingroup media
 */
media_source_t *media_source_create(media_source_type_t type, const char *path);

/**
 * @brief Destroy media source and free resources
 * @param source Media source to destroy (can be NULL)
 *
 * Cleans up media source and releases all resources. Safe to call with NULL.
 *
 * @note For WEBCAM type, calls webcam_destroy()
 * @note For FILE/STDIN types, closes FFmpeg decoder
 *
 * @ingroup media
 */
void media_source_destroy(media_source_t *source);

/* ============================================================================
 * Video Operations
 * ============================================================================ */

/**
 * @brief Read next video frame from media source
 * @param source Media source (must not be NULL)
 * @return Pointer to image_t frame, or NULL on error/end
 *
 * Reads the next video frame from the media source. Returns an image_t
 * structure in RGB format, compatible with existing ascii-chat pipeline.
 *
 * **Return values:**
 * - Non-NULL: Valid frame pointer (do NOT free - internal buffer)
 * - NULL: Error or end of stream (check media_source_at_end())
 *
 * **Frame ownership:**
 * - Returned frame is owned by media source
 * - Do NOT free the returned frame
 * - Frame is valid until next call or source destruction
 *
 * @note For WEBCAM type, calls webcam_read()
 * @note For FILE/STDIN types, decodes next video frame via FFmpeg
 * @note Frame timing is handled by caller (capture thread)
 *
 * @ingroup media
 */
image_t *media_source_read_video(media_source_t *source);

/**
 * @brief Check if media source has video stream
 * @param source Media source (must not be NULL)
 * @return true if source has video, false otherwise
 *
 * Determines if the media source provides video frames.
 *
 * @note WEBCAM and TEST sources always return true
 * @note FILE/STDIN sources return true if video stream detected
 *
 * @ingroup media
 */
bool media_source_has_video(media_source_t *source);

/* ============================================================================
 * Audio Operations
 * ============================================================================ */

/**
 * @brief Read audio samples from media source
 * @param source Media source (must not be NULL)
 * @param buffer Output buffer for audio samples (must not be NULL)
 * @param num_samples Number of samples to read
 * @return Number of samples actually read (may be less than requested)
 *
 * Reads audio samples from the media source. Samples are in 32-bit float
 * format, mono channel, 48kHz sample rate (compatible with Opus encoding).
 *
 * **Audio format:**
 * - Format: 32-bit float
 * - Channels: Mono (1 channel)
 * - Sample rate: 48000 Hz
 * - Range: -1.0 to 1.0
 *
 * **Return value:**
 * - Returns actual number of samples read (0 to num_samples)
 * - Returns 0 on error or end of stream
 * - Check media_source_at_end() to distinguish error from EOF
 *
 * @note WEBCAM type does NOT provide audio (returns 0)
 * @note FILE/STDIN types decode audio via FFmpeg if stream exists
 * @note Audio is resampled to 48kHz mono automatically
 *
 * @ingroup media
 */
size_t media_source_read_audio(media_source_t *source, float *buffer, size_t num_samples);

/**
 * @brief Check if media source has audio stream
 * @param source Media source (must not be NULL)
 * @return true if source has audio, false otherwise
 *
 * Determines if the media source provides audio samples.
 *
 * @note WEBCAM and TEST sources return false (no audio)
 * @note FILE/STDIN sources return true if audio stream detected
 *
 * @ingroup media
 */
bool media_source_has_audio(media_source_t *source);

/* ============================================================================
 * Playback Control
 * ============================================================================ */

/**
 * @brief Enable or disable looping
 * @param source Media source (must not be NULL)
 * @param loop true to enable looping, false to disable
 *
 * Enables loop mode for media files. When looping is enabled, the media
 * source will automatically seek to the beginning when EOF is reached.
 *
 * **Behavior:**
 * - FILE sources: Seek to beginning on EOF (if seekable)
 * - STDIN sources: Looping NOT supported (cannot seek stdin)
 * - WEBCAM/TEST sources: Ignored (infinite by nature)
 *
 * @note For stdin, loop flag is silently ignored
 * @note Looping works for both video and audio streams
 *
 * @ingroup media
 */
void media_source_set_loop(media_source_t *source, bool loop);

/**
 * @brief Pause media playback
 * @param source Media source (must not be NULL)
 *
 * Pauses the media source. When paused, read_video and read_audio return
 * no new data (NULL/silence) while maintaining the current playback position.
 * Resume with media_source_resume() to continue from the paused position.
 *
 * **Behavior:**
 * - media_source_read_video() returns NULL when paused
 * - media_source_read_audio() returns 0 (silence) when paused
 * - Playback position is preserved (no seeking occurs)
 *
 * @note All source types support pause (has no effect for WEBCAM/TEST)
 * @note Pausing is instantaneous (next read will return no data)
 *
 * @ingroup media
 */
void media_source_pause(media_source_t *source);

/**
 * @brief Resume media playback after pause
 * @param source Media source (must not be NULL)
 *
 * Resumes playback from where it was paused. Continues reading frames
 * and audio samples from the current position.
 *
 * @note Safe to call if not paused (no-op)
 * @note Resume is instantaneous (next read will return new data)
 *
 * @ingroup media
 */
void media_source_resume(media_source_t *source);

/**
 * @brief Check if media source is paused
 * @param source Media source (must not be NULL)
 * @return true if paused, false if playing
 *
 * Determines if the media source is currently paused.
 *
 * @ingroup media
 */
bool media_source_is_paused(media_source_t *source);

/**
 * @brief Toggle pause state of media source
 * @param source Media source (must not be NULL)
 *
 * Toggles between paused and playing states. If paused, resumes playback.
 * If playing, pauses playback.
 *
 * @ingroup media
 */
void media_source_toggle_pause(media_source_t *source);

/**
 * @brief Check if media source reached end of stream
 * @param source Media source (must not be NULL)
 * @return true if end reached, false otherwise
 *
 * Determines if the media source has reached end of stream. This distinguishes
 * between read errors and legitimate EOF conditions.
 *
 * **Return values:**
 * - true: End of stream reached (no more data)
 * - false: More data available or error occurred
 *
 * @note WEBCAM/TEST sources never reach end (always return false)
 * @note FILE/STDIN sources return true when all packets consumed
 * @note If loop is enabled, this returns false (EOF triggers rewind)
 *
 * @ingroup media
 */
bool media_source_at_end(media_source_t *source);

/**
 * @brief Rewind media source to beginning
 * @param source Media source (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Seeks to the beginning of the media source. Used for loop implementation.
 *
 * **Behavior:**
 * - FILE sources: Seek to timestamp 0 (if seekable)
 * - STDIN sources: ERROR_NOT_SUPPORTED (cannot seek stdin)
 * - WEBCAM/TEST sources: No-op (always returns OK)
 *
 * @note Called automatically by read functions if loop enabled
 * @note Can be called manually to restart playback
 *
 * @ingroup media
 */
asciichat_error_t media_source_rewind(media_source_t *source);

/**
 * @brief DEPRECATED: Synchronize audio decoder to video position
 * @param source Media source (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @deprecated This function is deprecated and causes audio playback issues.
 * Forcing audio to video position via seeking every ~1 second interrupts
 * audio playback with skips and loops. For file/media playback (mirror mode),
 * audio and video decoders naturally stay in sync when decoding from the same
 * source at their independent rates. Forced synchronization is not needed and
 * causes more problems than it solves.
 *
 * DO NOT USE in mirror mode. Audio/video sync should only be needed for
 * complex multi-client scenarios (server mode), and even then, seeking is
 * a poor solution that causes discontinuities.
 *
 * @note This function is being phased out. Use natural decode rates instead.
 * @ingroup media
 */
asciichat_error_t media_source_sync_audio_to_video(media_source_t *source)
    __attribute__((deprecated("Use natural audio/video decode rates instead - seeking causes skips and loops")));

/**
 * @brief Seek media source to timestamp
 * @param source Media source (must not be NULL)
 * @param timestamp_sec Timestamp in seconds
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Seeks both video and audio decoders to the specified timestamp.
 * Handles shared decoder locking for YouTube URLs.
 *
 * **Behavior:**
 * - FILE sources: Seek to timestamp (if seekable)
 * - STDIN sources: ERROR_NOT_SUPPORTED (cannot seek stdin)
 * - WEBCAM/TEST sources: No-op (always returns OK)
 *
 * **Thread safety:**
 * - For YouTube URLs with shared decoder, automatically locks during seek
 * - Safe to call from any thread
 *
 * @note FILE sources only (returns OK for others)
 * @note STDIN sources: ERROR_NOT_SUPPORTED (cannot seek stdin)
 * @note Negative timestamps are rejected
 * @note Seeks are approximate (may land before requested timestamp)
 *
 * @ingroup media
 */
asciichat_error_t media_source_seek(media_source_t *source, double timestamp_sec);

/**
 * @brief Get media source type
 * @param source Media source (must not be NULL)
 * @return Media source type
 *
 * Returns the type of the media source.
 *
 * @ingroup media
 */
media_source_type_t media_source_get_type(media_source_t *source);

/**
 * @brief Get media duration in seconds
 * @param source Media source (must not be NULL)
 * @return Duration in seconds, or -1.0 if unknown/infinite
 *
 * Returns the total duration of the media. Useful for progress display.
 *
 * **Return values:**
 * - Positive value: Duration in seconds (e.g., 120.5 for 2 minutes)
 * - -1.0: Duration unknown (stdin, live streams) or infinite (webcam)
 *
 * @note WEBCAM/TEST sources return -1.0 (infinite)
 * @note FILE sources return actual duration from container
 * @note STDIN sources return -1.0 (cannot determine duration)
 *
 * @ingroup media
 */
double media_source_get_duration(media_source_t *source);

/**
 * @brief Get current playback position in seconds
 * @param source Media source (must not be NULL)
 * @return Current position in seconds, or -1.0 if unknown
 *
 * Returns the current playback position. Useful for progress display.
 *
 * **Return values:**
 * - Positive value: Position in seconds (e.g., 45.2)
 * - -1.0: Position unknown or not applicable
 *
 * @note WEBCAM/TEST sources return -1.0 (no position concept)
 * @note FILE sources return position based on last decoded frame PTS
 *
 * @ingroup media
 */
double media_source_get_position(media_source_t *source);

/**
 * @brief Get video frame rate in frames per second
 * @param source Media source (must not be NULL)
 * @return Frame rate in FPS, or 0.0 if unknown
 *
 * Returns the native frame rate of the video stream.
 *
 * **Return values:**
 * - Positive value: Frame rate in FPS (e.g., 30.0, 60.0)
 * - 0.0: Frame rate unknown or not applicable
 *
 * @note WEBCAM sources return 0.0 (variable rate)
 * @note TEST sources return 0.0
 * @note FILE/STDIN sources return the video stream's fps from FFmpeg
 *
 * @ingroup media
 */
double media_source_get_video_fps(media_source_t *source);

/**
 * @brief Set audio context for playback buffer management
 * @param source Media source (must not be NULL)
 * @param audio_ctx Audio context pointer (opaque, may be NULL)
 *
 * Associates an audio context with the media source. Used to clear playback
 * buffers during seeking to prevent audio lag.
 *
 * @ingroup media
 */
void media_source_set_audio_context(media_source_t *source, void *audio_ctx);

/** @} */
