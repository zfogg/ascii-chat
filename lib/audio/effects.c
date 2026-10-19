#include <ascii-chat/audio/mixer.h>
#include <ascii-chat/util/time.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Utility functions
float db_to_linear(float db) {
  return powf(10.0f, db / 20.0f);
}

float linear_to_db(float linear) {
  return 20.0f * log10f(fmaxf(linear, 1e-12f));
}

float clamp_float(float value, float min, float max) {
  if (value < min) {
    return min;
  }
  if (value > max) {
    return max;
  }
  return value;
}

// Compressor implementation
void compressor_init(compressor_t *comp, float sample_rate) {
  comp->sample_rate = sample_rate;
  comp->envelope = 0.0f;
  comp->gain_lin = 1.0f;

  // Set default parameters with 0dB makeup gain (unity)
  // Soft clip at 0.7 provides 3dB headroom to prevent hard clipping
  // Ducking and crowd scaling naturally reduce volume, no compensation needed
  compressor_set_params(comp, -10.0f, 4.0f, 10 * NS_PER_MS_INT, 100 * NS_PER_MS_INT, 0.0f);
}

void compressor_set_params(compressor_t *comp, float threshold_dB, float ratio, uint64_t attack_ns, uint64_t release_ns,
                           float makeup_dB) {
  comp->threshold_dB = threshold_dB;
  comp->ratio = ratio;
  comp->attack_ns = attack_ns;
  comp->release_ns = release_ns;
  comp->makeup_dB = makeup_dB;
  comp->knee_dB = 2.0f; // Fixed soft knee

  // Calculate time constants
  float attack_tau = (float)attack_ns / NS_PER_SEC;
  float release_tau = (float)release_ns / NS_PER_SEC;
  comp->attack_coeff = expf(-1.0f / (attack_tau * comp->sample_rate + 1e-12f));
  comp->release_coeff = expf(-1.0f / (release_tau * comp->sample_rate + 1e-12f));
}

static float compressor_gain_reduction_db(const compressor_t *comp, float level_dB) {
  float over = level_dB - comp->threshold_dB;
  float knee = comp->knee_dB;

  if (knee > 0.0f) {
    if (over <= -knee * 0.5f)
      return 0.0f;
    if (over >= knee * 0.5f)
      return (1.0f / comp->ratio - 1.0f) * over;
    float x = over + knee * 0.5f;
    return (1.0f / comp->ratio - 1.0f) * (x * x) / (2.0f * knee);
  }
  if (over <= 0.0f) {
    return 0.0f;
  }
  return (1.0f / comp->ratio - 1.0f) * over;
}

float compressor_process_sample(compressor_t *comp, float sidechain) {
  float x = fabsf(sidechain);

  // Update envelope with attack/release
  if (x > comp->envelope)
    comp->envelope = comp->attack_coeff * comp->envelope + (1.0f - comp->attack_coeff) * x;
  else
    comp->envelope = comp->release_coeff * comp->envelope + (1.0f - comp->release_coeff) * x;

  // Calculate gain reduction
  float level_dB = linear_to_db(comp->envelope);
  float gr_dB = compressor_gain_reduction_db(comp, level_dB);
  float target_lin = db_to_linear(gr_dB + comp->makeup_dB);

  // Smooth gain changes
  if (target_lin < comp->gain_lin)
    comp->gain_lin = comp->attack_coeff * comp->gain_lin + (1.0f - comp->attack_coeff) * target_lin;
  else
    comp->gain_lin = comp->release_coeff * comp->gain_lin + (1.0f - comp->release_coeff) * target_lin;

  return comp->gain_lin;
}

/* ============================================================================
 * Noise Gate Implementation
 * ============================================================================
 */

void noise_gate_init(noise_gate_t *gate, float sample_rate) {
  if (!gate)
    return;

  gate->sample_rate = sample_rate;
  gate->envelope = 0.0f;
  gate->gate_open = false;

  // Default parameters
  // Slower attack (10ms) prevents clicking artifacts during gate transitions
  // threshold=0.01, attack=10ms, release=50ms, hysteresis=0.9
  noise_gate_set_params(gate, 0.01f, 10 * NS_PER_MS_INT, 50 * NS_PER_MS_INT, 0.9f);
}

void noise_gate_set_params(noise_gate_t *gate, float threshold, uint64_t attack_ns, uint64_t release_ns,
                           float hysteresis) {
  if (!gate)
    return;

  gate->threshold = threshold;
  gate->attack_ns = attack_ns;
  gate->release_ns = release_ns;
  gate->hysteresis = hysteresis;

  // Calculate coefficients for envelope follower
  // Using exponential moving average: coeff = 1 - exp(-1 / (time_s * sample_rate))
  float attack_s = (float)attack_ns / NS_PER_SEC;
  float release_s = (float)release_ns / NS_PER_SEC;
  gate->attack_coeff = 1.0f - expf(-1.0f / (attack_s * gate->sample_rate + 1e-12f));
  gate->release_coeff = 1.0f - expf(-1.0f / (release_s * gate->sample_rate + 1e-12f));
}

float noise_gate_process_sample(noise_gate_t *gate, float input, float peak_amplitude) {
  if (!gate)
    return input;

  // Determine target state with hysteresis
  float target;
  if (gate->gate_open) {
    // Gate is open - use lower threshold (hysteresis) to close
    target = (peak_amplitude > gate->threshold * gate->hysteresis) ? 1.0f : 0.0f;
  } else {
    // Gate is closed - use normal threshold to open
    target = (peak_amplitude > gate->threshold) ? 1.0f : 0.0f;
  }

  // Update gate state
  gate->gate_open = (target > 0.5f);

  // Update envelope with appropriate coefficient
  float coeff = (target > gate->envelope) ? gate->attack_coeff : gate->release_coeff;
  gate->envelope += coeff * (target - gate->envelope);

  // Apply gate
  return input * gate->envelope;
}

void noise_gate_process_buffer(noise_gate_t *gate, float *buffer, int num_samples) {
  if (!gate || !buffer || num_samples <= 0)
    return;

  // First pass: find peak amplitude
  float peak = 0.0f;
  for (int i = 0; i < num_samples; i++) {
    float abs_sample = fabsf(buffer[i]);
    if (abs_sample > peak) {
      peak = abs_sample;
    }
  }

  // Second pass: apply gate
  for (int i = 0; i < num_samples; i++) {
    buffer[i] = noise_gate_process_sample(gate, buffer[i], peak);
  }
}

bool noise_gate_is_open(const noise_gate_t *gate) {
  return gate ? gate->gate_open : false;
}

/* ============================================================================
 * High-Pass Filter Implementation
 * ============================================================================
 */

void highpass_filter_init(highpass_filter_t *filter, float cutoff_hz, float sample_rate) {
  if (!filter)
    return;

  filter->cutoff_hz = cutoff_hz;
  filter->sample_rate = sample_rate;

  // Calculate filter coefficient
  // alpha = 1 / (1 + 2*pi*fc/fs)
  filter->alpha = 1.0f / (1.0f + 2.0f * M_PI * cutoff_hz / sample_rate);

  highpass_filter_reset(filter);
}

void highpass_filter_reset(highpass_filter_t *filter) {
  if (!filter)
    return;

  filter->prev_input = 0.0f;
  filter->prev_output = 0.0f;
}

float highpass_filter_process_sample(highpass_filter_t *filter, float input) {
  if (!filter)
    return input;

  // First-order high-pass filter
  // y[n] = alpha * (y[n-1] + x[n] - x[n-1])
  float output = filter->alpha * (filter->prev_output + input - filter->prev_input);

  filter->prev_input = input;
  filter->prev_output = output;

  return output;
}

void highpass_filter_process_buffer(highpass_filter_t *filter, float *buffer, int num_samples) {
  if (!filter || !buffer || num_samples <= 0)
    return;

  for (int i = 0; i < num_samples; i++) {
    buffer[i] = highpass_filter_process_sample(filter, buffer[i]);
  }
}

/* ============================================================================
 * Low-Pass Filter Implementation
 * ============================================================================
 */

void lowpass_filter_init(lowpass_filter_t *filter, float cutoff_hz, float sample_rate) {
  if (!filter)
    return;

  filter->cutoff_hz = cutoff_hz;
  filter->sample_rate = sample_rate;

  // Calculate filter coefficient using RC time constant formula
  // alpha = dt / (RC + dt) where RC = 1 / (2 * pi * fc)
  float dt = 1.0f / sample_rate;
  float rc = 1.0f / (2.0f * (float)M_PI * cutoff_hz);
  filter->alpha = dt / (rc + dt);

  lowpass_filter_reset(filter);
}

void lowpass_filter_reset(lowpass_filter_t *filter) {
  if (!filter)
    return;

  filter->prev_output = 0.0f;
}

float lowpass_filter_process_sample(lowpass_filter_t *filter, float input) {
  if (!filter)
    return input;

  // First-order IIR low-pass filter: y[n] = alpha * x[n] + (1 - alpha) * y[n-1]
  float output = filter->alpha * input + (1.0f - filter->alpha) * filter->prev_output;

  filter->prev_output = output;

  return output;
}

void lowpass_filter_process_buffer(lowpass_filter_t *filter, float *buffer, int num_samples) {
  if (!filter || !buffer || num_samples <= 0)
    return;

  for (int i = 0; i < num_samples; i++) {
    buffer[i] = lowpass_filter_process_sample(filter, buffer[i]);
  }
}

/* ============================================================================
 * Soft Clipping Implementation
 * ============================================================================
 */

float soft_clip(float sample, float threshold, float steepness) {
  if (sample > threshold) {
    // Soft clip positive values using tanh curve
    // Maps samples above threshold asymptotically toward 1.0
    return threshold + (1.0f - threshold) * tanhf((sample - threshold) * steepness);
  }
  if (sample < -threshold) {
    // Soft clip negative values symmetrically
    return -threshold + (-1.0f + threshold) * tanhf((sample + threshold) * steepness);
  }
  return sample;
}

void soft_clip_buffer(float *buffer, int num_samples, float threshold, float steepness) {
  if (!buffer || num_samples <= 0)
    return;

  for (int i = 0; i < num_samples; i++) {
    buffer[i] = soft_clip(buffer[i], threshold, steepness);
  }
}

/* ============================================================================
 * Buffer Utility Functions
 * ============================================================================
 */

float smoothstep(float t) {
  if (t <= 0.0f)
    return 0.0f;
  if (t >= 1.0f)
    return 1.0f;
  return t * t * (3.0f - 2.0f * t);
}

int16_t float_to_int16(float sample) {
  // Clamp to [-1, 1] then scale to int16 range
  if (sample > 1.0f)
    sample = 1.0f;
  if (sample < -1.0f)
    sample = -1.0f;
  return (int16_t)(sample * 32767.0f);
}

float int16_to_float(int16_t sample) {
  return (float)sample / 32768.0f;
}

void buffer_float_to_int16(const float *src, int16_t *dst, int count) {
  if (!src || !dst || count <= 0)
    return;
  for (int i = 0; i < count; i++) {
    dst[i] = float_to_int16(src[i]);
  }
}

void buffer_int16_to_float(const int16_t *src, float *dst, int count) {
  if (!src || !dst || count <= 0)
    return;
  for (int i = 0; i < count; i++) {
    dst[i] = int16_to_float(src[i]);
  }
}

float buffer_peak(const float *buffer, int count) {
  if (!buffer || count <= 0)
    return 0.0f;

  float peak = 0.0f;
  for (int i = 0; i < count; i++) {
    float abs_sample = fabsf(buffer[i]);
    if (abs_sample > peak)
      peak = abs_sample;
  }
  return peak;
}

void apply_gain_buffer(float *buffer, int count, float gain) {
  if (!buffer || count <= 0)
    return;

  for (int i = 0; i < count; i++) {
    buffer[i] *= gain;
  }
}

void fade_buffer(float *buffer, int count, float start_gain, float end_gain) {
  if (!buffer || count <= 0)
    return;

  float step = (end_gain - start_gain) / (float)count;
  float gain = start_gain;
  for (int i = 0; i < count; i++) {
    buffer[i] *= gain;
    gain += step;
  }
}

void fade_buffer_smooth(float *buffer, int count, bool fade_in) {
  if (!buffer || count <= 0)
    return;

  for (int i = 0; i < count; i++) {
    float t = (float)i / (float)(count - 1);
    float gain = smoothstep(fade_in ? t : (1.0f - t));
    buffer[i] *= gain;
  }
}

void copy_buffer_with_gain(const float *src, float *dst, int count, float gain) {
  if (!src || !dst || count <= 0)
    return;

  for (int i = 0; i < count; i++) {
    dst[i] = src[i] * gain;
  }
}
