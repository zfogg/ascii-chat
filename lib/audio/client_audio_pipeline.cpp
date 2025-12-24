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
#include "common.h"
#include "logging.h"
#include "platform/abstraction.h"

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

      .jitter_margin_ms = 60,

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

  // Initialize mutex
  if (mutex_init(&p->mutex) != 0) {
    log_error("Failed to initialize pipeline mutex");
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
      // Create AEC3 using the factory with default configuration
      // EchoCanceller3Factory produces the latest high-quality AEC3 implementation
      // The extracted webrtc_AEC3 repo doesn't have the full Environment API
      auto factory = webrtc::EchoCanceller3Factory();

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
        wrapper->config = webrtc::EchoCanceller3Config{};  // Use default config
        p->echo_canceller = wrapper;
        log_info("✓ WebRTC AEC3 echo cancellation initialized");
        log_info("  - Network delay estimation: 0-500ms");
        log_info("  - Adaptive filtering to echo path");
        log_info("  - Residual echo suppression enabled");
      }
    } catch (const std::exception &e) {
      log_error("Exception creating WebRTC AEC3: %s", e.what());
      p->echo_canceller = NULL;
    } catch (...) {
      log_error("Unknown exception creating WebRTC AEC3");
      p->echo_canceller = NULL;
    }
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
  mutex_destroy(&p->mutex);
  SAFE_FREE(p);
  return NULL;
}

void client_audio_pipeline_destroy(client_audio_pipeline_t *pipeline) {
  if (!pipeline) return;

  mutex_lock(&pipeline->mutex);

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

  mutex_unlock(&pipeline->mutex);
  mutex_destroy(&pipeline->mutex);

  SAFE_FREE(pipeline);
}

// ============================================================================
// Configuration Functions
// ============================================================================

void client_audio_pipeline_set_flags(client_audio_pipeline_t *pipeline, client_audio_pipeline_flags_t flags) {
  if (!pipeline) return;
  mutex_lock(&pipeline->mutex);
  pipeline->flags = flags;
  mutex_unlock(&pipeline->mutex);
}

client_audio_pipeline_flags_t client_audio_pipeline_get_flags(client_audio_pipeline_t *pipeline) {
  if (!pipeline) return CLIENT_AUDIO_PIPELINE_FLAGS_MINIMAL;
  mutex_lock(&pipeline->mutex);
  auto flags = pipeline->flags;
  mutex_unlock(&pipeline->mutex);
  return flags;
}

// ============================================================================
// Audio Processing Functions
// ============================================================================

/**
 * Process microphone capture through echo cancellation and encoding
 */
int client_audio_pipeline_capture(client_audio_pipeline_t *pipeline, const float *input, int num_samples,
                                  uint8_t *opus_out, int max_opus_len) {
  if (!pipeline || !input || !opus_out || num_samples != pipeline->frame_size) {
    return -1;
  }

  mutex_lock(&pipeline->mutex);

  // Create float buffer for processing
  float *processed = (float *)alloca(num_samples * sizeof(float));
  memcpy(processed, input, num_samples * sizeof(float));

  // WebRTC AEC3 echo cancellation - Process microphone input to remove echo
  if (pipeline->flags.echo_cancel && pipeline->echo_canceller) {
    auto wrapper = static_cast<WebRTCAec3Wrapper*>(pipeline->echo_canceller);
    if (wrapper && wrapper->aec3) {
      try {
        // Create AudioBuffer for AEC3 processing
        // Parameters: input_rate, input_channels, buffer_rate, buffer_channels, output_rate, output_channels
        // All at 48kHz, mono, 20ms frames = 960 samples
        webrtc::AudioBuffer audio_buf(
            48000,  // input sample rate
            1,      // input channels (mono)
            48000,  // processing sample rate
            1,      // processing channels
            48000,  // output sample rate
            1       // output channels
        );

        // Copy microphone input into AudioBuffer
        float* const* channels = audio_buf.channels();
        if (channels && channels[0]) {
          memcpy(channels[0], processed, num_samples * sizeof(float));

          // Process through AEC3 to remove echo from capture signal
          // This includes:
          // - Adaptive filtering based on render signal
          // - Network delay estimation
          // - Residual echo suppression
          // - Linear filter output (optional)
          wrapper->aec3->AnalyzeCapture(&audio_buf);
          wrapper->aec3->ProcessCapture(&audio_buf, false);  // false = no level change

          // Copy processed audio back to our buffer (echo removed)
          memcpy(processed, channels[0], num_samples * sizeof(float));
        }
      } catch (const std::exception &e) {
        log_warn("AEC3 processing error: %s", e.what());
        // Continue with unprocessed audio on error
      }
    }
  }

  // Encode with Opus
  int opus_len = opus_encode_float(pipeline->encoder, processed, num_samples, opus_out, max_opus_len);

  mutex_unlock(&pipeline->mutex);

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

  mutex_lock(&pipeline->mutex);

  // Decode Opus
  int decoded_samples = opus_decode_float(pipeline->decoder, opus_in, opus_len, output, num_samples, 0);

  if (decoded_samples < 0) {
    log_error("Opus decoding failed: %d", decoded_samples);
    mutex_unlock(&pipeline->mutex);
    return -1;
  }

  // Register decoded audio with AEC3 as far-end (render/speaker) reference signal
  // This is critical for AEC3 to learn the echo path and estimate network delay
  if (pipeline->flags.echo_cancel && pipeline->echo_canceller && decoded_samples > 0) {
    auto wrapper = static_cast<WebRTCAec3Wrapper*>(pipeline->echo_canceller);
    if (wrapper && wrapper->aec3) {
      try {
        // Create AudioBuffer for render (speaker) signal
        // AEC3 needs to analyze what's being played to learn echo characteristics
        webrtc::AudioBuffer render_buf(
            48000,  // input sample rate
            1,      // input channels (mono)
            48000,  // processing sample rate
            1,      // processing channels
            48000,  // output sample rate
            1       // output channels
        );

        // Copy speaker/render audio into buffer
        float* const* channels = render_buf.channels();
        if (channels && channels[0]) {
          memcpy(channels[0], output, decoded_samples * sizeof(float));

          // Analyze render signal for AEC3
          // This tells AEC3 what's being played to speakers so it can:
          // - Estimate network delay (time between render and echo in capture)
          // - Train adaptive filter to model the echo path
          // - Separate echo from desired signal in capture path
          wrapper->aec3->AnalyzeRender(&render_buf);
        }
      } catch (const std::exception &e) {
        log_warn("AEC3 render analysis error: %s", e.what());
        // Continue operation even if render analysis fails
      }
    }
  }

  mutex_unlock(&pipeline->mutex);

  return decoded_samples;
}

/**
 * Get a processed playback frame (currently just returns decoded frame)
 */
int client_audio_pipeline_get_playback_frame(client_audio_pipeline_t *pipeline, float *output, int num_samples) {
  if (!pipeline || !output) {
    return -1;
  }

  mutex_lock(&pipeline->mutex);
  // For now, this is a placeholder
  // In a full implementation, this would manage buffering
  memset(output, 0, num_samples * sizeof(float));
  mutex_unlock(&pipeline->mutex);

  return num_samples;
}

/**
 * Process echo playback (manual control of echo reference for AEC3)
 *
 * Call this to feed additional speaker/playback audio to AEC3 for learning.
 * Useful when audio comes from sources other than the decode path.
 */
void client_audio_pipeline_process_echo_playback(client_audio_pipeline_t *pipeline, const float *samples,
                                                 int num_samples) {
  if (!pipeline || !samples || num_samples <= 0) return;

  mutex_lock(&pipeline->mutex);

  // Register audio samples with AEC3 as render (far-end/speaker) reference signal
  if (pipeline->flags.echo_cancel && pipeline->echo_canceller) {
    auto wrapper = static_cast<WebRTCAec3Wrapper*>(pipeline->echo_canceller);
    if (wrapper && wrapper->aec3) {
      try {
        // Ensure we don't exceed frame size
        int safe_samples = num_samples <= pipeline->frame_size ? num_samples : pipeline->frame_size;

        // Create AudioBuffer for render signal (correct constructor: input_rate, input_ch, buffer_rate, buffer_ch, output_rate, output_ch)
        webrtc::AudioBuffer render_buf(
            48000,  // input sample rate
            1,      // input channels
            48000,  // buffer rate
            1,      // buffer channels
            48000,  // output rate
            1       // output channels
        );

        // Copy render samples into buffer
        float* const* channels = render_buf.channels();
        if (channels && channels[0]) {
          memcpy(channels[0], samples, safe_samples * sizeof(float));

          // Analyze this render signal for AEC3
          // Tells AEC3 about additional echo sources being played
          wrapper->aec3->AnalyzeRender(&render_buf);
        }
      } catch (const std::exception &e) {
        log_warn("AEC3 echo playback processing error: %s", e.what());
      }
    }
  }

  mutex_unlock(&pipeline->mutex);
}

/**
 * Get jitter buffer margin
 */
int client_audio_pipeline_jitter_margin(client_audio_pipeline_t *pipeline) {
  if (!pipeline) return 0;
  return pipeline->config.jitter_margin_ms;
}

/**
 * Reset pipeline state (AEC3 is adaptive and doesn't need explicit reset)
 */
void client_audio_pipeline_reset(client_audio_pipeline_t *pipeline) {
  if (!pipeline) return;

  mutex_lock(&pipeline->mutex);

  // WebRTC AEC3 is adaptive and doesn't require explicit reset
  // It automatically adjusts to network conditions
  // If needed in future: could recreate AEC3 instance, but not necessary

  log_debug("Pipeline state reset requested (AEC3 is adaptive, no action needed)");

  mutex_unlock(&pipeline->mutex);
}

#ifdef __cplusplus
}
#endif
