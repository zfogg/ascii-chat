/**
 * @file client_pipeline.cpp
 * @brief Unified client-side audio processing pipeline with WebRTC AEC3
 *
 * Implements production-grade echo cancellation using WebRTC AEC3
 * (Acoustic Echo Cancellation v3) with automatic network delay estimation,
 * adaptive filtering, and residual echo suppression.
 *
 * Uses WebRTC directly via C++ API - no wrapper layer.
 */

// C++ headers must come FIRST before any C headers that include stdatomic.h
#include <algorithm>
#include <memory>
#include <cstring>
#include <cmath>
#include <atomic>

// WebRTC headers for AEC3 MUST come before ascii-chat headers to avoid macro conflicts
// Define required WebRTC macros before including headers
#define WEBRTC_APM_DEBUG_DUMP 0
#define WEBRTC_MODULE_AUDIO_PROCESSING 1
// WEBRTC_POSIX should only be defined on POSIX systems (Unix/macOS), not Windows
#if defined(__unix__) || defined(__APPLE__)
#define WEBRTC_POSIX 1
#endif

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
#include <ascii-chat/audio/client_pipeline.h>
#include <ascii-chat/audio/spectral.h>
#include <ascii-chat/audio/opus.h>
extern "C" {
#include <ascii-chat/options/options.h>
}
#include <ascii-chat/audio/wav_writer.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/util/time.h>

// Include mixer.h for compressor, noise gate, and filter functions
#include <ascii-chat/audio/mixer.h>

// For AEC3 metrics reporting
#include <ascii-chat/audio/analysis.h>

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

// Global tracking of max render RMS for AEC3 diagnostics (accessible from both threads)
static std::atomic<float> g_max_render_rms{0.0f};

// Global counter for render frames fed to AEC3 (for warmup tracking)
static std::atomic<int> g_render_frames_fed{0};

#ifdef __cplusplus
extern "C" {
#endif

#include <ascii-chat/debug/named.h>

// ============================================================================
// Default Configuration
// ============================================================================

client_audio_pipeline_config_t client_audio_pipeline_default_config(void) {
  return (client_audio_pipeline_config_t){
      .sample_rate = CLIENT_AUDIO_PIPELINE_SAMPLE_RATE,
      .frame_duration_ms = CLIENT_AUDIO_PIPELINE_FRAME_MS,
      .opus_bitrate = 32000,
      .opus_application = OPUS_APPLICATION_VOIP,

      .echo_filter_ms = 250,

      .processor = audio_processor_default_config(CLIENT_AUDIO_PIPELINE_SAMPLE_RATE),
      .jitter_margin_ms = 60,

      .flags = CLIENT_AUDIO_PIPELINE_FLAGS_ALL,
  };
}

client_audio_pipeline_config_t client_audio_pipeline_config_for_source(bool has_media_audio) {
  client_audio_pipeline_config_t config = client_audio_pipeline_default_config();
  const options_t *options = options_get();
  if (options) {
    config.processor.spectral_enabled = options->audio_spectral && !options->test_audio;
    config.processor.spectral.fft_size = options->audio_fft_size;
    config.processor.spectral.noise_gate = options->audio_spectral_gate;
    config.processor.spectral.compressor = options->audio_multiband;
    config.processor.spectral.adaptive_eq = options->audio_adaptive_eq;
    config.processor.spectral.pitch_correct = options->audio_pitch_correct;
    config.processor.spectral.auto_gain = options->audio_spectral_agc;
    bool media = options->test_audio || (has_media_audio && options->audio_capture_source != AUDIO_CAPTURE_SOURCE_MIC &&
                                         options->audio_capture_source != AUDIO_CAPTURE_SOURCE_REMOTE);
    config.opus_application = media ? OPUS_APPLICATION_AUDIO : OPUS_APPLICATION_VOIP;
    config.opus_bitrate = media ? 128000 : 32000;
  }
  return config;
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
  p->frame_size = p->config.sample_rate * p->config.frame_duration_ms / 1000;

  // No mutex needed - full-duplex means single callback thread handles all AEC3

  // Initialize Opus encoder/decoder first (no exceptions)
  int opus_error = 0;
  p->encoder = opus_encoder_create(p->config.sample_rate, 1, p->config.opus_application, &opus_error);
  if (!p->encoder || opus_error != OPUS_OK) {
    log_error("Failed to create Opus encoder: %d", opus_error);
    goto error;
  }
  if (audio_opus_configure_encoder(p->encoder, p->config.opus_bitrate) != ASCIICHAT_OK)
    goto error;

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
    // Configure AEC3 for better low-frequency (bass) echo cancellation
    webrtc::EchoCanceller3Config aec3_config;

    // Increase filter length for bass frequencies (default 13 blocks = ~17ms)
    // Bass at 80Hz has 12.5ms period, so we need at least 50+ blocks (~67ms)
    // to properly model the echo path for low frequencies
    aec3_config.filter.main.length_blocks = 50;           // ~67ms (was 13)
    aec3_config.filter.shadow.length_blocks = 50;         // ~67ms (was 13)
    aec3_config.filter.main_initial.length_blocks = 25;   // ~33ms (was 12)
    aec3_config.filter.shadow_initial.length_blocks = 25; // ~33ms (was 12)

    // More aggressive low-frequency suppression thresholds
    // Lower values = more aggressive echo suppression
    aec3_config.echo_audibility.audibility_threshold_lf = 5; // (was 10)

    // Create AEC3 using the factory
    auto factory = webrtc::EchoCanceller3Factory(aec3_config);

    std::unique_ptr<webrtc::EchoControl> echo_control = factory.Create(static_cast<int>(p->config.sample_rate), // 48kHz
                                                                       1, // num_render_channels (speaker output)
                                                                       1  // num_capture_channels (microphone input)
    );

    if (!echo_control) {
      log_warn("Failed to create WebRTC AEC3 instance - echo cancellation unavailable");
      p->echo_canceller = NULL;
    } else {
      // Successfully created AEC3 - wrap in our C++ wrapper for C compatibility
      auto wrapper = new WebRTCAec3Wrapper();
      wrapper->aec3 = std::move(echo_control);
      wrapper->config = aec3_config;
      p->echo_canceller = wrapper;

      log_info("✓ WebRTC AEC3 initialized (67ms filter for bass, adaptive delay)");

      // Create persistent AudioBuffer instances for AEC3
      p->aec3_render_buffer = new webrtc::AudioBuffer(48000, 1, 48000, 1, 48000, 1);
      p->aec3_capture_buffer = new webrtc::AudioBuffer(48000, 1, 48000, 1, 48000, 1);

      auto *render_buf = static_cast<webrtc::AudioBuffer *>(p->aec3_render_buffer);
      auto *capture_buf = static_cast<webrtc::AudioBuffer *>(p->aec3_capture_buffer);

      // Zero-initialize channel data
      float *const *render_ch = render_buf->channels();
      float *const *capture_ch = capture_buf->channels();
      if (render_ch && render_ch[0]) {
        memset(render_ch[0], 0, 480 * sizeof(float)); // 10ms at 48kHz
      }
      if (capture_ch && capture_ch[0]) {
        memset(capture_ch[0], 0, 480 * sizeof(float));
      }

      // Prime filterbank state with dummy processing cycle
      render_buf->SplitIntoFrequencyBands();
      render_buf->MergeFrequencyBands();
      capture_buf->SplitIntoFrequencyBands();
      capture_buf->MergeFrequencyBands();

      log_debug("  - AudioBuffer filterbank state initialized");

      // Warm up AEC3 with 10 silent frames to initialize internal state
      for (int warmup = 0; warmup < 10; warmup++) {
        memset(render_ch[0], 0, 480 * sizeof(float));
        memset(capture_ch[0], 0, 480 * sizeof(float));

        render_buf->SplitIntoFrequencyBands();
        wrapper->aec3->AnalyzeRender(render_buf);
        render_buf->MergeFrequencyBands();

        wrapper->aec3->AnalyzeCapture(capture_buf);
        capture_buf->SplitIntoFrequencyBands();
        wrapper->aec3->SetAudioBufferDelay(0);
        wrapper->aec3->ProcessCapture(capture_buf, false);
        capture_buf->MergeFrequencyBands();
      }
      log_debug("  - AEC3 warmed up with 10 silent frames");
      log_debug("  - Persistent AudioBuffer instances created");
    }
  }

  // Initialize debug WAV writers for AEC3 analysis (if echo_cancel enabled)
  p->debug_wav_aec3_in = NULL;
  p->debug_wav_aec3_out = NULL;
  if (p->flags.echo_cancel && wav_dump_enabled()) {
    // Record microphone diagnostics only when WAV dumping was requested.
    char tmp[PLATFORM_MAX_PATH_LENGTH];
    if (platform_get_temp_dir(tmp, sizeof(tmp))) {
      char path[PLATFORM_MAX_PATH_LENGTH];
      safe_snprintf(path, sizeof(path), "%s/aec3_input.wav", tmp);
      p->debug_wav_aec3_in = wav_writer_open(path, 48000, 1);
      safe_snprintf(path, sizeof(path), "%s/aec3_output.wav", tmp);
      p->debug_wav_aec3_out = wav_writer_open(path, 48000, 1);
    }
    if (p->debug_wav_aec3_in) {
      log_info("Debug: Recording AEC3 input WAV to temp dir");
    }
    if (p->debug_wav_aec3_out) {
      log_info("Debug: Recording AEC3 output WAV to temp dir");
    }

    log_info("✓ AEC3 echo cancellation enabled (full-duplex mode, no ring buffer delay)");
  }

  p->config.processor.sample_rate = p->config.sample_rate;
  p->config.processor.spectral.sample_rate = p->config.sample_rate;
  p->config.processor.flags = {p->flags.agc, p->flags.compressor, p->flags.noise_gate, p->flags.highpass,
                               p->flags.lowpass};
  if (audio_processor_create(&p->config.processor, &p->processor) != ASCIICHAT_OK)
    goto error;
  p->initialized = true;

  // Initialize startup fade-in to prevent initial microphone click
  // 200ms at 48kHz = 9600 samples - gradual ramp from silence to full volume
  // Longer fade-in (200ms vs 50ms) gives much smoother transition without audible pop
  p->capture_fadein_remaining = (p->config.sample_rate * 200) / 1000; // 200ms worth of samples
  log_debug("✓ Capture fade-in: %d samples (200ms)", p->capture_fadein_remaining);

  log_info("Audio pipeline created: %dHz, %llums frames, %dkbps Opus", p->config.sample_rate,
           (unsigned long long)p->config.frame_duration_ms, p->config.opus_bitrate / 1000);

  (void)NAMED_REGISTER_CLIENT_AUDIO_PIPELINE(p, "audio_pipeline", NULL);

  return p;

error:
  client_audio_pipeline_destroy(p);
  return NULL;
}

void client_audio_pipeline_destroy(client_audio_pipeline_t *pipeline) {
  if (!pipeline)
    return;

  audio_processor_destroy(pipeline->processor);
  pipeline->processor = NULL;

  // Clean up WebRTC AEC3 AudioBuffer instances
  if (pipeline->aec3_render_buffer) {
    delete static_cast<webrtc::AudioBuffer *>(pipeline->aec3_render_buffer);
    pipeline->aec3_render_buffer = NULL;
  }
  if (pipeline->aec3_capture_buffer) {
    delete static_cast<webrtc::AudioBuffer *>(pipeline->aec3_capture_buffer);
    pipeline->aec3_capture_buffer = NULL;
  }

  // Clean up WebRTC AEC3
  if (pipeline->echo_canceller) {
    delete static_cast<WebRTCAec3Wrapper *>(pipeline->echo_canceller);
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
  }
  if (pipeline->debug_wav_aec3_out) {
    wav_writer_close((wav_writer_t *)pipeline->debug_wav_aec3_out);
    pipeline->debug_wav_aec3_out = NULL;
  }

  NAMED_UNREGISTER(pipeline);

  SAFE_FREE(pipeline);
}

// ============================================================================
// Configuration Functions
// ============================================================================

void client_audio_pipeline_set_flags(client_audio_pipeline_t *pipeline, client_audio_pipeline_flags_t flags) {
  if (!pipeline)
    return;
  // No mutex needed - flags are only read by capture thread
  pipeline->flags = flags;
  audio_processor_set_flags(pipeline->processor,
                            {flags.agc, flags.compressor, flags.noise_gate, flags.highpass, flags.lowpass});
}

client_audio_pipeline_flags_t client_audio_pipeline_get_flags(client_audio_pipeline_t *pipeline) {
  if (!pipeline)
    return CLIENT_AUDIO_PIPELINE_FLAGS_MINIMAL;
  // No mutex needed - flags are only written from main thread during setup
  return pipeline->flags;
}

// ============================================================================
// Audio Processing Functions
// ============================================================================

/**
 * Encode already-processed audio to Opus.
 *
 * In full-duplex mode, AEC3 and DSP processing are done in process_duplex().
 * This function just does Opus encoding.
 */
int client_audio_pipeline_capture(client_audio_pipeline_t *pipeline, const float *input, int num_samples,
                                  uint8_t *opus_out, int max_opus_len) {
  if (!pipeline || !input || !opus_out || num_samples != pipeline->frame_size) {
    return -1;
  }

  // Input is already processed by process_duplex() in full-duplex mode.
  // Just encode with Opus.
  int opus_len = opus_encode_float(pipeline->encoder, input, num_samples, opus_out, max_opus_len);

  if (opus_len < 0) {
    log_error_every(US_PER_SEC_INT, "Opus encoding failed: %d", opus_len);
    return -1;
  }

  return opus_len;
}

/**
 * Process network playback (decode and register with echo canceller as reference)
 */
int client_audio_pipeline_playback(client_audio_pipeline_t *pipeline, const uint8_t *opus_in, int opus_len,
                                   float *output, int num_samples) {
  if (!pipeline || !output || num_samples <= 0 || opus_len < 0 || (!opus_in && opus_len != 0)) {
    return -1;
  }

  // No mutex needed - Opus decoder is only used from this thread

  // Decode Opus
  int decoded_samples = opus_decode_float(pipeline->decoder, opus_in, opus_len, output, num_samples, 0);

  if (decoded_samples < 0) {
    log_error_every(US_PER_SEC_INT, "Opus decoding failed: %d", decoded_samples);
    return -1;
  }

  // Received streams can contain quiet media; microphone gating belongs at capture.

  // NOTE: Render signal is queued to AEC3 in output_callback() when audio plays,
  // not here. The capture thread drains the queue and processes AEC3.

  return decoded_samples;
}

/**
 * Get a processed playback frame (currently just returns decoded frame)
 */
int client_audio_pipeline_get_playback_frame(client_audio_pipeline_t *pipeline, float *output, int num_samples) {
  if (!pipeline || !output) {
    return -1;
  }

  // No mutex needed - this is a placeholder
  memset(output, 0, num_samples * sizeof(float));
  return num_samples;
}

/**
 * Process AEC3 inline in full-duplex callback (REAL-TIME SAFE).
 *
 * This is the PROFESSIONAL approach to AEC3 timing:
 * - Called from a single PortAudio full-duplex callback
 * - render_samples = what is being played to speakers RIGHT NOW
 * - capture_samples = what microphone captured RIGHT NOW
 * - Perfect synchronization - no timing mismatch possible
 *
 * This function does ALL AEC3 processing inline:
 * 1. AnalyzeRender on render samples (speaker output)
 * 2. AnalyzeCapture + ProcessCapture on capture samples
 * 3. Apply filters, noise gate, compressor
 *
 * Returns processed capture samples in processed_output.
 * Opus encoding is done separately by the encoding thread.
 */
void client_audio_pipeline_process_duplex(client_audio_pipeline_t *pipeline, const float *render_samples,
                                          int render_count, const float *capture_samples, int capture_count,
                                          float *processed_output) {
  if (!pipeline || !processed_output)
    return;

  // Copy capture samples to output buffer for processing
  if (capture_samples && capture_count > 0) {
    memcpy(processed_output, capture_samples, capture_count * sizeof(float));
  } else {
    memset(processed_output, 0, capture_count * sizeof(float));
    return;
  }

  // Check for AEC3 bypass
  static int bypass_aec3 = -1;
  if (bypass_aec3 == -1) {
    const char *env = platform_getenv("BYPASS_AEC3");
    bypass_aec3 = (env && (strcmp(env, "1") == 0 || strcmp(env, "true") == 0)) ? 1 : 0;
    if (bypass_aec3) {
      log_warn("AEC3 BYPASSED (full-duplex mode) via BYPASS_AEC3=1");
    }
  }

  // Debug WAV recording
  if (pipeline->debug_wav_aec3_in) {
    wav_writer_write((wav_writer_t *)pipeline->debug_wav_aec3_in, capture_samples, capture_count);
  }

  // Apply startup fade-in using smoothstep curve
  if (pipeline->capture_fadein_remaining > 0) {
    const int total_fadein_samples = (pipeline->config.sample_rate * 200) / 1000;
    for (int i = 0; i < capture_count && pipeline->capture_fadein_remaining > 0; i++) {
      float progress = 1.0f - ((float)pipeline->capture_fadein_remaining / (float)total_fadein_samples);
      float gain = smoothstep(progress);
      processed_output[i] *= gain;
      pipeline->capture_fadein_remaining--;
    }
  }

  // WebRTC AEC3 processing - INLINE, no ring buffer, no mutex
  if (!bypass_aec3 && pipeline->flags.echo_cancel && pipeline->echo_canceller) {
    auto wrapper = static_cast<WebRTCAec3Wrapper *>(pipeline->echo_canceller);
    if (wrapper && wrapper->aec3) {
      const int webrtc_frame_size = 480; // 10ms at 48kHz

      auto *render_buf = static_cast<webrtc::AudioBuffer *>(pipeline->aec3_render_buffer);
      auto *capture_buf = static_cast<webrtc::AudioBuffer *>(pipeline->aec3_capture_buffer);

      if (render_buf && capture_buf) {
        float *const *render_channels = render_buf->channels();
        float *const *capture_channels = capture_buf->channels();

        if (render_channels && render_channels[0] && capture_channels && capture_channels[0]) {
          // Verify render_samples is valid before accessing
          if (!render_samples && render_count > 0) {
            log_warn_every(1000000, "AEC3: render_samples is NULL but render_count=%d", render_count);
            return;
          }

          // Process in 10ms chunks (AEC3 requirement)
          int render_offset = 0;
          int capture_offset = 0;

          // Process only complete 480-sample frames. The worker thread guarantees
          // render_count == capture_count and both are multiples of 480, so no
          // partial frames should occur. If they do (defensive), the tail samples
          // pass through unprocessed rather than corrupting AEC3 with padded data.
          while (render_offset + webrtc_frame_size <= render_count &&
                 capture_offset + webrtc_frame_size <= capture_count) {

            // STEP 1: Feed render signal (what's playing to speakers)
            copy_buffer_with_gain(&render_samples[render_offset], render_channels[0], webrtc_frame_size, 32768.0f);
            render_buf->SplitIntoFrequencyBands();
            wrapper->aec3->AnalyzeRender(render_buf);
            render_buf->MergeFrequencyBands();
            g_render_frames_fed.fetch_add(1, std::memory_order_relaxed);
            render_offset += webrtc_frame_size;

            // STEP 2: Process capture (microphone input)
            copy_buffer_with_gain(&processed_output[capture_offset], capture_channels[0], webrtc_frame_size, 32768.0f);
            wrapper->aec3->AnalyzeCapture(capture_buf);
            capture_buf->SplitIntoFrequencyBands();
            wrapper->aec3->ProcessCapture(capture_buf, false);
            capture_buf->MergeFrequencyBands();

            // Scale back to float range and soft clip
            for (int j = 0; j < webrtc_frame_size; j++) {
              float sample = capture_channels[0][j] / 32768.0f;
              processed_output[capture_offset + j] = soft_clip(sample, 0.6f, 2.5f);
            }

            // Log AEC3 metrics periodically
            static int duplex_log_count = 0;
            if (++duplex_log_count % 100 == 1) {
              webrtc::EchoControl::Metrics metrics = wrapper->aec3->GetMetrics();
              log_debug("AEC3 DUPLEX: ERL=%.1f ERLE=%.1f delay=%dms", metrics.echo_return_loss,
                        metrics.echo_return_loss_enhancement, metrics.delay_ms);
              audio_analysis_set_aec3_metrics(metrics.echo_return_loss, metrics.echo_return_loss_enhancement,
                                              metrics.delay_ms);
            }
            capture_offset += webrtc_frame_size;
          }
        }
      }
    }
  }

  // Debug WAV recording (after AEC3)
  if (pipeline->debug_wav_aec3_out) {
    wav_writer_write((wav_writer_t *)pipeline->debug_wav_aec3_out, processed_output, capture_count);
  }

  asciichat_error_t result =
      audio_processor_process(pipeline->processor, processed_output, processed_output, (size_t)capture_count);
  if (result != ASCIICHAT_OK)
    log_error("Audio enhancement failed: %d", result);
}

/**
 * Get jitter buffer margin
 */
int client_audio_pipeline_jitter_margin(client_audio_pipeline_t *pipeline) {
  if (!pipeline)
    return 0;
  return pipeline->config.jitter_margin_ms;
}

/**
 * Reset pipeline state
 */
void client_audio_pipeline_reset(client_audio_pipeline_t *pipeline) {
  if (!pipeline)
    return;

  audio_processor_reset(pipeline->processor);

  // Reset global counters
  g_render_frames_fed.store(0, std::memory_order_relaxed);
  g_max_render_rms.store(0.0f, std::memory_order_relaxed);

  log_info("Pipeline state reset");
}

#ifdef __cplusplus
}
#endif
