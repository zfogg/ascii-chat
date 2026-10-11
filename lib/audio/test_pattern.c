#include <ascii-chat/audio/test_pattern.h>
#include <math.h>

static double tone(double frequency, double time) {
  return sin(6.283185307179586 * frequency * time);
}

void audio_test_pattern(float *output, size_t count, uint64_t position, uint32_t sample_rate) {
  if (!output || !sample_rate)
    return;
  const uint64_t period = (uint64_t)sample_rate * 20;
  for (size_t i = 0; i < count; ++i) {
    double t = (double)((position + i) % period) / sample_rate;
    int section = (int)(t / 4);
    double u = t - section * 4;
    double value = 0;
    double fade = fmin(1.0, fmin(u, 4 - u) / 0.08);
    fade = sin(1.5707963267948966 * fade);
    fade *= fade;
    if (section == 0) {
      // A quiet major arpeggio tests pitch and transients without sharp clicks.
      const double notes[] = {261.625565, 329.627557, 391.995436, 523.251131};
      double beat = u - floor(u);
      double envelope = sin(3.141592653589793 * beat);
      value = 0.16 * envelope * envelope * tone(notes[(int)u], beat);
    } else if (section == 1) {
      // Soft harmonic chord: simultaneous low, middle, and upper frequencies.
      value = 0.06 * tone(130.812783, u) + 0.05 * tone(329.627557, u) + 0.04 * tone(783.990872, u);
    } else if (section == 2) {
      // Smooth logarithmic sweep from 150 Hz to 3 kHz.
      double k = log(20.0) / 4;
      value = 0.12 * sin(6.283185307179586 * 150 * (exp(k * u) - 1) / k);
    } else if (section == 3) {
      // Deterministic broadband noise bursts, with a low level and soft edges.
      uint64_t n = (position + i) % period;
      uint32_t hash = (uint32_t)n * 747796405u + 2891336453u;
      hash = ((hash >> ((hash >> 28) + 4)) ^ hash) * 277803737u;
      hash = (hash >> 22) ^ hash;
      double noise = (double)hash / 2147483648.0 - 1;
      double beat = fmod(u, 1.0);
      double envelope = beat < 0.5 ? sin(6.283185307179586 * beat) : 0;
      value = 0.045 * noise * envelope * envelope;
    } else if (u < 2) {
      // Speech-like amplitude modulation, then two seconds of silence.
      double envelope = 0.5 + 0.5 * tone(4, u);
      double end = fmin(1.0, (2 - u) / 0.08);
      value = end * envelope * (0.08 * tone(180, u) + 0.04 * tone(540, u) + 0.02 * tone(1260, u));
    }
    output[i] = (float)(value * fade);
  }
}
