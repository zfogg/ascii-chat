/**
 * @file tests/unit/network/consensus/test_coordinator.c
 * @brief Ring consensus coordinator unit tests
 */

#include <criterion/criterion.h>
#include <ascii-chat/network/consensus/coordinator.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/network/consensus/election.h>
#include <ascii-chat/util/time.h>
#include <string.h>

/* Test helper: create UUID for testing */
static void make_uuid(uint8_t id[16], int value) {
  memset(id, 0, 16);
  id[0] = value & 0xFF;
}

/* Test helper: create simple metrics for testing */
static participant_metrics_t make_metrics(int participant_idx, uint16_t rtt_ms, uint32_t upload_kbps) {
  participant_metrics_t m;
  memset(&m, 0, sizeof(m));
  make_uuid(m.participant_id, participant_idx);
  m.rtt_ns = (uint64_t)rtt_ms * 1000000;
  m.upload_kbps = upload_kbps;
  m.nat_tier = 0;
  m.stun_probe_success_pct = 100;
  m.connection_type = 0;
  m.measurement_time_ns = time_get_realtime_ns();
  m.measurement_window_ns = 1000000000ULL; /* 1 second */
  return m;
}

/* Test helper: create topology for testing */
static consensus_topology_t *make_test_topology(int my_idx, int num_participants) {
  uint8_t participants[64][16];
  for (int i = 0; i < num_participants; i++) {
    make_uuid(participants[i], i + 1);
  }
  uint8_t my_id[16];
  make_uuid(my_id, my_idx + 1);

  consensus_topology_t *topo = NULL;
  cr_assert_eq(consensus_topology_create(participants, num_participants, my_id, &topo), ASCIICHAT_OK);
  return topo;
}

/* Mock election function for testing */
static asciichat_error_t mock_election_func(void *context, consensus_state_t *state) {
  (void)context;
  (void)state;
  /* Just return success for testing */
  return ASCIICHAT_OK;
}

/* ============================================================================
 * Basic Lifecycle Tests
 * ============================================================================ */
// Use verbose logging with debug level enabled and stdout/stderr not disabled
TestSuite(coordinator);

Test(coordinator, create_and_destroy) {
  consensus_topology_t *topo = make_test_topology(1, 3);
  uint8_t my_id[16];
  make_uuid(my_id, 2);

  consensus_coordinator_t *coordinator = NULL;
  asciichat_error_t err = consensus_coordinator_create(my_id, topo, mock_election_func, NULL, &coordinator);

  cr_assert_eq(err, ASCIICHAT_OK);
  cr_assert_neq(coordinator, NULL);

  /* Verify initial state is IDLE */
  consensus_state_machine_t state = consensus_coordinator_get_state(coordinator);
  cr_assert_eq(state, CONSENSUS_STATE_IDLE);

  consensus_coordinator_destroy(coordinator);
  consensus_topology_destroy(topo);
}

Test(coordinator, create_invalid_params) {
  consensus_topology_t *topo = make_test_topology(1, 3);
  uint8_t my_id[16];
  make_uuid(my_id, 2);

  consensus_coordinator_t *coordinator = NULL;

  /* NULL my_id */
  asciichat_error_t err = consensus_coordinator_create(NULL, topo, mock_election_func, NULL, &coordinator);
  cr_assert_neq(err, ASCIICHAT_OK);

  /* NULL topology */
  err = consensus_coordinator_create(my_id, NULL, mock_election_func, NULL, &coordinator);
  cr_assert_neq(err, ASCIICHAT_OK);

  /* NULL election_func */
  err = consensus_coordinator_create(my_id, topo, NULL, NULL, &coordinator);
  cr_assert_neq(err, ASCIICHAT_OK);

  /* NULL output */
  err = consensus_coordinator_create(my_id, topo, mock_election_func, NULL, NULL);
  cr_assert_neq(err, ASCIICHAT_OK);

  consensus_topology_destroy(topo);
}

/* ============================================================================
 * Round Scheduling Tests
 * ============================================================================ */

Test(coordinator, round_scheduling_initial_interval) {
  consensus_topology_t *topo = make_test_topology(2, 3); /* I'm the leader */
  uint8_t my_id[16];
  make_uuid(my_id, 3); /* Leader is at position 2 (0-indexed) */

  consensus_coordinator_t *coordinator = NULL;
  cr_assert_eq(consensus_coordinator_create(my_id, topo, mock_election_func, NULL, &coordinator), ASCIICHAT_OK);

  /* Check that next round is scheduled ~5 minutes in future */
  uint64_t time_until_next = consensus_coordinator_time_until_next_round(coordinator);
  uint64_t five_minutes_ns = 5ULL * 60 * 1000000000;

  /* Should be close to 5 minutes, allowing some tolerance for test execution time */
  cr_assert_gt(time_until_next, five_minutes_ns - 1000000000ULL); /* Within 1 second of 5 min */
  cr_assert(time_until_next <= five_minutes_ns);

  consensus_coordinator_destroy(coordinator);
  consensus_topology_destroy(topo);
}

Test(coordinator, process_does_not_start_round_if_not_leader) {
  consensus_topology_t *topo = make_test_topology(0, 3); /* I'm not the leader */
  uint8_t my_id[16];
  make_uuid(my_id, 1);

  consensus_coordinator_t *coordinator = NULL;
  cr_assert_eq(consensus_coordinator_create(my_id, topo, mock_election_func, NULL, &coordinator), ASCIICHAT_OK);

  /* Process should not start a round since we're not the leader */
  asciichat_error_t err = consensus_coordinator_process(coordinator, 100);
  cr_assert_eq(err, ASCIICHAT_OK);

  /* State should still be IDLE */
  consensus_state_machine_t state = consensus_coordinator_get_state(coordinator);
  cr_assert_eq(state, CONSENSUS_STATE_IDLE);

  consensus_coordinator_destroy(coordinator);
  consensus_topology_destroy(topo);
}

/* ============================================================================
 * Leader Initiates Collection Tests
 * ============================================================================ */

Test(coordinator, leader_initiates_collection) {
  consensus_topology_t *topo = make_test_topology(2, 3); /* I'm the leader */
  uint8_t my_id[16];
  make_uuid(my_id, 3);

  consensus_coordinator_t *coordinator = NULL;
  cr_assert_eq(consensus_coordinator_create(my_id, topo, mock_election_func, NULL, &coordinator), ASCIICHAT_OK);

  /* Simulate 5+ minute wait */
  /* We can't actually wait 5 minutes in a test, so we'll test collection_start manually */
  asciichat_error_t err =
      consensus_coordinator_on_collection_start(coordinator, 1, time_get_realtime_ns() + 30000000000ULL);
  cr_assert_eq(err, ASCIICHAT_OK);

  /* State should transition to COLLECTING */
  consensus_state_machine_t state = consensus_coordinator_get_state(coordinator);
  cr_assert_eq(state, CONSENSUS_STATE_COLLECTING);

  /* Should have at least our own metrics */
  int metrics_count = consensus_coordinator_get_metrics_count(coordinator);
  cr_assert_gt(metrics_count, 0);

  consensus_coordinator_destroy(coordinator);
  consensus_topology_destroy(topo);
}

/* ============================================================================
 * Participant Relay Tests
 * ============================================================================ */

Test(coordinator, participant_receives_and_relays_metrics) {
  consensus_topology_t *topo = make_test_topology(1, 3); /* I'm not the leader */
  uint8_t my_id[16];
  make_uuid(my_id, 2);

  consensus_coordinator_t *coordinator = NULL;
  cr_assert_eq(consensus_coordinator_create(my_id, topo, mock_election_func, NULL, &coordinator), ASCIICHAT_OK);

  /* Receive collection start */
  uint64_t deadline_ns = time_get_realtime_ns() + 30000000000ULL;
  asciichat_error_t err = consensus_coordinator_on_collection_start(coordinator, 1, deadline_ns);
  cr_assert_eq(err, ASCIICHAT_OK);

  /* State should be COLLECTING */
  consensus_state_machine_t state = consensus_coordinator_get_state(coordinator);
  cr_assert_eq(state, CONSENSUS_STATE_COLLECTING);

  /* Should have our own metric */
  int count_before = consensus_coordinator_get_metrics_count(coordinator);
  cr_assert_gt(count_before, 0);

  /* Receive metrics from previous participant */
  uint8_t sender_id[16];
  make_uuid(sender_id, 1);
  participant_metrics_t metrics = make_metrics(1, 50, 100000);

  err = consensus_coordinator_on_stats_update(coordinator, sender_id, &metrics, 1);
  cr_assert_eq(err, ASCIICHAT_OK);

  /* Should have added the new metric */
  int count_after = consensus_coordinator_get_metrics_count(coordinator);
  cr_assert_eq(count_after, count_before + 1);

  consensus_coordinator_destroy(coordinator);
  consensus_topology_destroy(topo);
}

Test(coordinator, stats_update_rejects_invalid_state) {
  consensus_topology_t *topo = make_test_topology(1, 3);
  uint8_t my_id[16];
  make_uuid(my_id, 2);

  consensus_coordinator_t *coordinator = NULL;
  cr_assert_eq(consensus_coordinator_create(my_id, topo, mock_election_func, NULL, &coordinator), ASCIICHAT_OK);

  /* Try to update stats while in IDLE state (should fail) */
  uint8_t sender_id[16];
  make_uuid(sender_id, 1);
  participant_metrics_t metrics = make_metrics(1, 50, 100000);

  asciichat_error_t err = consensus_coordinator_on_stats_update(coordinator, sender_id, &metrics, 1);
  cr_assert_neq(err, ASCIICHAT_OK);

  consensus_coordinator_destroy(coordinator);
  consensus_topology_destroy(topo);
}

/* ============================================================================
 * Election Result Storage Tests
 * ============================================================================ */

Test(coordinator, stores_election_result) {
  consensus_topology_t *topo = make_test_topology(1, 3);
  uint8_t my_id[16];
  make_uuid(my_id, 2);

  consensus_coordinator_t *coordinator = NULL;
  cr_assert_eq(consensus_coordinator_create(my_id, topo, mock_election_func, NULL, &coordinator), ASCIICHAT_OK);

  /* Receive election result */
  uint8_t host_id[16], backup_id[16];
  make_uuid(host_id, 1);
  make_uuid(backup_id, 3);

  asciichat_error_t err = consensus_coordinator_on_election_result(coordinator, host_id, backup_id);
  cr_assert_eq(err, ASCIICHAT_OK);

  /* Query the stored result */
  uint8_t retrieved_host[16], retrieved_backup[16];
  err = consensus_coordinator_get_current_host(coordinator, retrieved_host, retrieved_backup);
  cr_assert_eq(err, ASCIICHAT_OK);

  /* Verify we got the same IDs back */
  cr_assert_eq(memcmp(retrieved_host, host_id, 16), 0);
  cr_assert_eq(memcmp(retrieved_backup, backup_id, 16), 0);

  consensus_coordinator_destroy(coordinator);
  consensus_topology_destroy(topo);
}

Test(coordinator, get_current_host_uses_fallback) {
  consensus_topology_t *topo = make_test_topology(1, 3);
  uint8_t my_id[16];
  make_uuid(my_id, 2);

  consensus_coordinator_t *coordinator = NULL;
  cr_assert_eq(consensus_coordinator_create(my_id, topo, mock_election_func, NULL, &coordinator), ASCIICHAT_OK);

  /* Before any election, should fail */
  uint8_t host_id[16], backup_id[16];
  asciichat_error_t err = consensus_coordinator_get_current_host(coordinator, host_id, backup_id);
  cr_assert_neq(err, ASCIICHAT_OK);

  /* Receive and store an election result */
  uint8_t result_host[16], result_backup[16];
  make_uuid(result_host, 1);
  make_uuid(result_backup, 3);

  err = consensus_coordinator_on_election_result(coordinator, result_host, result_backup);
  cr_assert_eq(err, ASCIICHAT_OK);

  /* Now should succeed and return the stored result */
  err = consensus_coordinator_get_current_host(coordinator, host_id, backup_id);
  cr_assert_eq(err, ASCIICHAT_OK);
  cr_assert_eq(memcmp(host_id, result_host, 16), 0);
  cr_assert_eq(memcmp(backup_id, result_backup, 16), 0);

  consensus_coordinator_destroy(coordinator);
  consensus_topology_destroy(topo);
}

/* ============================================================================
 * Timeout Handling Tests
 * ============================================================================ */

Test(coordinator, collection_completion_on_timeout) {
  consensus_topology_t *topo = make_test_topology(1, 3);
  uint8_t my_id[16];
  make_uuid(my_id, 2);

  consensus_coordinator_t *coordinator = NULL;
  cr_assert_eq(consensus_coordinator_create(my_id, topo, mock_election_func, NULL, &coordinator), ASCIICHAT_OK);

  /* Start collection with deadline already passed (use time_get_ns, not realtime)
     Set deadline to 0 (epoch) so it's definitely in the past compared to any call to time_get_ns() */
  uint64_t past_deadline = 0;
  asciichat_error_t err = consensus_coordinator_on_collection_start(coordinator, 1, past_deadline);
  cr_assert_eq(err, ASCIICHAT_OK);

  consensus_state_machine_t state = consensus_coordinator_get_state(coordinator);
  cr_assert_eq(state, CONSENSUS_STATE_COLLECTING);

  /* Process should detect timeout and handle it */
  err = consensus_coordinator_process(coordinator, 100);
  cr_assert_eq(err, ASCIICHAT_OK);

  /* After timeout, we should be out of COLLECTING state */
  state = consensus_coordinator_get_state(coordinator);
  cr_assert_neq(state, CONSENSUS_STATE_COLLECTING);

  consensus_coordinator_destroy(coordinator);
  consensus_topology_destroy(topo);
}

Test(coordinator, invalid_params_in_collection_start) {
  consensus_topology_t *topo = make_test_topology(1, 3);
  uint8_t my_id[16];
  make_uuid(my_id, 2);

  consensus_coordinator_t *coordinator = NULL;
  cr_assert_eq(consensus_coordinator_create(my_id, topo, mock_election_func, NULL, &coordinator), ASCIICHAT_OK);

  /* NULL coordinator */
  asciichat_error_t err = consensus_coordinator_on_collection_start(NULL, 1, time_get_realtime_ns());
  cr_assert_neq(err, ASCIICHAT_OK);

  consensus_coordinator_destroy(coordinator);
  consensus_topology_destroy(topo);
}

/* ============================================================================
 * Topology Update Tests
 * ============================================================================ */

Test(coordinator, on_ring_members_updates_topology) {
  consensus_topology_t *topo1 = make_test_topology(1, 3);
  uint8_t my_id[16];
  make_uuid(my_id, 2);

  consensus_coordinator_t *coordinator = NULL;
  cr_assert_eq(consensus_coordinator_create(my_id, topo1, mock_election_func, NULL, &coordinator), ASCIICHAT_OK);

  /* Create new topology */
  consensus_topology_t *topo2 = make_test_topology(1, 4);

  /* Update coordinator with new topology */
  asciichat_error_t err = consensus_coordinator_on_ring_members(coordinator, topo2);
  cr_assert_eq(err, ASCIICHAT_OK);

  /* State should be updated */
  consensus_state_machine_t state = consensus_coordinator_get_state(coordinator);
  cr_assert_eq(state, CONSENSUS_STATE_IDLE);

  consensus_coordinator_destroy(coordinator);
  consensus_topology_destroy(topo1);
  consensus_topology_destroy(topo2);
}

Test(coordinator, on_ring_members_resets_state_during_collection) {
  consensus_topology_t *topo1 = make_test_topology(1, 3);
  uint8_t my_id[16];
  make_uuid(my_id, 2);

  consensus_coordinator_t *coordinator = NULL;
  cr_assert_eq(consensus_coordinator_create(my_id, topo1, mock_election_func, NULL, &coordinator), ASCIICHAT_OK);

  /* Start collection */
  asciichat_error_t err =
      consensus_coordinator_on_collection_start(coordinator, 1, time_get_realtime_ns() + 30000000000ULL);
  cr_assert_eq(err, ASCIICHAT_OK);

  consensus_state_machine_t state = consensus_coordinator_get_state(coordinator);
  cr_assert_eq(state, CONSENSUS_STATE_COLLECTING);

  /* Create new topology */
  consensus_topology_t *topo2 = make_test_topology(1, 4);

  /* Update should reset state */
  err = consensus_coordinator_on_ring_members(coordinator, topo2);
  cr_assert_eq(err, ASCIICHAT_OK);

  /* State should be back to IDLE */
  state = consensus_coordinator_get_state(coordinator);
  cr_assert_eq(state, CONSENSUS_STATE_IDLE);

  consensus_coordinator_destroy(coordinator);
  consensus_topology_destroy(topo1);
  consensus_topology_destroy(topo2);
}

/* ============================================================================
 * Process Tests
 * ============================================================================ */

Test(coordinator, process_tolerates_null_timeout) {
  consensus_topology_t *topo = make_test_topology(0, 3);
  uint8_t my_id[16];
  make_uuid(my_id, 1);

  consensus_coordinator_t *coordinator = NULL;
  cr_assert_eq(consensus_coordinator_create(my_id, topo, mock_election_func, NULL, &coordinator), ASCIICHAT_OK);

  /* Process with different timeout values should work */
  asciichat_error_t err = consensus_coordinator_process(coordinator, 0);
  cr_assert_eq(err, ASCIICHAT_OK);

  err = consensus_coordinator_process(coordinator, 100);
  cr_assert_eq(err, ASCIICHAT_OK);

  err = consensus_coordinator_process(coordinator, 1000);
  cr_assert_eq(err, ASCIICHAT_OK);

  consensus_coordinator_destroy(coordinator);
  consensus_topology_destroy(topo);
}

Test(coordinator, process_rejects_null_coordinator) {
  asciichat_error_t err = consensus_coordinator_process(NULL, 100);
  cr_assert_neq(err, ASCIICHAT_OK);
}

Test(coordinator, get_state_of_null_coordinator) {
  consensus_state_machine_t state = consensus_coordinator_get_state(NULL);
  cr_assert_eq(state, CONSENSUS_STATE_FAILED);
}

Test(coordinator, time_until_next_round_with_null) {
  uint64_t time_until = consensus_coordinator_time_until_next_round(NULL);
  cr_assert_eq(time_until, 0);
}

/* ============================================================================
 * Election Result Fallback Tests
 * ============================================================================ */

Test(coordinator, election_result_persists_across_states) {
  consensus_topology_t *topo = make_test_topology(1, 3);
  uint8_t my_id[16];
  make_uuid(my_id, 2);

  consensus_coordinator_t *coordinator = NULL;
  cr_assert_eq(consensus_coordinator_create(my_id, topo, mock_election_func, NULL, &coordinator), ASCIICHAT_OK);

  /* Store an election result */
  uint8_t host_id[16], backup_id[16];
  make_uuid(host_id, 1);
  make_uuid(backup_id, 3);

  asciichat_error_t err = consensus_coordinator_on_election_result(coordinator, host_id, backup_id);
  cr_assert_eq(err, ASCIICHAT_OK);

  /* Start a new collection */
  err = consensus_coordinator_on_collection_start(coordinator, 2, time_get_realtime_ns() + 30000000000ULL);
  cr_assert_eq(err, ASCIICHAT_OK);

  /* During collection, old result should still be retrievable */
  uint8_t retrieved_host[16], retrieved_backup[16];
  err = consensus_coordinator_get_current_host(coordinator, retrieved_host, retrieved_backup);
  cr_assert_eq(err, ASCIICHAT_OK);
  cr_assert_eq(memcmp(retrieved_host, host_id, 16), 0);
  cr_assert_eq(memcmp(retrieved_backup, backup_id, 16), 0);

  consensus_coordinator_destroy(coordinator);
  consensus_topology_destroy(topo);
}

Test(coordinator, metrics_count_increases_with_updates) {
  consensus_topology_t *topo = make_test_topology(1, 3);
  uint8_t my_id[16];
  make_uuid(my_id, 2);

  consensus_coordinator_t *coordinator = NULL;
  cr_assert_eq(consensus_coordinator_create(my_id, topo, mock_election_func, NULL, &coordinator), ASCIICHAT_OK);

  /* Start collection */
  asciichat_error_t err =
      consensus_coordinator_on_collection_start(coordinator, 1, time_get_realtime_ns() + 30000000000ULL);
  cr_assert_eq(err, ASCIICHAT_OK);

  int count_after_start = consensus_coordinator_get_metrics_count(coordinator);
  cr_assert_gt(count_after_start, 0);

  /* Add more metrics */
  uint8_t sender_id[16];
  make_uuid(sender_id, 1);

  participant_metrics_t metrics[2];
  metrics[0] = make_metrics(1, 50, 100000);
  metrics[1] = make_metrics(3, 75, 80000);

  err = consensus_coordinator_on_stats_update(coordinator, sender_id, metrics, 2);
  cr_assert_eq(err, ASCIICHAT_OK);

  int count_after_update = consensus_coordinator_get_metrics_count(coordinator);
  cr_assert_eq(count_after_update, count_after_start + 2);

  consensus_coordinator_destroy(coordinator);
  consensus_topology_destroy(topo);
}
