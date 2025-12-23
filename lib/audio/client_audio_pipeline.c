/**
 * @file client_audio_pipeline.c
 * @brief Unified client-side audio processing pipeline implementation
 */

#include "audio/client_audio_pipeline.h"
#include "common.h"
#include "logging.h"

#include <opus/opus.h>
#include <speex/speex_echo.h>
#include <speex/speex_jitter.h>
#include <speex/speex_preprocess.h>
#include <string.h>

#include "platform/abstraction.h"
#include <fcntl.h>

// Suppress stderr temporarily (SpeexDSP prints "VAD has been replaced by a hack" warning)
static void suppress_stderr_for_vad_init(SpeexPreprocessState *preprocess) {
  int saved_stderr = -1;
  int devnull = -1;

  // Save stderr and redirect to /dev/null
#ifdef _WIN32
  saved_stderr = _dup(2);
  devnull = platform_open("NUL", O_WRONLY, 0);
#else
  saved_stderr = dup(2);
  devnull = platform_open("/dev/null", O_WRONLY, 0);
#endif

  if (saved_stderr >= 0 && devnull >= 0) {
#ifdef _WIN32
    _dup2(devnull, 2);
#else
    dup2(devnull, 2);
#endif
  }

  // Enable VAD (this prints the warning we want to suppress)
  int vad = 1;
  speex_preprocess_ctl(preprocess, SPEEX_PREPROCESS_SET_VAD, &vad);

  // Restore stderr
  if (saved_stderr >= 0) {
#ifdef _WIN32
    _dup2(saved_stderr, 2);
    _close(saved_stderr);
#else
    dup2(saved_stderr, 2);
    close(saved_stderr);
#endif
  }
  if (devnull >= 0) {
#ifdef _WIN32
    _close(devnull);
#else
    close(devnull);
#endif
  }
}

// ============================================================================
// Conversion Helpers
// ============================================================================

static void float_to_int16(const float *in, int16_t *out, int n) {
  for (int i = 0; i < n; i++) {
    float s = in[i] * 32767.0f;
    if (s > 32767.0f)
      s = 32767.0f;
    if (s < -32768.0f)
      s = -32768.0f;
    out[i] = (int16_t)s;
  }
}

static void int16_to_float(const int16_t *in, float *out, int n) {
  for (int i = 0; i < n; i++) {
    out[i] = (float)in[i] / 32768.0f;
  }
}

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

      .flags = CLIENT_AUDIO_PIPELINE_FLAGS_ALL,
  };
}

// ============================================================================
// Lifecycle Functions
// ============================================================================

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

  // Calculate filter length in samples
  int filter_length = p->config.sample_rate * p->config.echo_filter_ms / 1000;

  // Create Speex echo canceller
  p->echo_state = speex_echo_state_init(p->frame_size, filter_length);
  if (!p->echo_state) {
    log_error("Failed to create Speex echo canceller");
    goto error;
  }
  speex_echo_ctl(p->echo_state, SPEEX_ECHO_SET_SAMPLING_RATE, &p->config.sample_rate);

  // Create Speex preprocessor
  p->preprocess = speex_preprocess_state_init(p->frame_size, p->config.sample_rate);
  if (!p->preprocess) {
    log_error("Failed to create Speex preprocessor");
    goto error;
  }

  // Link preprocessor to echo canceller
  speex_preprocess_ctl(p->preprocess, SPEEX_PREPROCESS_SET_ECHO_STATE, p->echo_state);

  // Configure noise suppression
  int denoise = 1;
  speex_preprocess_ctl(p->preprocess, SPEEX_PREPROCESS_SET_DENOISE, &denoise);
  speex_preprocess_ctl(p->preprocess, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &p->config.noise_suppress_db);

  // Configure AGC
  int agc = 1;
  speex_preprocess_ctl(p->preprocess, SPEEX_PREPROCESS_SET_AGC, &agc);
  speex_preprocess_ctl(p->preprocess, SPEEX_PREPROCESS_SET_AGC_LEVEL, &p->config.agc_level);
  speex_preprocess_ctl(p->preprocess, SPEEX_PREPROCESS_SET_AGC_MAX_GAIN, &p->config.agc_max_gain);

  // Configure VAD (suppress "VAD has been replaced by a hack" warning)
  suppress_stderr_for_vad_init(p->preprocess);

  // Create Speex jitter buffer
  p->jitter = jitter_buffer_init(p->frame_size);
  if (!p->jitter) {
    log_error("Failed to create Speex jitter buffer");
    goto error;
  }

  // Configure jitter buffer margin
  int margin = p->config.jitter_margin_ms * p->config.sample_rate / 1000;
  jitter_buffer_ctl(p->jitter, JITTER_BUFFER_SET_MARGIN, &margin);

  p->jitter_timestamp = 0;
  p->jitter_frame_span = p->frame_size;

  // Create Opus encoder
  int opus_error;
  p->encoder = opus_encoder_create(p->config.sample_rate, 1, OPUS_APPLICATION_VOIP, &opus_error);
  if (opus_error != OPUS_OK) {
    log_error("Failed to create Opus encoder: %s", opus_strerror(opus_error));
    goto error;
  }
  opus_encoder_ctl(p->encoder, OPUS_SET_BITRATE(p->config.opus_bitrate));

  // Create Opus decoder
  p->decoder = opus_decoder_create(p->config.sample_rate, 1, &opus_error);
  if (opus_error != OPUS_OK) {
    log_error("Failed to create Opus decoder: %s", opus_strerror(opus_error));
    goto error;
  }

  // Initialize mixer components
  compressor_init(&p->compressor, (float)p->config.sample_rate);
  compressor_set_params(&p->compressor, p->config.comp_threshold_db, p->config.comp_ratio, p->config.comp_attack_ms,
                        p->config.comp_release_ms, p->config.comp_makeup_db);

  noise_gate_init(&p->noise_gate, (float)p->config.sample_rate);
  noise_gate_set_params(&p->noise_gate, p->config.gate_threshold, p->config.gate_attack_ms, p->config.gate_release_ms,
                        p->config.gate_hysteresis);

  highpass_filter_init(&p->highpass, p->config.highpass_hz, (float)p->config.sample_rate);
  lowpass_filter_init(&p->lowpass, p->config.lowpass_hz, (float)p->config.sample_rate);

  // Allocate echo reference ring buffer
  p->echo_ref_size = CLIENT_AUDIO_PIPELINE_ECHO_REF_SIZE;
  p->echo_ref_buffer = SAFE_CALLOC((size_t)p->echo_ref_size, sizeof(int16_t), int16_t *);
  if (!p->echo_ref_buffer) {
    log_error("Failed to allocate echo reference buffer");
    goto error;
  }
  p->echo_ref_write_pos = 0;
  p->echo_ref_read_pos = 0;
  p->echo_ref_available = 0;

  // Allocate work buffers
  p->work_i16 = SAFE_CALLOC((size_t)p->frame_size, sizeof(int16_t), int16_t *);
  p->echo_i16 = SAFE_CALLOC((size_t)p->frame_size, sizeof(int16_t), int16_t *);
  p->work_float = SAFE_CALLOC((size_t)p->frame_size, sizeof(float), float *);

  if (!p->work_i16 || !p->echo_i16 || !p->work_float) {
    log_error("Failed to allocate work buffers");
    goto error;
  }

  p->initialized = true;

  log_info("Client audio pipeline created: sample_rate=%d, frame_size=%d, echo_filter=%dms", p->config.sample_rate,
           p->frame_size, p->config.echo_filter_ms);

  return p;

error:
  client_audio_pipeline_destroy(p);
  return NULL;
}

void client_audio_pipeline_destroy(client_audio_pipeline_t *pipeline) {
  if (!pipeline) {
    return;
  }

  mutex_lock(&pipeline->mutex);

  if (pipeline->preprocess) {
    speex_preprocess_state_destroy(pipeline->preprocess);
    pipeline->preprocess = NULL;
  }

  if (pipeline->echo_state) {
    speex_echo_state_destroy(pipeline->echo_state);
    pipeline->echo_state = NULL;
  }

  if (pipeline->jitter) {
    jitter_buffer_destroy(pipeline->jitter);
    pipeline->jitter = NULL;
  }

  if (pipeline->encoder) {
    opus_encoder_destroy(pipeline->encoder);
    pipeline->encoder = NULL;
  }

  if (pipeline->decoder) {
    opus_decoder_destroy(pipeline->decoder);
    pipeline->decoder = NULL;
  }

  if (pipeline->echo_ref_buffer) {
    SAFE_FREE(pipeline->echo_ref_buffer);
  }

  if (pipeline->work_i16) {
    SAFE_FREE(pipeline->work_i16);
  }

  if (pipeline->echo_i16) {
    SAFE_FREE(pipeline->echo_i16);
  }

  if (pipeline->work_float) {
    SAFE_FREE(pipeline->work_float);
  }

  pipeline->initialized = false;

  mutex_unlock(&pipeline->mutex);
  mutex_destroy(&pipeline->mutex);

  SAFE_FREE(pipeline);

  log_info("Client audio pipeline destroyed");
}

// ============================================================================
// Flag Management
// ============================================================================

void client_audio_pipeline_set_flags(client_audio_pipeline_t *pipeline, client_audio_pipeline_flags_t flags) {
  if (!pipeline)
    return;
  mutex_lock(&pipeline->mutex);

  // Check if preprocessor flags changed (before overwriting)
  bool vad_changed = (pipeline->flags.vad != flags.vad);
  bool preprocess_changed =
      (pipeline->flags.noise_suppress != flags.noise_suppress) || (pipeline->flags.agc != flags.agc) || vad_changed;

  pipeline->flags = flags;

  // Apply preprocessor settings if they changed
  if (preprocess_changed && pipeline->preprocess) {
    int denoise = flags.noise_suppress ? 1 : 0;
    int agc = flags.agc ? 1 : 0;

    speex_preprocess_ctl(pipeline->preprocess, SPEEX_PREPROCESS_SET_DENOISE, &denoise);
    speex_preprocess_ctl(pipeline->preprocess, SPEEX_PREPROCESS_SET_AGC, &agc);

    // VAD setting change requires stderr suppression (prints warning)
    if (vad_changed) {
      suppress_stderr_for_vad_init(pipeline->preprocess);
    }
  }

  mutex_unlock(&pipeline->mutex);
}

client_audio_pipeline_flags_t client_audio_pipeline_get_flags(client_audio_pipeline_t *pipeline) {
  if (!pipeline)
    return CLIENT_AUDIO_PIPELINE_FLAGS_MINIMAL;
  mutex_lock(&pipeline->mutex);
  client_audio_pipeline_flags_t flags = pipeline->flags;
  mutex_unlock(&pipeline->mutex);
  return flags;
}

// ============================================================================
// Capture Path
// ============================================================================

int client_audio_pipeline_capture(client_audio_pipeline_t *pipeline, const float *input, int num_samples,
                                  uint8_t *opus_out, int opus_out_size) {
  if (!pipeline || !pipeline->initialized || !input || !opus_out) {
    return -1;
  }

  if (num_samples > pipeline->frame_size) {
    log_warn("Capture num_samples %d exceeds frame_size %d", num_samples, pipeline->frame_size);
    num_samples = pipeline->frame_size;
  }

  mutex_lock(&pipeline->mutex);

  // Convert input float to int16 for Speex
  float_to_int16(input, pipeline->work_i16, num_samples);

  // Echo cancellation
  if (pipeline->flags.echo_cancel && pipeline->echo_state) {
    // Get echo reference from ring buffer
    if (pipeline->echo_ref_available >= num_samples) {
      for (int i = 0; i < num_samples; i++) {
        pipeline->echo_i16[i] = pipeline->echo_ref_buffer[pipeline->echo_ref_read_pos];
        pipeline->echo_ref_read_pos = (pipeline->echo_ref_read_pos + 1) % pipeline->echo_ref_size;
        pipeline->echo_ref_available--;
      }
    } else {
      // Not enough reference data - use silence
      memset(pipeline->echo_i16, 0, (size_t)num_samples * sizeof(int16_t));
    }

    // Apply echo cancellation
    int16_t *aec_output = SAFE_MALLOC((size_t)num_samples * sizeof(int16_t), int16_t *);
    if (aec_output) {
      speex_echo_cancellation(pipeline->echo_state, pipeline->work_i16, pipeline->echo_i16, aec_output);
      memcpy(pipeline->work_i16, aec_output, (size_t)num_samples * sizeof(int16_t));
      SAFE_FREE(aec_output);
    }
  }

  // Speex preprocessor (noise suppression, AGC, VAD)
  // Settings are configured once in create() and set_flags(), not per-frame
  if ((pipeline->flags.noise_suppress || pipeline->flags.agc || pipeline->flags.vad) && pipeline->preprocess) {
    speex_preprocess_run(pipeline->preprocess, pipeline->work_i16);
  }

  // Convert back to float for mixer components
  int16_to_float(pipeline->work_i16, pipeline->work_float, num_samples);

  // High-pass filter
  if (pipeline->flags.highpass) {
    highpass_filter_process_buffer(&pipeline->highpass, pipeline->work_float, num_samples);
  }

  // Low-pass filter
  if (pipeline->flags.lowpass) {
    lowpass_filter_process_buffer(&pipeline->lowpass, pipeline->work_float, num_samples);
  }

  // Noise gate
  if (pipeline->flags.noise_gate) {
    noise_gate_process_buffer(&pipeline->noise_gate, pipeline->work_float, num_samples);
  }

  // Compressor
  if (pipeline->flags.compressor) {
    for (int i = 0; i < num_samples; i++) {
      float sample = pipeline->work_float[i];
      float abs_sample = sample < 0 ? -sample : sample;
      float gain = compressor_process_sample(&pipeline->compressor, abs_sample);
      pipeline->work_float[i] = sample * gain;
    }
  }

  // Soft clipping to prevent harsh distortion
  soft_clip_buffer(pipeline->work_float, num_samples, 0.95f);

  // Opus encode
  int opus_bytes = opus_encode_float(pipeline->encoder, pipeline->work_float, num_samples, opus_out, opus_out_size);

  mutex_unlock(&pipeline->mutex);

  if (opus_bytes < 0) {
    log_error("Opus encode failed: %s", opus_strerror(opus_bytes));
    return -1;
  }

  return opus_bytes;
}

// ============================================================================
// Playback Path
// ============================================================================

int client_audio_pipeline_playback(client_audio_pipeline_t *pipeline, const uint8_t *opus_in, int opus_len,
                                   float *output, int max_samples) {
  if (!pipeline || !pipeline->initialized || !output) {
    return -1;
  }

  mutex_lock(&pipeline->mutex);

  int samples_decoded = 0;

  if (pipeline->flags.jitter_buffer && pipeline->jitter) {
    // Put packet into jitter buffer
    if (opus_in && opus_len > 0) {
      JitterBufferPacket packet;
      packet.data = (char *)opus_in;
      packet.len = (spx_uint32_t)opus_len;
      packet.timestamp = pipeline->jitter_timestamp;
      packet.span = (spx_uint32_t)pipeline->jitter_frame_span;
      packet.sequence = 0;
      packet.user_data = 0;

      jitter_buffer_put(pipeline->jitter, &packet);
      pipeline->jitter_timestamp += (uint32_t)pipeline->jitter_frame_span;
    }

    // Get packet from jitter buffer
    JitterBufferPacket out_packet;
    char jitter_data[CLIENT_AUDIO_PIPELINE_MAX_OPUS_PACKET];
    out_packet.data = jitter_data;
    out_packet.len = CLIENT_AUDIO_PIPELINE_MAX_OPUS_PACKET;

    spx_int32_t start_offset;
    int jitter_result = jitter_buffer_get(pipeline->jitter, &out_packet, pipeline->jitter_frame_span, &start_offset);

    jitter_buffer_tick(pipeline->jitter);

    if (jitter_result == JITTER_BUFFER_OK) {
      // Decode the packet from jitter buffer
      samples_decoded = opus_decode_float(pipeline->decoder, (const unsigned char *)out_packet.data,
                                          (opus_int32)out_packet.len, pipeline->work_float, pipeline->frame_size, 0);
    } else if (jitter_result == JITTER_BUFFER_MISSING) {
      // Packet loss - use Opus PLC
      samples_decoded = opus_decode_float(pipeline->decoder, NULL, 0, pipeline->work_float, pipeline->frame_size, 0);
    } else {
      // Insertion or error - output silence
      samples_decoded = pipeline->frame_size;
      memset(pipeline->work_float, 0, (size_t)samples_decoded * sizeof(float));
    }
  } else {
    // No jitter buffer - decode directly
    if (opus_in && opus_len > 0) {
      samples_decoded =
          opus_decode_float(pipeline->decoder, opus_in, opus_len, pipeline->work_float, pipeline->frame_size, 0);
    } else {
      // PLC
      samples_decoded = opus_decode_float(pipeline->decoder, NULL, 0, pipeline->work_float, pipeline->frame_size, 0);
    }
  }

  if (samples_decoded < 0) {
    log_error("Opus decode failed: %s", opus_strerror(samples_decoded));
    mutex_unlock(&pipeline->mutex);
    return -1;
  }

  // Clamp to max_samples
  if (samples_decoded > max_samples) {
    samples_decoded = max_samples;
  }

  // Soft clipping
  soft_clip_buffer(pipeline->work_float, samples_decoded, 0.95f);

  // Copy to output
  memcpy(output, pipeline->work_float, (size_t)samples_decoded * sizeof(float));

  mutex_unlock(&pipeline->mutex);

  return samples_decoded;
}

int client_audio_pipeline_get_playback_frame(client_audio_pipeline_t *pipeline, float *output, int num_samples) {
  if (!pipeline || !pipeline->initialized || !output) {
    return -1;
  }

  mutex_lock(&pipeline->mutex);

  int samples_decoded = 0;

  if (pipeline->flags.jitter_buffer && pipeline->jitter) {
    // Get packet from jitter buffer
    JitterBufferPacket out_packet;
    char jitter_data[CLIENT_AUDIO_PIPELINE_MAX_OPUS_PACKET];
    out_packet.data = jitter_data;
    out_packet.len = CLIENT_AUDIO_PIPELINE_MAX_OPUS_PACKET;

    spx_int32_t start_offset;
    int jitter_result = jitter_buffer_get(pipeline->jitter, &out_packet, num_samples, &start_offset);

    jitter_buffer_tick(pipeline->jitter);

    if (jitter_result == JITTER_BUFFER_OK) {
      samples_decoded = opus_decode_float(pipeline->decoder, (const unsigned char *)out_packet.data,
                                          (opus_int32)out_packet.len, pipeline->work_float, pipeline->frame_size, 0);
    } else if (jitter_result == JITTER_BUFFER_MISSING) {
      samples_decoded = opus_decode_float(pipeline->decoder, NULL, 0, pipeline->work_float, pipeline->frame_size, 0);
    } else {
      samples_decoded = num_samples;
      memset(pipeline->work_float, 0, (size_t)samples_decoded * sizeof(float));
    }
  } else {
    // No jitter buffer - use PLC for silence
    samples_decoded = opus_decode_float(pipeline->decoder, NULL, 0, pipeline->work_float, pipeline->frame_size, 0);
  }

  if (samples_decoded < 0) {
    log_error("Opus decode failed: %s", opus_strerror(samples_decoded));
    mutex_unlock(&pipeline->mutex);
    return -1;
  }

  if (samples_decoded > num_samples) {
    samples_decoded = num_samples;
  }

  soft_clip_buffer(pipeline->work_float, samples_decoded, 0.95f);
  memcpy(output, pipeline->work_float, (size_t)samples_decoded * sizeof(float));

  mutex_unlock(&pipeline->mutex);

  return samples_decoded;
}

// ============================================================================
// Echo Reference
// ============================================================================

void client_audio_pipeline_feed_echo_ref(client_audio_pipeline_t *pipeline, const float *samples, int num_samples) {
  if (!pipeline || !pipeline->initialized || !samples || num_samples <= 0) {
    return;
  }

  mutex_lock(&pipeline->mutex);

  // Convert float to int16 and add to ring buffer
  for (int i = 0; i < num_samples; i++) {
    float s = samples[i] * 32767.0f;
    if (s > 32767.0f)
      s = 32767.0f;
    if (s < -32768.0f)
      s = -32768.0f;

    pipeline->echo_ref_buffer[pipeline->echo_ref_write_pos] = (int16_t)s;
    pipeline->echo_ref_write_pos = (pipeline->echo_ref_write_pos + 1) % pipeline->echo_ref_size;

    if (pipeline->echo_ref_available < pipeline->echo_ref_size) {
      pipeline->echo_ref_available++;
    } else {
      // Buffer overflow - advance read position
      pipeline->echo_ref_read_pos = (pipeline->echo_ref_read_pos + 1) % pipeline->echo_ref_size;
    }
  }

  mutex_unlock(&pipeline->mutex);
}

// ============================================================================
// Status and Diagnostics
// ============================================================================

int client_audio_pipeline_jitter_margin(client_audio_pipeline_t *pipeline) {
  if (!pipeline || !pipeline->initialized || !pipeline->jitter) {
    return 0;
  }

  mutex_lock(&pipeline->mutex);

  int available = 0;
  jitter_buffer_ctl(pipeline->jitter, JITTER_BUFFER_GET_AVAILABLE_COUNT, &available);

  // Convert samples to milliseconds
  int margin_ms = available * 1000 / pipeline->config.sample_rate;

  mutex_unlock(&pipeline->mutex);

  return margin_ms;
}

bool client_audio_pipeline_voice_detected(client_audio_pipeline_t *pipeline) {
  if (!pipeline || !pipeline->initialized || !pipeline->preprocess) {
    return false;
  }

  mutex_lock(&pipeline->mutex);

  int vad_state = 0;
  speex_preprocess_ctl(pipeline->preprocess, SPEEX_PREPROCESS_GET_PROB_START, &vad_state);

  mutex_unlock(&pipeline->mutex);

  return vad_state > 50; // > 50% probability of voice
}

void client_audio_pipeline_reset(client_audio_pipeline_t *pipeline) {
  if (!pipeline || !pipeline->initialized) {
    return;
  }

  mutex_lock(&pipeline->mutex);

  // Reset echo canceller
  if (pipeline->echo_state) {
    speex_echo_state_reset(pipeline->echo_state);
  }

  // Reset jitter buffer
  if (pipeline->jitter) {
    jitter_buffer_reset(pipeline->jitter);
    pipeline->jitter_timestamp = 0;
  }

  // Clear echo reference buffer
  memset(pipeline->echo_ref_buffer, 0, (size_t)pipeline->echo_ref_size * sizeof(int16_t));
  pipeline->echo_ref_write_pos = 0;
  pipeline->echo_ref_read_pos = 0;
  pipeline->echo_ref_available = 0;

  // Reset filters
  highpass_filter_reset(&pipeline->highpass);
  lowpass_filter_reset(&pipeline->lowpass);

  mutex_unlock(&pipeline->mutex);

  log_info("Client audio pipeline reset");
}
