/**
 * @file echo_cancel.c
 * @brief Acoustic Echo Cancellation implementation using Speex DSP
 */

#include "echo_cancel.h"
#include "common.h"
#include "logging.h"
#include "platform/abstraction.h"

#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>
#include <string.h>

// AEC state
static SpeexEchoState *g_echo_state = NULL;
static SpeexPreprocessState *g_preprocess_state = NULL;
static mutex_t g_aec_mutex;
static bool g_aec_initialized = false;

// Configuration
static int g_frame_size = 0;
static int g_sample_rate = 0;

// Ring buffer for playback reference (handles timing differences)
#define AEC_RING_BUFFER_SIZE (48000 / 2) // 500ms at 48kHz
static int16_t g_playback_ring[AEC_RING_BUFFER_SIZE];
static int g_playback_write_pos = 0;
static int g_playback_read_pos = 0;
static int g_playback_available = 0;

// Conversion buffers (speex uses int16, we use float)
static int16_t *g_input_i16 = NULL;
static int16_t *g_output_i16 = NULL;
static int16_t *g_playback_i16 = NULL;

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

bool echo_cancel_init(int sample_rate, int frame_size, int filter_length_ms) {
  if (g_aec_initialized) {
    log_warn("AEC already initialized");
    return true;
  }

  // Calculate filter length in samples
  int filter_length = (sample_rate * filter_length_ms) / 1000;

  // Create echo canceller state
  g_echo_state = speex_echo_state_init(frame_size, filter_length);
  if (!g_echo_state) {
    log_error("Failed to create Speex echo canceller");
    return false;
  }

  // Set sample rate
  speex_echo_ctl(g_echo_state, SPEEX_ECHO_SET_SAMPLING_RATE, &sample_rate);

  // Create preprocessor for additional noise suppression
  g_preprocess_state = speex_preprocess_state_init(frame_size, sample_rate);
  if (g_preprocess_state) {
    // Link preprocessor to echo canceller
    speex_preprocess_ctl(g_preprocess_state, SPEEX_PREPROCESS_SET_ECHO_STATE, g_echo_state);

    // Enable noise suppression
    int denoise = 1;
    speex_preprocess_ctl(g_preprocess_state, SPEEX_PREPROCESS_SET_DENOISE, &denoise);

    // Set noise suppression level (-25 dB is moderate)
    int noise_suppress = -25;
    speex_preprocess_ctl(g_preprocess_state, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &noise_suppress);
  }

  // Allocate conversion buffers
  g_input_i16 = SAFE_MALLOC((size_t)frame_size * sizeof(int16_t), int16_t *);
  g_output_i16 = SAFE_MALLOC((size_t)frame_size * sizeof(int16_t), int16_t *);
  g_playback_i16 = SAFE_MALLOC((size_t)frame_size * sizeof(int16_t), int16_t *);

  if (!g_input_i16 || !g_output_i16 || !g_playback_i16) {
    log_error("Failed to allocate AEC buffers");
    echo_cancel_destroy();
    return false;
  }

  // Initialize ring buffer
  memset(g_playback_ring, 0, sizeof(g_playback_ring));
  g_playback_write_pos = 0;
  g_playback_read_pos = 0;
  g_playback_available = 0;

  // Initialize mutex
  if (mutex_init(&g_aec_mutex) != 0) {
    log_error("Failed to initialize AEC mutex");
    echo_cancel_destroy();
    return false;
  }

  g_frame_size = frame_size;
  g_sample_rate = sample_rate;
  g_aec_initialized = true;

  log_info("Acoustic Echo Cancellation initialized: sample_rate=%d, frame_size=%d, filter=%dms", sample_rate,
           frame_size, filter_length_ms);

  return true;
}

void echo_cancel_playback(const float *samples, int num_samples) {
  if (!g_aec_initialized || !samples || num_samples <= 0) {
    return;
  }

  mutex_lock(&g_aec_mutex);

  // Convert float to int16 and add to ring buffer
  for (int i = 0; i < num_samples; i++) {
    float s = samples[i] * 32767.0f;
    if (s > 32767.0f)
      s = 32767.0f;
    if (s < -32768.0f)
      s = -32768.0f;

    g_playback_ring[g_playback_write_pos] = (int16_t)s;
    g_playback_write_pos = (g_playback_write_pos + 1) % AEC_RING_BUFFER_SIZE;

    if (g_playback_available < AEC_RING_BUFFER_SIZE) {
      g_playback_available++;
    } else {
      // Buffer overflow - advance read position
      g_playback_read_pos = (g_playback_read_pos + 1) % AEC_RING_BUFFER_SIZE;
    }
  }

  mutex_unlock(&g_aec_mutex);
}

void echo_cancel_capture(const float *input, float *output, int num_samples) {
  if (!g_aec_initialized || !input || !output || num_samples <= 0) {
    // Pass through if AEC not available
    if (input && output && num_samples > 0) {
      memcpy(output, input, (size_t)num_samples * sizeof(float));
    }
    return;
  }

  mutex_lock(&g_aec_mutex);

  // Process in frame_size chunks
  int processed = 0;
  while (processed < num_samples) {
    int to_process = num_samples - processed;
    if (to_process > g_frame_size) {
      to_process = g_frame_size;
    }

    // Convert input to int16
    float_to_int16(input + processed, g_input_i16, to_process);

    // Get playback reference from ring buffer
    if (g_playback_available >= to_process) {
      for (int i = 0; i < to_process; i++) {
        g_playback_i16[i] = g_playback_ring[g_playback_read_pos];
        g_playback_read_pos = (g_playback_read_pos + 1) % AEC_RING_BUFFER_SIZE;
        g_playback_available--;
      }
    } else {
      // Not enough playback data - use silence
      memset(g_playback_i16, 0, (size_t)to_process * sizeof(int16_t));
    }

    // Run echo cancellation
    speex_echo_cancellation(g_echo_state, g_input_i16, g_playback_i16, g_output_i16);

    // Run preprocessor for additional cleanup
    if (g_preprocess_state) {
      speex_preprocess_run(g_preprocess_state, g_output_i16);
    }

    // Convert back to float
    int16_to_float(g_output_i16, output + processed, to_process);

    processed += to_process;
  }

  mutex_unlock(&g_aec_mutex);
}

bool echo_cancel_is_active(void) {
  return g_aec_initialized;
}

void echo_cancel_reset(void) {
  if (!g_aec_initialized) {
    return;
  }

  mutex_lock(&g_aec_mutex);

  if (g_echo_state) {
    speex_echo_state_reset(g_echo_state);
  }

  // Clear ring buffer
  memset(g_playback_ring, 0, sizeof(g_playback_ring));
  g_playback_write_pos = 0;
  g_playback_read_pos = 0;
  g_playback_available = 0;

  mutex_unlock(&g_aec_mutex);

  log_info("AEC state reset");
}

void echo_cancel_destroy(void) {
  if (!g_aec_initialized) {
    return;
  }

  mutex_lock(&g_aec_mutex);

  if (g_preprocess_state) {
    speex_preprocess_state_destroy(g_preprocess_state);
    g_preprocess_state = NULL;
  }

  if (g_echo_state) {
    speex_echo_state_destroy(g_echo_state);
    g_echo_state = NULL;
  }

  if (g_input_i16) {
    SAFE_FREE(g_input_i16);
  }
  if (g_output_i16) {
    SAFE_FREE(g_output_i16);
  }
  if (g_playback_i16) {
    SAFE_FREE(g_playback_i16);
  }

  g_aec_initialized = false;

  mutex_unlock(&g_aec_mutex);
  mutex_destroy(&g_aec_mutex);

  log_info("Acoustic Echo Cancellation destroyed");
}
