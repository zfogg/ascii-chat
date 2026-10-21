#include <criterion/criterion.h>
#include <ascii-chat/audio/visualization.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <math.h>
#include <string.h>

static void enable_visualization(void) {
  options_t options = *options_get();
  options.waveform = true;
  options.fft = true;
  cr_assert_eq(options_state_set(&options), ASCIICHAT_OK);
}
TestSuite(audio_visualization, .init = enable_visualization);

Test(audio_visualization, clips_invalid_samples_and_mixes_sources_without_aliasing) {
  float microphone[] = {NAN, INFINITY, -INFINITY, 2.0f, -2.0f, 0.25f};
  float media[] = {0, 0, 0, 0, 0, 0.5f}, remote[] = {0, 0, 0, 0, 0, 0.5f};
  audio_visualization_submit(AUDIO_VISUALIZATION_SOURCE_MIC, microphone, 6);
  audio_visualization_submit(AUDIO_VISUALIZATION_SOURCE_MEDIA, media, 6);
  audio_visualization_submit(AUDIO_VISUALIZATION_SOURCE_REMOTE, remote, 6);
  float output[6];
  audio_visualization_read(AUDIO_VISUALIZATION_SOURCE_MIC, output, 6);
  float expected[] = {0, 0, 0, 1, -1, 0.25f};
  for (size_t i = 0; i < 6; ++i)
    cr_assert_float_eq(output[i], expected[i], 0.00001);
  audio_visualization_read(AUDIO_VISUALIZATION_SOURCE_LOCAL_MIX, output, 6);
  cr_assert_float_eq(output[5], 0.75, 0.00001);
  audio_visualization_read(AUDIO_VISUALIZATION_SOURCE_MIX, output, 6);
  cr_assert_float_eq(output[5], 1.0, 0.00001);
  audio_visualization_read((audio_visualization_source_t)-1, output, 6);
  for (size_t i = 0; i < 6; ++i)
    cr_assert_float_eq(output[i], 0, 0.00001);
  audio_visualization_read(AUDIO_VISUALIZATION_SOURCE_MIC, NULL, 6);
  audio_visualization_submit(AUDIO_VISUALIZATION_SOURCE_MIC, NULL, 6);
  audio_visualization_submit(AUDIO_VISUALIZATION_SOURCE_MIX, media, 6);
  cr_assert_float_eq(microphone[3], 2.0, 0.00001);
}

Test(audio_visualization, ring_wrap_preserves_latest_samples_and_zero_pads_older_history) {
  const size_t total = 48010;
  float *input = SAFE_MALLOC(total * sizeof(float), float *);
  for (size_t i = 0; i < total; ++i)
    input[i] = (float)(i % 100) / 100.0f;
  audio_visualization_submit(AUDIO_VISUALIZATION_SOURCE_MIC, input, total);
  float *output = SAFE_MALLOC(total * sizeof(float), float *);
  audio_visualization_read(AUDIO_VISUALIZATION_SOURCE_MIC, output, total);
  for (size_t i = 0; i < 10; ++i)
    cr_assert_float_eq(output[i], 0, 0.00001);
  for (size_t i = 10; i < total; ++i)
    cr_assert_float_eq(output[i], input[i], 0.00001);
  SAFE_FREE(output);
  SAFE_FREE(input);
}

Test(audio_visualization, waveform_and_fft_render_bounded_plain_and_colored_frames) {
  float *samples = SAFE_MALLOC(48000 * sizeof(float), float *);
  for (size_t i = 0; i < 48000; ++i)
    samples[i] = 0.75f * sinf((float)i * 6.28318530718f * 440.0f / 48000.0f);
  audio_visualization_submit(AUDIO_VISUALIZATION_SOURCE_MIC, samples, 48000);
  SAFE_FREE(samples);
  const render_mode_t modes[] = {RENDER_MODE_FOREGROUND, RENDER_MODE_BACKGROUND, RENDER_MODE_HALF_BLOCK};
  for (size_t i = 0; i < 3; ++i)
    for (int color = 0; color < 3; ++color) {
      options_t options = *options_get();
      options.render_mode = modes[i];
      cr_assert_eq(options_state_set(&options), ASCIICHAT_OK);
      char *wave =
          audio_visualization_render_waveform(16, 8, AUDIO_VISUALIZATION_SOURCE_MIC, color != 0, color, true, true);
      char *fft =
          audio_visualization_render_fft(16, 8, AUDIO_VISUALIZATION_SOURCE_MIC, color != 0, color, false, false);
      cr_assert_not_null(wave);
      cr_assert_not_null(fft);
      cr_assert_gt(strlen(wave), 0);
      cr_assert_gt(strlen(fft), 0);
      size_t rows = 0;
      for (const char *p = wave; *p; ++p)
        rows += *p == '\n';
      cr_assert_eq(rows, 8);
      if (color == 0) {
        cr_assert_null(strchr(wave, '\033'));
        cr_assert_null(strchr(fft, '\033'));
      } else {
        cr_assert_not_null(strchr(wave, '\033'));
        cr_assert_not_null(strchr(fft, '\033'));
      }
      SAFE_FREE(wave);
      SAFE_FREE(fft);
    }
  cr_assert_null(audio_visualization_render_waveform(7, 8, AUDIO_VISUALIZATION_SOURCE_MIC, false, 0, false, false));
  cr_assert_null(audio_visualization_render_waveform(513, 8, AUDIO_VISUALIZATION_SOURCE_MIC, false, 0, false, false));
  cr_assert_null(audio_visualization_render_fft(16, 3, AUDIO_VISUALIZATION_SOURCE_MIC, false, 0, false, false));
  cr_assert_null(audio_visualization_render_fft(16, 8, AUDIO_VISUALIZATION_SOURCE_COUNT, false, 0, false, false));
}
