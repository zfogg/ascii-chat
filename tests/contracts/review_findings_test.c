#include <criterion/criterion.h>
#include <ascii-chat/common.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/network/consensus/handoff.h>
#include <ascii-chat/video/face_follow.h>
#include <ascii-chat/audio/mixer.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/audio/analysis.h>
#include <ascii-chat/util/fnv1a.h>
#include <math.h>
#include <string.h>

static void test_mixer_high_slot(void) {
  mixer_t *mixer = mixer_create(65, 48000);
  cr_assert(mixer);
  audio_ring_buffer_t *buffers[65];
  char names[65][16];
  for (int i = 0; i < 65; ++i) {
    safe_snprintf(names[i], sizeof(names[i]), "source-%d", i);
    buffers[i] = audio_ring_buffer_create_for_capture();
    cr_assert(buffers[i]);
    cr_assert_eq(mixer_add_source(mixer, names[i], buffers[i]), i);
  }
  float input[480], output[480];
  for (int i = 0; i < 480; ++i)
    input[i] = 0.1f;
  cr_assert_eq(audio_ring_buffer_write(buffers[64], input, 480), ASCIICHAT_OK);
  cr_assert_eq(mixer_process_excluding_source(mixer, output, 480, fnv1a_hash_string(names[0])), 480);
  cr_assert(output[240] > 0.01f);
  cr_assert_eq(mixer_process_excluding_source(mixer, output, 480, fnv1a_hash_string(names[64])), 480);
  cr_assert(fabsf(output[240]) < 0.00001f);
  mixer_remove_source(mixer, names[64]);
  cr_assert_eq(mixer->active_sources_mask[1], 0);
  mixer_destroy(mixer);
  for (int i = 0; i < 65; ++i)
    audio_ring_buffer_destroy(buffers[i]);
}

static void test_handoff_unmeasured_host(void) {
  consensus_handoff_t state;
  uint8_t session[16] = {1}, host[16] = {2}, peer[16] = {3};
  consensus_handoff_init(&state, session, host);
  cr_assert_eq(consensus_handoff_join(&state, peer), ASCIICHAT_OK);
  consensus_handoff_message_t message = {.kind = HANDOFF_SAMPLE_START, .sequence = 1, .capabilities = 2};
  memcpy(message.session, session, 16);
  memcpy(message.participant, peer, 16);
  cr_assert_eq(consensus_handoff_receive(&state, &message, CONSENSUS_HANDOFF_HEADER, 100), ASCIICHAT_OK);
  message.kind = HANDOFF_SAMPLE_DATA;
  cr_assert_eq(
      consensus_handoff_receive(&state, &message, CONSENSUS_HANDOFF_HEADER + CONSENSUS_HANDOFF_PROBE_BYTES, 110),
      ASCIICHAT_OK);
  cr_assert(consensus_handoff_tick(&state, 111, &message));
  message.kind = HANDOFF_READY;
  cr_assert_eq(consensus_handoff_receive(&state, &message, CONSENSUS_HANDOFF_HEADER, 112), ASCIICHAT_OK);
  consensus_handoff_leave(&state, host);
  cr_assert(consensus_handoff_tick(&state, 113, &message));
  cr_assert_eq(message.kind, HANDOFF_COMMIT);
}

static void test_audio_analysis_levels(void) {
  cr_assert_eq(audio_analysis_init(), 0);
  audio_analysis_track_sent_sample(0.25f);
  audio_analysis_track_sent_sample(0.75f);
  audio_analysis_track_received_sample(-0.5f);
  audio_analysis_track_received_sample(-0.5f);
  const audio_analysis_stats_t *sent = audio_analysis_get_sent_stats();
  const audio_analysis_stats_t *received = audio_analysis_get_received_stats();
  cr_assert(fabsf(sent->dc_offset - 0.5f) < 0.00001f);
  cr_assert(fabsf(sent->rms_level - sqrtf(0.3125f)) < 0.00001f);
  cr_assert(fabsf(received->dc_offset + 0.5f) < 0.00001f);
  cr_assert(fabsf(received->rms_level - 0.5f) < 0.00001f);
  audio_analysis_destroy();
  cr_assert_eq(audio_analysis_init(), 0);
  sent = audio_analysis_get_sent_stats();
  received = audio_analysis_get_received_stats();
  cr_assert(sent->total_samples == 0 && sent->dc_offset == 0.0f && sent->rms_level == 0.0f);
  cr_assert(received->total_samples == 0 && received->dc_offset == 0.0f && received->rms_level == 0.0f);
  audio_analysis_destroy();
}

static void setup(void) {
  log_init(NULL, LOG_WARN, false, false);
  asciichat_errno_suppress(true);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
}

static void teardown(void) {
  options_state_destroy();
  log_destroy();
}

TestSuite(review_regressions, .init = setup, .fini = teardown, .timeout = 30);

Test(review_regressions, first_frame_framing) {
  face_follow_state_t face;
  face_follow_init(&face);
  face_follow_detection_t detection = {{0.2f, 0.3f, 0.12f, 0.2f}, 0.95f};
  face_follow_observe(&face, &detection, 1, 0);
  face_follow_tick(&face, 0, true);
  cr_assert(face.crop.w < 0.75f);
}

Test(review_regressions, unmeasured_host_loss) {
  test_handoff_unmeasured_host();
}

Test(review_regressions, mixer_high_slot_self_exclusion) {
  test_mixer_high_slot();
}

Test(review_regressions, audio_rms_dc_reset) {
  test_audio_analysis_levels();
}
