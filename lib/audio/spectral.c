#include <ascii-chat/audio/spectral.h>
#include <ascii-chat/common.h>
#include <ascii-chat/platform/init.h>
#include <fftw3.h>
#include <math.h>
#include <string.h>

#define SPECTRAL_MAX_SIZE 2048
#define SPECTRAL_MAX_BINS (SPECTRAL_MAX_SIZE / 2 + 1)

struct spectral_processor {
  spectral_config_t config;
  fftw_plan forward, inverse;
  double time[SPECTRAL_MAX_SIZE], restored[SPECTRAL_MAX_SIZE];
  fftw_complex frequency[SPECTRAL_MAX_BINS];
  float input[SPECTRAL_MAX_SIZE], overlap[SPECTRAL_MAX_SIZE], ready[SPECTRAL_MAX_SIZE / 4];
  float window[SPECTRAL_MAX_SIZE], magnitude[SPECTRAL_MAX_BINS];
  float noise[SPECTRAL_MAX_BINS], smoothed[SPECTRAL_MAX_BINS], gains[SPECTRAL_MAX_BINS];
  float band_gain[4], band_average[4], eq_gain[4];
  double previous_phase[SPECTRAL_MAX_BINS], synthesis_phase[SPECTRAL_MAX_BINS];
  double shifted_magnitude[SPECTRAL_MAX_BINS], shifted_frequency[SPECTRAL_MAX_BINS];
  float pitch_hz, auto_gain;
  int pending, cursor;
  size_t frames;
};

// FFTW execution is independent per instance; planning and destruction share state.
static static_mutex_t planner_lock = STATIC_MUTEX_INIT;

spectral_config_t spectral_default_config(int sample_rate) {
  return (spectral_config_t){.sample_rate = sample_rate,
                             .fft_size = 1024,
                             .noise_gate = true,
                             .reduction_db = 18.0f,
                             .crossovers = {250, 2000, 8000},
                             .thresholds_db = {-18, -18, -18, -18},
                             .ratios = {2, 3, 3, 2}};
}

void spectral_reset(spectral_processor_t *p) {
  if (!p)
    return;
  memset(p->input, 0, sizeof(p->input));
  memset(p->overlap, 0, sizeof(p->overlap));
  memset(p->ready, 0, sizeof(p->ready));
  memset(p->noise, 0, sizeof(p->noise));
  memset(p->smoothed, 0, sizeof(p->smoothed));
  memset(p->magnitude, 0, sizeof(p->magnitude));
  memset(p->band_average, 0, sizeof(p->band_average));
  for (int i = 0; i < SPECTRAL_MAX_BINS; ++i)
    p->gains[i] = 1;
  for (int i = 0; i < 4; ++i)
    p->band_gain[i] = p->eq_gain[i] = 1;
  p->pending = p->cursor = 0;
  memset(p->previous_phase, 0, sizeof(p->previous_phase));
  memset(p->synthesis_phase, 0, sizeof(p->synthesis_phase));
  p->pitch_hz = 0;
  p->auto_gain = 1;
  p->frames = 0;
}

asciichat_error_t spectral_create(const spectral_config_t *c, spectral_processor_t **out) {
  if (!out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing spectral output pointer");
  *out = NULL;
  if (!c || c->sample_rate < 16000 || c->sample_rate > 192000 ||
      (c->fft_size != 512 && c->fft_size != 1024 && c->fft_size != 2048) || !isfinite(c->reduction_db) ||
      c->reduction_db < 0 || c->reduction_db > 60)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid spectral configuration");
  for (int b = 0; b < 4; ++b) {
    if (!isfinite(c->ratios[b]) || c->ratios[b] < 1 || c->ratios[b] > 20 || !isfinite(c->thresholds_db[b]) ||
        c->thresholds_db[b] > 0 || c->thresholds_db[b] < -90)
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid spectral compressor band");
    if (b < 3 && (!isfinite(c->crossovers[b]) || c->crossovers[b] <= 0 || c->crossovers[b] > c->sample_rate / 2.0f ||
                  (b && c->crossovers[b] <= c->crossovers[b - 1])))
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid spectral crossover");
  }
  spectral_processor_t *p = SAFE_CALLOC(1, sizeof(*p), spectral_processor_t *);
  if (!p)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate spectral processor");
  p->config = *c;
  static_mutex_lock(&planner_lock);
  p->forward = fftw_plan_dft_r2c_1d(c->fft_size, p->time, p->frequency, FFTW_ESTIMATE);
  p->inverse = fftw_plan_dft_c2r_1d(c->fft_size, p->frequency, p->restored, FFTW_ESTIMATE);
  static_mutex_unlock(&planner_lock);
  if (!p->forward || !p->inverse) {
    spectral_destroy(p);
    return SET_ERRNO(ERROR_MEMORY, "Cannot create FFTW plans");
  }
  for (int i = 0; i < c->fft_size; ++i)
    p->window[i] = (float)(0.5 - 0.5 * cos(6.283185307179586 * i / c->fft_size));
  spectral_reset(p);
  *out = p;
  return ASCIICHAT_OK;
}

void spectral_destroy(spectral_processor_t *p) {
  if (!p)
    return;
  static_mutex_lock(&planner_lock);
  if (p->forward)
    fftw_destroy_plan(p->forward);
  if (p->inverse)
    fftw_destroy_plan(p->inverse);
  static_mutex_unlock(&planner_lock);
  SAFE_FREE(p);
}

static int band_for(const spectral_processor_t *p, int bin) {
  float hz = (float)bin * p->config.sample_rate / p->config.fft_size;
  int band = 0;
  while (band < 3 && hz >= p->config.crossovers[band])
    ++band;
  return band;
}

// Normalized autocorrelation with a two-period minimum observation window.
static float detect_pitch(spectral_processor_t *p) {
  int n = p->config.fft_size;
  int first = p->config.sample_rate / 500;
  int last = n / 2;
  if (last > p->config.sample_rate / 70)
    last = p->config.sample_rate / 70;
  float correlation[SPECTRAL_MAX_SIZE / 2 + 1] = {0};
  double mean = 0, power = 0;
  for (int i = 0; i < n; ++i)
    mean += p->input[i] / (double)n;
  for (int i = 0; i < n; ++i)
    power += (p->input[i] - mean) * (p->input[i] - mean);
  if (power / n < 1e-6)
    return 0;
  float best = 0;
  for (int lag = first; lag <= last; ++lag) {
    double cross = 0, a = 0, b = 0;
    for (int i = 0; i < n - lag; ++i) {
      double x = p->input[i] - mean, y = p->input[i + lag] - mean;
      cross += x * y;
      a += x * x;
      b += y * y;
    }
    correlation[lag] = (float)(cross / sqrt(a * b + 1e-20));
    if (correlation[lag] > best)
      best = correlation[lag];
  }
  if (best < 0.85f)
    return 0;
  for (int lag = first + 1; lag < last; ++lag) {
    float value = correlation[lag];
    if (value > 0.95f * best && value > correlation[lag - 1] && value >= correlation[lag + 1]) {
      float delta = 0.5f * (correlation[lag - 1] - correlation[lag + 1]) /
                    (correlation[lag - 1] - 2 * value + correlation[lag + 1]);
      return p->config.sample_rate / (lag + delta);
    }
  }
  return 0;
}

static void correct_pitch(spectral_processor_t *p) {
  const double two_pi = 6.283185307179586;
  int n = p->config.fft_size, hop = n / 4;
  double factor = 1;
  if (p->pitch_hz > 0) {
    double note = round(69 + 12 * log2(p->pitch_hz / 440.0));
    factor = 440 * pow(2, (note - 69) / 12) / p->pitch_hz;
  }
  memset(p->shifted_magnitude, 0, sizeof(p->shifted_magnitude));
  memset(p->shifted_frequency, 0, sizeof(p->shifted_frequency));
  for (int k = 0; k <= n / 2; ++k) {
    double phase = atan2(p->frequency[k][1], p->frequency[k][0]);
    double expected = two_pi * k * hop / n;
    double delta = remainder(phase - p->previous_phase[k] - expected, two_pi);
    p->previous_phase[k] = phase;
    double frequency = (expected + delta) / hop;
    int target = (int)round(k * factor);
    if (target <= n / 2) {
      double magnitude = hypot(p->frequency[k][0], p->frequency[k][1]);
      p->shifted_magnitude[target] += magnitude;
      p->shifted_frequency[target] += magnitude * frequency * factor;
    }
  }
  for (int k = 0; k <= n / 2; ++k) {
    double magnitude = p->shifted_magnitude[k];
    double frequency = magnitude > 1e-15 ? p->shifted_frequency[k] / magnitude : two_pi * k / n;
    p->synthesis_phase[k] = remainder(p->synthesis_phase[k] + frequency * hop, two_pi);
    p->frequency[k][0] = magnitude * cos(p->synthesis_phase[k]);
    p->frequency[k][1] = (k == 0 || k == n / 2) ? 0 : magnitude * sin(p->synthesis_phase[k]);
  }
}

static void process_frame(spectral_processor_t *p) {
  int n = p->config.fft_size, hop = n / 4;
  float dt = (float)hop / p->config.sample_rate;
  for (int i = 0; i < n; ++i)
    p->time[i] = p->input[i] * p->window[i];
  fftw_execute(p->forward);
  float energy[4] = {0};
  for (int k = 0; k <= n / 2; ++k) {
    double re = p->frequency[k][0], im = p->frequency[k][1];
    float power = (float)(re * re + im * im);
    p->magnitude[k] = sqrtf(power) * ((k == 0 || k == n / 2) ? 2.0f : 4.0f) / n;
    // Parseval normalization includes the periodic Hann window's mean square (3/8).
    energy[band_for(p, k)] += power * ((k == 0 || k == n / 2) ? 1 : 2) / (n * n * 0.375f);
    if (p->config.noise_gate) {
      float level = p->magnitude[k];
      // Learn only after a complete input window. Falling noise estimates track
      // quickly; rising estimates move slowly so speech is not learned as noise.
      p->smoothed[k] += (1 - expf(-dt / 0.05f)) * (level - p->smoothed[k]);
      size_t warmup = 3 + (size_t)(0.25f / dt);
      if (p->frames >= 3 && p->frames < warmup)
        p->noise[k] += (level - p->noise[k]) / (float)(p->frames - 2);
      if (p->noise[k] < 1e-9f && level > 1e-9f)
        p->noise[k] = level;
      if (p->frames >= warmup) {
        float tau = p->smoothed[k] < p->noise[k] ? 0.5f : 8.0f;
        p->noise[k] += (1 - expf(-dt / tau)) * (p->smoothed[k] - p->noise[k]);
        float ratio = level / fmaxf(p->noise[k], 1e-10f);
        float floor = powf(10.0f, -p->config.reduction_db / 20.0f);
        float target = floor + (1 - floor) * fminf(1, fmaxf(0, (ratio - 1.5f) / 2.5f));
        float coefficient = 1 - expf(-dt / (target > p->gains[k] ? 0.005f : 0.08f));
        p->gains[k] += coefficient * (target - p->gains[k]);
      }
    }
  }
  float total = energy[0] + energy[1] + energy[2] + energy[3];
  if (p->config.pitch_correct)
    p->pitch_hz = detect_pitch(p);
  if (p->config.auto_gain) {
    float desired = total > 1e-5f ? fminf(4, fmaxf(0.25f, 0.15f / sqrtf(total))) : 1;
    p->auto_gain += (1 - expf(-dt / (desired < p->auto_gain ? 0.01f : 0.5f))) * (desired - p->auto_gain);
  }
  for (int b = 0; b < 4; ++b) {
    float db = 10 * log10f(fmaxf(energy[b], 1e-12f));
    float reduction = fmaxf(0, db - p->config.thresholds_db[b]) * (1 - 1 / p->config.ratios[b]);
    float target = powf(10, -reduction / 20);
    p->band_gain[b] += (1 - expf(-dt / (target < p->band_gain[b] ? 0.01f : 0.15f))) * (target - p->band_gain[b]);
    // Bounded spectral balancing; no claim of measured room-response inversion.
    if (total > 1e-6f) {
      p->band_average[b] += (1 - expf(-dt / 3)) * (energy[b] - p->band_average[b]);
      float average = 0;
      for (int j = 0; j < 4; ++j)
        average += p->band_average[j] / 4;
      float eq = sqrtf(average / fmaxf(p->band_average[b], 1e-10f));
      eq = fminf(1.41254f, fmaxf(0.70795f, eq));
      p->eq_gain[b] += (1 - expf(-dt / 1)) * (eq - p->eq_gain[b]);
    }
  }
  for (int k = 0; k <= n / 2; ++k) {
    int b = band_for(p, k);
    float gain = p->config.noise_gate ? p->gains[k] : 1;
    if (p->config.compressor)
      gain *= p->band_gain[b];
    if (p->config.adaptive_eq)
      gain *= p->eq_gain[b];
    if (p->config.auto_gain)
      gain *= p->auto_gain;
    p->frequency[k][0] *= gain;
    p->frequency[k][1] *= gain;
  }
  if (p->config.pitch_correct)
    correct_pitch(p);
  fftw_execute(p->inverse);
  // Four overlapping periodic Hann windows have a constant squared sum of 1.5.
  for (int i = 0; i < n; ++i)
    p->overlap[i] += (float)(p->restored[i] * p->window[i] / (n * 1.5));
  memcpy(p->ready, p->overlap, (size_t)hop * sizeof(float));
  memmove(p->overlap, p->overlap + hop, (size_t)(n - hop) * sizeof(float));
  memset(p->overlap + n - hop, 0, (size_t)hop * sizeof(float));
  memmove(p->input, p->input + hop, (size_t)(n - hop) * sizeof(float));
  p->frames++;
}

asciichat_error_t spectral_process(spectral_processor_t *p, const float *input, float *output, size_t count) {
  if (!p || (!input && count) || (!output && count))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid spectral buffer");
  int n = p->config.fft_size, hop = n / 4;
  for (size_t i = 0; i < count; ++i) {
    float sample = input[i];
    output[i] = p->ready[p->cursor++];
    p->input[n - hop + p->pending++] = isfinite(sample) ? sample : 0;
    if (p->pending == hop) {
      process_frame(p);
      p->pending = p->cursor = 0;
    }
  }
  return ASCIICHAT_OK;
}

const float *spectral_spectrum(const spectral_processor_t *p, size_t *bins) {
  if (bins)
    *bins = p ? (size_t)p->config.fft_size / 2 + 1 : 0;
  return p ? p->magnitude : NULL;
}

float spectral_pitch_hz(const spectral_processor_t *p) {
  return p ? p->pitch_hz : 0;
}
