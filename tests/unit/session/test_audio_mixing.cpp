#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include "common/session/audio.h"
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/options/rcu.h>
#include <string.h>

static session_audio_ctx_t *host, *participant;

static void setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  REQUIRE((options_state_init()) == (ASCIICHAT_OK));
  host = session_audio_create(true);
  participant = session_audio_create(false);
  REQUIRE((host) != nullptr);
  REQUIRE((participant) != nullptr);
}

static void teardown(void) {
  session_audio_destroy(host);
  session_audio_destroy(participant);
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
static void feed(uint32_t id, float value) {
  float samples[AUDIO_SAMPLE_RATE * AUDIO_CHANNELS / 10];
  for (size_t i = 0; i < ARRAY_SIZE(samples); ++i)
    samples[i] = value;
  REQUIRE((session_audio_write_source(host, id, samples, ARRAY_SIZE(samples))) == (ASCIICHAT_OK));
}

static void finish_startup_fade(void) {
  // Each source uses a network jitter buffer with a startup fade. Compare the
  // steady mix only after all fed sources have crossed that boundary.
  float startup[(AUDIO_CROSSFADE_SAMPLES * AUDIO_CHANNELS)];
  REQUIRE((session_audio_mix_excluding(host, 0, startup, (AUDIO_CROSSFADE_SAMPLES * AUDIO_CHANNELS))) == ((AUDIO_CROSSFADE_SAMPLES * AUDIO_CHANNELS)));
}

TEST_CASE("session_audio_mixing/mix_minus_excludes_the_recipient_and_averages_only_nonempty_sources", "[session_audio_mixing]") {
  REQUIRE((session_audio_add_source(host, 1)) == (ASCIICHAT_OK));
  REQUIRE((session_audio_add_source(host, 2)) == (ASCIICHAT_OK));
  REQUIRE((session_audio_add_source(host, 3)) == (ASCIICHAT_OK));
  feed(1, 0.25f);
  feed(2, 0.75f);
  finish_startup_fade();
  float mixed[16];
  REQUIRE((session_audio_mix_excluding(host, 2, mixed, 16)) == (16));
  for (size_t i = 0; i < 16; ++i)
    REQUIRE((mixed[i]) == Catch::Approx(0.25f));
  REQUIRE((session_audio_mix_excluding(host, 0, mixed, 16)) == (16));
  for (size_t i = 0; i < 16; ++i)
    REQUIRE((mixed[i]) == Catch::Approx(0.5f));
}

TEST_CASE("session_audio_mixing/over_range_multiple_sources_clip_and_empty_mix_is_silent", "[session_audio_mixing]") {
  float mixed[16];
  memset(mixed, 0x7f, sizeof(mixed));
  REQUIRE((session_audio_mix_excluding(host, 0, mixed, 16)) == (16));
  for (size_t i = 0; i < 16; ++i)
    REQUIRE((mixed[i]) == (0));
  REQUIRE((session_audio_add_source(host, 1)) == (ASCIICHAT_OK));
  REQUIRE((session_audio_add_source(host, 2)) == (ASCIICHAT_OK));
  feed(1, 2.0f);
  feed(2, 2.0f);
  finish_startup_fade();
  REQUIRE((session_audio_mix_excluding(host, 0, mixed, 16)) == (16));
  for (size_t i = 0; i < 16; ++i)
    REQUIRE((mixed[i]) == (1.0f));
  session_audio_remove_source(host, 1);
  session_audio_remove_source(host, 2);
  REQUIRE((session_audio_add_source(host, 1)) == (ASCIICHAT_OK));
  REQUIRE((session_audio_add_source(host, 2)) == (ASCIICHAT_OK));
  feed(1, -2.0f);
  feed(2, -2.0f);
  finish_startup_fade();
  REQUIRE((session_audio_mix_excluding(host, 0, mixed, 16)) == (16));
  for (size_t i = 0; i < 16; ++i)
    REQUIRE((mixed[i]) == (-1.0f));
}

TEST_CASE("session_audio_mixing/duplicate_registration_preserves_samples_and_capacity_is_reusable", "[session_audio_mixing]") {
  REQUIRE((session_audio_add_source(host, 1)) == (ASCIICHAT_OK));
  feed(1, 0.4f);
  REQUIRE((session_audio_add_source(host, 1)) == (ASCIICHAT_OK));
  finish_startup_fade();
  for (uint32_t id = 2; id <= MAX_SESSION_PARTICIPANTS; ++id)
    REQUIRE((session_audio_add_source(host, id)) == (ASCIICHAT_OK));
  REQUIRE((session_audio_add_source(host, MAX_SESSION_PARTICIPANTS + 1)) == (ERROR_RESOURCE_EXHAUSTED));
  float mixed[16];
  REQUIRE((session_audio_mix_excluding(host, 0, mixed, 16)) == (16));
  REQUIRE((mixed[0]) == Catch::Approx(0.4f));
  session_audio_remove_source(host, 1);
  session_audio_remove_source(host, 1);
  REQUIRE((session_audio_write_source(host, 1, mixed, 16)) == (ERROR_NOT_FOUND));
  REQUIRE((session_audio_add_source(host, MAX_SESSION_PARTICIPANTS + 1)) == (ASCIICHAT_OK));
}

TEST_CASE("session_audio_mixing/participant_cannot_register_or_mix_host_sources", "[session_audio_mixing]") {
  float sample = 0.5f, output = 0.75f;
  REQUIRE((session_audio_add_source(participant, 1)) == (ERROR_INVALID_STATE));
  REQUIRE((session_audio_write_source(participant, 1, &sample, 1)) == (ERROR_INVALID_STATE));
  REQUIRE((session_audio_mix_excluding(participant, 0, &output, 1)) == (0));
  REQUIRE((output) == (0.75f));
  session_audio_remove_source(participant, 1);
  REQUIRE(!(session_audio_is_running(participant)));
  session_audio_stop(participant);
}

TEST_CASE("session_audio_mixing/capture_and_playback_use_the_owned_rings_without_opening_devices", "[session_audio_mixing]") {
  audio_context_t *audio = static_cast<audio_context_t *>(session_audio_get_context(participant));
  REQUIRE((audio) != nullptr);
  float input[] = {0.25f, -0.5f, 0.75f, -0.25f, 0.5f, -0.75f}, output[6] = {0};
  REQUIRE((audio_ring_buffer_write(audio->capture_buffer, input, ARRAY_SIZE(input))) == (ASCIICHAT_OK));
  REQUIRE((session_audio_read_captured(participant, output, ARRAY_SIZE(output))) == (ARRAY_SIZE(output)));
  REQUIRE(std::memcmp((input), (output), (sizeof(input))) == 0);
  REQUIRE((session_audio_read_captured(participant, output, 3)) == (0));
  audio->playback_buffer->jitter_buffer_enabled = false;
  REQUIRE((session_audio_write_playback(participant, input, ARRAY_SIZE(input))) == (ASCIICHAT_OK));
  REQUIRE((audio_ring_buffer_read(audio->playback_buffer, output, ARRAY_SIZE(output))) == (ARRAY_SIZE(output)));
  REQUIRE(std::memcmp((input), (output), (sizeof(input))) == 0);
  int media_token;
  session_audio_set_capture_source(participant, &media_token);
  REQUIRE((audio->capture_media_source) == (&media_token));
  session_audio_set_capture_source(participant, NULL);
  REQUIRE((audio->capture_media_source) == nullptr);
  REQUIRE((session_audio_read_captured(participant, NULL, 3)) == (0));
  REQUIRE((session_audio_write_playback(participant, NULL, 3)) == (ERROR_INVALID_PARAM));
}
