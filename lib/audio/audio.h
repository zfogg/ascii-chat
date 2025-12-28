#pragma once

/**
 * @file audio.h
 * @brief 🔊 Audio Capture and Playback Interface for ascii-chat
 * @ingroup audio
 * @addtogroup audio
 * @{
 *
 * This header provides audio capture and playback functionality using PortAudio,
 * enabling real-time audio streaming for ascii-chat video chat sessions.
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
 * - Single full-duplex stream for simultaneous capture and playback
 * - Single callback receives both render and capture at EXACT same instant
 * - Perfect timing for WebRTC AEC3 echo cancellation (no ring buffer delay)
 * - Ring buffers only for network I/O (playback from jitter buffer, capture to encoder)
 * - Automatic device enumeration and selection
 *
 * AUDIO PARAMETERS:
 * =================
 * - Sample Rate: 48kHz (professional quality, Opus-compatible)
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
 * - Linux: ALSA backend only (JACK explicitly disabled)
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
#include <stdatomic.h>
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
#include "util/ringbuffer.h"

/* ============================================================================
 * Audio Configuration Constants
 * ============================================================================
 */

/** @brief Audio sample rate (48kHz professional quality, Opus-compatible) */
#define AUDIO_SAMPLE_RATE 48000
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
 * @brief Audio context for full-duplex capture and playback
 *
 * Manages a single PortAudio full-duplex stream for simultaneous audio
 * capture and playback. Uses a single callback that receives both render
 * (speaker output) and capture (microphone input) at the EXACT same instant,
 * enabling perfect timing for WebRTC AEC3 echo cancellation.
 *
 * @note The audio context must be initialized with audio_init() before use.
 * @note Use audio_start_duplex()/audio_stop_duplex() to control the stream.
 * @note State is protected by state_mutex for thread-safe operations.
 *
 * @ingroup audio
 */
typedef struct {
  PaStream *duplex_stream;              ///< PortAudio full-duplex stream (simultaneous input+output)
  PaStream *input_stream;               ///< Separate input stream (when full-duplex unavailable)
  PaStream *output_stream;              ///< Separate output stream (when full-duplex unavailable)
  audio_ring_buffer_t *capture_buffer;  ///< Ring buffer for processed capture (after AEC3) for encoder thread
  audio_ring_buffer_t *playback_buffer; ///< Ring buffer for decoded audio from network
  audio_ring_buffer_t *render_buffer;   ///< Ring buffer for render reference (separate streams mode)
  bool initialized;                     ///< True if context has been initialized
  bool running;                         ///< True if duplex stream is active
  bool separate_streams;                ///< True if using separate input/output streams
  _Atomic bool shutting_down;           ///< True when shutdown started - callback outputs silence
  mutex_t state_mutex;                  ///< Mutex protecting context state
  void *audio_pipeline;                 ///< Client audio pipeline for AEC3 echo cancellation (opaque pointer)
  double sample_rate;                   ///< Actual sample rate of streams (48kHz)
  double input_device_rate;             ///< Native sample rate of input device
  double output_device_rate;            ///< Native sample rate of output device
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
 * @note Use audio_start_duplex() to begin full-duplex audio I/O.
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

/**
 * @brief Set audio pipeline for echo cancellation
 * @param ctx Audio context (must not be NULL)
 * @param pipeline Client audio pipeline pointer (opaque, can be NULL)
 *
 * Associates an audio pipeline with the context for echo cancellation.
 * The pipeline is fed playback samples from the output callback,
 * allowing AEC3 to properly synchronize render and capture signals.
 *
 * @note Safe to call at any time, including before/after audio streams are started.
 * @note Pass NULL to disable pipeline integration.
 *
 * @ingroup audio
 */
void audio_set_pipeline(audio_context_t *ctx, void *pipeline);

/** @} */

/* ============================================================================
 * Full-Duplex Audio Control
 * @{
 */

/**
 * @brief Start full-duplex audio (simultaneous capture and playback)
 * @param ctx Audio context (must not be NULL, must be initialized)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Opens a single PortAudio stream that handles BOTH input (microphone) and
 * output (speakers) simultaneously. This is CRITICAL for proper AEC3 echo
 * cancellation because:
 *
 * - Single callback receives both render and capture samples at the EXACT same instant
 * - No timing mismatch between input/output callbacks
 * - No ring buffer delay for AEC3 reference signal
 * - Perfect synchronization for echo cancellation
 *
 * The duplex callback:
 * 1. Reads from playback_buffer → outputs to speakers
 * 2. Processes capture through AEC3 (using render as reference)
 * 3. Writes processed capture to capture_buffer for encoder thread
 *
 * @note Real-time priority is automatically requested for callback thread.
 * @note If already running, this function has no effect.
 *
 * @warning Both input and output devices must be available.
 *
 * @ingroup audio
 */
asciichat_error_t audio_start_duplex(audio_context_t *ctx);

/**
 * @brief Stop full-duplex audio
 * @param ctx Audio context (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Stops the full-duplex audio stream and closes the stream.
 *
 * @note If not running, this function has no effect.
 * @note Buffers are not cleared by this function.
 *
 * @ingroup audio
 */
asciichat_error_t audio_stop_duplex(audio_context_t *ctx);

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
 * @warning LIMITATION: This function only affects the calling thread. When used
 *          with PortAudio, the audio callbacks run in PortAudio's internal threads,
 *          not the thread that calls audio_start_duplex. Some PortAudio
 *          backends (WASAPI, CoreAudio) automatically use real-time priority for
 *          their callback threads. For other backends, this function has limited
 *          effect when called from the main thread.
 *
 * @ingroup audio
 */
asciichat_error_t audio_set_realtime_priority(void);

/** @} */

/* ============================================================================
 * Audio Device Enumeration
 * @{
 */

/** @brief Maximum length of audio device name */
#define AUDIO_DEVICE_NAME_MAX 256

/**
 * @brief Audio device information structure
 *
 * Contains information about an audio device including its index, name,
 * and capabilities (input/output channel counts).
 *
 * @ingroup audio
 */
typedef struct {
  int index;                        ///< PortAudio device index
  char name[AUDIO_DEVICE_NAME_MAX]; ///< Human-readable device name
  int max_input_channels;           ///< Maximum input channels (0 if output only)
  int max_output_channels;          ///< Maximum output channels (0 if input only)
  double default_sample_rate;       ///< Default sample rate in Hz
  bool is_default_input;            ///< True if this is the default input device
  bool is_default_output;           ///< True if this is the default output device
} audio_device_info_t;

/**
 * @brief List available audio input devices (microphones)
 * @param out_devices Pointer to store allocated array of devices (must not be NULL)
 * @param out_count Pointer to store device count (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Enumerates all audio input devices (microphones) available on the system.
 * The caller is responsible for freeing the returned array with audio_free_device_list().
 *
 * @note This function initializes PortAudio temporarily if not already initialized.
 * @note Devices with maxInputChannels > 0 are included.
 *
 * @warning Must call audio_free_device_list() to free the returned array.
 *
 * @ingroup audio
 */
asciichat_error_t audio_list_input_devices(audio_device_info_t **out_devices, unsigned int *out_count);

/**
 * @brief List available audio output devices (speakers)
 * @param out_devices Pointer to store allocated array of devices (must not be NULL)
 * @param out_count Pointer to store device count (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Enumerates all audio output devices (speakers/headphones) available on the system.
 * The caller is responsible for freeing the returned array with audio_free_device_list().
 *
 * @note This function initializes PortAudio temporarily if not already initialized.
 * @note Devices with maxOutputChannels > 0 are included.
 *
 * @warning Must call audio_free_device_list() to free the returned array.
 *
 * @ingroup audio
 */
asciichat_error_t audio_list_output_devices(audio_device_info_t **out_devices, unsigned int *out_count);

/**
 * @brief Free device list allocated by audio_list_input_devices/audio_list_output_devices
 * @param devices Device array to free (can be NULL)
 *
 * Frees the device array allocated by audio_list_input_devices() or
 * audio_list_output_devices(). Safe to call with NULL.
 *
 * @ingroup audio
 */
void audio_free_device_list(audio_device_info_t *devices);

/** @} */

/* ============================================================================
 * Protocol Helper Functions - Audio Format Conversion
 * @{
 */

/**
 * @brief Dequantize network audio samples from int32 to float
 * @param samples_ptr Pointer to quantized samples (network byte order uint32_t values)
 * @param total_samples Number of samples to convert
 * @param out_samples Output buffer for float samples (must be pre-allocated)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Converts quantized audio samples from network byte order (uint32_t) to floating
 * point format. Performs the following transformation:
 * - Read uint32_t in network byte order
 * - Convert to int32_t via ntohl()
 * - Scale to float range [-1.0, 1.0] by dividing by 2147483647.0
 *
 * This helper eliminates code duplication between server and client handlers.
 *
 * @note out_samples must be pre-allocated by caller with space for total_samples floats
 * @note samples_ptr should point to total_samples * sizeof(uint32_t) bytes
 *
 * @ingroup audio
 */
asciichat_error_t audio_dequantize_samples(const uint8_t *samples_ptr, uint32_t total_samples, float *out_samples);

/** @} */

/* ============================================================================
 * Audio Ring Buffer Management
 * @{
 */

/**
 * @brief Create a new audio ring buffer (for playback with jitter buffering)
 * @return Pointer to newly created audio ring buffer, or NULL on failure
 *
 * Creates a new audio ring buffer for storing audio samples. The buffer has
 * a fixed size defined by AUDIO_RING_BUFFER_SIZE and is optimized for real-time
 * audio streaming with jitter buffering support (enabled by default).
 *
 * @note The ring buffer is thread-safe and can be used concurrently by capture
 *       and playback threads.
 * @note Ring buffer includes jitter buffer threshold for network latency compensation.
 * @note Use audio_ring_buffer_create_for_capture() for capture buffers.
 *
 * @warning Must call audio_ring_buffer_destroy() to free resources.
 *
 * @ingroup audio
 */
audio_ring_buffer_t *audio_ring_buffer_create(void);

/**
 * @brief Create a new audio ring buffer for capture (without jitter buffering)
 * @return Pointer to newly created audio ring buffer, or NULL on failure
 *
 * Creates an audio ring buffer optimized for direct microphone input from PortAudio.
 * Unlike playback buffers, capture buffers disable jitter buffering since PortAudio
 * writes directly from the microphone with no network latency.
 *
 * @note The ring buffer is thread-safe and can be used concurrently by capture
 *       and playback threads.
 * @note Jitter buffering is disabled for capture buffers.
 *
 * @warning Must call audio_ring_buffer_destroy() to free resources.
 *
 * @ingroup audio
 */
audio_ring_buffer_t *audio_ring_buffer_create_for_capture(void);

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
 * @brief Clear all audio samples from ring buffer
 * @param rb Audio ring buffer (must not be NULL)
 *
 * Resets the ring buffer to empty state, clearing all samples.
 * Used during shutdown to prevent stale audio from playing.
 *
 * @ingroup audio
 */
void audio_ring_buffer_clear(audio_ring_buffer_t *rb);

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

/* ============================================================================
 * Audio Resampling Utilities
 * @{
 */

/**
 * @brief Resample audio using linear interpolation
 * @param src Source samples at src_rate
 * @param src_samples Number of source samples
 * @param dst Destination buffer at dst_rate
 * @param dst_samples Number of destination samples to produce
 * @param src_rate Source sample rate (e.g., 48000)
 * @param dst_rate Destination sample rate (e.g., 44100)
 *
 * Performs simple linear interpolation resampling from one sample rate to another.
 * Suitable for real-time audio where computational efficiency is more important
 * than perfect audio quality. For higher quality, consider a polyphase resampler.
 *
 * Example usage:
 * @code
 * // Resample from 48kHz buffer to 44.1kHz output
 * size_t in_samples = 960;  // 20ms at 48kHz
 * size_t out_samples = 882; // 20ms at 44.1kHz
 * resample_linear(input, in_samples, output, out_samples, 48000, 44100);
 * @endcode
 *
 * @note If src_samples or dst_samples is 0, dst is filled with silence.
 * @note Thread-safe: No shared state.
 *
 * @ingroup audio
 */
void resample_linear(const float *src, size_t src_samples, float *dst, size_t dst_samples, double src_rate,
                     double dst_rate);

/** @} */
