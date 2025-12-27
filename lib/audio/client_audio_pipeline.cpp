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

      .flags = CLIENT_AUDIO_PIPELINE_FLAGS_ALL,
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

  // Allocate lock-free render ring buffer for AEC3
  // This allows the output callback to feed render samples without blocking
  p->render_ring_buffer = SAFE_CALLOC(CLIENT_AUDIO_PIPELINE_RENDER_BUFFER_SIZE, sizeof(float), float *);
  if (!p->render_ring_buffer) {
    log_error("Failed to allocate render ring buffer");
    mutex_destroy(&p->aec3_mutex);
    SAFE_FREE(p);
    return NULL;
  }
  atomic_store(&p->render_ring_write_idx, 0);
  atomic_store(&p->render_ring_read_idx, 0);


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
      // Configure AEC3 for network audio with significant jitter buffer delay
      // Our system has ~200-300ms delay from jitter buffers, so we need a MUCH
      // longer filter than the default 13 blocks (~43ms).
      //
      // Block size at 48kHz = 480 samples = 10ms
      // 300ms delay = 30 blocks minimum, use 50 blocks for safety margin
      webrtc::EchoCanceller3Config aec3_config;

      // CRITICAL: Increase filter length to handle network delay
      // Default is 13 blocks (43ms) which is FAR too short for our 200-300ms delay.
      // 50 blocks = 500ms, giving us headroom for jitter.
      aec3_config.filter.main.length_blocks = 50;          // 500ms (default: 13 = 43ms)
      aec3_config.filter.shadow.length_blocks = 50;        // Match main filter
      aec3_config.filter.main_initial.length_blocks = 50;  // Same length from the start
      aec3_config.filter.shadow_initial.length_blocks = 50;

      // Increase delay estimation starting point for network audio
      // Default is 5 blocks (50ms), but we expect ~200ms delay from jitter buffer
      // Use 15 blocks (150ms) as a more conservative starting point
      aec3_config.delay.default_delay = 15;  // Start at 150ms (more conservative)

      // More conservative delay estimation for stability
      aec3_config.delay.delay_estimate_smoothing = 0.7f;             // Default 0.7 - standard smoothing
      aec3_config.delay.delay_candidate_detection_threshold = 0.2f;  // Default 0.2 - standard sensitivity

      // Allow longer delay searches for network audio
      aec3_config.delay.hysteresis_limit_blocks = 3;  // Default 1 - more stable delay
      aec3_config.delay.fixed_capture_delay_samples = 0;  // No fixed delay - let AEC3 detect

      // Standard filter adaptation (conservative for stability)
      aec3_config.filter.main.leakage_converged = 0.00005f;  // Default 0.00005f - standard
      aec3_config.filter.main.leakage_diverged = 0.05f;      // Default 0.05f - standard
      aec3_config.filter.shadow.rate = 0.7f;                 // Default 0.7f - standard

      // Extended learning period for network audio (more time to converge)
      aec3_config.filter.initial_state_seconds = 5.0f;  // Default 2.5s, give more time but not excessive

      // Moderate ERLE limits (close to defaults)
      aec3_config.erle.max_l = 4.5f;   // Default 4.0 - slight increase for network stability
      aec3_config.erle.max_h = 1.8f;   // Default 1.5 - slight increase for network stability

      // Enable anti-howling protection (critical for feedback prevention)
      aec3_config.suppressor.high_bands_suppression.anti_howling_activation_threshold = 1.f;   // Default 25 - trigger early
      aec3_config.suppressor.high_bands_suppression.anti_howling_gain = 0.001f;  // Default 0.01 - prevent howling without being excessive

      // Keep default echo suppression gain (no boost to avoid feedback loops)
      aec3_config.ep_strength.default_gain = 1.0f;  // Default 1.0f - no boost to prevent feedback

      // Balanced masking thresholds (close to defaults)
      // enr_transparent: below this, no suppression (lower = more aggressive)
      // enr_suppress: above this, full suppression (lower = more aggressive)
      aec3_config.suppressor.normal_tuning.mask_lf.enr_transparent = 0.25f;  // Default 0.3 - very slight adjustment
      aec3_config.suppressor.normal_tuning.mask_lf.enr_suppress = 0.35f;     // Default 0.4 - very slight adjustment
      aec3_config.suppressor.normal_tuning.mask_hf.enr_transparent = 0.065f; // Default 0.07 - minimal adjustment
      aec3_config.suppressor.normal_tuning.mask_hf.enr_suppress = 0.095f;    // Default 0.1 - minimal adjustment

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

        log_info("✓ WebRTC AEC3 initialized with conservative network config");
        log_info("  - Filter length: 50 blocks (500ms) for network jitter");
        log_info("  - Initial delay: 150ms (conservative starting point)");
        log_info("  - Learning period: 5 seconds (extended for convergence)");
        log_info("  - ERLE limits: max_l=4.5, max_h=1.8 (conservative)");
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

    p->capture_timestamp_us = 0;
    p->render_timestamp_us = 0;
    log_info("✓ AEC3 echo cancellation enabled - call analyze_render() with received audio");
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

  // Clean up lock-free render ring buffer
  if (pipeline->render_ring_buffer) {
    SAFE_FREE(pipeline->render_ring_buffer);
    pipeline->render_ring_buffer = NULL;
  }

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
  // Process render and capture in INTERLEAVED lock-step as per demo.cc pattern:
  // For each capture frame, process one render frame first, then capture.
  if (pipeline->flags.echo_cancel && pipeline->echo_canceller) {
    auto wrapper = static_cast<WebRTCAec3Wrapper*>(pipeline->echo_canceller);
    if (wrapper && wrapper->aec3) {
      mutex_lock(&pipeline->aec3_mutex);

      const int webrtc_frame_size = 480;  // 10ms at 48kHz
      const int buffer_size = CLIENT_AUDIO_PIPELINE_RENDER_BUFFER_SIZE;

      try {
        // Process capture in 480-sample chunks, with interleaved render processing
        for (int i = 0; i < num_samples; i += webrtc_frame_size) {
          int chunk_size = (i + webrtc_frame_size <= num_samples) ? webrtc_frame_size : (num_samples - i);

          // ---- STEP 1: Drain ALL available render frames ----
          // Process all render frames to keep buffer from overflowing
          // This is critical: render frames must be processed to maintain sync with capture
          if (pipeline->render_ring_buffer) {
            static int render_count = 0;
            static float render_rms_sum = 0.0f;
            static int render_rms_count = 0;

            int frames_processed = 0;
            const int max_frames_per_chunk = 8;  // Limit to prevent blocking too long

            while (frames_processed < max_frames_per_chunk) {
              int write_idx = atomic_load(&pipeline->render_ring_write_idx);
              int read_idx = atomic_load(&pipeline->render_ring_read_idx);
              int available = (write_idx - read_idx + buffer_size) % buffer_size;

              if (available < webrtc_frame_size) break;  // Not enough data

              // Read 480 samples from ring buffer
              float render_chunk[480];
              for (int j = 0; j < webrtc_frame_size; j++) {
                int idx = (read_idx + j) % buffer_size;
                render_chunk[j] = pipeline->render_ring_buffer[idx];
              }

              // Feed to AEC3 AnalyzeRender
              // CRITICAL: WebRTC expects float samples in int16 range [-32768, 32767]
              // PortAudio provides float samples in range [-1.0, 1.0]
              webrtc::AudioBuffer render_buf(48000, 1, 48000, 1, 48000, 1);
              float* const* render_channels = render_buf.channels();
              if (render_channels && render_channels[0]) {
                for (int j = 0; j < webrtc_frame_size; j++) {
                  render_channels[0][j] = render_chunk[j] * 32768.0f;
                }
                render_buf.SplitIntoFrequencyBands();
                wrapper->aec3->AnalyzeRender(&render_buf);
                render_buf.MergeFrequencyBands();
              }

              // Update read index
              atomic_store(&pipeline->render_ring_read_idx, (read_idx + webrtc_frame_size) % buffer_size);
              frames_processed++;
              render_count++;

              // Accumulate RMS for logging
              float rms = 0.0f;
              for (int s = 0; s < webrtc_frame_size; s++) rms += render_chunk[s] * render_chunk[s];
              rms = sqrtf(rms / webrtc_frame_size);
              render_rms_sum += rms;
              render_rms_count++;

              if (render_count % 200 == 1) {
                float avg_rms = render_rms_count > 0 ? render_rms_sum / render_rms_count : 0.0f;
                log_info("AEC3: Render frame #%d, avg_RMS=%.4f, frames_processed=%d",
                         render_count, avg_rms, frames_processed);
                render_rms_sum = 0.0f;
                render_rms_count = 0;
              }
            }
          }

          // ---- STEP 2: Process capture frame ----
          // CRITICAL: WebRTC expects float samples in int16 range [-32768, 32767]
          // PortAudio provides float samples in range [-1.0, 1.0]
          // Scale up before processing, scale back down after
          webrtc::AudioBuffer capture_buf(48000, 1, 48000, 1, 48000, 1);
          float* const* capture_channels = capture_buf.channels();
          if (capture_channels && capture_channels[0]) {
            // Calculate input RMS BEFORE AEC3 processing
            static float last_input_rms = 0.0f;
            float input_energy = 0.0f;
            for (int j = 0; j < chunk_size; j++) {
              input_energy += (processed + i)[j] * (processed + i)[j];
            }
            last_input_rms = sqrtf(input_energy / chunk_size);

            // Scale up microphone input to int16 range for WebRTC
            for (int j = 0; j < chunk_size; j++) {
              capture_channels[0][j] = (processed + i)[j] * 32768.0f;
            }

            // AnalyzeCapture on NON-split buffer (per demo.cc line 137)
            wrapper->aec3->AnalyzeCapture(&capture_buf);

            // Split into frequency bands for ProcessCapture (per demo.cc line 138)
            capture_buf.SplitIntoFrequencyBands();

            // NOTE: Do NOT call SetAudioBufferDelay(0) here!
            // demo.cc uses external delay estimation with pre-recorded synchronized WAV files.
            // Our real-time system has variable network/jitter buffer delay (~200-300ms).
            // Let AEC3 use its internal delay estimation instead.

            // ProcessCapture on SPLIT buffer (per demo.cc line 141)
            wrapper->aec3->ProcessCapture(&capture_buf, false);

            // Merge frequency bands back to time domain
            capture_buf.MergeFrequencyBands();

            // Scale back down to PortAudio float range [-1.0, 1.0]
            // Apply feedback prevention: if output > input, reduce gain
            static float feedback_gain = 1.0f;
            float out_energy = 0.0f;
            for (int j = 0; j < chunk_size; j++) {
              float sample = capture_channels[0][j] / 32768.0f;
              out_energy += sample * sample;
            }
            float out_rms_check = sqrtf(out_energy / chunk_size);

            // Feedback detection: if output > input, reduce gain (but not too aggressively)
            // Only reduce gain if output is significantly higher AND input is audible
            if (out_rms_check > last_input_rms * 1.5f && last_input_rms > 0.02f) {
              feedback_gain *= 0.95f;  // Reduce gain by 5% (was 20% - too aggressive)
              if (feedback_gain < 0.5f) feedback_gain = 0.5f;  // Minimum 50% (was 10%)
            } else if (feedback_gain < 1.0f) {
              feedback_gain *= 1.05f;  // Recover faster (was 1.02)
              if (feedback_gain > 1.0f) feedback_gain = 1.0f;
            }

            // Apply gain (with headroom to prevent clipping) and soft clip
            for (int j = 0; j < chunk_size; j++) {
              // Apply 0.85 headroom gain plus feedback_gain to prevent clipping
              float sample = (capture_channels[0][j] / 32768.0f) * feedback_gain * 0.85f;
              // Soft clip at 0.95 to preserve dynamics (was hard limit at 0.4!)
              if (sample > 0.95f) sample = 0.95f + 0.05f * tanhf((sample - 0.95f) * 10.0f);
              else if (sample < -0.95f) sample = -0.95f + 0.05f * tanhf((sample + 0.95f) * 10.0f);
              (processed + i)[j] = sample;
            }

            // Log capture signal stats and metrics every second
            static int capture_count = 0;
            capture_count++;
            if (capture_count % 100 == 1) {
              // Calculate RMS AFTER AEC3 processing
              float out_energy = 0.0f;
              for (int s = 0; s < chunk_size; s++) {
                out_energy += (processed + i)[s] * (processed + i)[s];
              }
              float out_rms = sqrtf(out_energy / chunk_size);

              // Calculate actual reduction
              float reduction_db = 0.0f;
              if (last_input_rms > 0.0001f && out_rms > 0.0001f) {
                reduction_db = 20.0f * log10f(out_rms / last_input_rms);
              }

              try {
                webrtc::EchoControl::Metrics metrics = wrapper->aec3->GetMetrics();
                log_info("AEC3: IN=%.4f OUT=%.4f (%.1fdB) ERL=%.1f ERLE=%.1f delay=%dms",
                         last_input_rms, out_rms, reduction_db,
                         metrics.echo_return_loss, metrics.echo_return_loss_enhancement,
                         metrics.delay_ms);
                audio_analysis_set_aec3_metrics(metrics.echo_return_loss,
                                                metrics.echo_return_loss_enhancement,
                                                metrics.delay_ms);
              } catch (...) {}
            }
          }
        }
      } catch (const std::exception &e) {
        log_warn("AEC3 processing error: %s", e.what());
      }

      mutex_unlock(&pipeline->aec3_mutex);
    }
  }

  // Debug: Write output after AEC3 processing
  if (pipeline->debug_wav_aec3_out) {
    wav_writer_write((wav_writer_t *)pipeline->debug_wav_aec3_out, processed, num_samples);
  }

  // Encode with Opus
  int opus_len = opus_encode_float(pipeline->encoder, processed, num_samples, opus_out, max_opus_len);

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
 * Feed render signal to lock-free ring buffer for AEC3
 *
 * Called from PortAudio's real-time output callback when audio goes to speakers.
 * This is the "render" signal - what will be played to speakers.
 *
 * CRITICAL: This function is LOCK-FREE. It NEVER blocks.
 * - Uses atomic operations to write to a ring buffer
 * - The capture thread drains this buffer and feeds samples to AEC3
 * - If buffer is full, drops oldest samples (overflow)
 *
 * This design prevents priority inversion where the audio callback
 * would block on a mutex held by the capture thread.
 */
void client_audio_pipeline_analyze_render(client_audio_pipeline_t *pipeline, const float *samples,
                                          int num_samples) {
  if (!pipeline || !samples || num_samples <= 0) return;
  if (!pipeline->flags.echo_cancel || !pipeline->echo_canceller) return;
  if (!pipeline->render_ring_buffer) return;

  // LOCK-FREE: Write samples to ring buffer using atomic operations
  // The capture thread will drain this buffer before processing

  int write_idx = atomic_load(&pipeline->render_ring_write_idx);
  int read_idx = atomic_load(&pipeline->render_ring_read_idx);

  // Calculate available space (ring buffer)
  int buffer_size = CLIENT_AUDIO_PIPELINE_RENDER_BUFFER_SIZE;
  int used = (write_idx - read_idx + buffer_size) % buffer_size;
  int available = buffer_size - used - 1;  // -1 to distinguish full from empty

  if (num_samples > available) {
    // Buffer overflow - drop oldest samples by advancing read pointer
    static int overflow_count = 0;
    overflow_count++;
    if (overflow_count % 100 == 1) {
      log_debug("AEC3 render buffer overflow #%d: dropping %d samples",
                overflow_count, num_samples - available);
    }
    // Advance read pointer to make room (drop oldest samples)
    int to_drop = num_samples - available;
    atomic_store(&pipeline->render_ring_read_idx,
                 (read_idx + to_drop) % buffer_size);
  }

  // Write samples to ring buffer
  for (int i = 0; i < num_samples; i++) {
    int idx = (write_idx + i) % buffer_size;
    pipeline->render_ring_buffer[idx] = samples[i];
  }

  // Update write index atomically
  atomic_store(&pipeline->render_ring_write_idx,
               (write_idx + num_samples) % buffer_size);

  // Log render signal stats every second
  static int render_count = 0;
  render_count++;
  if (render_count % 100 == 1) {
    float render_energy = 0.0f;
    int sample_count = num_samples < 100 ? num_samples : 100;
    for (int s = 0; s < sample_count; s++) {
      render_energy += samples[s] * samples[s];
    }
    render_energy = sqrtf(render_energy / sample_count);
    log_info("AEC3 Render queued: %d samples, RMS=%.6f, buffer_used=%d/%d",
             num_samples, render_energy, used + num_samples, buffer_size);
  }
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
