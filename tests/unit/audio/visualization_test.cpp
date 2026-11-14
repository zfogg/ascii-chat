#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/audio/visualization.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/video/anim/waveform.h>
#include <ascii-chat/video/anim/fft.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <libplatform/compiler.h>
#include <libplatform/system.h>
#include <libplatform/terminal.h>
#include <ascii-chat/util/time.h>
#include <math.h>
#include <string.h>

static void enable_visualization(void) {
  options_t options = *options_get();
  options.waveform = true;
  options.fft = true;
  ASCIICHAT_REQUIRE_EQ(options_state_set(&options), ASCIICHAT_OK);
}
TEST_CASE("audio_visualization/fft_refreshes_on_a_bounded_cadence", "[audio_visualization]") {
  float samples[48000] = {0};
  audio_visualization_submit(AUDIO_VISUALIZATION_SOURCE_MIC, samples, 48000);
  char *quiet = audio_visualization_render_fft(16, 8, AUDIO_VISUALIZATION_SOURCE_MIC, false, 0, false, false);
  ASCIICHAT_REQUIRE_NOT_NULL(quiet);
  uint64_t quiet_finished = time_get_ns();
  for (size_t i = 0; i < 48000; ++i)
    samples[i] = (float)sin(2 * 3.141592653589793 * i / 48.0);
  audio_visualization_submit(AUDIO_VISUALIZATION_SOURCE_MIC, samples, 48000);
  char *cached = audio_visualization_render_fft(16, 8, AUDIO_VISUALIZATION_SOURCE_MIC, false, 0, false, false);
  ASCIICHAT_REQUIRE_NOT_NULL(cached);
  if (time_get_ns() - quiet_finished < 50 * NS_PER_MS_INT)
    ASCIICHAT_CHECK_STR_EQ(quiet, cached);
  platform_sleep_ms(150);
  char *updated = audio_visualization_render_fft(16, 8, AUDIO_VISUALIZATION_SOURCE_MIC, false, 0, false, false);
  ASCIICHAT_REQUIRE_NOT_NULL(updated);
  ASCIICHAT_CHECK_STR_NE(quiet, updated);
  SAFE_FREE(quiet);
  SAFE_FREE(cached);
  SAFE_FREE(updated);
}

TEST_CASE("audio_visualization/clips_invalid_samples_and_mixes_sources_without_aliasing", "[audio_visualization]") {
  float microphone[] = {NAN, INFINITY, -INFINITY, 2.0f, -2.0f, 0.25f};
  float media[] = {0, 0, 0, 0, 0, 0.5f}, remote[] = {0, 0, 0, 0, 0, 0.5f};
  audio_visualization_submit(AUDIO_VISUALIZATION_SOURCE_MIC, microphone, 6);
  audio_visualization_submit(AUDIO_VISUALIZATION_SOURCE_MEDIA, media, 6);
  audio_visualization_submit(AUDIO_VISUALIZATION_SOURCE_REMOTE, remote, 6);
  float output[6];
  audio_visualization_read(AUDIO_VISUALIZATION_SOURCE_MIC, output, 6);
  float expected[] = {0, 0, 0, 1, -1, 0.25f};
  for (size_t i = 0; i < 6; ++i)
    ASCIICHAT_REQUIRE_FLOAT_EQ(output[i], expected[i], 0.00001);
  audio_visualization_read(AUDIO_VISUALIZATION_SOURCE_LOCAL_MIX, output, 6);
  ASCIICHAT_REQUIRE_FLOAT_EQ(output[5], 0.75, 0.00001);
  audio_visualization_read(AUDIO_VISUALIZATION_SOURCE_MIX, output, 6);
  ASCIICHAT_REQUIRE_FLOAT_EQ(output[5], 1.0, 0.00001);
  audio_visualization_read((audio_visualization_source_t)-1, output, 6);
  for (size_t i = 0; i < 6; ++i)
    ASCIICHAT_REQUIRE_FLOAT_EQ(output[i], 0, 0.00001);
  audio_visualization_read(AUDIO_VISUALIZATION_SOURCE_MIC, NULL, 6);
  audio_visualization_submit(AUDIO_VISUALIZATION_SOURCE_MIC, NULL, 6);
  audio_visualization_submit(AUDIO_VISUALIZATION_SOURCE_MIX, media, 6);
  ASCIICHAT_REQUIRE_FLOAT_EQ(microphone[3], 2.0, 0.00001);
}

TEST_CASE("audio_visualization/ring_wrap_preserves_latest_samples_and_zero_pads_older_history", "[audio_visualization]") {
  const size_t total = 48010;
  float *input = SAFE_MALLOC(total * sizeof(float), float *);
  for (size_t i = 0; i < total; ++i)
    input[i] = (float)(i % 100) / 100.0f;
  audio_visualization_submit(AUDIO_VISUALIZATION_SOURCE_MIC, input, total);
  float *output = SAFE_MALLOC(total * sizeof(float), float *);
  audio_visualization_read(AUDIO_VISUALIZATION_SOURCE_MIC, output, total);
  for (size_t i = 0; i < 10; ++i)
    ASCIICHAT_REQUIRE_FLOAT_EQ(output[i], 0, 0.00001);
  for (size_t i = 10; i < total; ++i)
    ASCIICHAT_REQUIRE_FLOAT_EQ(output[i], input[i], 0.00001);
  SAFE_FREE(output);
  SAFE_FREE(input);
}

TEST_CASE("audio_visualization/waveform_and_fft_render_bounded_plain_and_colored_frames", "[audio_visualization]") {
  float *samples = SAFE_MALLOC(48000 * sizeof(float), float *);
  for (size_t i = 0; i < 48000; ++i)
    samples[i] = 0.75f * sinf((float)i * 6.28318530718f * 440.0f / 48000.0f);
  audio_visualization_submit(AUDIO_VISUALIZATION_SOURCE_MIC, samples, 48000);
  SAFE_FREE(samples);
  const terminal_render_mode_t modes[] = {RENDER_MODE_FOREGROUND, RENDER_MODE_BACKGROUND, RENDER_MODE_HALF_BLOCK};
  for (size_t i = 0; i < 3; ++i)
    for (int color = 0; color < 3; ++color) {
      options_t options = *options_get();
      options.render_mode = modes[i];
      ASCIICHAT_REQUIRE_EQ(options_state_set(&options), ASCIICHAT_OK);
      char *wave =
          audio_visualization_render_waveform(16, 8, AUDIO_VISUALIZATION_SOURCE_MIC, color != 0, color, true, true);
      char *fft =
          audio_visualization_render_fft(16, 8, AUDIO_VISUALIZATION_SOURCE_MIC, color != 0, color, false, false);
      ASCIICHAT_REQUIRE_NOT_NULL(wave);
      ASCIICHAT_REQUIRE_NOT_NULL(fft);
      ASCIICHAT_REQUIRE_GT(strlen(wave), 0);
      ASCIICHAT_REQUIRE_GT(strlen(fft), 0);
      size_t rows = 0;
      for (const char *p = wave; *p; ++p)
        rows += *p == '\n';
      ASCIICHAT_REQUIRE_EQ(rows, 8);
      if (color == 0) {
        ASCIICHAT_REQUIRE_NULL(strchr(wave, '\033'));
        ASCIICHAT_REQUIRE_NULL(strchr(fft, '\033'));
      } else {
        ASCIICHAT_REQUIRE_NOT_NULL(strchr(wave, '\033'));
        ASCIICHAT_REQUIRE_NOT_NULL(strchr(fft, '\033'));
      }
      SAFE_FREE(wave);
      SAFE_FREE(fft);
    }
  ASCIICHAT_REQUIRE_NULL(audio_visualization_render_waveform(7, 8, AUDIO_VISUALIZATION_SOURCE_MIC, false, 0, false, false));
  ASCIICHAT_REQUIRE_NULL(audio_visualization_render_waveform(513, 8, AUDIO_VISUALIZATION_SOURCE_MIC, false, 0, false, false));
  ASCIICHAT_REQUIRE_NULL(audio_visualization_render_fft(16, 3, AUDIO_VISUALIZATION_SOURCE_MIC, false, 0, false, false));
  ASCIICHAT_REQUIRE_NULL(audio_visualization_render_fft(16, 8, AUDIO_VISUALIZATION_SOURCE_COUNT, false, 0, false, false));
}
