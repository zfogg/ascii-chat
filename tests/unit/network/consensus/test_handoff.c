#include <criterion/criterion.h>
#include <ascii-chat/network/consensus/handoff.h>
#include <string.h>

static uint8_t session_id[16] = {9}, host_id[16] = {1}, slow_id[16] = {2}, fast_id[16] = {3};

static void sample(consensus_handoff_t *s, const uint8_t id[16], uint64_t start, uint64_t end, uint32_t sequence) {
  consensus_handoff_message_t m = {
      .kind = HANDOFF_SAMPLE_START, .sequence = sequence, .capabilities = 2, .nat_tier = 255};
  memcpy(m.session, session_id, 16);
  memcpy(m.participant, id, 16);
  cr_assert_eq(consensus_handoff_receive(s, &m, CONSENSUS_HANDOFF_HEADER, start), ASCIICHAT_OK);
  m.kind = HANDOFF_SAMPLE_DATA;
  cr_assert_eq(consensus_handoff_receive(s, &m, CONSENSUS_HANDOFF_HEADER + CONSENSUS_HANDOFF_PROBE_BYTES, end),
               ASCIICHAT_OK);
}
static consensus_handoff_t setup(void) {
  consensus_handoff_t s;
  consensus_handoff_init(&s, session_id, host_id);
  cr_assert_eq(consensus_handoff_join(&s, host_id), ASCIICHAT_OK);
  cr_assert_eq(consensus_handoff_join(&s, slow_id), ASCIICHAT_OK);
  cr_assert_eq(consensus_handoff_join(&s, fast_id), ASCIICHAT_OK);
  return s;
}
Test(handoff, measured_successor_prepares_before_commit) {
  consensus_handoff_t s = setup();
  consensus_handoff_message_t action;
  sample(&s, slow_id, 100, 300, 1);
  sample(&s, fast_id, 100, 110, 1);
  cr_assert(consensus_handoff_tick(&s, 301, &action));
  cr_assert_eq(action.kind, HANDOFF_PREPARE);
  cr_assert_eq(memcmp(action.participant, fast_id, 16), 0);
  action.kind = HANDOFF_READY;
  cr_assert_eq(consensus_handoff_receive(&s, &action, CONSENSUS_HANDOFF_HEADER, 302), ASCIICHAT_OK);
  cr_assert_not(consensus_handoff_tick(&s, 303, &action));
  consensus_handoff_leave(&s, host_id);
  cr_assert(consensus_handoff_tick(&s, 304, &action));
  cr_assert_eq(action.kind, HANDOFF_COMMIT);
  cr_assert_eq(memcmp(action.participant, fast_id, 16), 0);
}
Test(handoff, unknown_and_expired_never_win) {
  consensus_handoff_t s = setup();
  consensus_handoff_message_t action;
  cr_assert_not(consensus_handoff_tick(&s, 1, &action));
  sample(&s, fast_id, 100, 110, 1);
  cr_assert_not(consensus_handoff_tick(&s, 16000, &action));
}
Test(handoff, no_successor_reports_failure_after_deadline_once) {
  consensus_handoff_t s = setup();
  consensus_handoff_message_t action;
  consensus_handoff_leave(&s, host_id);
  cr_assert_not(consensus_handoff_tick(&s, 100, &action));
  cr_assert(consensus_handoff_tick(&s, 5100, &action));
  cr_assert_eq(action.kind, HANDOFF_FAILED);
  cr_assert_not(consensus_handoff_tick(&s, 5200, &action));
}
Test(handoff, ties_ignore_arrival_order) {
  consensus_handoff_t s = setup();
  consensus_handoff_message_t action;
  sample(&s, fast_id, 100, 110, 1);
  sample(&s, slow_id, 100, 110, 1);
  cr_assert(consensus_handoff_tick(&s, 111, &action));
  cr_assert_eq(memcmp(action.participant, slow_id, 16), 0);
}
Test(handoff, failed_preparation_tries_next_candidate) {
  consensus_handoff_t s = setup();
  consensus_handoff_message_t action;
  sample(&s, slow_id, 100, 300, 1);
  sample(&s, fast_id, 100, 110, 1);
  cr_assert(consensus_handoff_tick(&s, 301, &action));
  action.kind = HANDOFF_REJECT;
  cr_assert_eq(consensus_handoff_receive(&s, &action, CONSENSUS_HANDOFF_HEADER, 302), ASCIICHAT_OK);
  cr_assert(consensus_handoff_tick(&s, 303, &action));
  cr_assert_eq(memcmp(action.participant, slow_id, 16), 0);
}
Test(handoff, departure_fences_preparation_ack) {
  consensus_handoff_t s = setup();
  consensus_handoff_message_t action;
  sample(&s, fast_id, 100, 110, 1);
  cr_assert(consensus_handoff_tick(&s, 111, &action));
  consensus_handoff_leave(&s, slow_id);
  action.kind = HANDOFF_READY;
  cr_assert_neq(consensus_handoff_receive(&s, &action, CONSENSUS_HANDOFF_HEADER, 112), ASCIICHAT_OK);
}
Test(handoff, exact_wire_length_and_version) {
  uint8_t bytes[CONSENSUS_HANDOFF_HEADER];
  consensus_handoff_message_t m = {.kind = HANDOFF_PLAN, .generation = 0x01020304, .sequence = 17}, out;
  memcpy(m.session, session_id, 16);
  memcpy(m.participant, fast_id, 16);
  consensus_handoff_encode(&m, bytes);
  cr_assert_eq(bytes[36], 1);
  cr_assert_eq(bytes[39], 4);
  cr_assert_eq(consensus_handoff_decode(bytes, sizeof(bytes), &out), ASCIICHAT_OK);
  cr_assert_eq(out.generation, m.generation);
  cr_assert_neq(consensus_handoff_decode(bytes, sizeof(bytes) - 1, &out), ASCIICHAT_OK);
  bytes[0] = 99;
  cr_assert_neq(consensus_handoff_decode(bytes, sizeof(bytes), &out), ASCIICHAT_OK);
}
Test(handoff, accepts_128_members_and_rejects_129th) {
  consensus_handoff_t s;
  consensus_handoff_init(&s, session_id, host_id);
  for (unsigned i = 0; i < 128; ++i) {
    uint8_t id[16] = {0};
    id[14] = (uint8_t)(i >> 8);
    id[15] = (uint8_t)i;
    cr_assert_eq(consensus_handoff_join(&s, id), ASCIICHAT_OK);
  }
  cr_assert_eq(s.count, 128);
  uint8_t extra[16] = {0};
  extra[14] = 1;
  cr_assert_neq(consensus_handoff_join(&s, extra), ASCIICHAT_OK);
  consensus_handoff_leave(&s, host_id);
  /* host_id was not one of the generated IDs; membership is unchanged. */
  cr_assert_eq(s.count, 128);
  uint8_t first[16] = {0};
  consensus_handoff_leave(&s, first);
  cr_assert_eq(consensus_handoff_join(&s, extra), ASCIICHAT_OK);
}

Test(handoff, incumbent_leaves_before_first_sample) {
  consensus_handoff_t s;
  consensus_handoff_init(&s, session_id, host_id);
  consensus_handoff_join(&s, fast_id);
  sample(&s, fast_id, 100, 110, 1);
  consensus_handoff_message_t action;
  cr_assert(consensus_handoff_tick(&s, 111, &action));
  action.kind = HANDOFF_READY;
  cr_assert_eq(consensus_handoff_receive(&s, &action, CONSENSUS_HANDOFF_HEADER, 112), ASCIICHAT_OK);
  consensus_handoff_leave(&s, host_id);
  cr_assert(consensus_handoff_tick(&s, 113, &action));
  cr_assert_eq(action.kind, HANDOFF_COMMIT);
}

Test(handoff, rejected_peer_can_succeed_in_later_handoff) {
  consensus_handoff_t s = setup();
  consensus_handoff_message_t action;
  sample(&s, slow_id, 100, 300, 1);
  sample(&s, fast_id, 100, 110, 1);
  cr_assert(consensus_handoff_tick(&s, 301, &action));
  action.kind = HANDOFF_REJECT;
  cr_assert_eq(consensus_handoff_receive(&s, &action, CONSENSUS_HANDOFF_HEADER, 302), ASCIICHAT_OK);
  cr_assert(consensus_handoff_tick(&s, 303, &action));
  action.kind = HANDOFF_READY;
  cr_assert_eq(consensus_handoff_receive(&s, &action, CONSENSUS_HANDOFF_HEADER, 304), ASCIICHAT_OK);
  consensus_handoff_leave(&s, host_id);
  cr_assert(consensus_handoff_tick(&s, 305, &action));
  cr_assert_eq(action.kind, HANDOFF_COMMIT);
  consensus_handoff_leave(&s, slow_id);
  cr_assert(consensus_handoff_tick(&s, 306, &action));
  cr_assert_eq(action.kind, HANDOFF_PREPARE);
  cr_assert_eq(memcmp(action.participant, fast_id, 16), 0);
}

Test(handoff, fresh_measurement_retries_a_transient_preparation_failure) {
  consensus_handoff_t s = setup();
  consensus_handoff_message_t action;
  sample(&s, fast_id, 100, 110, 1);
  cr_assert(consensus_handoff_tick(&s, 111, &action));
  action.kind = HANDOFF_REJECT;
  cr_assert_eq(consensus_handoff_receive(&s, &action, CONSENSUS_HANDOFF_HEADER, 112), ASCIICHAT_OK);
  cr_assert_not(consensus_handoff_tick(&s, 113, &action));
  sample(&s, fast_id, 2000, 2010, 2);
  cr_assert(consensus_handoff_tick(&s, 2011, &action));
  cr_assert_eq(action.kind, HANDOFF_PREPARE);
}
