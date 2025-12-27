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
#include "logging.h"
#include "platform/abstraction.h"

// Include mixer.h for compressor, noise gate, and filter functions
#include "audio/mixer.h"

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
      // CRITICAL: Must match AUDIO_JITTER_BUFFER_THRESHOLD in audio.c!
      .jitter_margin_ms = 100,  // 100ms (was 200ms)

      // Higher cutoff to cut low-frequency rumble and feedback
      .highpass_hz = 150.0f,   // Was 80Hz, increased to break rumble feedback loop
      .lowpass_hz = 8000.0f,

      // Compressor: only compress loud peaks, boost overall output
      // Was: threshold -10dB (too aggressive), makeup +3dB (too quiet)
      .comp_threshold_db = -6.0f,   // Only compress peaks above -6dB (was -10dB)
      .comp_ratio = 3.0f,           // Gentler 3:1 ratio (was 4:1)
      .comp_attack_ms = 5.0f,       // Faster attack for peaks (was 10ms)
      .comp_release_ms = 150.0f,    // Slower release (was 100ms)
      .comp_makeup_db = 6.0f,       // More makeup gain (was 3dB)

      // Noise gate: VERY aggressive to cut quiet background audio completely
      // User feedback: "don't amplify or play quiet background audio at all"
      .gate_threshold = 0.08f,      // -22dB threshold (was 0.02/-34dB) - cuts quiet audio hard
      .gate_attack_ms = 0.5f,       // Very fast attack
      .gate_release_ms = 30.0f,     // Fast release (was 50ms)
      .gate_hysteresis = 0.3f,      // Tighter hysteresis = stays closed longer

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

  // Initialize render accumulation buffer
  memset(p->render_accum_buffer, 0, sizeof(p->render_accum_buffer));
  p->render_accum_idx = 0;

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
    try {
      // Configure AEC3 for network audio echo cancellation
      // Audio analysis shows echo at 200ms. Default filter is 13 blocks (130ms)
      // which is too short. Need filter length >= delay to cancel echo.
      // Block size = 10ms at 48kHz
      // (50 blocks crashed, 25 may crash, try 20 blocks = 200ms)
      webrtc::EchoCanceller3Config aec3_config;

      // 50 blocks = 500ms filter (matches erl-works)
      aec3_config.filter.main.length_blocks = 50;
      aec3_config.filter.shadow.length_blocks = 50;
      aec3_config.filter.main_initial.length_blocks = 50;
      aec3_config.filter.shadow_initial.length_blocks = 50;

      // Use EXTERNAL delay estimator with known jitter buffer delay
      // Internal delay estimator struggles with our timing because AnalyzeRender
      // is called in capture thread, not at actual playback time.
      // Instead, we'll use external delay estimator with the known delay.
      aec3_config.delay.use_external_delay_estimator = true;
      aec3_config.delay.default_delay = 10;  // 100ms = jitter buffer threshold
      aec3_config.delay.delay_headroom_samples = 1920;  // 40ms headroom
      aec3_config.delay.hysteresis_limit_blocks = 5;
      aec3_config.delay.fixed_capture_delay_samples = 0;

      // Still need filters for the adaptive part
      aec3_config.delay.num_filters = 20;
      aec3_config.delay.down_sampling_factor = 2;

      // Make delay estimator MORE SENSITIVE for weak audio signals (macOS issue)
      // Default is 0.2 - lower means more sensitive to weak correlations
      // macOS MacBook has excellent acoustic isolation - need VERY low threshold
      aec3_config.delay.delay_candidate_detection_threshold = 0.01f;

      // Lower delay selection thresholds for faster convergence with weak signals
      // Defaults are initial=5, converged=20
      aec3_config.delay.delay_selection_thresholds.initial = 1;
      aec3_config.delay.delay_selection_thresholds.converged = 5;

      // Lower activity power thresholds for weak render/capture signals
      // Defaults are 10000.f - way too high for RMS=0.007 audio (power ~50)
      aec3_config.delay.render_alignment_mixing.activity_power_threshold = 10.f;
      aec3_config.delay.capture_alignment_mixing.activity_power_threshold = 10.f;

      // Lower render level thresholds for weak echo detection
      // Default active_render_limit=100 - may miss weak render signals
      aec3_config.render_levels.active_render_limit = 1.f;
      aec3_config.render_levels.poor_excitation_render_limit = 5.f;
      aec3_config.render_levels.poor_excitation_render_limit_ds8 = 1.f;

      // Lower echo audibility thresholds for weak echoes
      // These defaults are designed for loudspeaker setups, not headphones/laptops
      aec3_config.echo_audibility.low_render_limit = 1.f;
      aec3_config.echo_audibility.normal_render_limit = 1.f;
      aec3_config.echo_audibility.floor_power = 1.f;
      aec3_config.echo_audibility.audibility_threshold_lf = 0.1f;
      aec3_config.echo_audibility.audibility_threshold_mf = 0.1f;
      aec3_config.echo_audibility.audibility_threshold_hf = 0.1f;

      // Lower echo model noise gate for weak signals
      aec3_config.echo_model.noise_gate_power = 100.f;  // Default is 27509
      aec3_config.echo_model.min_noise_floor_power = 100.f;  // Default is 1638400

      // FAST CONVERGENCE settings
      // Short initial phase - get to converged mode quickly
      aec3_config.filter.initial_state_seconds = 1.0f;  // Default 2.5, was 10.0 (too slow!)
      aec3_config.filter.config_change_duration_blocks = 25;  // Default 250 (faster transitions)

      // AGGRESSIVE adaptation rates for fast convergence
      // leakage_converged: default 0.00005 is VERY slow, increase 20x for faster learning
      aec3_config.filter.main.leakage_converged = 0.001f;  // Default 0.00005
      aec3_config.filter.main.leakage_diverged = 0.3f;     // Default 0.05
      aec3_config.filter.main_initial.leakage_converged = 0.01f;  // Default 0.005
      aec3_config.filter.main_initial.leakage_diverged = 0.5f;    // Default 0.5

      // Shadow filter tracks faster for quick adaptation
      aec3_config.filter.shadow.rate = 0.9f;          // Default 0.7
      aec3_config.filter.shadow_initial.rate = 0.95f; // Default 0.9

      // Lower error floor for more aggressive adaptation
      aec3_config.filter.main.error_floor = 0.0001f;  // Default 0.001

      // Lower filter noise gates for weak signals
      aec3_config.filter.main.noise_gate = 1000.f;  // Default 20075344
      aec3_config.filter.shadow.noise_gate = 1000.f;

      // Echo path - don't assume too weak, let filter learn the actual strength
      aec3_config.ep_strength.default_gain = 0.5f;  // Default 1.0, moderate assumption
      aec3_config.ep_strength.bounded_erl = false;  // Don't bound - let it learn full range

      // Note: default_delay already set above (9 = 90ms initial delay)

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

        log_info("✓ WebRTC AEC3 initialized (500ms filter, adaptive delay)");

        // Create persistent AudioBuffer instances for AEC3
        // CRITICAL: AudioBuffer has internal filterbank state that must persist across frames.
        // Creating new buffers each frame resets the filterbank and causes discontinuities!
        p->aec3_render_buffer = new webrtc::AudioBuffer(48000, 1, 48000, 1, 48000, 1);
        p->aec3_capture_buffer = new webrtc::AudioBuffer(48000, 1, 48000, 1, 48000, 1);
        log_info("  - Persistent AudioBuffer instances created");
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

  // Initialize audio processing components (compressor, noise gate, filters)
  // These are applied in the capture path after AEC3 and before Opus encoding
  {
    float sample_rate = (float)p->config.sample_rate;

  // Initialize compressor with config values
  compressor_init(&p->compressor, sample_rate);
  compressor_set_params(&p->compressor,
                        p->config.comp_threshold_db,
                        p->config.comp_ratio,
                        p->config.comp_attack_ms,
                        p->config.comp_release_ms,
                        p->config.comp_makeup_db);
  log_info("✓ Capture compressor: threshold=%.1fdB, ratio=%.1f:1, makeup=+%.1fdB",
           p->config.comp_threshold_db, p->config.comp_ratio, p->config.comp_makeup_db);

  // Initialize noise gate with config values
  noise_gate_init(&p->noise_gate, sample_rate);
  noise_gate_set_params(&p->noise_gate,
                        p->config.gate_threshold,
                        p->config.gate_attack_ms,
                        p->config.gate_release_ms,
                        p->config.gate_hysteresis);
  log_info("✓ Capture noise gate: threshold=%.4f (%.1fdB)",
           p->config.gate_threshold, 20.0f * log10f(p->config.gate_threshold + 1e-10f));

  // Initialize PLAYBACK noise gate - cuts quiet received audio before speakers
  // Very low threshold - only cut actual silence, not quiet voice audio
  // The server sends audio with RMS=0.01-0.02, so threshold must be below that
  noise_gate_init(&p->playback_noise_gate, sample_rate);
  noise_gate_set_params(&p->playback_noise_gate,
                        0.002f,   // -54dB threshold - only cut near-silence
                        1.0f,     // 1ms attack - fast open
                        50.0f,    // 50ms release - smooth close
                        0.4f);    // Hysteresis
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
  p->capture_fadein_remaining = (p->config.sample_rate * 200) / 1000;  // 200ms worth of samples
  log_info("✓ Capture fade-in: %d samples (200ms)", p->capture_fadein_remaining);

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

  // Clean up WebRTC AEC3 AudioBuffer instances
  if (pipeline->aec3_render_buffer) {
    delete static_cast<webrtc::AudioBuffer*>(pipeline->aec3_render_buffer);
    pipeline->aec3_render_buffer = NULL;
  }
  if (pipeline->aec3_capture_buffer) {
    delete static_cast<webrtc::AudioBuffer*>(pipeline->aec3_capture_buffer);
    pipeline->aec3_capture_buffer = NULL;
  }

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
  // No mutex needed - flags are only read by capture thread
  pipeline->flags = flags;
}

client_audio_pipeline_flags_t client_audio_pipeline_get_flags(client_audio_pipeline_t *pipeline) {
  if (!pipeline) return CLIENT_AUDIO_PIPELINE_FLAGS_MINIMAL;
  // No mutex needed - flags are only written from main thread during setup
  return pipeline->flags;
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

  // Apply startup fade-in to prevent initial click when microphone connects
  // This gradually ramps volume from 0 to 1 over the first 200ms (9600 samples at 48kHz)
  if (pipeline->capture_fadein_remaining > 0) {
    // Calculate the total fade-in duration for computing gain ramp
    const int total_fadein_samples = (pipeline->config.sample_rate * 200) / 1000;

    for (int i = 0; i < num_samples && pipeline->capture_fadein_remaining > 0; i++) {
      // Calculate fade-in gain: starts at 0.0, ends at 1.0
      // fadein_remaining counts down from total_fadein_samples to 0
      float progress = 1.0f - ((float)pipeline->capture_fadein_remaining / (float)total_fadein_samples);
      // Use smooth S-curve (smoothstep) for less abrupt transition
      float gain = progress * progress * (3.0f - 2.0f * progress);
      processed[i] *= gain;
      pipeline->capture_fadein_remaining--;
    }

    static int logged_fadein = 0;
    if (!logged_fadein) {
      log_info("Capture fade-in active: ramping microphone volume over 200ms");
      logged_fadein = 1;
    }
  }

  // Check for AEC3 bypass (for debugging)
  static int bypass_aec3 = -1;
  if (bypass_aec3 == -1) {
    const char *env = getenv("BYPASS_AEC3");
    bypass_aec3 = (env && (strcmp(env, "1") == 0 || strcmp(env, "true") == 0)) ? 1 : 0;
    if (bypass_aec3) {
      log_warn("AEC3 BYPASSED via BYPASS_AEC3=1");
    }
  }

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

  // WebRTC AEC3 echo cancellation - NO MUTEX, all processing in capture thread
  // This is the PROPER real-time audio design:
  // 1. Output callback queues render samples to lock-free ring buffer
  // 2. Capture callback (here) drains ring buffer and calls ALL AEC3 functions
  // 3. No mutex contention, no blocking in either callback
  if (!bypass_aec3 && pipeline->flags.echo_cancel && pipeline->echo_canceller) {
    auto wrapper = static_cast<WebRTCAec3Wrapper*>(pipeline->echo_canceller);
    if (wrapper && wrapper->aec3) {
      // NO MUTEX - all AEC3 calls happen in this thread only

      const int webrtc_frame_size = 480;  // 10ms at 48kHz
      const int buffer_size = CLIENT_AUDIO_PIPELINE_RENDER_BUFFER_SIZE;

      // Static variable for tracking max capture signal level (for convergence diagnostics)
      static float s_max_capture_rms = 0.0f;

      // Get render buffer for AnalyzeRender calls
      auto* render_buf = static_cast<webrtc::AudioBuffer*>(pipeline->aec3_render_buffer);
      auto* capture_buf = static_cast<webrtc::AudioBuffer*>(pipeline->aec3_capture_buffer);

      if (!render_buf || !capture_buf) {
        goto skip_aec3;
      }

      float* const* render_channels = render_buf->channels();
      float* const* capture_channels = capture_buf->channels();

      if (!render_channels || !render_channels[0] || !capture_channels || !capture_channels[0]) {
        goto skip_aec3;
      }

      try {
        // Process render and capture in INTERLEAVED lockstep (per demo.cc pattern)
        // For each 480-sample capture chunk, process ONE render chunk first.
        // This maintains the timing relationship AEC3 expects.

        // Get current ring buffer state
        int read_idx = atomic_load(&pipeline->render_ring_read_idx);

        for (int i = 0; i < num_samples; i += webrtc_frame_size) {
          int chunk_size = (i + webrtc_frame_size <= num_samples) ? webrtc_frame_size : (num_samples - i);

          // STEP 1: Process ONE render frame (if available) BEFORE capture
          // This is the key to the demo.cc interleaved pattern
          int write_idx = atomic_load(&pipeline->render_ring_write_idx);
          int render_available = (write_idx - read_idx + buffer_size) % buffer_size;

          if (render_available >= webrtc_frame_size) {
            // Copy 480 samples from ring buffer
            for (int j = 0; j < webrtc_frame_size; j++) {
              render_channels[0][j] = pipeline->render_ring_buffer[read_idx] * 32768.0f;
              read_idx = (read_idx + 1) % buffer_size;
            }

            // AnalyzeRender on split buffer
            render_buf->SplitIntoFrequencyBands();
            wrapper->aec3->AnalyzeRender(render_buf);
            render_buf->MergeFrequencyBands();

            g_render_frames_fed.fetch_add(1, std::memory_order_relaxed);
          }

          // Update read index
          atomic_store(&pipeline->render_ring_read_idx, read_idx);

          // STEP 2: Process capture frame

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

          // AnalyzeCapture on NON-split buffer (per demo.cc)
          wrapper->aec3->AnalyzeCapture(capture_buf);

          // Split into frequency bands for ProcessCapture
          capture_buf->SplitIntoFrequencyBands();

          // SetAudioBufferDelay: The render samples in our ring buffer were
          // queued when audio played to speakers. We process them here in the
          // capture thread. The delay is approximately the ring buffer latency
          // plus the acoustic path delay. With ~600 samples buffered (~12ms)
          // and jitter buffer (~100ms), total is ~112ms.
          int render_buffered = (write_idx - read_idx + buffer_size) % buffer_size;
          int buffer_delay_ms = (render_buffered * 1000) / 48000 + 100;  // ring buffer + jitter
          wrapper->aec3->SetAudioBufferDelay(buffer_delay_ms);

          // ProcessCapture on SPLIT buffer
          wrapper->aec3->ProcessCapture(capture_buf, false);

          // Merge frequency bands back to time domain
          capture_buf->MergeFrequencyBands();

          // Scale back to [-1.0, 1.0] range with SOFT clipping
          for (int j = 0; j < chunk_size; j++) {
            float sample = capture_channels[0][j] / 32768.0f;
            if (sample > 0.5f) {
              sample = 0.5f + 0.5f * tanhf((sample - 0.5f) * 2.0f);
            } else if (sample < -0.5f) {
              sample = -0.5f + 0.5f * tanhf((sample + 0.5f) * 2.0f);
            }
            (processed + i)[j] = sample;
          }

          // Log capture signal stats and metrics every second
          static int capture_count = 0;
          capture_count++;

          // Track max signal levels
          if (last_input_rms > s_max_capture_rms) s_max_capture_rms = last_input_rms;

          if (capture_count % 100 == 1) {
            // Calculate RMS AFTER AEC3 processing
            float out_energy = 0.0f;
            for (int s = 0; s < chunk_size; s++) {
              out_energy += (processed + i)[s] * (processed + i)[s];
            }
            float out_rms = sqrtf(out_energy / chunk_size);

            float reduction_db = 0.0f;
            if (last_input_rms > 0.0001f && out_rms > 0.0001f) {
              reduction_db = 20.0f * log10f(out_rms / last_input_rms);
            }

            int total_render = g_render_frames_fed.load(std::memory_order_relaxed);

            try {
              webrtc::EchoControl::Metrics metrics = wrapper->aec3->GetMetrics();
              float max_render = g_max_render_rms.load(std::memory_order_relaxed);
              log_info("AEC3: IN=%.4f OUT=%.4f (%.1fdB) ERL=%.1f ERLE=%.1f delay=%dms "
                       "buf_delay=%dms max_cap=%.3f max_ren=%.3f total_render=%d",
                       last_input_rms, out_rms, reduction_db,
                       metrics.echo_return_loss, metrics.echo_return_loss_enhancement,
                       metrics.delay_ms, buffer_delay_ms, s_max_capture_rms,
                       max_render, total_render);
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
skip_aec3:

  // Debug: Write output after AEC3 processing
  if (pipeline->debug_wav_aec3_out) {
    wav_writer_write((wav_writer_t *)pipeline->debug_wav_aec3_out, processed, num_samples);
  }

  // ============================================================================
  // CAPTURE PROCESSING CHAIN: Apply filters, noise gate, and compressor
  // This adds the missing gain staging that was causing 82-90% "very quiet" samples
  // ============================================================================

  // 1. High-pass filter (remove low-frequency rumble < 80Hz)
  if (pipeline->flags.highpass) {
    highpass_filter_process_buffer(&pipeline->highpass, processed, num_samples);
  }

  // 2. Low-pass filter (remove high-frequency hiss > 8000Hz)
  if (pipeline->flags.lowpass) {
    lowpass_filter_process_buffer(&pipeline->lowpass, processed, num_samples);
  }

  // 3. AGC (Automatic Gain Control) - normalize audio to target level BEFORE noise gate
  // This ensures all clients send audio at similar levels regardless of mic sensitivity.
  // Must come before noise gate so we boost the signal, then gate cuts what's still quiet.
  {
    // Calculate current RMS
    float sum_squares = 0.0f;
    for (int i = 0; i < num_samples; i++) {
      sum_squares += processed[i] * processed[i];
    }
    float rms = sqrtf(sum_squares / num_samples);

    // AGC disabled - was amplifying quiet sounds too much
    // Just pass through audio without gain adjustment
    (void)rms;  // Suppress unused variable warning
  }

  // 4. Noise gate (cut silence/background noise) - AFTER AGC
  if (pipeline->flags.noise_gate) {
    noise_gate_process_buffer(&pipeline->noise_gate, processed, num_samples);
  }

  // 5. Compressor with makeup gain (+6dB configured)
  // This is the KEY gain stage that was missing!
  if (pipeline->flags.compressor) {
    for (int i = 0; i < num_samples; i++) {
      // Compressor returns a gain multiplier including makeup gain
      float gain = compressor_process_sample(&pipeline->compressor, processed[i]);
      processed[i] *= gain;

      // Soft clip to prevent clipping from makeup gain
      if (processed[i] > 0.95f) {
        processed[i] = 0.95f + 0.05f * tanhf((processed[i] - 0.95f) * 10.0f);
      } else if (processed[i] < -0.95f) {
        processed[i] = -0.95f + 0.05f * tanhf((processed[i] + 0.95f) * 10.0f);
      }
    }
  }

  // Encode with Opus
  int opus_len = opus_encode_float(pipeline->encoder, processed, num_samples, opus_out, max_opus_len);

  if (opus_len < 0) {
    log_error("Opus encoding failed: %d", opus_len);
    return -1;
  }

  // DEBUG: Log encoded packet info for debugging transmission issues
  static int encode_count = 0;
  encode_count++;
  if (encode_count <= 10 || encode_count % 100 == 0) {
    float rms = 0.0f;
    for (int i = 0; i < num_samples; i++) {
      rms += processed[i] * processed[i];
    }
    rms = sqrtf(rms / num_samples);
    log_info("CLIENT OPUS ENCODE #%d: input_rms=%.6f, opus_len=%d, first4=[0x%02x,0x%02x,0x%02x,0x%02x]",
             encode_count, rms, opus_len,
             opus_len > 0 ? opus_out[0] : 0,
             opus_len > 1 ? opus_out[1] : 0,
             opus_len > 2 ? opus_out[2] : 0,
             opus_len > 3 ? opus_out[3] : 0);
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
 * Queue render signal for AEC3 processing (lock-free, real-time safe).
 *
 * Called from PortAudio's real-time output callback.
 * This function ONLY queues data to the lock-free ring buffer.
 * NO AEC3 calls happen here - all AEC3 processing is done in the capture thread.
 *
 * This is the PROPER design for real-time audio:
 * - Output callback: Lock-free queue (this function)
 * - Capture callback: Drains queue and calls ALL AEC3 functions sequentially
 * - NO MUTEXES in either callback
 */
void client_audio_pipeline_analyze_render(client_audio_pipeline_t *pipeline, const float *samples,
                                          int num_samples) {
  if (!pipeline || !samples || num_samples <= 0) return;
  if (!pipeline->flags.echo_cancel || !pipeline->echo_canceller) return;
  if (!pipeline->render_ring_buffer) return;

  const int buffer_size = CLIENT_AUDIO_PIPELINE_RENDER_BUFFER_SIZE;

  // Lock-free write to ring buffer
  // The capture thread will drain this and call AnalyzeRender
  int write_idx = atomic_load(&pipeline->render_ring_write_idx);
  int read_idx = atomic_load(&pipeline->render_ring_read_idx);

  // Calculate available space (leave 1 slot empty to distinguish full from empty)
  int available = (read_idx - write_idx - 1 + buffer_size) % buffer_size;

  if (available < num_samples) {
    // Buffer full - drop oldest samples by advancing read pointer
    // This is better than blocking in real-time callback
    static int drop_count = 0;
    if (++drop_count % 100 == 1) {
      log_warn("Render ring buffer full, dropping %d samples (capture thread too slow?)",
               num_samples - available);
    }
    // Advance read index to make room
    int advance = num_samples - available + 1;
    atomic_store(&pipeline->render_ring_read_idx, (read_idx + advance) % buffer_size);
  }

  // Write samples to ring buffer
  for (int i = 0; i < num_samples; i++) {
    pipeline->render_ring_buffer[write_idx] = samples[i];
    write_idx = (write_idx + 1) % buffer_size;
  }
  atomic_store(&pipeline->render_ring_write_idx, write_idx);

  // Track render RMS for diagnostics (lock-free)
  float energy = 0.0f;
  for (int i = 0; i < num_samples; i++) {
    energy += samples[i] * samples[i];
  }
  float rms = sqrtf(energy / num_samples);
  float current_max = g_max_render_rms.load(std::memory_order_relaxed);
  while (rms > current_max &&
         !g_max_render_rms.compare_exchange_weak(current_max, rms,
                                                  std::memory_order_relaxed)) {}

  // Log periodically
  static int render_call_count = 0;
  render_call_count++;
  if (render_call_count % 500 == 1) {
    int buffered = (write_idx - read_idx + buffer_size) % buffer_size;
    log_info("AEC3 render queued: %d samples, RMS=%.6f, buffer=%d samples (%.1fms)",
             num_samples, rms, buffered, buffered * 1000.0f / 48000.0f);
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
 * Reset pipeline state (AEC3 is adaptive, echo reference buffer reset)
 */
void client_audio_pipeline_reset(client_audio_pipeline_t *pipeline) {
  if (!pipeline) return;

  // Reset ring buffer indices (atomic, lock-free)
  atomic_store(&pipeline->render_ring_write_idx, 0);
  atomic_store(&pipeline->render_ring_read_idx, 0);
  pipeline->render_accum_idx = 0;

  // Reset global counters
  g_render_frames_fed.store(0, std::memory_order_relaxed);
  g_max_render_rms.store(0.0f, std::memory_order_relaxed);

  log_info("Pipeline state reset (lock-free)");
}

#ifdef __cplusplus
}
#endif
