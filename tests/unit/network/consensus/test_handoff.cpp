#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <ascii-chat/network/consensus/handoff.h>
#include <string.h>

static uint8_t session_id[16] = {9}, host_id[16] = {1}, slow_id[16] = {2}, fast_id[16] = {3};

static void sample(consensus_handoff_t *s, const uint8_t id[16], uint64_t start, uint64_t end, uint32_t sequence) {
  consensus_handoff_message_t m = {.kind = HANDOFF_SAMPLE_START, .sequence = sequence, .capabilities = 2, .nat_tier = 255};
  memcpy(m.session, session_id, 16);
  memcpy(m.participant, id, 16);
  REQUIRE((consensus_handoff_receive(s, &m, CONSENSUS_HANDOFF_HEADER, start)) == (ASCIICHAT_OK));
  m.kind = HANDOFF_SAMPLE_DATA;
  REQUIRE((consensus_handoff_receive(s, &m, CONSENSUS_HANDOFF_HEADER + CONSENSUS_HANDOFF_PROBE_BYTES, end)) == (ASCIICHAT_OK));
}
static consensus_handoff_t setup(void) {
  consensus_handoff_t s;
  consensus_handoff_init(&s, session_id, host_id);
  REQUIRE((consensus_handoff_join(&s, host_id)) == (ASCIICHAT_OK));
  REQUIRE((consensus_handoff_join(&s, slow_id)) == (ASCIICHAT_OK));
  REQUIRE((consensus_handoff_join(&s, fast_id)) == (ASCIICHAT_OK));
  return s;
}
TEST_CASE("handoff/measured_successor_prepares_before_commit", "[handoff]") {
  consensus_handoff_t s = setup();
  consensus_handoff_message_t action;
  sample(&s, slow_id, 100, 300, 1);
  sample(&s, fast_id, 100, 110, 1);
  REQUIRE(consensus_handoff_tick(&s, 301, &action));
  REQUIRE((action.kind) == (HANDOFF_PREPARE));
  REQUIRE((memcmp(action.participant, fast_id, 16)) == (0));
  action.kind = HANDOFF_READY;
  REQUIRE((consensus_handoff_receive(&s, &action, CONSENSUS_HANDOFF_HEADER, 302)) == (ASCIICHAT_OK));
  REQUIRE(!(consensus_handoff_tick(&s, 303, &action)));
  consensus_handoff_leave(&s, host_id);
  REQUIRE(consensus_handoff_tick(&s, 304, &action));
  REQUIRE((action.kind) == (HANDOFF_COMMIT));
  REQUIRE((memcmp(action.participant, fast_id, 16)) == (0));
}
TEST_CASE("handoff/unknown_and_expired_never_win", "[handoff]") {
  consensus_handoff_t s = setup();
  consensus_handoff_message_t action;
  REQUIRE(!(consensus_handoff_tick(&s, 1, &action)));
  sample(&s, fast_id, 100, 110, 1);
  REQUIRE(!(consensus_handoff_tick(&s, 16000, &action)));
}
TEST_CASE("handoff/no_successor_reports_failure_after_deadline_once", "[handoff]") {
  consensus_handoff_t s = setup();
  consensus_handoff_message_t action;
  consensus_handoff_leave(&s, host_id);
  REQUIRE(!(consensus_handoff_tick(&s, 100, &action)));
  REQUIRE(consensus_handoff_tick(&s, 15100, &action));
  REQUIRE((action.kind) == (HANDOFF_FAILED));
  REQUIRE(!(consensus_handoff_tick(&s, 15200, &action)));
}
TEST_CASE("handoff/ties_ignore_arrival_order", "[handoff]") {
  consensus_handoff_t s = setup();
  consensus_handoff_message_t action;
  sample(&s, fast_id, 100, 110, 1);
  sample(&s, slow_id, 100, 110, 1);
  REQUIRE(consensus_handoff_tick(&s, 111, &action));
  REQUIRE((memcmp(action.participant, slow_id, 16)) == (0));
}
TEST_CASE("handoff/failed_preparation_tries_next_candidate", "[handoff]") {
  consensus_handoff_t s = setup();
  consensus_handoff_message_t action;
  sample(&s, slow_id, 100, 300, 1);
  sample(&s, fast_id, 100, 110, 1);
  REQUIRE(consensus_handoff_tick(&s, 301, &action));
  action.kind = HANDOFF_REJECT;
  REQUIRE((consensus_handoff_receive(&s, &action, CONSENSUS_HANDOFF_HEADER, 302)) == (ASCIICHAT_OK));
  REQUIRE(consensus_handoff_tick(&s, 303, &action));
  REQUIRE((memcmp(action.participant, slow_id, 16)) == (0));
}
TEST_CASE("handoff/departure_fences_preparation_ack", "[handoff]") {
  consensus_handoff_t s = setup();
  consensus_handoff_message_t action;
  sample(&s, fast_id, 100, 110, 1);
  REQUIRE(consensus_handoff_tick(&s, 111, &action));
  consensus_handoff_leave(&s, slow_id);
  action.kind = HANDOFF_READY;
  REQUIRE((consensus_handoff_receive(&s, &action, CONSENSUS_HANDOFF_HEADER, 112)) != (ASCIICHAT_OK));
}
TEST_CASE("handoff/exact_wire_length_and_version", "[handoff]") {
  uint8_t bytes[CONSENSUS_HANDOFF_HEADER];
  consensus_handoff_message_t m = {.kind = HANDOFF_PLAN, .generation = 0x01020304, .sequence = 17}, out;
  memcpy(m.session, session_id, 16);
  memcpy(m.participant, fast_id, 16);
  consensus_handoff_encode(&m, bytes);
  REQUIRE((bytes[36]) == (1));
  REQUIRE((bytes[39]) == (4));
  REQUIRE((consensus_handoff_decode(bytes, sizeof(bytes), &out)) == (ASCIICHAT_OK));
  REQUIRE((out.generation) == (m.generation));
  REQUIRE((consensus_handoff_decode(bytes, sizeof(bytes) - 1, &out)) != (ASCIICHAT_OK));
  bytes[0] = 99;
  REQUIRE((consensus_handoff_decode(bytes, sizeof(bytes), &out)) != (ASCIICHAT_OK));
}
TEST_CASE("handoff/accepts_128_members_and_rejects_129th", "[handoff]") {
  consensus_handoff_t s;
  consensus_handoff_init(&s, session_id, host_id);
  for (unsigned i = 0; i < 128; ++i) {
    uint8_t id[16] = {0};
    id[14] = (uint8_t)(i >> 8);
    id[15] = (uint8_t)i;
    REQUIRE((consensus_handoff_join(&s, id)) == (ASCIICHAT_OK));
  }
  REQUIRE((s.count) == (128));
  uint8_t extra[16] = {0};
  extra[14] = 1;
  REQUIRE((consensus_handoff_join(&s, extra)) != (ASCIICHAT_OK));
  consensus_handoff_leave(&s, host_id);
  /* host_id was not one of the generated IDs; membership is unchanged. */
  REQUIRE((s.count) == (128));
  uint8_t first[16] = {0};
  consensus_handoff_leave(&s, first);
  REQUIRE((consensus_handoff_join(&s, extra)) == (ASCIICHAT_OK));
}

TEST_CASE("handoff/incumbent_leaves_before_first_sample", "[handoff]") {
  consensus_handoff_t s;
  consensus_handoff_init(&s, session_id, host_id);
  consensus_handoff_join(&s, fast_id);
  sample(&s, fast_id, 100, 110, 1);
  consensus_handoff_message_t action;
  REQUIRE(consensus_handoff_tick(&s, 111, &action));
  action.kind = HANDOFF_READY;
  REQUIRE((consensus_handoff_receive(&s, &action, CONSENSUS_HANDOFF_HEADER, 112)) == (ASCIICHAT_OK));
  consensus_handoff_leave(&s, host_id);
  REQUIRE(consensus_handoff_tick(&s, 113, &action));
  REQUIRE((action.kind) == (HANDOFF_COMMIT));
}

TEST_CASE("handoff/rejected_peer_can_succeed_in_later_handoff", "[handoff]") {
  consensus_handoff_t s = setup();
  consensus_handoff_message_t action;
  sample(&s, slow_id, 100, 300, 1);
  sample(&s, fast_id, 100, 110, 1);
  REQUIRE(consensus_handoff_tick(&s, 301, &action));
  action.kind = HANDOFF_REJECT;
  REQUIRE((consensus_handoff_receive(&s, &action, CONSENSUS_HANDOFF_HEADER, 302)) == (ASCIICHAT_OK));
  REQUIRE(consensus_handoff_tick(&s, 303, &action));
  action.kind = HANDOFF_READY;
  REQUIRE((consensus_handoff_receive(&s, &action, CONSENSUS_HANDOFF_HEADER, 304)) == (ASCIICHAT_OK));
  consensus_handoff_leave(&s, host_id);
  REQUIRE(consensus_handoff_tick(&s, 305, &action));
  REQUIRE((action.kind) == (HANDOFF_COMMIT));
  consensus_handoff_leave(&s, slow_id);
  REQUIRE(consensus_handoff_tick(&s, 306, &action));
  REQUIRE((action.kind) == (HANDOFF_PREPARE));
  REQUIRE((memcmp(action.participant, fast_id, 16)) == (0));
}

TEST_CASE("handoff/fresh_measurement_retries_a_transient_preparation_failure", "[handoff]") {
  consensus_handoff_t s = setup();
  consensus_handoff_message_t action;
  sample(&s, fast_id, 100, 110, 1);
  REQUIRE(consensus_handoff_tick(&s, 111, &action));
  action.kind = HANDOFF_REJECT;
  REQUIRE((consensus_handoff_receive(&s, &action, CONSENSUS_HANDOFF_HEADER, 112)) == (ASCIICHAT_OK));
  REQUIRE(!(consensus_handoff_tick(&s, 113, &action)));
  sample(&s, fast_id, 2000, 2010, 2);
  REQUIRE(consensus_handoff_tick(&s, 2011, &action));
  REQUIRE((action.kind) == (HANDOFF_PREPARE));
}

TEST_CASE("handoff/successive_commits_share_generation_but_advance_decision_sequence", "[handoff]") {
  consensus_handoff_t state = setup();
  consensus_handoff_message_t action;
  sample(&state, fast_id, 100, 110, 1);
  REQUIRE(consensus_handoff_tick(&state, 111, &action));
  action.kind = HANDOFF_READY;
  REQUIRE((consensus_handoff_receive(&state, &action, CONSENSUS_HANDOFF_HEADER, 112)) == (ASCIICHAT_OK));
  consensus_handoff_leave(&state, host_id);
  REQUIRE(consensus_handoff_tick(&state, 113, &action));
  REQUIRE((action.kind) == (HANDOFF_COMMIT));
  uint32_t generation = action.generation, sequence = action.sequence;
  REQUIRE((state.host_sequence) == (sequence));
  sample(&state, slow_id, 16000, 16010, 1);
  REQUIRE(consensus_handoff_tick(&state, 16011, &action));
  REQUIRE((action.kind) == (HANDOFF_PREPARE));
  REQUIRE((state.host_sequence) == (sequence));
  action.kind = HANDOFF_READY;
  REQUIRE((consensus_handoff_receive(&state, &action, CONSENSUS_HANDOFF_HEADER, 16012)) == (ASCIICHAT_OK));
  REQUIRE(consensus_handoff_tick(&state, 16013, &action));
  REQUIRE((action.kind) == (HANDOFF_COMMIT));
  REQUIRE((action.generation) == (generation));
  REQUIRE((action.sequence) > (sequence));
  REQUIRE((state.host_sequence) == (action.sequence));
  REQUIRE(std::memcmp((action.participant), (slow_id), (16)) == 0);
}

TEST_CASE("handoff/silent_incumbent_without_first_sample_has_bounded_failure", "[handoff]") {
  consensus_handoff_t s = setup();
  consensus_handoff_message_t action;
  REQUIRE(!(consensus_handoff_tick(&s, 0, &action)));
  REQUIRE(!(consensus_handoff_tick(&s, CONSENSUS_HANDOFF_FRESH_MS + 1, &action)));
  REQUIRE(s.host_lost);
  REQUIRE(consensus_handoff_tick(&s, CONSENSUS_HANDOFF_FRESH_MS + 1 + CONSENSUS_HANDOFF_RECOVERY_MS, &action));
  REQUIRE((action.kind) == (HANDOFF_FAILED));
  sample(&s, fast_id, 31000, 31010, 1);
  REQUIRE(!(consensus_handoff_tick(&s, 31011, &action)));
}

TEST_CASE("handoff/fresh_samples_cannot_extend_failed_recovery", "[handoff]") {
  consensus_handoff_t s = setup();
  consensus_handoff_message_t action;
  sample(&s, fast_id, 100, 110, 1);
  consensus_handoff_host_lost(&s, host_id);
  REQUIRE(consensus_handoff_tick(&s, 111, &action));
  action.kind = HANDOFF_REJECT;
  REQUIRE((consensus_handoff_receive(&s, &action, CONSENSUS_HANDOFF_HEADER, 112)) == (ASCIICHAT_OK));
  sample(&s, fast_id, 2000, 2010, 2);
  REQUIRE(!(consensus_handoff_tick(&s, 2011, &action)));
  REQUIRE(consensus_handoff_tick(&s, 15111, &action));
  REQUIRE((action.kind) == (HANDOFF_FAILED));
}

TEST_CASE("handoff/silent_preparation_fails_at_shared_deadline_even_with_other_candidates", "[handoff]") {
  consensus_handoff_t s = setup();
  consensus_handoff_message_t action;
  sample(&s, fast_id, 100, 110, 1);
  sample(&s, slow_id, 100, 300, 1);
  consensus_handoff_host_lost(&s, host_id);
  REQUIRE(consensus_handoff_tick(&s, 301, &action));
  REQUIRE((action.kind) == (HANDOFF_PREPARE));
  REQUIRE(consensus_handoff_tick(&s, 5301, &action));
  REQUIRE((action.kind) == (HANDOFF_PREPARE));
  REQUIRE((memcmp(action.participant, slow_id, 16)) == (0));
  REQUIRE(!(consensus_handoff_tick(&s, 10301, &action)));
  REQUIRE(consensus_handoff_tick(&s, 15301, &action));
  REQUIRE((action.kind) == (HANDOFF_FAILED));
}

TEST_CASE("handoff/rejected_successor_recovers_to_fresh_backup", "[handoff]") {
  consensus_handoff_t s = setup();
  consensus_handoff_message_t action;
  sample(&s, fast_id, 100, 110, 1);
  sample(&s, slow_id, 100, 300, 1);
  consensus_handoff_host_lost(&s, host_id);
  REQUIRE(consensus_handoff_tick(&s, 301, &action));
  action.kind = HANDOFF_REJECT;
  REQUIRE((consensus_handoff_receive(&s, &action, CONSENSUS_HANDOFF_HEADER, 302)) == (ASCIICHAT_OK));
  REQUIRE(consensus_handoff_tick(&s, 303, &action));
  REQUIRE((memcmp(action.participant, slow_id, 16)) == (0));
  action.kind = HANDOFF_READY;
  REQUIRE((consensus_handoff_receive(&s, &action, CONSENSUS_HANDOFF_HEADER, 304)) == (ASCIICHAT_OK));
  REQUIRE(consensus_handoff_tick(&s, 305, &action));
  REQUIRE((action.kind) == (HANDOFF_COMMIT));
  consensus_handoff_host_lost(&s, host_id); // Delayed report about the old host.
  REQUIRE(!(s.host_lost));
}

TEST_CASE("handoff/incomplete_refresh_cannot_change_eligible_metrics", "[handoff]") {
  consensus_handoff_t s = setup();
  sample(&s, fast_id, 100, 110, 1);
  consensus_handoff_message_t m = {.kind = HANDOFF_SAMPLE_START, .sequence = 2, .capabilities = 0, .nat_tier = 0};
  memcpy(m.session, session_id, 16);
  memcpy(m.participant, fast_id, 16);
  REQUIRE((consensus_handoff_receive(&s, &m, CONSENSUS_HANDOFF_HEADER, 2000)) == (ASCIICHAT_OK));
  consensus_handoff_message_t action;
  REQUIRE(consensus_handoff_tick(&s, 2001, &action));
  REQUIRE((memcmp(action.participant, fast_id, 16)) == (0));
  action.kind = HANDOFF_READY;
  REQUIRE((consensus_handoff_receive(&s, &action, CONSENSUS_HANDOFF_HEADER, 2002)) == (ASCIICHAT_OK));
  m.kind = HANDOFF_SAMPLE_DATA;
  REQUIRE((consensus_handoff_receive(&s, &m, CONSENSUS_HANDOFF_HEADER + CONSENSUS_HANDOFF_PROBE_BYTES, 2010)) == (ASCIICHAT_OK));
  // A completed refresh withdrawing host capability revokes the old preparation.
  consensus_handoff_host_lost(&s, host_id);
  REQUIRE(!(consensus_handoff_tick(&s, 2011, &action)));
  REQUIRE(!(s.ready));
}

TEST_CASE("handoff/departed_peer_and_late_probe_cannot_win", "[handoff]") {
  consensus_handoff_t s = setup();
  sample(&s, fast_id, 100, 110, 1);
  consensus_handoff_leave(&s, fast_id);
  consensus_handoff_message_t m = {.kind = HANDOFF_SAMPLE_DATA, .sequence = 1};
  memcpy(m.session, session_id, 16);
  memcpy(m.participant, fast_id, 16);
  REQUIRE((consensus_handoff_receive(&s, &m, CONSENSUS_HANDOFF_HEADER + CONSENSUS_HANDOFF_PROBE_BYTES, 111)) != (ASCIICHAT_OK));
  sample(&s, slow_id, 100, 300, 1);
  consensus_handoff_message_t action;
  REQUIRE(consensus_handoff_tick(&s, 301, &action));
  REQUIRE((memcmp(action.participant, slow_id, 16)) == (0));
}

TEST_CASE("handoff/completed_refresh_changes_the_election", "[handoff]") {
  consensus_handoff_t s = setup();
  sample(&s, fast_id, 100, 110, 1);
  sample(&s, slow_id, 100, 300, 1);
  // The formerly fast peer now takes a second to upload the probe.
  sample(&s, fast_id, 2000, 3000, 2);
  consensus_handoff_message_t action;
  REQUIRE(consensus_handoff_tick(&s, 3001, &action));
  REQUIRE((memcmp(action.participant, slow_id, 16)) == (0));
}

TEST_CASE("handoff/stale_prepared_peer_cannot_commit_on_host_loss", "[handoff]") {
  consensus_handoff_t s = setup();
  sample(&s, fast_id, 100, 110, 1);
  consensus_handoff_message_t action;
  REQUIRE(consensus_handoff_tick(&s, 111, &action));
  action.kind = HANDOFF_READY;
  REQUIRE((consensus_handoff_receive(&s, &action, CONSENSUS_HANDOFF_HEADER, 112)) == (ASCIICHAT_OK));
  consensus_handoff_host_lost(&s, host_id);
  REQUIRE(!(consensus_handoff_tick(&s, 16000, &action)));
  REQUIRE(consensus_handoff_tick(&s, 31000, &action));
  REQUIRE((action.kind) == (HANDOFF_FAILED));
}

TEST_CASE("handoff/probe_body_after_timeout_is_ineligible", "[handoff]") {
  consensus_handoff_t s = setup();
  sample(&s, fast_id, 100, 5101, 1);
  consensus_handoff_message_t action;
  REQUIRE(!(consensus_handoff_tick(&s, 5102, &action)));
}

TEST_CASE("handoff/silent_successor_recovers_to_prepared_backup_within_budget", "[handoff]") {
  consensus_handoff_t s = setup();
  consensus_handoff_message_t action;
  sample(&s, fast_id, 100, 110, 1);
  sample(&s, slow_id, 100, 300, 1);
  consensus_handoff_host_lost(&s, host_id);
  REQUIRE(consensus_handoff_tick(&s, 301, &action));
  REQUIRE(consensus_handoff_tick(&s, 5301, &action));
  REQUIRE((memcmp(action.participant, slow_id, 16)) == (0));
  action.kind = HANDOFF_READY;
  REQUIRE((consensus_handoff_receive(&s, &action, CONSENSUS_HANDOFF_HEADER, 5302)) == (ASCIICHAT_OK));
  REQUIRE(consensus_handoff_tick(&s, 5303, &action));
  REQUIRE((action.kind) == (HANDOFF_COMMIT));
}

TEST_CASE("handoff/membership_capacity_rejects_excess_without_mutation", "[handoff]") {
  consensus_handoff_t s;
  consensus_handoff_init(&s, session_id, host_id);
  for (size_t i = 0; i < MAX_SESSION_PARTICIPANTS; ++i) {
    uint8_t id[16] = {0};
    id[0] = (uint8_t)i;
    REQUIRE((consensus_handoff_join(&s, id)) == (ASCIICHAT_OK));
  }
  REQUIRE((s.count) == (MAX_SESSION_PARTICIPANTS));
  uint32_t generation = s.generation;
  uint8_t extra[16] = {0, 1};
  REQUIRE((consensus_handoff_join(&s, extra)) == (ERROR_INVALID_STATE));
  REQUIRE((s.count) == (MAX_SESSION_PARTICIPANTS));
  REQUIRE((s.generation) == (generation));
  uint8_t duplicate[16] = {0};
  REQUIRE((consensus_handoff_join(&s, duplicate)) == (ASCIICHAT_OK));
  REQUIRE((s.count) == (MAX_SESSION_PARTICIPANTS));
}

TEST_CASE("handoff/departure_during_pending_preparation_reissues_the_current_generation", "[handoff]") {
  consensus_handoff_t s = setup();
  sample(&s, fast_id, 100, 110, 1);
  consensus_handoff_message_t action;
  REQUIRE(consensus_handoff_tick(&s, 111, &action));
  REQUIRE((action.kind) == (HANDOFF_PREPARE));
  consensus_handoff_message_t obsolete = action;
  obsolete.kind = HANDOFF_READY;
  consensus_handoff_leave(&s, host_id);
  REQUIRE(!(s.ready));
  REQUIRE(consensus_handoff_tick(&s, 112, &action));
  REQUIRE((action.kind) == (HANDOFF_PREPARE));
  REQUIRE((action.generation) > (obsolete.generation));
  REQUIRE((action.sequence) > (obsolete.sequence));
  uint64_t deadline = s.failure_deadline_ms;
  REQUIRE((consensus_handoff_receive(&s, &obsolete, CONSENSUS_HANDOFF_HEADER, 113)) == (ERROR_NETWORK_PROTOCOL));
  REQUIRE(!(s.ready));
  REQUIRE((s.failure_deadline_ms) == (deadline));
  action.kind = HANDOFF_READY;
  REQUIRE((consensus_handoff_receive(&s, &action, CONSENSUS_HANDOFF_HEADER, 114)) == (ASCIICHAT_OK));
  REQUIRE(consensus_handoff_tick(&s, 115, &action));
  REQUIRE((action.kind) == (HANDOFF_COMMIT));
  REQUIRE(std::memcmp((action.participant), (fast_id), (16)) == 0);
}
