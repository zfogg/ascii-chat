/**
 * @file tests/unit/network/consensus/test_state.c
 * @brief Ring consensus state machine unit tests
 */

#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/network/consensus/state.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/network/consensus/topology.h>
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
  m.rtt_ns = (uint16_t)rtt_ms * 1000000;
  m.upload_kbps = upload_kbps;
  m.nat_tier = 0;
  m.stun_probe_success_pct = 100;
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
  consensus_topology_create(participants, num_participants, my_id, &topo);
  return topo;
}

/* ============================================================================
 * Basic Lifecycle Tests
 * ============================================================================ */
// Use verbose logging with debug level enabled and stdout/stderr not disabled
TEST_CASE("state/create_and_destroy", "[state]") {
  consensus_topology_t *topo = make_test_topology(1, 3);
  uint8_t my_id[16];
  make_uuid(my_id, 2);

  consensus_state_t *state = NULL;
  asciichat_error_t err = consensus_state_create(my_id, topo, &state);

  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(state, NULL);

  consensus_state_machine_t current = consensus_state_get_current_state(state);
  ASCIICHAT_REQUIRE_EQ(current, CONSENSUS_STATE_IDLE);

  consensus_state_destroy(state);
  consensus_topology_destroy(topo);
}

TEST_CASE("state/create_invalid_params", "[state]") {
  consensus_topology_t *topo = make_test_topology(1, 3);
  uint8_t my_id[16];
  make_uuid(my_id, 2);

  consensus_state_t *state = NULL;

  /* NULL my_id */
  asciichat_error_t err = consensus_state_create(NULL, topo, &state);
  ASCIICHAT_REQUIRE_NE(err, ASCIICHAT_OK);

  /* NULL topology */
  err = consensus_state_create(my_id, NULL, &state);
  ASCIICHAT_REQUIRE_NE(err, ASCIICHAT_OK);

  /* NULL output */
  err = consensus_state_create(my_id, topo, NULL);
  ASCIICHAT_REQUIRE_NE(err, ASCIICHAT_OK);

  consensus_topology_destroy(topo);
}

/* ============================================================================
 * State Transition Tests
 * ============================================================================ */

TEST_CASE("state/valid_transition_idle_to_collecting", "[state]") {
  consensus_topology_t *topo = make_test_topology(1, 3);
  uint8_t my_id[16];
  make_uuid(my_id, 2);

  consensus_state_t *state = NULL;
  consensus_state_create(my_id, topo, &state);

  asciichat_error_t err = consensus_state_start_collection(state);
  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK);

  consensus_state_machine_t current = consensus_state_get_current_state(state);
  ASCIICHAT_REQUIRE_EQ(current, CONSENSUS_STATE_COLLECTING);

  consensus_state_destroy(state);
  consensus_topology_destroy(topo);
}

TEST_CASE("state/valid_transition_collecting_to_collection_complete_non_leader", "[state]") {
  consensus_topology_t *topo = make_test_topology(0, 3); /* Non-leader */
  uint8_t my_id[16];
  make_uuid(my_id, 1);

  consensus_state_t *state = NULL;
  consensus_state_create(my_id, topo, &state);

  consensus_state_start_collection(state);
  participant_metrics_t m1 = make_metrics(1, 10, 5000);
  participant_metrics_t m2 = make_metrics(2, 20, 4000);
  consensus_state_add_metrics(state, &m1);
  consensus_state_add_metrics(state, &m2);

  asciichat_error_t err = consensus_state_collection_complete(state);
  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK);

  consensus_state_machine_t current = consensus_state_get_current_state(state);
  ASCIICHAT_REQUIRE_EQ(current, CONSENSUS_STATE_IDLE);

  consensus_state_destroy(state);
  consensus_topology_destroy(topo);
}

TEST_CASE("state/valid_transition_collecting_to_election_start_leader", "[state]") {
  consensus_topology_t *topo = make_test_topology(2, 3); /* Leader (last position) */
  uint8_t my_id[16];
  make_uuid(my_id, 3);

  consensus_state_t *state = NULL;
  consensus_state_create(my_id, topo, &state);

  ASCIICHAT_REQUIRE(consensus_state_is_leader(state), "Should be leader");

  consensus_state_start_collection(state);
  participant_metrics_t m1 = make_metrics(1, 10, 5000);
  participant_metrics_t m2 = make_metrics(2, 20, 4000);
  consensus_state_add_metrics(state, &m1);
  consensus_state_add_metrics(state, &m2);

  asciichat_error_t err = consensus_state_collection_complete(state);
  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK);

  consensus_state_machine_t current = consensus_state_get_current_state(state);
  ASCIICHAT_REQUIRE_EQ(current, CONSENSUS_STATE_ELECTION_START);

  consensus_state_destroy(state);
  consensus_topology_destroy(topo);
}

TEST_CASE("state/valid_transition_election_start_to_election_complete", "[state]") {
  consensus_topology_t *topo = make_test_topology(2, 3); /* Leader */
  uint8_t my_id[16];
  make_uuid(my_id, 3);

  consensus_state_t *state = NULL;
  consensus_state_create(my_id, topo, &state);

  consensus_state_start_collection(state);
  participant_metrics_t m1 = make_metrics(1, 10, 5000);
  participant_metrics_t m2 = make_metrics(2, 20, 4000);
  consensus_state_add_metrics(state, &m1);
  consensus_state_add_metrics(state, &m2);
  consensus_state_collection_complete(state);

  asciichat_error_t err = consensus_state_compute_election(state);
  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK);

  consensus_state_machine_t current = consensus_state_get_current_state(state);
  ASCIICHAT_REQUIRE_EQ(current, CONSENSUS_STATE_ELECTION_COMPLETE);

  consensus_state_destroy(state);
  consensus_topology_destroy(topo);
}

TEST_CASE("state/valid_transition_election_complete_to_idle", "[state]") {
  consensus_topology_t *topo = make_test_topology(2, 3); /* Leader */
  uint8_t my_id[16];
  make_uuid(my_id, 3);

  consensus_state_t *state = NULL;
  consensus_state_create(my_id, topo, &state);

  consensus_state_start_collection(state);
  participant_metrics_t m1 = make_metrics(1, 10, 5000);
  participant_metrics_t m2 = make_metrics(2, 20, 4000);
  consensus_state_add_metrics(state, &m1);
  consensus_state_add_metrics(state, &m2);
  consensus_state_collection_complete(state);
  consensus_state_compute_election(state);

  asciichat_error_t err = consensus_state_reset_to_idle(state);
  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK);

  consensus_state_machine_t current = consensus_state_get_current_state(state);
  ASCIICHAT_REQUIRE_EQ(current, CONSENSUS_STATE_IDLE);

  consensus_state_destroy(state);
  consensus_topology_destroy(topo);
}

/* ============================================================================
 * Invalid Transition Tests
 * ============================================================================ */

TEST_CASE("state/invalid_transition_idle_to_election_start", "[state]") {
  consensus_topology_t *topo = make_test_topology(1, 3);
  uint8_t my_id[16];
  make_uuid(my_id, 2);

  consensus_state_t *state = NULL;
  consensus_state_create(my_id, topo, &state);

  /* Can't go directly from IDLE to ELECTION_START */
  asciichat_error_t err = consensus_state_compute_election(state);
  ASCIICHAT_REQUIRE_NE(err, ASCIICHAT_OK);

  consensus_state_destroy(state);
  consensus_topology_destroy(topo);
}

TEST_CASE("state/invalid_transition_idle_to_collection_complete", "[state]") {
  consensus_topology_t *topo = make_test_topology(1, 3);
  uint8_t my_id[16];
  make_uuid(my_id, 2);

  consensus_state_t *state = NULL;
  consensus_state_create(my_id, topo, &state);

  /* Can't go from IDLE to COLLECTION_COMPLETE */
  asciichat_error_t err = consensus_state_collection_complete(state);
  ASCIICHAT_REQUIRE_NE(err, ASCIICHAT_OK);

  consensus_state_destroy(state);
  consensus_topology_destroy(topo);
}

TEST_CASE("state/invalid_transition_collecting_to_collecting", "[state]") {
  consensus_topology_t *topo = make_test_topology(1, 3);
  uint8_t my_id[16];
  make_uuid(my_id, 2);

  consensus_state_t *state = NULL;
  consensus_state_create(my_id, topo, &state);
  consensus_state_start_collection(state);

  /* Can't start collection again */
  asciichat_error_t err = consensus_state_start_collection(state);
  ASCIICHAT_REQUIRE_NE(err, ASCIICHAT_OK);

  consensus_state_destroy(state);
  consensus_topology_destroy(topo);
}

TEST_CASE("state/invalid_transition_election_complete_to_collection", "[state]") {
  consensus_topology_t *topo = make_test_topology(2, 3); /* Leader */
  uint8_t my_id[16];
  make_uuid(my_id, 3);

  consensus_state_t *state = NULL;
  consensus_state_create(my_id, topo, &state);

  consensus_state_start_collection(state);
  participant_metrics_t m1 = make_metrics(1, 10, 5000);
  participant_metrics_t m2 = make_metrics(2, 20, 4000);
  consensus_state_add_metrics(state, &m1);
  consensus_state_add_metrics(state, &m2);
  consensus_state_collection_complete(state);
  consensus_state_compute_election(state);

  /* Can't add metrics after election complete */
  asciichat_error_t err = consensus_state_add_metrics(state, &m1);
  ASCIICHAT_REQUIRE_NE(err, ASCIICHAT_OK);

  consensus_state_destroy(state);
  consensus_topology_destroy(topo);
}

/* ============================================================================
 * Metrics Collection Tests
 * ============================================================================ */

TEST_CASE("state/add_single_metric", "[state]") {
  consensus_topology_t *topo = make_test_topology(1, 3);
  uint8_t my_id[16];
  make_uuid(my_id, 2);

  consensus_state_t *state = NULL;
  consensus_state_create(my_id, topo, &state);
  consensus_state_start_collection(state);

  participant_metrics_t m = make_metrics(1, 15, 5000);
  asciichat_error_t err = consensus_state_add_metrics(state, &m);

  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(consensus_state_get_metrics_count(state), 1);

  consensus_state_destroy(state);
  consensus_topology_destroy(topo);
}

TEST_CASE("state/add_multiple_metrics", "[state]") {
  consensus_topology_t *topo = make_test_topology(1, 3);
  uint8_t my_id[16];
  make_uuid(my_id, 2);

  consensus_state_t *state = NULL;
  consensus_state_create(my_id, topo, &state);
  consensus_state_start_collection(state);

  for (int i = 1; i <= 5; i++) {
    participant_metrics_t m = make_metrics(i, 10 + i, 5000 - i * 100);
    consensus_state_add_metrics(state, &m);
  }

  ASCIICHAT_REQUIRE_EQ(consensus_state_get_metrics_count(state), 5);

  consensus_state_destroy(state);
  consensus_topology_destroy(topo);
}

TEST_CASE("state/metrics_array_grows", "[state]") {
  consensus_topology_t *topo = make_test_topology(1, 3);
  uint8_t my_id[16];
  make_uuid(my_id, 2);

  consensus_state_t *state = NULL;
  consensus_state_create(my_id, topo, &state);
  consensus_state_start_collection(state);

  /* Add 20 metrics (initial capacity is 10) */
  for (int i = 1; i <= 20; i++) {
    participant_metrics_t m = make_metrics(i, 10 + i, 5000 - i * 100);
    asciichat_error_t err = consensus_state_add_metrics(state, &m);
    ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK);
  }

  ASCIICHAT_REQUIRE_EQ(consensus_state_get_metrics_count(state), 20);

  consensus_state_destroy(state);
  consensus_topology_destroy(topo);
}

TEST_CASE("state/retrieve_metric_at_index", "[state]") {
  consensus_topology_t *topo = make_test_topology(1, 3);
  uint8_t my_id[16];
  make_uuid(my_id, 2);

  consensus_state_t *state = NULL;
  consensus_state_create(my_id, topo, &state);
  consensus_state_start_collection(state);

  participant_metrics_t m1 = make_metrics(1, 10, 5000);
  participant_metrics_t m2 = make_metrics(2, 20, 4000);
  consensus_state_add_metrics(state, &m1);
  consensus_state_add_metrics(state, &m2);

  participant_metrics_t retrieved;
  asciichat_error_t err = consensus_state_get_metric_at(state, 0, &retrieved);
  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(retrieved.rtt_ns, 10000000);

  err = consensus_state_get_metric_at(state, 1, &retrieved);
  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(retrieved.rtt_ns, 20000000);

  consensus_state_destroy(state);
  consensus_topology_destroy(topo);
}

TEST_CASE("state/retrieve_metric_out_of_bounds", "[state]") {
  consensus_topology_t *topo = make_test_topology(1, 3);
  uint8_t my_id[16];
  make_uuid(my_id, 2);

  consensus_state_t *state = NULL;
  consensus_state_create(my_id, topo, &state);
  consensus_state_start_collection(state);

  participant_metrics_t m1 = make_metrics(1, 10, 5000);
  consensus_state_add_metrics(state, &m1);

  participant_metrics_t retrieved;
  asciichat_error_t err = consensus_state_get_metric_at(state, 5, &retrieved);
  ASCIICHAT_REQUIRE_NE(err, ASCIICHAT_OK);

  consensus_state_destroy(state);
  consensus_topology_destroy(topo);
}

TEST_CASE("state/add_metrics_not_in_collecting_state", "[state]") {
  consensus_topology_t *topo = make_test_topology(1, 3);
  uint8_t my_id[16];
  make_uuid(my_id, 2);

  consensus_state_t *state = NULL;
  consensus_state_create(my_id, topo, &state);

  /* Try to add metrics before starting collection */
  participant_metrics_t m = make_metrics(1, 10, 5000);
  asciichat_error_t err = consensus_state_add_metrics(state, &m);

  ASCIICHAT_REQUIRE_NE(err, ASCIICHAT_OK);

  consensus_state_destroy(state);
  consensus_topology_destroy(topo);
}

/* ============================================================================
 * Election Result Storage Tests
 * ============================================================================ */

TEST_CASE("state/election_result_storage_best_lowest_rtt", "[state]") {
  consensus_topology_t *topo = make_test_topology(2, 3); /* Leader */
  uint8_t my_id[16];
  make_uuid(my_id, 3);

  consensus_state_t *state = NULL;
  consensus_state_create(my_id, topo, &state);

  consensus_state_start_collection(state);
  participant_metrics_t m1 = make_metrics(1, 50, 5000); /* High RTT */
  participant_metrics_t m2 = make_metrics(2, 10, 5000); /* Same bandwidth, lower RTT wins. */
  consensus_state_add_metrics(state, &m1);
  consensus_state_add_metrics(state, &m2);
  consensus_state_collection_complete(state);
  consensus_state_compute_election(state);

  uint8_t host_id[16];
  asciichat_error_t err = consensus_state_get_elected_host(state, host_id);
  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK);

  /* Verify host is participant 2 (lowest RTT) */
  ASCIICHAT_REQUIRE_EQ(host_id[0], 2);

  consensus_state_destroy(state);
  consensus_topology_destroy(topo);
}

TEST_CASE("state/election_result_storage_best_highest_bandwidth", "[state]") {
  consensus_topology_t *topo = make_test_topology(2, 3); /* Leader */
  uint8_t my_id[16];
  make_uuid(my_id, 3);

  consensus_state_t *state = NULL;
  consensus_state_create(my_id, topo, &state);

  consensus_state_start_collection(state);
  participant_metrics_t m1 = make_metrics(1, 10, 2000); /* Low bandwidth */
  participant_metrics_t m2 = make_metrics(2, 10, 5000); /* High bandwidth - should be host */
  consensus_state_add_metrics(state, &m1);
  consensus_state_add_metrics(state, &m2);
  consensus_state_collection_complete(state);
  consensus_state_compute_election(state);

  uint8_t host_id[16];
  asciichat_error_t err = consensus_state_get_elected_host(state, host_id);
  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK);

  /* Verify host is participant 2 (highest bandwidth) */
  ASCIICHAT_REQUIRE_EQ(host_id[0], 2);

  consensus_state_destroy(state);
  consensus_topology_destroy(topo);
}

TEST_CASE("state/election_result_backup_selection", "[state]") {
  consensus_topology_t *topo = make_test_topology(2, 3); /* Leader */
  uint8_t my_id[16];
  make_uuid(my_id, 3);

  consensus_state_t *state = NULL;
  consensus_state_create(my_id, topo, &state);

  consensus_state_start_collection(state);
  participant_metrics_t m1 = make_metrics(1, 10, 5000); /* Best */
  participant_metrics_t m2 = make_metrics(2, 20, 4000); /* Second best */
  participant_metrics_t m3 = make_metrics(3, 50, 2000); /* Worst */
  consensus_state_add_metrics(state, &m1);
  consensus_state_add_metrics(state, &m2);
  consensus_state_add_metrics(state, &m3);
  consensus_state_collection_complete(state);
  consensus_state_compute_election(state);

  uint8_t host_id[16];
  uint8_t backup_id[16];
  consensus_state_get_elected_host(state, host_id);
  asciichat_error_t err = consensus_state_get_elected_backup(state, backup_id);

  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(host_id[0], 1);   /* Best */
  ASCIICHAT_REQUIRE_EQ(backup_id[0], 2); /* Second best */

  consensus_state_destroy(state);
  consensus_topology_destroy(topo);
}

TEST_CASE("state/get_election_result_not_complete", "[state]") {
  consensus_topology_t *topo = make_test_topology(2, 3);
  uint8_t my_id[16];
  make_uuid(my_id, 3);

  consensus_state_t *state = NULL;
  consensus_state_create(my_id, topo, &state);

  uint8_t host_id[16];
  /* Try to get election result without completing election */
  asciichat_error_t err = consensus_state_get_elected_host(state, host_id);

  ASCIICHAT_REQUIRE_NE(err, ASCIICHAT_OK);

  consensus_state_destroy(state);
  consensus_topology_destroy(topo);
}

/* ============================================================================
 * Leader Detection Tests
 * ============================================================================ */

TEST_CASE("state/is_leader_true_for_last_position", "[state]") {
  consensus_topology_t *topo = make_test_topology(2, 3); /* Last position (leader) */
  uint8_t my_id[16];
  make_uuid(my_id, 3);

  consensus_state_t *state = NULL;
  consensus_state_create(my_id, topo, &state);

  ASCIICHAT_REQUIRE(consensus_state_is_leader(state));

  consensus_state_destroy(state);
  consensus_topology_destroy(topo);
}

TEST_CASE("state/is_leader_false_for_non_last_position", "[state]") {
  consensus_topology_t *topo = make_test_topology(0, 3); /* First position (not leader) */
  uint8_t my_id[16];
  make_uuid(my_id, 1);

  consensus_state_t *state = NULL;
  consensus_state_create(my_id, topo, &state);

  ASCIICHAT_REQUIRE_NOT(consensus_state_is_leader(state));

  consensus_state_destroy(state);
  consensus_topology_destroy(topo);
}

TEST_CASE("state/is_leader_true_single_participant", "[state]") {
  consensus_topology_t *topo = make_test_topology(0, 1); /* Only participant */
  uint8_t my_id[16];
  make_uuid(my_id, 1);

  consensus_state_t *state = NULL;
  consensus_state_create(my_id, topo, &state);

  ASCIICHAT_REQUIRE(consensus_state_is_leader(state));

  consensus_state_destroy(state);
  consensus_topology_destroy(topo);
}
