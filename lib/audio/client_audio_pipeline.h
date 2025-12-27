/**
 * @file client_audio_pipeline.h
 * @brief Unified client-side audio processing pipeline
 * @ingroup audio
 *
 * This header provides a complete audio processing pipeline for ascii-chat clients,
 * integrating WebRTC AEC3 (production-grade echo cancellation), SpeexDSP (noise suppression, AGC, VAD),
 * Speex jitter buffer, Opus codec, and mixer components (compression, noise gate, filters).
 *
 * PIPELINE ARCHITECTURE:
 * ======================
 *
 * CAPTURE PATH (microphone → network):
 *   Mic Input (float32, 48kHz)
 *     ↓ Echo Cancellation (WebRTC AEC3 - automatic network delay estimation + adaptive filtering)
 *     ↓ Preprocessor (Noise/AGC/VAD)
 *     ↓ High-Pass Filter (remove rumble)
 *     ↓ Low-Pass Filter (remove hiss)
 *     ↓ Noise Gate (silence detection)
 *     ↓ Compressor (dynamic range control)
 *     ↓ Opus Encode
 *     → Network
 *
 * PLAYBACK PATH (network → speakers):
 *   Network
 *     ↓ Speex Jitter Buffer
 *     ↓ Opus Decode
 *     ↓ Register with AEC (WebRTC AEC3 reference signal for echo learning)
 *     ↓ Soft Clipping
 *     → Speakers
 *
 * COMPONENT FLAGS:
 * ================
 * Each processing stage can be individually enabled/disabled via flags.
 * This allows testing and debugging specific components.
 *
 * THREAD SAFETY:
 * ==============
 * - Pipeline has internal mutex for state protection
 * - Capture and playback paths can run concurrently
 * - Echo reference feeding is thread-safe
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#include "audio/mixer.h"
#include "platform/mutex.h"

// Forward declarations for SpeexDSP types
typedef struct SpeexPreprocessState_ SpeexPreprocessState;
typedef struct JitterBuffer_ JitterBuffer;

// Forward declarations for WebRTC types (opaque pointers for C compatibility)
typedef struct webrtc_EchoCanceller3 webrtc_EchoCanceller3;

// Forward declarations for Opus types
typedef struct OpusEncoder OpusEncoder;
typedef struct OpusDecoder OpusDecoder;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name Audio Pipeline Constants
 * @{
 */

/** Default sample rate (48kHz, native for Opus) */
#define CLIENT_AUDIO_PIPELINE_SAMPLE_RATE 48000

/** Default frame size in milliseconds */
#define CLIENT_AUDIO_PIPELINE_FRAME_MS 20

/** Default frame size in samples at 48kHz */
#define CLIENT_AUDIO_PIPELINE_FRAME_SIZE (CLIENT_AUDIO_PIPELINE_SAMPLE_RATE * CLIENT_AUDIO_PIPELINE_FRAME_MS / 1000)

/** Echo reference ring buffer size (500ms at 48kHz) */
#define CLIENT_AUDIO_PIPELINE_ECHO_REF_SIZE (CLIENT_AUDIO_PIPELINE_SAMPLE_RATE / 2)

/** Render ring buffer size for lock-free AEC3 feeding (500ms at 48kHz = 24000 samples)
 *  Increased from 100ms to handle jitter buffer delays (200ms) and give AEC3 more history */
#define CLIENT_AUDIO_PIPELINE_RENDER_BUFFER_SIZE 24000

/** Maximum Opus packet size */
#define CLIENT_AUDIO_PIPELINE_MAX_OPUS_PACKET 4000

/** @} */

/**
 * @brief Component enable/disable flags
 *
 * Each flag controls whether a specific processing component is active.
 * Disabling components can help with debugging or reduce CPU usage.
 */
typedef struct {
  /** Enable Speex acoustic echo cancellation */
  bool echo_cancel;
  /** Enable Speex noise suppression */
  bool noise_suppress;
  /** Enable Speex automatic gain control */
  bool agc;
  /** Enable Speex voice activity detection */
  bool vad;
  /** Enable Speex jitter buffer for playback */
  bool jitter_buffer;
  /** Enable dynamic range compressor */
  bool compressor;
  /** Enable noise gate */
  bool noise_gate;
  /** Enable high-pass filter (removes low-frequency rumble) */
  bool highpass;
  /** Enable low-pass filter (removes high-frequency hiss) */
  bool lowpass;
} client_audio_pipeline_flags_t;

/**
 * @brief Default flags with all processing enabled
 */
#define CLIENT_AUDIO_PIPELINE_FLAGS_ALL                                                                                \
  ((client_audio_pipeline_flags_t){                                                                                    \
      .echo_cancel = true,                                                                                             \
      .noise_suppress = true,                                                                                          \
      .agc = true,                                                                                                     \
      .vad = true,                                                                                                     \
      .jitter_buffer = true,                                                                                           \
      .compressor = true,                                                                                              \
      .noise_gate = true,                                                                                              \
      .highpass = true,                                                                                                \
      .lowpass = true,                                                                                                 \
  })

/**
 * @brief Minimal flags for testing (only codec, no processing)
 */
#define CLIENT_AUDIO_PIPELINE_FLAGS_MINIMAL                                                                            \
  ((client_audio_pipeline_flags_t){                                                                                    \
      .echo_cancel = false,                                                                                            \
      .noise_suppress = false,                                                                                         \
      .agc = false,                                                                                                    \
      .vad = false,                                                                                                    \
      .jitter_buffer = false,                                                                                          \
      .compressor = false,                                                                                             \
      .noise_gate = false,                                                                                             \
      .highpass = false,                                                                                               \
      .lowpass = false,                                                                                                \
  })

/**
 * @brief Pipeline configuration parameters
 *
 * All parameters have sensible defaults. Pass NULL to create() for defaults.
 */
typedef struct {
  /** Sample rate in Hz (default: 48000) */
  int sample_rate;
  /** Frame size in milliseconds (default: 20) */
  int frame_size_ms;
  /** Opus bitrate in bps (default: 24000) */
  int opus_bitrate;

  /** Echo cancellation filter length in milliseconds (default: 250) */
  int echo_filter_ms;

  /** Noise suppression level in dB (default: -25, negative = more suppression) */
  int noise_suppress_db;
  /** AGC target level (default: 8000, range 1-32767) */
  int agc_level;
  /** AGC maximum gain in dB (default: 30) */
  int agc_max_gain;

  /** Jitter buffer margin in milliseconds (default: 60) */
  int jitter_margin_ms;

  /** High-pass filter cutoff frequency in Hz (default: 80) */
  float highpass_hz;
  /** Low-pass filter cutoff frequency in Hz (default: 8000) */
  float lowpass_hz;

  /** Compressor threshold in dB (default: -10) */
  float comp_threshold_db;
  /** Compressor ratio (default: 4.0 for 4:1) */
  float comp_ratio;
  /** Compressor attack time in ms (default: 10) */
  float comp_attack_ms;
  /** Compressor release time in ms (default: 100) */
  float comp_release_ms;
  /** Compressor makeup gain in dB (default: 3) */
  float comp_makeup_db;

  /** Noise gate threshold (linear, default: 0.01 = -40dB) */
  float gate_threshold;
  /** Noise gate attack time in ms (default: 2) */
  float gate_attack_ms;
  /** Noise gate release time in ms (default: 50) */
  float gate_release_ms;
  /** Noise gate hysteresis (default: 0.9) */
  float gate_hysteresis;

  /** Component enable flags */
  client_audio_pipeline_flags_t flags;
} client_audio_pipeline_config_t;

/**
 * @brief Get default configuration
 * @return Configuration with sensible defaults for voice chat
 */
client_audio_pipeline_config_t client_audio_pipeline_default_config(void);

/**
 * @brief Client audio pipeline state
 *
 * Owns all audio processing components and buffers.
 * Created with client_audio_pipeline_create(), destroyed with _destroy().
 */
typedef struct {
  /** Component enable flags */
  client_audio_pipeline_flags_t flags;

  /** WebRTC AEC3 echo cancellation (opaque pointer to webrtc::EchoCanceller3) */
  void *echo_canceller;
  /** Speex preprocessor state (noise/AGC/VAD) */
  SpeexPreprocessState *preprocess;

  /** Speex jitter buffer for playback path */
  JitterBuffer *jitter;
  /** Jitter buffer timestamp counter */
  uint32_t jitter_timestamp;
  /** Frame span in timestamp units */
  int jitter_frame_span;

  /** Opus encoder */
  OpusEncoder *encoder;
  /** Opus decoder */
  OpusDecoder *decoder;

  /** Dynamic range compressor */
  compressor_t compressor;
  /** Noise gate */
  noise_gate_t noise_gate;
  /** High-pass filter */
  highpass_filter_t highpass;
  /** Low-pass filter */
  lowpass_filter_t lowpass;

  /** Configuration */
  client_audio_pipeline_config_t config;

  /** Frame size in samples */
  int frame_size;

  /** Work buffer for int16 conversion (Speex uses int16) */
  int16_t *work_i16;
  /** Work buffer for echo reference */
  int16_t *echo_i16;
  /** Work buffer for float processing */
  float *work_float;

  /** Timing synchronization for AEC3 */
  int64_t capture_timestamp_us; // When was the last capture processed
  int64_t render_timestamp_us;  // When was render last analyzed

  /** Pipeline mutex for thread safety (ONLY for AEC3 state, not echo_ref_buffer) */
  mutex_t aec3_mutex; // Separate mutex for AEC3 processing only

  /** Lock-free render ring buffer for AEC3 feeding
   * The output callback writes render samples here without blocking.
   * The capture thread drains this buffer before processing.
   * This prevents priority inversion where the audio callback would block on aec3_mutex.
   */
  float *render_ring_buffer;
  _Atomic int render_ring_write_idx;
  _Atomic int render_ring_read_idx;

  /** Initialization state */
  bool initialized;

  /** Debug WAV writers for AEC3 analysis */
  void *debug_wav_aec3_in;  // Microphone input before AEC3
  void *debug_wav_aec3_out; // Microphone output after AEC3
} client_audio_pipeline_t;

/**
 * @name Lifecycle Functions
 * @{
 */

/**
 * @brief Create a new client audio pipeline
 * @param config Pipeline configuration (NULL for defaults)
 * @return New pipeline instance, or NULL on failure
 *
 * Allocates and initializes all audio processing components.
 * Must be destroyed with client_audio_pipeline_destroy().
 */
client_audio_pipeline_t *client_audio_pipeline_create(const client_audio_pipeline_config_t *config);

/**
 * @brief Destroy a client audio pipeline
 * @param pipeline Pipeline to destroy (can be NULL)
 *
 * Frees all resources including SpeexDSP states, Opus codec,
 * and all work buffers.
 */
void client_audio_pipeline_destroy(client_audio_pipeline_t *pipeline);

/** @} */

/**
 * @name Flag Management
 * @{
 */

/**
 * @brief Set component enable flags
 * @param pipeline Pipeline instance
 * @param flags New flags to set
 *
 * Thread-safe. Changes take effect on next capture/playback call.
 */
void client_audio_pipeline_set_flags(client_audio_pipeline_t *pipeline, client_audio_pipeline_flags_t flags);

/**
 * @brief Get current component enable flags
 * @param pipeline Pipeline instance
 * @return Current flags
 */
client_audio_pipeline_flags_t client_audio_pipeline_get_flags(client_audio_pipeline_t *pipeline);

/** @} */

/**
 * @name Capture Path (Microphone → Network)
 * @{
 */

/**
 * @brief Process captured audio and encode to Opus
 * @param pipeline Pipeline instance
 * @param input Input samples (float32, -1.0 to 1.0)
 * @param num_samples Number of input samples (should match frame_size)
 * @param opus_out Output buffer for Opus packet
 * @param opus_out_size Size of opus_out buffer (should be >= MAX_OPUS_PACKET)
 * @return Number of bytes written to opus_out, or negative on error
 *
 * Processing pipeline (when flags enabled):
 * 1. Convert float → int16
 * 2. Echo cancellation (subtract speaker output from mic input)
 * 3. Speex preprocessor (noise suppression, AGC, VAD)
 * 4. Convert int16 → float
 * 5. High-pass filter
 * 6. Low-pass filter
 * 7. Noise gate
 * 8. Compressor
 * 9. Opus encode
 */
int client_audio_pipeline_capture(client_audio_pipeline_t *pipeline, const float *input, int num_samples,
                                  uint8_t *opus_out, int opus_out_size);

/** @} */

/**
 * @name Playback Path (Network → Speakers)
 * @{
 */

/**
 * @brief Decode Opus packet and process for playback
 * @param pipeline Pipeline instance
 * @param opus_in Opus packet data (can be NULL for packet loss concealment)
 * @param opus_len Opus packet length (0 if opus_in is NULL)
 * @param output Output buffer for decoded samples (float32)
 * @param max_samples Maximum samples to write to output
 * @return Number of samples written to output, or negative on error
 *
 * Processing pipeline (when flags enabled):
 * 1. Put Opus packet into jitter buffer
 * 2. Get packet from jitter buffer (handles reordering, loss)
 * 3. Opus decode
 * 4. Soft clipping
 *
 * Note: Output should be fed to speakers AND to echo reference
 * via client_audio_pipeline_feed_echo_ref().
 */
int client_audio_pipeline_playback(client_audio_pipeline_t *pipeline, const uint8_t *opus_in, int opus_len,
                                   float *output, int max_samples);

/**
 * @brief Get audio frame from jitter buffer for playback callback
 * @param pipeline Pipeline instance
 * @param output Output buffer for decoded samples (float32)
 * @param num_samples Number of samples to retrieve
 * @return Number of samples written, or negative on error
 *
 * This is called from the audio output callback to get the next
 * frame of audio. It pulls from the jitter buffer and decodes.
 */
int client_audio_pipeline_get_playback_frame(client_audio_pipeline_t *pipeline, float *output, int num_samples);

/** @} */

/**
 * @name Echo Playback (for Buffered AEC)
 * @{
 */

/**
 * @brief Feed render signal to AEC3 for echo cancellation
 * @param pipeline Pipeline instance
 * @param samples Audio samples received from network (the render signal)
 * @param num_samples Number of samples
 *
 * Call this when audio is received from the network and decoded.
 * This is the "render" signal - what will be played to speakers.
 * AEC3 uses this to estimate and subtract the echo from microphone input.
 *
 * Thread-safe. Must be called before the corresponding capture is processed.
 */
void client_audio_pipeline_analyze_render(client_audio_pipeline_t *pipeline, const float *samples, int num_samples);

/** @} */

/**
 * @name Status and Diagnostics
 * @{
 */

/**
 * @brief Get jitter buffer margin (buffered time in ms)
 * @param pipeline Pipeline instance
 * @return Current jitter buffer margin in milliseconds
 */
int client_audio_pipeline_jitter_margin(client_audio_pipeline_t *pipeline);

/**
 * @brief Check if VAD detected voice activity in last capture
 * @param pipeline Pipeline instance
 * @return true if voice was detected, false otherwise
 */
bool client_audio_pipeline_voice_detected(client_audio_pipeline_t *pipeline);

/**
 * @brief Reset pipeline state
 * @param pipeline Pipeline instance
 *
 * Resets echo canceller, jitter buffer, and filter states.
 * Call when starting a new audio session.
 */
void client_audio_pipeline_reset(client_audio_pipeline_t *pipeline);

/** @} */

#ifdef __cplusplus
}
#endif
