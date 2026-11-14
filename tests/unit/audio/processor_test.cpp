#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/audio/processor.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

TEST_CASE("audio_processor/block_partition_invariance_and_finite_output", "[audio_processor]") {
  float input[48000], reference[48000], split[48000];
  for (size_t i = 0; i < 48000; ++i)
    input[i] = i < 12000 ? 0 : 0.12f * sinf((float)i * 0.0916298f);
  audio_processor_config_t config = audio_processor_default_config(48000);
  audio_processor_t *processor = NULL;
  ASCIICHAT_REQUIRE_EQ(audio_processor_create(&config, &processor), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(audio_processor_process(processor, input, reference, 48000), ASCIICHAT_OK);
  audio_processor_reset(processor);
  const size_t blocks[] = {128, 960, 1, 13, 127};
  size_t position = 0, block = 0;
  while (position < 48000) {
    size_t count = blocks[block++ % 5];
    if (count > 48000 - position)
      count = 48000 - position;
    ASCIICHAT_REQUIRE_EQ(audio_processor_process(processor, input + position, split + position, count), ASCIICHAT_OK);
    position += count;
  }
  ASCIICHAT_REQUIRE_EQ(memcmp(reference, split, sizeof(reference)), 0);
  double energy = 0;
  for (size_t i = 0; i < 48000; ++i) {
    ASCIICHAT_REQUIRE(isfinite(split[i]) && fabsf(split[i]) <= 1);
    energy += (double)split[i] * split[i];
  }
  ASCIICHAT_REQUIRE(energy > 1);
  audio_processor_destroy(processor);
}

TEST_CASE("audio_processor/disabled_processing_preserves_in_place_samples", "[audio_processor]") {
  float input[48000], split[48000];
  for (size_t i = 0; i < 48000; ++i)
    input[i] = 0.12f * sinf((float)i * 0.0916298f);
  audio_processor_config_t config = audio_processor_default_config(48000);
  audio_processor_t *processor = NULL;
  config.spectral_enabled = false;
  config.flags = (audio_processor_flags_t){0};
  ASCIICHAT_REQUIRE_EQ(audio_processor_create(&config, &processor), ASCIICHAT_OK);
  memcpy(split, input, sizeof(input));
  ASCIICHAT_REQUIRE_EQ(audio_processor_process(processor, split, split, 48000), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(memcmp(input, split, sizeof(input)), 0);
  audio_processor_destroy(processor);
}
