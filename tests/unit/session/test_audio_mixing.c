#include <criterion/criterion.h>
#include "common/session/audio.h"
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/options/rcu.h>
#include <string.h>

static session_audio_ctx_t *host, *participant;

static void setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  host = session_audio_create(true);
  participant = session_audio_create(false);
  cr_assert_not_null(host);
  cr_assert_not_null(participant);
}

static void teardown(void) {
  session_audio_destroy(host);
  session_audio_destroy(participant);
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}

TestSuite(session_audio_mixing, .init = setup, .fini = teardown);

static void feed(uint32_t id, float value) {
  float samples[4096];
  for (size_t i = 0; i < 4096; ++i)
    samples[i] = value;
  cr_assert_eq(session_audio_write_source(host, id, samples, 4096), ASCIICHAT_OK);
}

static void finish_startup_fade(void) {
  // Each source uses a network jitter buffer with a startup fade. Compare the
  // steady mix only after all fed sources have crossed that boundary.
  float startup[AUDIO_CROSSFADE_SAMPLES];
  cr_assert_eq(session_audio_mix_excluding(host, 0, startup, AUDIO_CROSSFADE_SAMPLES), AUDIO_CROSSFADE_SAMPLES);
}

Test(session_audio_mixing, mix_minus_excludes_the_recipient_and_averages_only_nonempty_sources) {
  cr_assert_eq(session_audio_add_source(host, 1), ASCIICHAT_OK);
  cr_assert_eq(session_audio_add_source(host, 2), ASCIICHAT_OK);
  cr_assert_eq(session_audio_add_source(host, 3), ASCIICHAT_OK);
  feed(1, 0.25f);
  feed(2, 0.75f);
  finish_startup_fade();
  float mixed[16];
  cr_assert_eq(session_audio_mix_excluding(host, 2, mixed, 16), 16);
  for (size_t i = 0; i < 16; ++i)
    cr_assert_float_eq(mixed[i], 0.25f, 1e-6);
  cr_assert_eq(session_audio_mix_excluding(host, 0, mixed, 16), 16);
  for (size_t i = 0; i < 16; ++i)
    cr_assert_float_eq(mixed[i], 0.5f, 1e-6);
}

Test(session_audio_mixing, over_range_multiple_sources_clip_and_empty_mix_is_silent) {
  float mixed[16];
  memset(mixed, 0x7f, sizeof(mixed));
  cr_assert_eq(session_audio_mix_excluding(host, 0, mixed, 16), 16);
  for (size_t i = 0; i < 16; ++i)
    cr_assert_eq(mixed[i], 0);
  cr_assert_eq(session_audio_add_source(host, 1), ASCIICHAT_OK);
  cr_assert_eq(session_audio_add_source(host, 2), ASCIICHAT_OK);
  feed(1, 2.0f);
  feed(2, 2.0f);
  finish_startup_fade();
  cr_assert_eq(session_audio_mix_excluding(host, 0, mixed, 16), 16);
  for (size_t i = 0; i < 16; ++i)
    cr_assert_eq(mixed[i], 1.0f);
  session_audio_remove_source(host, 1);
  session_audio_remove_source(host, 2);
  cr_assert_eq(session_audio_add_source(host, 1), ASCIICHAT_OK);
  cr_assert_eq(session_audio_add_source(host, 2), ASCIICHAT_OK);
  feed(1, -2.0f);
  feed(2, -2.0f);
  finish_startup_fade();
  cr_assert_eq(session_audio_mix_excluding(host, 0, mixed, 16), 16);
  for (size_t i = 0; i < 16; ++i)
    cr_assert_eq(mixed[i], -1.0f);
}

Test(session_audio_mixing, duplicate_registration_preserves_samples_and_capacity_is_reusable) {
  cr_assert_eq(session_audio_add_source(host, 1), ASCIICHAT_OK);
  feed(1, 0.4f);
  cr_assert_eq(session_audio_add_source(host, 1), ASCIICHAT_OK);
  finish_startup_fade();
  for (uint32_t id = 2; id <= MAX_SESSION_PARTICIPANTS; ++id)
    cr_assert_eq(session_audio_add_source(host, id), ASCIICHAT_OK);
  cr_assert_eq(session_audio_add_source(host, MAX_SESSION_PARTICIPANTS + 1), ERROR_RESOURCE_EXHAUSTED);
  float mixed[16];
  cr_assert_eq(session_audio_mix_excluding(host, 0, mixed, 16), 16);
  cr_assert_float_eq(mixed[0], 0.4f, 1e-6);
  session_audio_remove_source(host, 1);
  session_audio_remove_source(host, 1);
  cr_assert_eq(session_audio_write_source(host, 1, mixed, 16), ERROR_NOT_FOUND);
  cr_assert_eq(session_audio_add_source(host, MAX_SESSION_PARTICIPANTS + 1), ASCIICHAT_OK);
}

Test(session_audio_mixing, participant_cannot_register_or_mix_host_sources) {
  float sample = 0.5f, output = 0.75f;
  cr_assert_eq(session_audio_add_source(participant, 1), ERROR_INVALID_STATE);
  cr_assert_eq(session_audio_write_source(participant, 1, &sample, 1), ERROR_INVALID_STATE);
  cr_assert_eq(session_audio_mix_excluding(participant, 0, &output, 1), 0);
  cr_assert_eq(output, 0.75f);
  session_audio_remove_source(participant, 1);
  cr_assert_not(session_audio_is_running(participant));
  session_audio_stop(participant);
}

Test(session_audio_mixing, capture_and_playback_use_the_owned_rings_without_opening_devices) {
  audio_context_t *audio = session_audio_get_context(participant);
  cr_assert_not_null(audio);
  float input[] = {0.25f, -0.5f, 0.75f, -0.25f, 0.5f, -0.75f}, output[6] = {0};
  cr_assert_eq(audio_ring_buffer_write(audio->capture_buffer, input, ARRAY_SIZE(input)), ASCIICHAT_OK);
  cr_assert_eq(session_audio_read_captured(participant, output, ARRAY_SIZE(output)), ARRAY_SIZE(output));
  cr_assert_arr_eq(input, output, sizeof(input));
  cr_assert_eq(session_audio_read_captured(participant, output, 3), 0);
  audio->playback_buffer->jitter_buffer_enabled = false;
  cr_assert_eq(session_audio_write_playback(participant, input, ARRAY_SIZE(input)), ASCIICHAT_OK);
  cr_assert_eq(audio_ring_buffer_read(audio->playback_buffer, output, ARRAY_SIZE(output)), 3);
  cr_assert_arr_eq(input, output, sizeof(input));
  int media_token;
  session_audio_set_capture_source(participant, &media_token);
  cr_assert_eq(audio->capture_media_source, &media_token);
  session_audio_set_capture_source(participant, NULL);
  cr_assert_null(audio->capture_media_source);
  cr_assert_eq(session_audio_read_captured(participant, NULL, 3), 0);
  cr_assert_eq(session_audio_write_playback(participant, NULL, 3), ERROR_INVALID_PARAM);
}
