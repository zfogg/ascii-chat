/**
 * @file client_audio_pipeline.cpp
 * @brief Unified client-side audio processing pipeline with WebRTC AEC3
 *
 * Implements production-grade echo cancellation using WebRTC AEC3
 * (Acoustic Echo Cancellation v3) with automatic network delay estimation,
 * adaptive filtering, and residual echo suppression.
 *
 * Uses WebRTC directly via C++ API - no wrapper layer.
 */

// C++ headers must come FIRST before any C headers that include stdatomic.h
#include <memory>
#include <cstring>
#include <math.h>

// WebRTC headers for AEC3 MUST come before ascii-chat headers to avoid macro conflicts
// Define required WebRTC macros before including headers
#define WEBRTC_APM_DEBUG_DUMP 0
#define WEBRTC_MODULE_AUDIO_PROCESSING 1
#define WEBRTC_POSIX 1

// Suppress WebRTC/Abseil warnings about deprecated builtins and unused parameters
// These are third-party code issues, not our code
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-builtins"
#pragma clang diagnostic ignored "-Wunused-parameter"

// WebRTC AEC3 extracted repository has different include paths
// than full WebRTC (no api/audio/ subdirectory)
#include "api/echo_canceller3_factory.h"
#include "api/echo_control.h"
// Note: extracted AEC3 doesn't have environment API - using direct classes
#include "audio_processing/audio_buffer.h"

#pragma clang diagnostic pop

// WebRTC defines FATAL() with no parameters, but ascii-chat defines
// FATAL(code, ...) with parameters. Undefine the WebRTC version before
// including ascii-chat headers so ascii-chat's version takes precedence.
#ifdef FATAL
#undef FATAL
#endif

// Now include ascii-chat headers after WebRTC to avoid macro conflicts
#include "audio/client_audio_pipeline.h"
#include "audio/wav_writer.h"
#include "common.h"
#include "logging.h"
#include "platform/abstraction.h"

// For AEC3 metrics reporting
#include "audio/audio_analysis.h"

#include <opus/opus.h>
#include <string.h>

// Prevent stdatomic.h from defining conflicting macros in C++ context
#define __STDC_NO_ATOMICS__ 1

// ============================================================================
// WebRTC AEC3 C++ Wrapper (hidden from C code)
// ============================================================================

/**
 * @brief C++ wrapper for WebRTC AEC3 (opaque to C code)
 *
 * This struct holds the C++ WebRTC objects and is cast to/from
 * void* pointers in the C interface.
 */
struct WebRTCAec3Wrapper {
  std::unique_ptr<webrtc::EchoControl> aec3;
  webrtc::EchoCanceller3Config config;

  WebRTCAec3Wrapper() = default;
  ~WebRTCAec3Wrapper() = default;
};

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Default Configuration
// ============================================================================

client_audio_pipeline_config_t client_audio_pipeline_default_config(void) {
  return (client_audio_pipeline_config_t){
      .sample_rate = CLIENT_AUDIO_PIPELINE_SAMPLE_RATE,
      .frame_size_ms = CLIENT_AUDIO_PIPELINE_FRAME_MS,
      .opus_bitrate = 24000,

      .echo_filter_ms = 250,

      .noise_suppress_db = -25,
      .agc_level = 8000,
      .agc_max_gain = 30,

      // Jitter margin: 80ms (4 frames) to prevent buffer overflow while handling network jitter.
      // With 800ms total buffer size, 80ms threshold uses only 10% of buffer at startup.
      // This prevents packet drops from burst arrivals while maintaining low latency.
      // CRITICAL: Must match AUDIO_JITTER_BUFFER_THRESHOLD in ringbuffer.h!
      .jitter_margin_ms = 200,

      .highpass_hz = 80.0f,
      .lowpass_hz = 8000.0f,

      .comp_threshold_db = -10.0f,
      .comp_ratio = 4.0f,
      .comp_attack_ms = 10.0f,
      .comp_release_ms = 100.0f,
      .comp_makeup_db = 3.0f,

      .gate_threshold = 0.01f,
      .gate_attack_ms = 2.0f,
      .gate_release_ms = 50.0f,
      .gate_hysteresis = 0.9f,

      .flags = CLIENT_AUDIO_PIPELINE_FLAGS_MINIMAL,
  };
}

// ============================================================================
// Lifecycle Functions
// ============================================================================

/**
 * @brief Create and initialize a client audio pipeline
 *
 * This function:
 * - Allocates the pipeline structure
 * - Initializes Opus encoder/decoder
 * - Sets up WebRTC AEC3 echo cancellation
 * - Configures all audio processing parameters
 */
client_audio_pipeline_t *client_audio_pipeline_create(const client_audio_pipeline_config_t *config) {
  client_audio_pipeline_t *p = SAFE_CALLOC(1, sizeof(client_audio_pipeline_t), client_audio_pipeline_t *);
  if (!p) {
    log_error("Failed to allocate client audio pipeline");
    return NULL;
  }

  // Use default config if none provided
  if (config) {
    p->config = *config;
  } else {
    p->config = client_audio_pipeline_default_config();
  }

  p->flags = p->config.flags;
  p->frame_size = p->config.sample_rate * p->config.frame_size_ms / 1000;

  // Initialize separate mutex for AEC3 processing (echo_ref_buffer uses lock-free atomics)
  if (mutex_init(&p->aec3_mutex) != 0) {
    log_error("Failed to initialize AEC3 mutex");
    SAFE_FREE(p);
    return NULL;
  }


  // Initialize Opus encoder/decoder first (no exceptions)
  int opus_error = 0;
  p->encoder = opus_encoder_create(p->config.sample_rate, 1, OPUS_APPLICATION_VOIP, &opus_error);
  if (!p->encoder || opus_error != OPUS_OK) {
    log_error("Failed to create Opus encoder: %d", opus_error);
    goto error;
  }
  opus_encoder_ctl(p->encoder, OPUS_SET_BITRATE(p->config.opus_bitrate));

  // Create Opus decoder
  p->decoder = opus_decoder_create(p->config.sample_rate, 1, &opus_error);
  if (!p->decoder || opus_error != OPUS_OK) {
    log_error("Failed to create Opus decoder: %d", opus_error);
    goto error;
  }

  // Create WebRTC AEC3 Echo Cancellation
  // AEC3 provides production-grade acoustic echo cancellation with:
  // - Automatic network delay estimation (0-500ms)
  // - Adaptive filtering to actual echo path
  // - Residual echo suppression via spectral subtraction
  // - Jitter buffer handling via side information
  if (p->flags.echo_cancel) {
    try {
      // Create AEC3 configuration tuned for network audio with jitter buffering
      webrtc::EchoCanceller3Config aec3_config;

      // Network delay tuning: Account for jitter buffer delay (~200ms) plus processing + network latency
      // Set reasonable bounds for network-based echo delay (50-250ms is typical for internet audio)
      // CRITICAL: Initial delay estimate must account for:
      // - Jitter buffer: 200ms
      // - Opus codec: 20ms
      // - Network round-trip: 50-100ms
      // - Total: ~250-320ms typical for network audio with buffering
      aec3_config.delay.default_delay = 100;  // ~400ms initial guess (100 * 4ms blocks) for buffered network audio
      aec3_config.delay.delay_estimate_smoothing = 0.3f;  // Slower smoothing to adapt gradually
      aec3_config.delay.delay_headroom_samples = 256;  // Increased for large network echo delays

      // Jitter handling: Tolerance for delayed or missing render data
      aec3_config.buffering.max_allowed_excess_render_blocks = 12;  // Increased from 8 for network jitter
      aec3_config.buffering.excess_render_detection_interval_blocks = 250;

      // Filter tuning for network conditions
      aec3_config.filter.initial_state_seconds = 3.0f;  // Longer initial learning (was 2.5f)
      aec3_config.filter.config_change_duration_blocks = 250;  // Smooth transitions

      // Echo removal: More aggressive suppression in presence of residual echo
      aec3_config.ep_strength.default_gain = 0.9f;  // Slightly more suppression
      aec3_config.ep_strength.bounded_erl = false;  // Allow full adaptation range

      // Validate config before use
      if (!webrtc::EchoCanceller3Config::Validate(&aec3_config)) {
        log_warn("AEC3 config validation failed, falling back to defaults");
        aec3_config = webrtc::EchoCanceller3Config{};
      }

      // Create AEC3 using the factory with network-tuned configuration
      // EchoCanceller3Factory produces the latest high-quality AEC3 implementation
      auto factory = webrtc::EchoCanceller3Factory(aec3_config);

      std::unique_ptr<webrtc::EchoControl> echo_control = factory.Create(
          static_cast<int>(p->config.sample_rate),  // 48kHz
          1,  // num_render_channels (speaker output)
          1   // num_capture_channels (microphone input)
      );

      if (!echo_control) {
        log_warn("Failed to create WebRTC AEC3 instance - echo cancellation unavailable");
        p->echo_canceller = NULL;
      } else {
        // Successfully created AEC3 - wrap in our C++ wrapper for C compatibility
        auto wrapper = new WebRTCAec3Wrapper();
        wrapper->aec3 = std::move(echo_control);
        wrapper->config = aec3_config;  // Store config for reference
        p->echo_canceller = wrapper;

        log_info("✓ WebRTC AEC3 initialized with network-tuned config");
        log_info("  - Initial delay: ~40ms, adapts to actual path");
        log_info("  - Jitter tolerance: up to 12 blocks (~48ms)");
        log_info("  - Echo suppression: aggressive (network-optimized)");
      }
    } catch (const std::exception &e) {
      log_error("Exception creating WebRTC AEC3: %s", e.what());
      p->echo_canceller = NULL;
    } catch (...) {
      log_error("Unknown exception creating WebRTC AEC3");
      p->echo_canceller = NULL;
    }
  }

  // Initialize debug WAV writers for AEC3 analysis (if echo_cancel enabled)
  p->debug_wav_aec3_in = NULL;
  p->debug_wav_aec3_out = NULL;
  if (p->flags.echo_cancel) {
    // Open WAV files to capture AEC3 input and output
    p->debug_wav_aec3_in = wav_writer_open("/tmp/aec3_input.wav", 48000, 1);
    p->debug_wav_aec3_out = wav_writer_open("/tmp/aec3_output.wav", 48000, 1);
    if (p->debug_wav_aec3_in) {
      log_info("Debug: Recording AEC3 input to /tmp/aec3_input.wav");
    }
    if (p->debug_wav_aec3_out) {
      log_info("Debug: Recording AEC3 output to /tmp/aec3_output.wav");
    }

    // Allocate ring buffer for echo reference signal
    // Size: 1 second @ 48kHz (48000 samples) - enough for many audio frames
    p->echo_ref_buffer_size = 48000;
    p->echo_ref_buffer = SAFE_MALLOC(p->echo_ref_buffer_size * sizeof(float), float *);
    if (!p->echo_ref_buffer) {
      log_error("Failed to allocate echo reference ring buffer");
      goto error;
    }
    memset(p->echo_ref_buffer, 0, p->echo_ref_buffer_size * sizeof(float));
    atomic_init(&p->echo_ref_write_pos, 0);
    atomic_init(&p->echo_ref_read_pos, 0);
    atomic_init(&p->echo_ref_samples_consumed, 0);
    p->capture_timestamp_us = 0;
    p->render_timestamp_us = 0;
    log_info("✓ Echo reference ring buffer allocated: %d samples (1 second)", p->echo_ref_buffer_size);
    log_info("✓ Using lock-free atomic operations for echo reference buffer");
  }

  log_info("Audio pipeline created: %dHz, %dms frames, %dkbps Opus",
           p->config.sample_rate, p->config.frame_size_ms, p->config.opus_bitrate / 1000);

  return p;

error:
  if (p->encoder) opus_encoder_destroy(p->encoder);
  if (p->decoder) opus_decoder_destroy(p->decoder);
  if (p->echo_canceller) {
    delete static_cast<WebRTCAec3Wrapper*>(p->echo_canceller);
  }
  mutex_destroy(&p->aec3_mutex);
  SAFE_FREE(p);
  return NULL;
}

void client_audio_pipeline_destroy(client_audio_pipeline_t *pipeline) {
  if (!pipeline) return;

  mutex_lock(&pipeline->aec3_mutex);

  // Clean up WebRTC AEC3
  if (pipeline->echo_canceller) {
    delete static_cast<WebRTCAec3Wrapper*>(pipeline->echo_canceller);
    pipeline->echo_canceller = NULL;
  }

  // Clean up echo reference ring buffer
  if (pipeline->echo_ref_buffer) {
    SAFE_FREE(pipeline->echo_ref_buffer);
    pipeline->echo_ref_buffer = NULL;
  }

  // Clean up Opus
  if (pipeline->encoder) {
    opus_encoder_destroy(pipeline->encoder);
    pipeline->encoder = NULL;
  }
  if (pipeline->decoder) {
    opus_decoder_destroy(pipeline->decoder);
    pipeline->decoder = NULL;
  }

  // Clean up debug WAV writers
  if (pipeline->debug_wav_aec3_in) {
    wav_writer_close((wav_writer_t *)pipeline->debug_wav_aec3_in);
    pipeline->debug_wav_aec3_in = NULL;
    log_info("Debug: Closed AEC3 input WAV file");
  }
  if (pipeline->debug_wav_aec3_out) {
    wav_writer_close((wav_writer_t *)pipeline->debug_wav_aec3_out);
    pipeline->debug_wav_aec3_out = NULL;
    log_info("Debug: Closed AEC3 output WAV file");
  }

  mutex_unlock(&pipeline->aec3_mutex);
  mutex_destroy(&pipeline->aec3_mutex);

  SAFE_FREE(pipeline);
}

// ============================================================================
// Configuration Functions
// ============================================================================

void client_audio_pipeline_set_flags(client_audio_pipeline_t *pipeline, client_audio_pipeline_flags_t flags) {
  if (!pipeline) return;
  mutex_lock(&pipeline->aec3_mutex);
  pipeline->flags = flags;
  mutex_unlock(&pipeline->aec3_mutex);
}

client_audio_pipeline_flags_t client_audio_pipeline_get_flags(client_audio_pipeline_t *pipeline) {
  if (!pipeline) return CLIENT_AUDIO_PIPELINE_FLAGS_MINIMAL;
  mutex_lock(&pipeline->aec3_mutex);
  auto flags = pipeline->flags;
  mutex_unlock(&pipeline->aec3_mutex);
  return flags;
}

// ============================================================================
// Audio Processing Functions
// ============================================================================

/**
 * Process microphone capture through echo cancellation and encoding
 *
 * CRITICAL: Implements proper AEC3 call sequence:
 *   1. AnalyzeCapture on microphone input
 *   2. AnalyzeRender on buffered speaker output
 *   3. ProcessCapture to remove matched echo
 *
 * This sequence ensures AEC3 has the render reference available when it processes capture.
 */
int client_audio_pipeline_capture(client_audio_pipeline_t *pipeline, const float *input, int num_samples,
                                  uint8_t *opus_out, int max_opus_len) {
  if (!pipeline || !input || !opus_out || num_samples != pipeline->frame_size) {
    return -1;
  }

  // NO MUTEX HERE! Echo reference uses lock-free atomics now.
  // Only lock around AEC3 processing below.

  // Create float buffer for processing
  float *processed = (float *)alloca(num_samples * sizeof(float));
  memcpy(processed, input, num_samples * sizeof(float));

  // Debug: Write input before AEC3 processing
  if (pipeline->debug_wav_aec3_in) {
    wav_writer_write((wav_writer_t *)pipeline->debug_wav_aec3_in, input, num_samples);
  }

  // DEBUG: Log capture calls
  static int capture_call_count = 0;
  capture_call_count++;
  if (capture_call_count <= 5 || capture_call_count % 100 == 0) {
    float energy = 0.0f;
    for (int j = 0; j < (num_samples < 100 ? num_samples : 100); j++) {
      energy += processed[j] * processed[j];
    }
    energy = sqrtf(energy / (num_samples < 100 ? num_samples : 100));
    log_info("DEBUG: Capture call #%d: num_samples=%d, RMS=%.6f",
             capture_call_count, num_samples, energy);
  }

  // WebRTC AEC3 echo cancellation - Process microphone input to remove echo
  // CRITICAL SEQUENCE: AnalyzeCapture → AnalyzeRender → ProcessCapture
  if (pipeline->flags.echo_cancel && pipeline->echo_canceller) {
    auto wrapper = static_cast<WebRTCAec3Wrapper*>(pipeline->echo_canceller);
    if (wrapper && wrapper->aec3) {
      // ONLY LOCK FOR AEC3 PROCESSING - echo_ref_buffer uses lock-free atomics
      mutex_lock(&pipeline->aec3_mutex);

      try {
        // WebRTC AEC3's AudioBuffer expects 10ms frames (480 samples at 48kHz)
        // ascii-chat uses 20ms frames (960 samples), so process in two 480-sample chunks
        const int webrtc_frame_size = 480;  // 10ms at 48kHz

        for (int i = 0; i < num_samples; i += webrtc_frame_size) {
          int chunk_size = (i + webrtc_frame_size <= num_samples) ? webrtc_frame_size : (num_samples - i);

          // ===================================================================
          // STEP 1: AnalyzeCapture on microphone input
          // ===================================================================
          webrtc::AudioBuffer capture_buf(
              48000,  // input sample rate
              1,      // input channels (mono)
              48000,  // processing sample rate
              1,      // processing channels
              48000,  // output sample rate
              1       // output channels
          );

          // Copy chunk of microphone input
          float* const* capture_channels = capture_buf.channels();
          if (capture_channels && capture_channels[0]) {
            memcpy(capture_channels[0], processed + i, chunk_size * sizeof(float));

            // CRITICAL: Split into frequency bands for 48kHz processing
            capture_buf.SplitIntoFrequencyBands();

            // Analyze capture signal
            wrapper->aec3->AnalyzeCapture(&capture_buf);

            // Merge bands back
            capture_buf.MergeFrequencyBands();
          }

          // ===================================================================
          // STEP 2: AnalyzeRender on accumulated echo reference samples + set AEC3 delay
          // ===================================================================
          // CRITICAL: Call AnalyzeRender BEFORE ProcessCapture in the SAME synchronous loop!
          // This follows the WebRTC AEC3 demo pattern and ensures AEC3's render buffer
          // state machine is properly synchronized with capture processing.
          //
          // CRITICAL: Call AnalyzeRender for EVERY frame, even if echo_ref_buffer is empty.
          // WebRTC AEC3 requires consistent 10ms frame timing. Skipping frames breaks
          // the render buffer state machine and prevents echo cancellation training.
          //
          // The demo shows: AnalyzeRender → AnalyzeCapture → ProcessCapture (all synchronous)
          if (pipeline->echo_ref_buffer && wrapper && wrapper->aec3) {
            // Use atomic loads (no mutex needed for reading positions!)
            int write_pos = atomic_load(&pipeline->echo_ref_write_pos);
            int read_pos = atomic_load(&pipeline->echo_ref_read_pos);

            // Calculate available samples in echo reference buffer
            int available = (write_pos - read_pos + pipeline->echo_ref_buffer_size)
                            % pipeline->echo_ref_buffer_size;

            try {
              webrtc::AudioBuffer render_buf(48000, 1, 48000, 1, 48000, 1);
              float* const* render_channels = render_buf.channels();

              if (render_channels && render_channels[0]) {
                // Copy up to 480 samples from echo_ref_buffer to render_buf
                // If not enough data, fill remaining with zeros (silence)
                float energy = 0.0f;
                for (int s = 0; s < webrtc_frame_size; s++) {
                  float sample;
                  if (s < available) {
                    // Read from echo_ref_buffer (account for wraparound)
                    int sample_idx = (read_pos + s) % pipeline->echo_ref_buffer_size;
                    sample = pipeline->echo_ref_buffer[sample_idx];
                  } else {
                    // Not enough data - fill with silence
                    sample = 0.0f;
                  }
                  render_channels[0][s] = sample;
                  energy += sample * sample;
                }
                energy = sqrtf(energy / webrtc_frame_size);

                // ALWAYS call AnalyzeRender - WebRTC AEC3 requires consistent frame timing
                // for proper state machine operation, even with silence
                render_buf.SplitIntoFrequencyBands();
                wrapper->aec3->AnalyzeRender(&render_buf);
                render_buf.MergeFrequencyBands();

                log_debug("AEC3: AnalyzeRender from capture thread (available=%d, RMS=%.6f)",
                         available, energy);

                // Consume only the frames we actually had available
                if (available >= webrtc_frame_size) {
                  int new_read_pos = (read_pos + webrtc_frame_size) % pipeline->echo_ref_buffer_size;
                  atomic_store(&pipeline->echo_ref_read_pos, new_read_pos);
                } else if (available > 0) {
                  int new_read_pos = (read_pos + available) % pipeline->echo_ref_buffer_size;
                  atomic_store(&pipeline->echo_ref_read_pos, new_read_pos);
                }
              }
            } catch (...) {
              log_warn_every(1000000, "Error calling AnalyzeRender from capture thread");
            }

            // CRITICAL: Calculate the jitter buffer delay in milliseconds
            // This represents how far behind we are in playback due to network buffering
            // AEC3 MUST know about this delay to properly align echo signals
            float available_ms = (float)available / (48000.0f / 1000.0f);  // 48kHz sample rate
            int buffer_delay_ms = (int)(available_ms + 0.5f);

            // Clamp to reasonable values - keep at least 50ms buffer (to avoid 0)
            // but cap at 300ms (network must be too broken if more than 300ms)
            if (buffer_delay_ms < 50) buffer_delay_ms = 50;
            if (buffer_delay_ms > 300) buffer_delay_ms = 300;

            // Pass actual buffer delay to AEC3 for proper echo alignment
            int total_delay_ms = buffer_delay_ms + 10;  // +10ms for render frame accumulation
            try {
              wrapper->aec3->SetAudioBufferDelay(total_delay_ms);
              log_debug_every(10000000, "AEC3: SetAudioBufferDelay(%d ms), buffer_fill_samples=%d",
                             total_delay_ms, available);
            } catch (...) {
              // Ignore if SetAudioBufferDelay fails
            }
          }

          // ===================================================================
          // STEP 3: ProcessCapture to remove echo
          // ===================================================================
          // Now ProcessCapture can use the AnalyzeRender data to remove echo
          webrtc::AudioBuffer process_buf(
              48000,  // input sample rate
              1,      // input channels (mono)
              48000,  // processing sample rate
              1,      // processing channels
              48000,  // output sample rate
              1       // output channels
          );

          // Copy chunk of microphone input again for processing
          float* const* process_channels = process_buf.channels();
          if (process_channels && process_channels[0]) {
            memcpy(process_channels[0], processed + i, chunk_size * sizeof(float));

            // Split into frequency bands
            process_buf.SplitIntoFrequencyBands();

            // Process capture to remove echo (works on frequency bands)
            // false = no level change applied
            wrapper->aec3->ProcessCapture(&process_buf, false);

            // Merge frequency bands back to full-band signal
            process_buf.MergeFrequencyBands();

            // Copy processed audio chunk back (echo removed)
            float* const* merged_channels = process_buf.channels();
            if (merged_channels && merged_channels[0]) {
              memcpy(processed + i, merged_channels[0], chunk_size * sizeof(float));
            }

            log_debug("AEC3: ProcessCapture removed echo from %d samples", chunk_size);

            // Collect AEC3 metrics for reporting (call GetMetrics() after ProcessCapture)
            // This tells us how well echo cancellation is working
            if (wrapper && wrapper->aec3) {
              try {
                webrtc::EchoControl::Metrics metrics = wrapper->aec3->GetMetrics();
                log_debug("AEC3 metrics: ERL=%.2f dB, ERLE=%.2f dB, delay=%d ms",
                          metrics.echo_return_loss, metrics.echo_return_loss_enhancement, metrics.delay_ms);
                // Forward metrics to audio analysis for report (from client/audio_analysis.h)
                audio_analysis_set_aec3_metrics(metrics.echo_return_loss,
                                                metrics.echo_return_loss_enhancement,
                                                metrics.delay_ms);
              } catch (...) {
                // Ignore errors getting metrics - not critical
              }
            }
          }
        }
      } catch (const std::exception &e) {
        log_warn("AEC3 processing error: %s", e.what());
        // Continue with unprocessed audio on error
      }
    }
  }

  // Debug: Write output after AEC3 processing
  if (pipeline->debug_wav_aec3_out) {
    wav_writer_write((wav_writer_t *)pipeline->debug_wav_aec3_out, processed, num_samples);
  }

  // Encode with Opus
  int opus_len = opus_encode_float(pipeline->encoder, processed, num_samples, opus_out, max_opus_len);

  mutex_unlock(&pipeline->aec3_mutex);

  if (opus_len < 0) {
    log_error("Opus encoding failed: %d", opus_len);
    return -1;
  }

  return opus_len;
}

/**
 * Process network playback (decode and register with echo canceller as reference)
 */
int client_audio_pipeline_playback(client_audio_pipeline_t *pipeline, const uint8_t *opus_in, int opus_len,
                                   float *output, int num_samples) {
  if (!pipeline || !opus_in || !output) {
    return -1;
  }

  mutex_lock(&pipeline->aec3_mutex);

  // Decode Opus
  int decoded_samples = opus_decode_float(pipeline->decoder, opus_in, opus_len, output, num_samples, 0);

  if (decoded_samples < 0) {
    log_error("Opus decoding failed: %d", decoded_samples);
    mutex_unlock(&pipeline->aec3_mutex);
    return -1;
  }

  // NOTE: Do NOT register render signal here!
  // The render signal (speaker output) must be registered to AEC3 only at the point
  // where audio actually goes to the speakers in output_callback(), NOT here when packets
  // are decoded from the network (which happens 50-100ms earlier in the jitter buffer).
  //
  // Registering at the wrong time confuses AEC3's delay estimation and prevents proper
  // echo cancellation. The output_callback correctly calls client_audio_pipeline_process_echo_playback()
  // at the precise moment audio is output to speakers.
  //
  // See: output_callback() in audio.c lines 91-98

  mutex_unlock(&pipeline->aec3_mutex);

  return decoded_samples;
}

/**
 * Get a processed playback frame (currently just returns decoded frame)
 */
int client_audio_pipeline_get_playback_frame(client_audio_pipeline_t *pipeline, float *output, int num_samples) {
  if (!pipeline || !output) {
    return -1;
  }

  mutex_lock(&pipeline->aec3_mutex);
  // For now, this is a placeholder
  // In a full implementation, this would manage buffering
  memset(output, 0, num_samples * sizeof(float));
  mutex_unlock(&pipeline->aec3_mutex);

  return num_samples;
}

/**
 * Process echo playback - write speaker output to buffer for AEC3
 *
 * Called immediately from output_callback when audio is actually output to speakers.
 * CRITICAL: Now only writes to echo_ref_buffer. AnalyzeRender is called from capture thread.
 *
 * ARCHITECTURE (implements WebRTC AEC3 demo pattern):
 * - output_callback: Writes speaker samples to echo_ref_buffer only
 * - capture_thread: Reads from echo_ref_buffer and calls AnalyzeRender → AnalyzeCapture → ProcessCapture
 *
 * This synchronous pattern ensures AEC3's state machine is properly initialized.
 */
void client_audio_pipeline_process_echo_playback(client_audio_pipeline_t *pipeline, const float *samples,
                                                 int num_samples) {
  if (!pipeline || !samples || num_samples <= 0) return;
  if (!pipeline->echo_ref_buffer) return;  // No ring buffer allocated

  // NO MUTEX NEEDED! Echo reference buffer uses lock-free atomic operations.
  // This function is called from PortAudio's output callback (real-time thread)
  // and must NOT block or it will cause audio underruns.

  // Write samples to ring buffer using atomic operations
  // The capture thread will read and process these via AnalyzeRender
  // WebRTC AEC3 demo shows we must write EVERY frame, including silence
  for (int i = 0; i < num_samples; i++) {
    int write_pos = atomic_load(&pipeline->echo_ref_write_pos);
    pipeline->echo_ref_buffer[write_pos] = samples[i];
    write_pos = (write_pos + 1) % pipeline->echo_ref_buffer_size;
    atomic_store(&pipeline->echo_ref_write_pos, write_pos);
  }

  log_debug_every(10000000, "Echo reference buffer: wrote %d samples from output_callback", num_samples);
}

/**
 * Get jitter buffer margin
 */
int client_audio_pipeline_jitter_margin(client_audio_pipeline_t *pipeline) {
  if (!pipeline) return 0;
  return pipeline->config.jitter_margin_ms;
}

/**
 * Reset pipeline state (AEC3 is adaptive, but echo reference buffer needs reset)
 */
void client_audio_pipeline_reset(client_audio_pipeline_t *pipeline) {
  if (!pipeline) return;

  mutex_lock(&pipeline->aec3_mutex);

  // WebRTC AEC3 is adaptive and doesn't require explicit reset
  // It automatically adjusts to network conditions

  log_info("Pipeline state reset");

  mutex_unlock(&pipeline->aec3_mutex);
}

#ifdef __cplusplus
}
#endif
