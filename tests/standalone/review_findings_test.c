#include <ascii-chat/common.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/network/consensus/handoff.h>
#include <ascii-chat/video/face_follow.h>
#include <ascii-chat/audio/mixer.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/util/fnv1a.h>
#include <math.h>
#include <string.h>

#define CHECK(expr)                                                                                                    \
  do {                                                                                                                 \
    if (!(expr))                                                                                                       \
      FATAL(ERROR_ASSERTION_FAILED, "Review regression line %d: %s", __LINE__, #expr);                                 \
  } while (0)

static void test_mixer_high_slot(void) {
  mixer_t *mixer = mixer_create(65, 48000);
  CHECK(mixer);
  audio_ring_buffer_t *buffers[65];
  char names[65][16];
  for (int i = 0; i < 65; ++i) {
    safe_snprintf(names[i], sizeof(names[i]), "source-%d", i);
    buffers[i] = audio_ring_buffer_create_for_capture();
    CHECK(buffers[i]);
    CHECK(mixer_add_source(mixer, names[i], buffers[i]) == i);
  }
  float input[480], output[480];
  for (int i = 0; i < 480; ++i)
    input[i] = 0.1f;
  CHECK(audio_ring_buffer_write(buffers[64], input, 480) == ASCIICHAT_OK);
  CHECK(mixer_process_excluding_source(mixer, output, 480, fnv1a_hash_string(names[0])) == 480);
  CHECK(output[240] > 0.01f);
  CHECK(mixer_process_excluding_source(mixer, output, 480, fnv1a_hash_string(names[64])) == 480);
  CHECK(fabsf(output[240]) < 0.00001f);
  mixer_remove_source(mixer, names[64]);
  CHECK(mixer->active_sources_mask[1] == 0);
  mixer_destroy(mixer);
  for (int i = 0; i < 65; ++i)
    audio_ring_buffer_destroy(buffers[i]);
}

static void test_handoff_unmeasured_host(void) {
  consensus_handoff_t state;
  uint8_t session[16] = {1}, host[16] = {2}, peer[16] = {3};
  consensus_handoff_init(&state, session, host);
  CHECK(consensus_handoff_join(&state, peer) == ASCIICHAT_OK);
  consensus_handoff_message_t message = {.kind = HANDOFF_SAMPLE_START, .sequence = 1, .capabilities = 2};
  memcpy(message.session, session, 16);
  memcpy(message.participant, peer, 16);
  CHECK(consensus_handoff_receive(&state, &message, CONSENSUS_HANDOFF_HEADER, 100) == ASCIICHAT_OK);
  message.kind = HANDOFF_SAMPLE_DATA;
  CHECK(consensus_handoff_receive(&state, &message, CONSENSUS_HANDOFF_HEADER + CONSENSUS_HANDOFF_PROBE_BYTES, 110) ==
        ASCIICHAT_OK);
  CHECK(consensus_handoff_tick(&state, 111, &message));
  message.kind = HANDOFF_READY;
  CHECK(consensus_handoff_receive(&state, &message, CONSENSUS_HANDOFF_HEADER, 112) == ASCIICHAT_OK);
  consensus_handoff_leave(&state, host);
  CHECK(consensus_handoff_tick(&state, 113, &message));
  CHECK(message.kind == HANDOFF_COMMIT);
}

int main(void) {
  log_init(NULL, LOG_WARN, false, false);
  CHECK(options_state_init() == ASCIICHAT_OK);
  face_follow_state_t face;
  face_follow_init(&face);
  face_follow_detection_t detection = {{0.2f, 0.3f, 0.12f, 0.2f}, 0.95f};
  face_follow_observe(&face, &detection, 1, 0);
  face_follow_tick(&face, 0, true);
  CHECK(face.crop.w < 0.75f);
  test_handoff_unmeasured_host();
  test_mixer_high_slot();
  options_state_destroy();
  log_set_terminal_output(true);
  log_warn("PASS: first-frame framing, unmeasured incumbent loss, mixer slot 64 and self exclusion");
  return 0;
}
