#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/audio/client_pipeline.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <math.h>
TEST_CASE("client_stream/opus_round_trip_preserves_audible_and_quiet_media", "[client_stream]") {
  client_audio_pipeline_config_t config = client_audio_pipeline_default_config();
  config.flags = CLIENT_AUDIO_PIPELINE_FLAGS_MINIMAL;
  config.opus_application = 2049;
  config.opus_bitrate = 128000;
  client_audio_pipeline_t *pipeline = client_audio_pipeline_create(&config);
  ASCIICHAT_REQUIRE_NOT_NULL(pipeline);
  float input[1920], output[1920];
  uint8_t packet[4000];
  for (int quiet = 0; quiet < 2; ++quiet) {
    double energy = 0;
    float amplitude = quiet ? 0.002f : 0.25f;
    for (int block = 0; block < 20; ++block) {
      for (int i = 0; i < 1920; ++i)
        input[i] = amplitude * sinf(6.283185307f * 440 * (block * 960 + i / 2) / 48000);
      int length = client_audio_pipeline_capture(pipeline, input, 1920, packet, sizeof(packet));
      ASCIICHAT_REQUIRE_GT(length, 0);
      ASCIICHAT_REQUIRE_EQ(client_audio_pipeline_playback(pipeline, packet, length, output, 1920), 1920);
      for (int i = 0; i < 1920; ++i) {
        ASCIICHAT_REQUIRE(isfinite(output[i]));
        if (block > 5)
          energy += output[i] * output[i];
      }
    }
    double rms = sqrt(energy / (14 * 1920));
    ASCIICHAT_REQUIRE_GT(rms, amplitude * 0.5);
    ASCIICHAT_REQUIRE_LT(rms, amplitude);
  }
  ASCIICHAT_REQUIRE_EQ(client_audio_pipeline_playback(pipeline, NULL, 0, output, 1920), 1920);
  ASCIICHAT_REQUIRE_EQ(client_audio_pipeline_capture(pipeline, input, 1919, packet, sizeof(packet)), -1);
  ASCIICHAT_REQUIRE_EQ(client_audio_pipeline_playback(pipeline, NULL, 1, output, 1920), -1);
  ASCIICHAT_REQUIRE_EQ(client_audio_pipeline_playback(pipeline, packet, -1, output, 1920), -1);
  ASCIICHAT_REQUIRE_EQ(client_audio_pipeline_get_playback_frame(pipeline, output, 1920), 1920);
  for (int i = 0; i < 1920; ++i)
    ASCIICHAT_REQUIRE_EQ(output[i], 0);
  ASCIICHAT_REQUIRE_EQ(client_audio_pipeline_jitter_margin(pipeline), 60);
  client_audio_pipeline_reset(pipeline);
  client_audio_pipeline_destroy(pipeline);
}

TEST_CASE("client_stream/duplex_startup_fades_in_and_echo_processing_remains_finite", "[client_stream]") {
  for (int echo = 0; echo < 2; ++echo) {
    client_audio_pipeline_config_t config = client_audio_pipeline_default_config();
    config.flags = echo ? CLIENT_AUDIO_PIPELINE_FLAGS_ALL : CLIENT_AUDIO_PIPELINE_FLAGS_MINIMAL;
    config.processor.spectral_enabled = echo;
    client_audio_pipeline_t *pipeline = client_audio_pipeline_create(&config);
    ASCIICHAT_REQUIRE_NOT_NULL(pipeline);
    float input[960], render[960], output[960];
    for (int i = 0; i < 960; ++i) {
      input[i] = 0.1f;
      render[i] = 0;
    }
    client_audio_pipeline_process_duplex(pipeline, render, 960, input, 960, output);
    ASCIICHAT_REQUIRE_FLOAT_EQ(output[0], 0, 0.00001);
    for (int block = 0; block < 40; ++block)
      client_audio_pipeline_process_duplex(pipeline, render, 960, input, 960, output);
    for (int i = 0; i < 960; ++i) {
      ASCIICHAT_REQUIRE(isfinite(output[i]));
      if (!echo)
        ASCIICHAT_REQUIRE_FLOAT_EQ(output[i], input[i], 0.00001);
    }
    client_audio_pipeline_set_flags(pipeline, CLIENT_AUDIO_PIPELINE_FLAGS_MINIMAL);
    ASCIICHAT_REQUIRE_NOT(client_audio_pipeline_get_flags(pipeline).echo_cancel);
    client_audio_pipeline_process_duplex(pipeline, NULL, 0, NULL, 960, output);
    for (int i = 0; i < 960; ++i)
      ASCIICHAT_REQUIRE_EQ(output[i], 0);
    client_audio_pipeline_reset(pipeline);
    client_audio_pipeline_destroy(pipeline);
  }
}
