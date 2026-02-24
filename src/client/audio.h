/**
 * @file client/audio.h
 * @ingroup client_audio
 * @brief ascii-chat Client Audio Processing Management Interface
 *
 * Defines the interface for audio system initialization, capture thread
 * management, and audio sample processing in the ascii-chat client.
 *
 * The audio subsystem handles bidirectional audio streaming using Opus encoding
 * and PortAudio for platform-independent audio capture and playback.
 *
 * ## Audio Pipeline
 *
 * - **Capture**: Microphone → PortAudio → Opus encoder → Send to server
 * - **Playback**: Server packet → Opus decoder → Ring buffer → PortAudio → Speaker
 *
 * ## Configuration
 *
 * **Options**:
 * - `--audio`: Enable audio (disabled by default)
 * - `--audio-device <index>`: Select audio input/output device
 *
 * **Parameters**:
 * - Sample rate: 44100 Hz
 * - Channels: 1 (mono)
 * - Format: 32-bit float samples
 * - Opus bitrate: Variable (16-48 kbps)
 * - Batch size: 256 samples (~5.8ms at 44.1kHz)
 * - Ring buffer: 8192 samples (~185ms jitter buffer)
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 *
 * @see topic_client_audio "Audio Architecture"
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <ascii-chat/audio/audio.h>
#include <ascii-chat/audio/client_audio_pipeline.h>

/**
 * @brief Process received audio samples from server
 *
 * Queues received audio samples for playback. Handles jitter buffering
 * to smooth out network timing variations. Called by protocol thread
 * when processing PACKET_TYPE_AUDIO_BATCH from server.
 *
 * The samples are queued in a ring buffer that smooths out timing
 * variations. The playback thread drains this buffer at a constant rate.
 *
 * @param samples Audio sample data from server (32-bit float, mono)
 * @param num_samples Number of samples in buffer
 *
 * @note This is called by the protocol thread - must be thread-safe.
 *       Ring buffer handles synchronization internally.
 *
 * @ingroup client_audio
 */
void audio_process_received_samples(const float *samples, int num_samples);

/**
 * @brief Initialize audio subsystem
 *
 * Sets up PortAudio, selects audio device, and initializes Opus codec.
 * Must be called before spawning audio threads.
 *
 * Initialization steps:
 * 1. Initialize PortAudio library
 * 2. Enumerate audio devices (for --audio-device selection)
 * 3. Open audio device for input (microphone) and output (speakers)
 * 4. Create Opus encoder for capture
 * 5. Create Opus decoder for playback
 * 6. Initialize ring buffer for jitter buffering
 * 7. Start PortAudio stream
 *
 * **Options affecting initialization**:
 * - `--audio-device <index>`: Which audio device to use (default: system default)
 * - `--audio` flag must be set (audio disabled by default)
 *
 * @return 0 on success, negative on error:
 *         - ERROR_AUDIO: Device not found or PortAudio error
 *         - ERROR_MEMORY: Insufficient memory for buffers
 *
 * @note Optional - only called if --audio flag is set.
 *       Client works without audio (defaults to disabled).
 *
 * @ingroup client_audio
 *
 * @see audio_cleanup "Cleanup audio resources"
 */
int audio_client_init(void);

/**
 * @brief Start audio capture thread
 *
 * Spawns audio capture/transmission thread in global worker pool.
 * Thread continuously captures audio from microphone, encodes with Opus,
 * and sends to server.
 *
 * The capture loop:
 * 1. Captures samples from PortAudio input stream
 * 2. Encodes with Opus encoder (variable bitrate)
 * 3. Sends PACKET_TYPE_AUDIO_OPUS packet to server
 * 4. Checks exit signals
 * 5. Repeats until exit signal or connection loss
 *
 * Must be called after audio_client_init() and server connection established.
 *
 * @return 0 on success, negative on error (thread pool full)
 *
 * @note Audio playback (from ring buffer) happens continuously in
 *       PortAudio callback, not in a separate thread.
 *
 * @ingroup client_audio
 *
 * @see audio_stop_thread "Stop audio capture thread"
 */
int audio_start_thread(void);

/**
 * @brief Stop audio capture thread
 *
 * Signals audio capture thread to exit and waits for it to join.
 * Must be called before reconnection or shutdown.
 *
 * The function:
 * 1. Sets exit flag
 * 2. Polls audio_thread_exited() until thread exits
 * 3. Drains remaining samples in ring buffer
 *
 * Playback continues until ring buffer is empty (graceful fade-out).
 *
 * @note Audio playback thread is internal to PortAudio callback and
 *       continues until PortAudio stream is closed in audio_cleanup().
 *
 * @ingroup client_audio
 *
 * @see audio_start_thread "Start audio capture thread"
 */
void audio_stop_thread(void);

/**
 * @brief Check if audio capture thread has exited
 *
 * Atomically checks whether the audio capture thread has finished.
 * Main thread uses this to wait for clean shutdown before reconnection.
 *
 * @return true if thread exited, false otherwise
 *
 * @note Playback thread (PortAudio callback) exits when PortAudio
 *       stream is closed, not when capture thread exits.
 *
 * @ingroup client_audio
 */
bool audio_thread_exited(void);

/**
 * @brief Cleanup audio subsystem
 *
 * Closes PortAudio stream and releases audio resources.
 * Must be called after audio_stop_thread() and before process exit.
 *
 * The cleanup process:
 * 1. Stops PortAudio stream (interrupts playback)
 * 2. Closes audio device
 * 3. Destroys Opus encoder/decoder
 * 4. Frees ring buffer
 * 5. Terminates PortAudio
 *
 * Must be called in proper order during shutdown to avoid resource leaks.
 *
 * @ingroup client_audio
 *
 * @see audio_client_init "Initialize audio subsystem"
 */
void audio_cleanup(void);

/**
 * @brief Get the audio pipeline (for advanced usage)
 *
 * Returns the internal audio pipeline structure for direct access to
 * encoder/decoder or buffer manipulation (advanced usage only).
 *
 * @return Pointer to the audio pipeline, or NULL if not initialized
 *
 * @note Most code should use audio_process_received_samples() and
 *       the capture thread instead of direct pipeline access.
 *
 * @ingroup client_audio
 */
client_audio_pipeline_t *audio_get_pipeline(void);

/**
 * @brief Decode Opus packet using the audio pipeline
 *
 * Decodes a single Opus-encoded audio packet to PCM samples.
 * Can be used independently of the audio pipeline for testing or
 * external audio processing.
 *
 * @param opus_data Opus packet data (variable-length frame)
 * @param opus_len Opus packet length in bytes
 * @param output Output buffer for decoded samples (float array)
 * @param max_samples Maximum samples output buffer can hold
 * @return Number of decoded samples, or negative on error (invalid Opus data)
 *
 * @note The pipeline must be initialized (audio_client_init) for this to work.
 *
 * @ingroup client_audio
 */
int audio_decode_opus(const uint8_t *opus_data, size_t opus_len, float *output, int max_samples);

/**
 * @brief Get the global audio context for use by other subsystems
 *
 * Returns the audio context for access to low-level audio state.
 * Used by capture subsystem to enable microphone fallback when media file has no audio.
 *
 * @return Pointer to the audio context, or NULL if not initialized
 *
 * @note Most code should use the higher-level interface functions
 *       rather than direct context access.
 *
 * @ingroup client_audio
 */
audio_context_t *audio_get_context(void);
