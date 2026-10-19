#include <ascii-chat/audio/processor.h>
#include <ascii-chat/audio/mixer.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/time.h>
#include <math.h>
#include <string.h>

struct audio_processor {
  audio_processor_config_t config;
  spectral_processor_t *spectral;
  compressor_t compressor;
  noise_gate_t gate;
  highpass_filter_t highpass;
  lowpass_filter_t lowpass;
  double energy;
  size_t level_samples;
  float gain, target_gain, max_gain, gain_attack, gain_release;
};

audio_processor_config_t audio_processor_default_config(int sample_rate) {
  return (audio_processor_config_t){
      .sample_rate = sample_rate,
      .flags = {.agc = true, .compressor = true, .highpass = true},
      .spectral_enabled = true,
      .spectral = spectral_default_config(sample_rate),
      .agc_level = 16000,
      .agc_max_gain = 35,
      .highpass_hz = 150,
      .lowpass_hz = 8000,
      .comp_threshold_db = -12,
      .comp_ratio = 3,
      .comp_makeup_db = 6,
      .comp_attack_ns = 5 * NS_PER_MS_INT,
      .comp_release_ns = 150 * NS_PER_MS_INT,
      .gate_threshold = 0.08f,
      .gate_hysteresis = 0.3f,
      .gate_attack_ns = 500 * NS_PER_US_INT,
      .gate_release_ns = 30 * NS_PER_MS_INT,
  };
}

void audio_processor_reset(audio_processor_t *p) {
  if (!p)
    return;
  spectral_reset(p->spectral);
  const audio_processor_config_t *c = &p->config;
  compressor_init(&p->compressor, (float)c->sample_rate);
  compressor_set_params(&p->compressor, c->comp_threshold_db, c->comp_ratio, c->comp_attack_ns, c->comp_release_ns,
                        c->comp_makeup_db);
  noise_gate_init(&p->gate, (float)c->sample_rate);
  noise_gate_set_params(&p->gate, c->gate_threshold, c->gate_attack_ns, c->gate_release_ns, c->gate_hysteresis);
  highpass_filter_init(&p->highpass, c->highpass_hz, (float)c->sample_rate);
  lowpass_filter_init(&p->lowpass, c->lowpass_hz, (float)c->sample_rate);
  p->energy = 0;
  p->level_samples = 0;
  p->gain = p->target_gain = 1;
}

asciichat_error_t audio_processor_create(const audio_processor_config_t *c, audio_processor_t **out) {
  if (!out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing audio processor output");
  *out = NULL;
  if (!c || c->sample_rate < 16000 || c->sample_rate > 192000 || c->spectral.sample_rate != c->sample_rate ||
      c->agc_level < 1 || c->agc_level > 32767 || c->agc_max_gain < 0 || c->agc_max_gain > 36 ||
      !isfinite(c->highpass_hz) || c->highpass_hz <= 0 || c->highpass_hz >= c->sample_rate / 2.0f ||
      !isfinite(c->lowpass_hz) || c->lowpass_hz <= 0 || c->lowpass_hz > c->sample_rate / 2.0f ||
      !isfinite(c->comp_ratio) || c->comp_ratio < 1 || c->comp_ratio > 20 || !isfinite(c->comp_threshold_db) ||
      !isfinite(c->comp_makeup_db) || !isfinite(c->gate_threshold) || c->gate_threshold < 0 || c->gate_threshold > 1 ||
      !isfinite(c->gate_hysteresis) || c->gate_hysteresis < 0 || c->gate_hysteresis > 1)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid audio processor configuration");
  audio_processor_t *p = SAFE_CALLOC(1, sizeof(*p), audio_processor_t *);
  p->config = *c;
  p->max_gain = powf(10, c->agc_max_gain / 20.0f);
  p->gain_attack = 1 - powf(0.75f, 100.0f / c->sample_rate);
  p->gain_release = 1 - powf(0.95f, 100.0f / c->sample_rate);
  if (c->spectral_enabled) {
    asciichat_error_t result = spectral_create(&c->spectral, &p->spectral);
    if (result != ASCIICHAT_OK) {
      SAFE_FREE(p);
      return result;
    }
  }
  audio_processor_reset(p);
  *out = p;
  return ASCIICHAT_OK;
}

void audio_processor_set_flags(audio_processor_t *p, audio_processor_flags_t flags) {
  if (p)
    p->config.flags = flags;
}

asciichat_error_t audio_processor_process(audio_processor_t *p, const float *input, float *output, size_t count) {
  if (!p || (!input && count) || (!output && count))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid audio processor buffers");
  if (!count)
    return ASCIICHAT_OK;
  if (p->spectral) {
    asciichat_error_t result = spectral_process(p->spectral, input, output, count);
    if (result != ASCIICHAT_OK)
      return result;
  } else if (input != output) {
    memcpy(output, input, count * sizeof(float));
  }
  const audio_processor_config_t *c = &p->config;
  // Accumulate levels on the sample clock, independent of host callback sizes.
  const size_t level_frame = (size_t)c->sample_rate / 100;
  for (size_t i = 0; i < count; ++i) {
    float sample = isfinite(output[i]) ? output[i] : 0;
    if (c->flags.agc && !(p->spectral && c->spectral.auto_gain)) {
      p->energy += (double)sample * sample;
      if (++p->level_samples == level_frame) {
        float rms = (float)sqrt(p->energy / level_frame);
        float target = fminf(0.5f, fmaxf(0.05f, c->agc_level / 32768.0f));
        p->target_gain = rms > 1e-5f ? fminf(p->max_gain, fmaxf(1, target / rms)) : 1;
        p->energy = 0;
        p->level_samples = 0;
      }
      float coefficient = p->target_gain < p->gain ? p->gain_attack : p->gain_release;
      p->gain += coefficient * (p->target_gain - p->gain);
      sample *= p->gain;
    }
    if (c->flags.highpass)
      sample = highpass_filter_process_sample(&p->highpass, sample);
    if (c->flags.lowpass)
      sample = lowpass_filter_process_sample(&p->lowpass, sample);
    if (c->flags.noise_gate)
      sample = noise_gate_process_sample(&p->gate, sample, fabsf(sample));
    if (c->flags.compressor && !(p->spectral && c->spectral.compressor)) {
      sample *= compressor_process_sample(&p->compressor, sample);
      sample = soft_clip(sample, 0.7f, 3);
    }
    output[i] = sample;
  }
  return ASCIICHAT_OK;
}

void audio_processor_destroy(audio_processor_t *p) {
  if (!p)
    return;
  spectral_destroy(p->spectral);
  p->spectral = NULL;
  SAFE_FREE(p);
}
