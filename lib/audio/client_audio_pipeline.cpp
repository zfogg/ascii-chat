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
#include "audio/client_audio_pipeline.h"
#include "audio/wav_writer.h"
#include "common.h"
#include "log/logging.h"
#include "platform/abstraction.h"

// Include mixer.h for compressor, noise gate, and filter functions
#include "audio/mixer.h"

// For AEC3 metrics reporting
#include "audio/analysis.h"

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

      // Jitter margin: wait this long before starting playback
      // Lower = less latency but more risk of underruns
      // CRITICAL: Must match AUDIO_JITTER_BUFFER_THRESHOLD in ringbuffer.h!
      .jitter_margin_ms = 20, // 20ms = 1 Opus packet (optimized for LAN)

      // Higher cutoff to cut low-frequency rumble and feedback
      .highpass_hz = 150.0f, // Was 80Hz, increased to break rumble feedback loop
      .lowpass_hz = 8000.0f,

      // Compressor: only compress loud peaks, minimal makeup to avoid clipping
      // User reported clipping with +6dB makeup gain
      .comp_threshold_db = -6.0f, // Only compress peaks above -6dB
      .comp_ratio = 3.0f,         // Gentler 3:1 ratio
      .comp_attack_ms = 5.0f,     // Fast attack for peaks
      .comp_release_ms = 150.0f,  // Slower release
      .comp_makeup_db = 2.0f,     // Reduced from 6dB to prevent clipping

      // Noise gate: VERY aggressive to cut quiet background audio completely
      // User feedback: "don't amplify or play quiet background audio at all"
      .gate_threshold = 0.08f,  // -22dB threshold (was 0.02/-34dB) - cuts quiet audio hard
      .gate_attack_ms = 0.5f,   // Very fast attack
      .gate_release_ms = 30.0f, // Fast release (was 50ms)
      .gate_hysteresis = 0.3f,  // Tighter hysteresis = stays closed longer

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

  // No mutex needed - full-duplex means single callback thread handles all AEC3

  // Initialize Opus encoder/decoder first (no exceptions)
  int opus_error = 0;
  p->encoder = opus_encoder_create(p->config.sample_rate, 1, OPUS_APPLICATION_VOIP, &opus_error);
  if (!p->encoder || opus_error != OPUS_OK) {
    log_error("Failed to create Opus encoder: %d", opus_error);
    goto error;
  }
  opus_encoder_ctl(p->encoder, OPUS_SET_BITRATE(p->config.opus_bitrate));

  // CRITICAL: Disable DTX (Discontinuous Transmission) to prevent "beeps"
  // DTX stops sending frames during silence, causing audible clicks/beeps when audio resumes
  opus_encoder_ctl(p->encoder, OPUS_SET_DTX(0));

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

      log_info("  - AudioBuffer filterbank state initialized");

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
      log_info("  - AEC3 warmed up with 10 silent frames");
      log_info("  - Persistent AudioBuffer instances created");
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

    log_info("✓ AEC3 echo cancellation enabled (full-duplex mode, no ring buffer delay)");
  }

  // Initialize audio processing components (compressor, noise gate, filters)
  // These are applied in the capture path after AEC3 and before Opus encoding
  {
    float sample_rate = (float)p->config.sample_rate;

    // Initialize compressor with config values
    compressor_init(&p->compressor, sample_rate);
    compressor_set_params(&p->compressor, p->config.comp_threshold_db, p->config.comp_ratio, p->config.comp_attack_ms,
                          p->config.comp_release_ms, p->config.comp_makeup_db);
    log_info("✓ Capture compressor: threshold=%.1fdB, ratio=%.1f:1, makeup=+%.1fdB", p->config.comp_threshold_db,
             p->config.comp_ratio, p->config.comp_makeup_db);

    // Initialize noise gate with config values
    noise_gate_init(&p->noise_gate, sample_rate);
    noise_gate_set_params(&p->noise_gate, p->config.gate_threshold, p->config.gate_attack_ms, p->config.gate_release_ms,
                          p->config.gate_hysteresis);
    log_info("✓ Capture noise gate: threshold=%.4f (%.1fdB)", p->config.gate_threshold,
             20.0f * log10f(p->config.gate_threshold + 1e-10f));

    // Initialize PLAYBACK noise gate - cuts quiet received audio before speakers
    // Very low threshold - only cut actual silence, not quiet voice audio
    // The server sends audio with RMS=0.01-0.02, so threshold must be below that
    noise_gate_init(&p->playback_noise_gate, sample_rate);
    noise_gate_set_params(&p->playback_noise_gate,
                          0.002f, // -54dB threshold - only cut near-silence
                          1.0f,   // 1ms attack - fast open
                          50.0f,  // 50ms release - smooth close
                          0.4f);  // Hysteresis
    log_info("✓ Playback noise gate: threshold=0.002 (-54dB)");

    // Initialize highpass filter (removes low-frequency rumble)
    highpass_filter_init(&p->highpass, p->config.highpass_hz, sample_rate);
    log_info("✓ Capture highpass filter: %.1f Hz", p->config.highpass_hz);

    // Initialize lowpass filter (removes high-frequency hiss)
    lowpass_filter_init(&p->lowpass, p->config.lowpass_hz, sample_rate);
    log_info("✓ Capture lowpass filter: %.1f Hz", p->config.lowpass_hz);
  }

  p->initialized = true;

  // Initialize startup fade-in to prevent initial microphone click
  // 200ms at 48kHz = 9600 samples - gradual ramp from silence to full volume
  // Longer fade-in (200ms vs 50ms) gives much smoother transition without audible pop
  p->capture_fadein_remaining = (p->config.sample_rate * 200) / 1000; // 200ms worth of samples
  log_info("✓ Capture fade-in: %d samples (200ms)", p->capture_fadein_remaining);

  log_info("Audio pipeline created: %dHz, %dms frames, %dkbps Opus", p->config.sample_rate, p->config.frame_size_ms,
           p->config.opus_bitrate / 1000);

  return p;

error:
  if (p->encoder)
    opus_encoder_destroy(p->encoder);
  if (p->decoder)
    opus_decoder_destroy(p->decoder);
  if (p->echo_canceller) {
    delete static_cast<WebRTCAec3Wrapper *>(p->echo_canceller);
  }
  SAFE_FREE(p);
  return NULL;
}

void client_audio_pipeline_destroy(client_audio_pipeline_t *pipeline) {
  if (!pipeline)
    return;

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

  // No mutex needed - Opus decoder is only used from this thread

  // Decode Opus
  int decoded_samples = opus_decode_float(pipeline->decoder, opus_in, opus_len, output, num_samples, 0);

  if (decoded_samples < 0) {
    log_error("Opus decoding failed: %d", decoded_samples);
    return -1;
  }

  // Apply playback noise gate - cut quiet background audio before it reaches speakers
  if (decoded_samples > 0) {
    noise_gate_process_buffer(&pipeline->playback_noise_gate, output, decoded_samples);
  }

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

          while (capture_offset < capture_count || render_offset < render_count) {
            // STEP 1: Feed render signal (what's playing to speakers)
            // In full-duplex, this is THE EXACT audio being played RIGHT NOW
            if (render_samples && render_offset < render_count) {
              int render_chunk = (render_offset + webrtc_frame_size <= render_count) ? webrtc_frame_size
                                                                                     : (render_count - render_offset);
              if (render_chunk == webrtc_frame_size) {
                // Scale float [-1,1] to WebRTC int16-range [-32768, 32767]
                copy_buffer_with_gain(&render_samples[render_offset], render_channels[0], webrtc_frame_size, 32768.0f);
                render_buf->SplitIntoFrequencyBands();
                wrapper->aec3->AnalyzeRender(render_buf);
                render_buf->MergeFrequencyBands();
                g_render_frames_fed.fetch_add(1, std::memory_order_relaxed);
              }
              render_offset += render_chunk;
            }

            // STEP 2: Process capture (microphone input)
            if (capture_offset < capture_count) {
              int capture_chunk = (capture_offset + webrtc_frame_size <= capture_count)
                                      ? webrtc_frame_size
                                      : (capture_count - capture_offset);
              if (capture_chunk == webrtc_frame_size) {
                // Scale float [-1,1] to WebRTC int16-range [-32768, 32767]
                copy_buffer_with_gain(&processed_output[capture_offset], capture_channels[0], webrtc_frame_size,
                                      32768.0f);

                // AEC3 sequence: AnalyzeCapture, split, ProcessCapture, merge
                wrapper->aec3->AnalyzeCapture(capture_buf);
                capture_buf->SplitIntoFrequencyBands();

                // NOTE: SetAudioBufferDelay() is just an initial hint when use_external_delay_estimator=false
                // (default). AEC3's internal delay estimator will find the actual delay (~144ms in practice). We don't
                // call it here - let AEC3 estimate delay automatically.

                wrapper->aec3->ProcessCapture(capture_buf, false);
                capture_buf->MergeFrequencyBands();

                // Scale back to float range and apply soft clip to prevent distortion
                // Use gentle soft_clip (threshold=0.6, steepness=2.5) to leave headroom for compressor
                for (int j = 0; j < webrtc_frame_size; j++) {
                  float sample = capture_channels[0][j] / 32768.0f;
                  processed_output[capture_offset + j] = soft_clip(sample, 0.6f, 2.5f);
                }

                // Log AEC3 metrics periodically
                static int duplex_log_count = 0;
                if (++duplex_log_count % 100 == 1) {
                  webrtc::EchoControl::Metrics metrics = wrapper->aec3->GetMetrics();
                  log_info("AEC3 DUPLEX: ERL=%.1f ERLE=%.1f delay=%dms", metrics.echo_return_loss,
                           metrics.echo_return_loss_enhancement, metrics.delay_ms);
                  audio_analysis_set_aec3_metrics(metrics.echo_return_loss, metrics.echo_return_loss_enhancement,
                                                  metrics.delay_ms);
                }
              }
              capture_offset += capture_chunk;
            }
          }
        }
      }
    }
  }

  // Debug WAV recording (after AEC3)
  if (pipeline->debug_wav_aec3_out) {
    wav_writer_write((wav_writer_t *)pipeline->debug_wav_aec3_out, processed_output, capture_count);
  }

  // Apply capture processing chain: filters, noise gate, compressor
  if (pipeline->flags.highpass) {
    highpass_filter_process_buffer(&pipeline->highpass, processed_output, capture_count);
  }
  if (pipeline->flags.lowpass) {
    lowpass_filter_process_buffer(&pipeline->lowpass, processed_output, capture_count);
  }
  if (pipeline->flags.noise_gate) {
    noise_gate_process_buffer(&pipeline->noise_gate, processed_output, capture_count);
  }
  if (pipeline->flags.compressor) {
    for (int i = 0; i < capture_count; i++) {
      float gain = compressor_process_sample(&pipeline->compressor, processed_output[i]);
      processed_output[i] *= gain;
    }
    // Apply soft clipping after compressor - threshold=0.7 gives 3dB headroom
    soft_clip_buffer(processed_output, capture_count, 0.7f, 3.0f);
  }
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

  // Reset global counters
  g_render_frames_fed.store(0, std::memory_order_relaxed);
  g_max_render_rms.store(0.0f, std::memory_order_relaxed);

  log_info("Pipeline state reset");
}

#ifdef __cplusplus
}
#endif
