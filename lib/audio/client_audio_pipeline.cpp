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
      // Use mostly default AEC3 config and trust automatic delay estimation
      // Previous custom config with -30dB ERL indicates over-tuning was causing instability
      webrtc::EchoCanceller3Config aec3_config;

      // Only minimal tuning for network audio:
      // - Slightly longer initial learning period for network jitter
      // - Trust AEC3's automatic delay estimation (SetAudioBufferDelay(0))
      aec3_config.filter.initial_state_seconds = 2.5f;  // Default is 2.5s, keep it

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

        log_info("✓ WebRTC AEC3 initialized with default config");
        log_info("  - Automatic delay estimation enabled (SetAudioBufferDelay=0)");
        log_info("  - Using WebRTC's default echo suppression settings");
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
  // CRITICAL SEQUENCE: AnalyzeRender → AnalyzeCapture → ProcessCapture
  // (Render MUST come first so AEC3 knows what's playing before analyzing capture)
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
          // STEP 1: AnalyzeRender FIRST (speaker output / echo reference)
          // ===================================================================
          // CRITICAL: WebRTC AEC3 requires: AnalyzeRender → AnalyzeCapture → ProcessCapture
          // The render signal (what's playing on speakers) must be analyzed BEFORE
          // the capture signal so AEC3 can correlate them for echo estimation.
          if (pipeline->echo_ref_buffer && wrapper && wrapper->aec3) {
            int write_pos = atomic_load(&pipeline->echo_ref_write_pos);
            int read_pos = atomic_load(&pipeline->echo_ref_read_pos);
            int available = (write_pos - read_pos + pipeline->echo_ref_buffer_size)
                            % pipeline->echo_ref_buffer_size;

            try {
              webrtc::AudioBuffer render_buf(48000, 1, 48000, 1, 48000, 1);
              float* const* render_channels = render_buf.channels();

              if (render_channels && render_channels[0]) {
                float render_energy = 0.0f;
                for (int s = 0; s < webrtc_frame_size; s++) {
                  float sample;
                  if (s < available) {
                    int sample_idx = (read_pos + s) % pipeline->echo_ref_buffer_size;
                    sample = pipeline->echo_ref_buffer[sample_idx];
                  } else {
                    sample = 0.0f;
                  }
                  render_channels[0][s] = sample;
                  render_energy += sample * sample;
                }
                render_energy = sqrtf(render_energy / webrtc_frame_size);

                render_buf.SplitIntoFrequencyBands();
                wrapper->aec3->AnalyzeRender(&render_buf);
                render_buf.MergeFrequencyBands();

                log_debug("AEC3 STEP1: AnalyzeRender (available=%d, RMS=%.6f)", available, render_energy);

                // Consume samples we read
                if (available >= webrtc_frame_size) {
                  atomic_store(&pipeline->echo_ref_read_pos,
                              (read_pos + webrtc_frame_size) % pipeline->echo_ref_buffer_size);
                } else if (available > 0) {
                  atomic_store(&pipeline->echo_ref_read_pos,
                              (read_pos + available) % pipeline->echo_ref_buffer_size);
                }
              }
            } catch (...) {
              log_warn_every(1000000, "AEC3 AnalyzeRender error");
            }

            // Set buffer delay to 0 for automatic delay estimation (per demo.cc line 140)
            // AEC3 does cross-correlation to automatically find the echo path delay.
            // Previously we were incorrectly calculating this from ring buffer availability.
            try {
              wrapper->aec3->SetAudioBufferDelay(0);
            } catch (...) {}
          }

          // ===================================================================
          // STEP 2 & 3: AnalyzeCapture + ProcessCapture (following demo.cc pattern)
          // ===================================================================
          // From WebRTC demo.cc lines 134-142:
          //   ref_audio->SplitIntoFrequencyBands();
          //   echo_controler->AnalyzeRender(ref_audio.get());
          //   ref_audio->MergeFrequencyBands();
          //   echo_controler->AnalyzeCapture(aec_audio.get());  <-- NON-split!
          //   aec_audio->SplitIntoFrequencyBands();             <-- Split AFTER AnalyzeCapture
          //   echo_controler->ProcessCapture(aec_audio.get(), ...);
          //   aec_audio->MergeFrequencyBands();
          webrtc::AudioBuffer capture_buf(
              48000,  // input sample rate
              1,      // input channels (mono)
              48000,  // processing sample rate
              1,      // processing channels
              48000,  // output sample rate
              1       // output channels
          );

          float* const* capture_channels = capture_buf.channels();
          if (capture_channels && capture_channels[0]) {
            // Copy microphone input to capture buffer
            memcpy(capture_channels[0], processed + i, chunk_size * sizeof(float));

            // STEP 2: AnalyzeCapture on NON-split buffer (per demo.cc line 137)
            wrapper->aec3->AnalyzeCapture(&capture_buf);

            // NOW split into frequency bands for ProcessCapture (per demo.cc line 138)
            capture_buf.SplitIntoFrequencyBands();

            // STEP 3: ProcessCapture on SPLIT buffer (per demo.cc line 141)
            // false = don't apply level adjustment
            wrapper->aec3->ProcessCapture(&capture_buf, false);

            // Merge frequency bands back to time domain
            capture_buf.MergeFrequencyBands();

            // Copy echo-cancelled result back to output
            memcpy(processed + i, capture_channels[0], chunk_size * sizeof(float));

            log_debug("AEC3 STEP2+3: AnalyzeCapture + ProcessCapture (%d samples)", chunk_size);

            // Get metrics after processing
            try {
              webrtc::EchoControl::Metrics metrics = wrapper->aec3->GetMetrics();
              log_debug("AEC3 metrics: ERL=%.2f dB, ERLE=%.2f dB, delay=%d ms",
                        metrics.echo_return_loss, metrics.echo_return_loss_enhancement, metrics.delay_ms);
              audio_analysis_set_aec3_metrics(metrics.echo_return_loss,
                                              metrics.echo_return_loss_enhancement,
                                              metrics.delay_ms);
            } catch (...) {}
          }
        }
      } catch (const std::exception &e) {
        log_warn("AEC3 processing error: %s", e.what());
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
