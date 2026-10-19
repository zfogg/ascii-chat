/** Standalone check against the compiled library, including sanitizer builds. */
#include <ascii-chat/audio/processor.h>
#include <assert.h>
#include <math.h>
#include <string.h>

int main(void) {
  float input[48000], reference[48000], split[48000];
  for (size_t i = 0; i < 48000; ++i)
    input[i] = i < 12000 ? 0 : 0.12f * sinf((float)i * 0.0916298f);
  audio_processor_config_t config = audio_processor_default_config(48000);
  audio_processor_t *processor = NULL;
  assert(audio_processor_create(&config, &processor) == ASCIICHAT_OK);
  assert(audio_processor_process(processor, input, reference, 48000) == ASCIICHAT_OK);
  audio_processor_reset(processor);
  const size_t blocks[] = {128, 960, 1, 13, 127};
  size_t position = 0, block = 0;
  while (position < 48000) {
    size_t count = blocks[block++ % 5];
    if (count > 48000 - position)
      count = 48000 - position;
    assert(audio_processor_process(processor, input + position, split + position, count) == ASCIICHAT_OK);
    position += count;
  }
  assert(memcmp(reference, split, sizeof(reference)) == 0);
  double energy = 0;
  for (size_t i = 0; i < 48000; ++i) {
    assert(isfinite(split[i]) && fabsf(split[i]) <= 1);
    energy += (double)split[i] * split[i];
  }
  assert(energy > 1);
  audio_processor_destroy(processor);
  config.spectral_enabled = false;
  config.flags = (audio_processor_flags_t){0};
  assert(audio_processor_create(&config, &processor) == ASCIICHAT_OK);
  memcpy(split, input, sizeof(input));
  assert(audio_processor_process(processor, split, split, 48000) == ASCIICHAT_OK);
  assert(memcmp(input, split, sizeof(input)) == 0);
  audio_processor_destroy(processor);
  return 0;
}
