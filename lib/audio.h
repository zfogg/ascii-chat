#pragma once

/**
 * @file audio.h
 * @defgroup audio Audio System
 * @ingroup audio
 * @brief Audio Capture and Playback Interface for ASCII-Chat
 *
 * This header provides audio capture and playback functionality using PortAudio,
 * enabling real-time audio streaming for ASCII-Chat video chat sessions.
 *
 * CORE FEATURES:
 * ==============
 * - Real-time audio capture from microphone/input devices
 * - Real-time audio playback to speakers/output devices
 * - Thread-safe ring buffers for audio data
 * - Low-latency audio processing
 * - Platform-specific real-time priority scheduling
 * - Configurable audio parameters (sample rate, buffer size)
 *
 * AUDIO ARCHITECTURE:
 * ===================
 * The audio system uses PortAudio for cross-platform audio I/O:
 * - Separate input and output streams for full-duplex audio
 * - Ring buffers for efficient producer-consumer audio data transfer
 * - Lock-free or mutex-protected buffers depending on platform
 * - Automatic device enumeration and selection
 *
 * AUDIO PARAMETERS:
 * =================
 * - Sample Rate: 44.1kHz (CD quality)
 * - Channels: Mono (1 channel)
 * - Buffer Size: 256 frames per buffer (low latency)
 * - Format: 32-bit floating point samples
 *
 * THREAD SAFETY:
 * ==============
 * - Audio context state protected by mutex
 * - Ring buffers provide thread-safe audio data transfer
 * - Real-time priority scheduling on supported platforms
 *
 * PLATFORM SUPPORT:
 * ==================
 * - Windows: DirectSound, WASAPI, or ASIO backends
 * - Linux: ALSA or JACK backends
 * - macOS: CoreAudio backend
 *
 * @note Audio capture and playback can be started/stopped independently.
 * @note The audio system uses ring buffers to smooth out timing variations
 *       between capture/playback threads and network I/O.
 * @note Real-time priority is automatically requested on supported platforms
 *       to minimize audio glitches and latency.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <stdbool.h>
#include <portaudio.h>
#ifdef __linux__
#include <sched.h>
#include <sys/resource.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <mach/kern_return.h>
#endif

#include "common.h"
#include "platform/mutex.h"
#include "ringbuffer.h"

/* ============================================================================
 * Audio Configuration Constants
 * ============================================================================
 */

/** @brief Audio sample rate (44.1kHz CD quality) */
#define AUDIO_SAMPLE_RATE 44100
/** @brief Audio frames per buffer (256 frames for low latency) */
#define AUDIO_FRAMES_PER_BUFFER 256
/** @brief Number of audio channels (1 = mono) */
#define AUDIO_CHANNELS 1
/** @brief Total audio buffer size (frames × channels) */
#define AUDIO_BUFFER_SIZE (AUDIO_FRAMES_PER_BUFFER * AUDIO_CHANNELS)

/* ============================================================================
 * Data Structures
 * ============================================================================
 */

/**
 * @brief Audio context for capture and playback
 *
 * Manages PortAudio streams and ring buffers for audio capture and playback.
 * Provides thread-safe audio I/O with separate input and output streams
 * for full-duplex operation.
 *
 * @note The audio context must be initialized with audio_init() before use.
 * @note Capture and playback can be started/stopped independently.
 * @note State is protected by state_mutex for thread-safe operations.
 *
 * @ingroup audio
 */
typedef struct {
  PaStream *input_stream;       ///< PortAudio input stream for capture
  PaStream *output_stream;       ///< PortAudio output stream for playback
  audio_ring_buffer_t *capture_buffer;  ///< Ring buffer for captured audio samples
  audio_ring_buffer_t *playback_buffer; ///< Ring buffer for audio samples to play
  bool initialized;             ///< True if context has been initialized
  bool recording;               ///< True if audio capture is active
  bool playing;                 ///< True if audio playback is active
  mutex_t state_mutex;          ///< Mutex protecting context state
} audio_context_t;

/* ============================================================================
 * Audio Context Lifecycle
 * @{
 */

/**
 * @brief Initialize audio context and PortAudio
 * @param ctx Audio context to initialize (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Initializes the audio context and PortAudio library. Creates ring buffers
 * for capture and playback but does not start streams. Must be called before
 * any other audio functions.
 *
 * @note PortAudio initialization is idempotent and thread-safe.
 * @note Ring buffers are created but empty after initialization.
 * @note Use audio_start_capture() and audio_start_playback() to begin I/O.
 *
 * @warning Must call audio_destroy() to clean up resources when done.
 *
 * @ingroup audio
 */
asciichat_error_t audio_init(audio_context_t *ctx);

/**
 * @brief Destroy audio context and clean up resources
 * @param ctx Audio context to destroy (can be NULL)
 *
 * Stops all audio streams, destroys ring buffers, and cleans up PortAudio
 * resources. Safe to call multiple times or with NULL pointer.
 *
 * @note Automatically stops capture and playback if active.
 * @note Ring buffers are destroyed and all data is lost.
 * @note PortAudio is terminated after last context is destroyed.
 *
 * @ingroup audio
 */
void audio_destroy(audio_context_t *ctx);

/** @} */

/* ============================================================================
 * Audio Capture Control
 * @{
 */

/**
 * @brief Start audio capture from input device
 * @param ctx Audio context (must not be NULL, must be initialized)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Starts audio capture from the default input device (microphone). Audio
 * samples are captured in a background thread and written to the capture
 * ring buffer. Use audio_read_samples() to read captured samples.
 *
 * @note Capture can be started independently of playback.
 * @note If capture is already active, this function has no effect.
 * @note Real-time priority is automatically requested for capture thread.
 *
 * @warning Input device must be available and not in use by another application.
 *
 * @ingroup audio
 */
asciichat_error_t audio_start_capture(audio_context_t *ctx);

/**
 * @brief Stop audio capture
 * @param ctx Audio context (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Stops audio capture and closes the input stream. Captured samples remain
 * in the capture buffer until read. Use audio_read_samples() to read any
 * remaining samples before stopping.
 *
 * @note If capture is not active, this function has no effect.
 * @note Capture buffer is not cleared by this function.
 *
 * @ingroup audio
 */
asciichat_error_t audio_stop_capture(audio_context_t *ctx);

/** @} */

/* ============================================================================
 * Audio Playback Control
 * @{
 */

/**
 * @brief Start audio playback to output device
 * @param ctx Audio context (must not be NULL, must be initialized)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Starts audio playback to the default output device (speakers). Audio
 * samples are read from the playback ring buffer and played in a background
 * thread. Use audio_write_samples() to write samples for playback.
 *
 * @note Playback can be started independently of capture.
 * @note If playback is already active, this function has no effect.
 * @note Real-time priority is automatically requested for playback thread.
 *
 * @warning Output device must be available and not in use by another application.
 *
 * @ingroup audio
 */
asciichat_error_t audio_start_playback(audio_context_t *ctx);

/**
 * @brief Stop audio playback
 * @param ctx Audio context (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Stops audio playback and closes the output stream. Samples remaining in
 * the playback buffer are not played. Use audio_write_samples() to queue
 * more samples before stopping.
 *
 * @note If playback is not active, this function has no effect.
 * @note Playback buffer is not cleared by this function.
 *
 * @ingroup audio
 */
asciichat_error_t audio_stop_playback(audio_context_t *ctx);

/** @} */

/* ============================================================================
 * Audio I/O Operations
 * @{
 */

/**
 * @brief Read captured audio samples from capture buffer
 * @param ctx Audio context (must not be NULL)
 * @param buffer Output buffer for audio samples (must not be NULL)
 * @param num_samples Number of samples to read (must be > 0)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Reads audio samples from the capture ring buffer. Samples are in floating
 * point format (-1.0 to 1.0 range). This function is non-blocking and returns
 * whatever samples are available up to num_samples.
 *
 * @note Actual number of samples read may be less than num_samples if buffer
 *       doesn't contain enough samples.
 * @note Samples are removed from the buffer after reading.
 * @note Thread-safe: Can be called from any thread.
 *
 * @warning Buffer must be large enough to hold num_samples float values.
 *
 * @ingroup audio
 */
asciichat_error_t audio_read_samples(audio_context_t *ctx, float *buffer, int num_samples);

/**
 * @brief Write audio samples to playback buffer
 * @param ctx Audio context (must not be NULL)
 * @param buffer Input buffer of audio samples (must not be NULL)
 * @param num_samples Number of samples to write (must be > 0)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Writes audio samples to the playback ring buffer for playback. Samples
 * should be in floating point format (-1.0 to 1.0 range). This function
 * is non-blocking and writes as many samples as buffer space allows.
 *
 * @note Actual number of samples written may be less than num_samples if buffer
 *       doesn't have enough space.
 * @note Samples are queued for playback by the playback thread.
 * @note Thread-safe: Can be called from any thread.
 *
 * @warning Buffer must contain at least num_samples float values.
 *
 * @ingroup audio
 */
asciichat_error_t audio_write_samples(audio_context_t *ctx, const float *buffer, int num_samples);

/** @} */

/* ============================================================================
 * Real-Time Priority Scheduling
 * @{
 */

/**
 * @brief Request real-time priority for current thread
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Requests real-time priority scheduling for the current thread. This reduces
 * audio latency and glitches by ensuring audio threads get scheduled promptly.
 * Platform-specific implementation (SCHED_FIFO on Linux, thread priority on
 * Windows/macOS).
 *
 * @note This function should be called from audio capture/playback threads.
 * @note On some platforms, real-time priority requires appropriate permissions.
 * @note If real-time priority cannot be obtained, the function may succeed but
 *       use a lower priority level.
 *
 * @warning Real-time priority can cause system instability if misused.
 *          Only use in audio threads that process data quickly.
 *
 * @ingroup audio
 */
asciichat_error_t audio_set_realtime_priority(void);

/** @} */

/* ============================================================================
 * Audio Ring Buffer Management
 * @{
 */

/**
 * @brief Create a new audio ring buffer
 * @return Pointer to newly created audio ring buffer, or NULL on failure
 *
 * Creates a new audio ring buffer for storing audio samples. The buffer has
 * a fixed size defined by AUDIO_RING_BUFFER_SIZE and is optimized for real-time
 * audio streaming with jitter buffering support.
 *
 * @note The ring buffer is thread-safe and can be used concurrently by capture
 *       and playback threads.
 * @note Ring buffer includes jitter buffer threshold for network latency compensation.
 *
 * @warning Must call audio_ring_buffer_destroy() to free resources.
 *
 * @ingroup audio
 */
audio_ring_buffer_t *audio_ring_buffer_create(void);

/**
 * @brief Destroy an audio ring buffer
 * @param rb Audio ring buffer to destroy (can be NULL)
 *
 * Destroys an audio ring buffer and frees all associated resources. Safe to
 * call multiple times or with NULL pointer.
 *
 * @note All samples in the buffer are discarded.
 *
 * @ingroup audio
 */
void audio_ring_buffer_destroy(audio_ring_buffer_t *rb);

/**
 * @brief Write audio samples to ring buffer
 * @param rb Audio ring buffer (must not be NULL)
 * @param data Audio samples to write (must not be NULL)
 * @param samples Number of samples to write
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Writes audio samples to the ring buffer. Samples should be in floating point
 * format (-1.0 to 1.0 range). This function is non-blocking and writes as many
 * samples as buffer space allows.
 *
 * @note Actual number of samples written may be less than requested if buffer
 *       is full or nearly full.
 * @note Thread-safe: Can be called from multiple threads simultaneously.
 *
 * @warning Data must contain at least samples float values.
 *
 * @ingroup audio
 */
asciichat_error_t audio_ring_buffer_write(audio_ring_buffer_t *rb, const float *data, int samples);

/**
 * @brief Read audio samples from ring buffer
 * @param rb Audio ring buffer (must not be NULL)
 * @param data Output buffer for audio samples (must not be NULL)
 * @param samples Maximum number of samples to read
 * @return Number of samples actually read (may be less than requested)
 *
 * Reads audio samples from the ring buffer. Samples are in floating point
 * format (-1.0 to 1.0 range). This function is non-blocking and returns
 * whatever samples are available up to the requested count.
 *
 * @note Actual number of samples read may be less than samples if buffer
 *       doesn't contain enough samples.
 * @note Samples are removed from the buffer after reading.
 * @note Thread-safe: Can be called from multiple threads simultaneously.
 *
 * @warning Data buffer must be large enough to hold samples float values.
 *
 * @ingroup audio
 */
size_t audio_ring_buffer_read(audio_ring_buffer_t *rb, float *data, size_t samples);

/**
 * @brief Get number of samples available for reading
 * @param rb Audio ring buffer (must not be NULL)
 * @return Number of samples available to read
 *
 * Returns the current number of samples available in the ring buffer for
 * reading. Useful for determining if enough samples are available before
 * calling audio_ring_buffer_read().
 *
 * @note Thread-safe: Can be called from any thread.
 * @note Value may change immediately after return due to concurrent writes.
 *
 * @ingroup audio
 */
size_t audio_ring_buffer_available_read(audio_ring_buffer_t *rb);

/**
 * @brief Get number of sample slots available for writing
 * @param rb Audio ring buffer (must not be NULL)
 * @return Number of sample slots available for writing
 *
 * Returns the current number of sample slots available in the ring buffer for
 * writing. Useful for determining if enough space is available before calling
 * audio_ring_buffer_write().
 *
 * @note Thread-safe: Can be called from any thread.
 * @note Value may change immediately after return due to concurrent reads.
 *
 * @ingroup audio
 */
size_t audio_ring_buffer_available_write(audio_ring_buffer_t *rb);

/** @} */
