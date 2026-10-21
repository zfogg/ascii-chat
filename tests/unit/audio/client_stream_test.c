#include <criterion/criterion.h>
#include <ascii-chat/audio/client_pipeline.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <math.h>

TestSuite(client_stream);

Test(client_stream, opus_round_trip_preserves_audible_and_quiet_media) {
  client_audio_pipeline_config_t config = client_audio_pipeline_default_config();
  config.flags = CLIENT_AUDIO_PIPELINE_FLAGS_MINIMAL;
  config.opus_application = 2049;
  config.opus_bitrate = 128000;
  client_audio_pipeline_t *pipeline = client_audio_pipeline_create(&config);
  cr_assert_not_null(pipeline);
  float input[960], output[960];
  uint8_t packet[4000];
  for (int quiet = 0; quiet < 2; ++quiet) {
    double energy = 0;
    float amplitude = quiet ? 0.002f : 0.25f;
    for (int block = 0; block < 20; ++block) {
      for (int i = 0; i < 960; ++i)
        input[i] = amplitude * sinf(6.283185307f * 440 * (block * 960 + i) / 48000);
      int length = client_audio_pipeline_capture(pipeline, input, 960, packet, sizeof(packet));
      cr_assert_gt(length, 0);
      cr_assert_eq(client_audio_pipeline_playback(pipeline, packet, length, output, 960), 960);
      for (int i = 0; i < 960; ++i) {
        cr_assert(isfinite(output[i]));
        if (block > 5)
          energy += output[i] * output[i];
      }
    }
    double rms = sqrt(energy / (14 * 960));
    cr_assert_gt(rms, amplitude * 0.5);
    cr_assert_lt(rms, amplitude);
  }
  cr_assert_eq(client_audio_pipeline_playback(pipeline, NULL, 0, output, 960), 960);
  cr_assert_eq(client_audio_pipeline_capture(pipeline, input, 959, packet, sizeof(packet)), -1);
  cr_assert_eq(client_audio_pipeline_playback(pipeline, NULL, 1, output, 960), -1);
  cr_assert_eq(client_audio_pipeline_playback(pipeline, packet, -1, output, 960), -1);
  cr_assert_eq(client_audio_pipeline_get_playback_frame(pipeline, output, 960), 960);
  for (int i = 0; i < 960; ++i)
    cr_assert_eq(output[i], 0);
  cr_assert_eq(client_audio_pipeline_jitter_margin(pipeline), 60);
  client_audio_pipeline_reset(pipeline);
  client_audio_pipeline_destroy(pipeline);
}

Test(client_stream, duplex_startup_fades_in_and_echo_processing_remains_finite) {
  for (int echo = 0; echo < 2; ++echo) {
    client_audio_pipeline_config_t config = client_audio_pipeline_default_config();
    config.flags = echo ? CLIENT_AUDIO_PIPELINE_FLAGS_ALL : CLIENT_AUDIO_PIPELINE_FLAGS_MINIMAL;
    config.processor.spectral_enabled = echo;
    client_audio_pipeline_t *pipeline = client_audio_pipeline_create(&config);
    cr_assert_not_null(pipeline);
    float input[480], render[480], output[480];
    for (int i = 0; i < 480; ++i) {
      input[i] = 0.1f;
      render[i] = 0;
    }
    client_audio_pipeline_process_duplex(pipeline, render, 480, input, 480, output);
    cr_assert_float_eq(output[0], 0, 0.00001);
    for (int block = 0; block < 40; ++block)
      client_audio_pipeline_process_duplex(pipeline, render, 480, input, 480, output);
    for (int i = 0; i < 480; ++i) {
      cr_assert(isfinite(output[i]));
      if (!echo)
        cr_assert_float_eq(output[i], input[i], 0.00001);
    }
    client_audio_pipeline_set_flags(pipeline, CLIENT_AUDIO_PIPELINE_FLAGS_MINIMAL);
    cr_assert_not(client_audio_pipeline_get_flags(pipeline).echo_cancel);
    client_audio_pipeline_process_duplex(pipeline, NULL, 0, NULL, 480, output);
    for (int i = 0; i < 480; ++i)
      cr_assert_eq(output[i], 0);
    client_audio_pipeline_reset(pipeline);
    client_audio_pipeline_destroy(pipeline);
  }
}
