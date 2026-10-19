/** Standalone check against the compiled library, including sanitizer builds. */
#include <ascii-chat/audio/processor.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/** Evaluate checks in every build, including processor calls with side effects. */
#define CHECK(condition)                                                                                               \
  do {                                                                                                                 \
    if (!(condition)) {                                                                                                \
      log_error("%s:%d: CHECK failed: %s", __FILE__, __LINE__, #condition);                                            \
      return EXIT_FAILURE;                                                                                             \
    }                                                                                                                  \
  } while (0)

int main(void) {
  float input[48000], reference[48000], split[48000];
  for (size_t i = 0; i < 48000; ++i)
    input[i] = i < 12000 ? 0 : 0.12f * sinf((float)i * 0.0916298f);
  audio_processor_config_t config = audio_processor_default_config(48000);
  audio_processor_t *processor = NULL;
  CHECK(audio_processor_create(&config, &processor) == ASCIICHAT_OK);
  CHECK(audio_processor_process(processor, input, reference, 48000) == ASCIICHAT_OK);
  audio_processor_reset(processor);
  const size_t blocks[] = {128, 960, 1, 13, 127};
  size_t position = 0, block = 0;
  while (position < 48000) {
    size_t count = blocks[block++ % 5];
    if (count > 48000 - position)
      count = 48000 - position;
    CHECK(audio_processor_process(processor, input + position, split + position, count) == ASCIICHAT_OK);
    position += count;
  }
  CHECK(memcmp(reference, split, sizeof(reference)) == 0);
  double energy = 0;
  for (size_t i = 0; i < 48000; ++i) {
    CHECK(isfinite(split[i]) && fabsf(split[i]) <= 1);
    energy += (double)split[i] * split[i];
  }
  CHECK(energy > 1);
  audio_processor_destroy(processor);
  config.spectral_enabled = false;
  config.flags = (audio_processor_flags_t){0};
  CHECK(audio_processor_create(&config, &processor) == ASCIICHAT_OK);
  memcpy(split, input, sizeof(input));
  CHECK(audio_processor_process(processor, split, split, 48000) == ASCIICHAT_OK);
  CHECK(memcmp(input, split, sizeof(input)) == 0);
  audio_processor_destroy(processor);
  return 0;
}
