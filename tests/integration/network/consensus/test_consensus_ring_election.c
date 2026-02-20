/**
 * @file test_consensus_ring_election.c
 * @brief Integration test for ring consensus protocol
 *
 * Tests the complete consensus flow:
 * - Ring topology formation
 * - Metrics collection around the ring
 * - Leader election computation
 * - Result propagation to all participants
 * - All participants converge on same elected host
 */

#include <criterion/criterion.h>
#include <ascii-chat/network/consensus/coordinator.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/network/consensus/topology.h>
#include <ascii-chat/network/consensus/election.h>
#include <ascii-chat/network/consensus/state.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// Test Fixtures and Helpers
// ============================================================================

/**
 * Simulates a participant in the consensus ring
 */
typedef struct {
  uint8_t id[16];
  consensus_coordinator_t *coordinator;
  consensus_topology_t *topology;
  consensus_state_t *state;

  // Simulation state
  participant_metrics_t my_metrics;
  uint8_t elected_host[16];
  uint8_t elected_backup[16];
  bool has_election_result;
} participant_t;

/**
 * Create a UUID for testing
 */
static void make_test_id(uint8_t id[16], int value) {
  memset(id, 0, 16);
  id[0] = (value >> 24) & 0xFF;
  id[1] = (value >> 16) & 0xFF;
  id[2] = (value >> 8) & 0xFF;
  id[3] = value & 0xFF;
}

/**
 * Create realistic metrics for a participant
 */
static void make_test_metrics(participant_metrics_t *m, int participant_id, int nat_tier, uint32_t upload_kbps,
                              uint32_t rtt_ns) {
  memset(m, 0, sizeof(*m));
  make_test_id(m->participant_id, participant_id);
  m->nat_tier = nat_tier;
  m->upload_kbps = upload_kbps;
  m->rtt_ns = rtt_ns;
  m->stun_probe_success_pct = 95;
  m->connection_type = 0; // Direct
  snprintf((char *)m->public_address, sizeof(m->public_address), "192.168.1.%d", participant_id);
  m->public_port = 27224 + participant_id;
}

/**
 * Election callback for testing - just runs the algorithm
 */
static asciichat_error_t test_election_callback(void *context, consensus_state_t *state) {
  participant_t *p = (participant_t *)context;

  // Run deterministic election
  participant_metrics_t *metrics = NULL;
  int metrics_count = consensus_state_get_metrics_count(state);

  if (metrics_count <= 0) {
    log_warn("No metrics available for election");
    return ASCIICHAT_OK;
  }

  // Allocate space for metrics
  metrics = malloc(sizeof(participant_metrics_t) * metrics_count);

  // Get all collected metrics by iterating
  for (int i = 0; i < metrics_count; i++) {
    asciichat_error_t err = consensus_state_get_metric_at(state, i, &metrics[i]);
    if (err != ASCIICHAT_OK) {
      free(metrics);
      return err;
    }
  }

  // Run election
  int best_idx, backup_idx;
  asciichat_error_t err = consensus_election_choose_hosts(metrics, metrics_count, &best_idx, &backup_idx);
  if (err != ASCIICHAT_OK) {
    free(metrics);
    return err;
  }

  // Store results
  memcpy(p->elected_host, metrics[best_idx].participant_id, 16);
  memcpy(p->elected_backup, metrics[backup_idx].participant_id, 16);
  p->has_election_result = true;

  log_info("Election computed: host=%u, backup=%u", p->elected_host[0], p->elected_backup[0]);

  free(metrics);
  return consensus_state_compute_election(state);
}

/**
 * Destroy a participant
 */
static void destroy_participant(participant_t *p) {
  if (!p)
    return;
  if (p->coordinator)
    consensus_coordinator_destroy(p->coordinator);
  if (p->state)
    consensus_state_destroy(p->state);
  if (p->topology)
    consensus_topology_destroy(p->topology);
  memset(p, 0, sizeof(*p));
}

// ============================================================================
// Integration Tests
// ============================================================================

/**
 * Test basic topology formation with 4 participants
 */
// Use verbose logging with debug level enabled and stdout/stderr not disabled
TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(consensus_integration, LOG_DEBUG, LOG_DEBUG, false, false);

Test(consensus_integration, topology_formation) {
  // Create 4 participants
  uint8_t participants[4][16];
  participant_t parts[4];

  for (int i = 0; i < 4; i++) {
    make_test_id(participants[i], i + 1);
    memset(&parts[i], 0, sizeof(participant_t));
    memcpy(parts[i].id, participants[i], 16);
  }

  // Create topology for each participant
  for (int i = 0; i < 4; i++) {
    asciichat_error_t err = consensus_topology_create(participants, 4, participants[i], &parts[i].topology);
    cr_assert_eq(err, ASCIICHAT_OK);
  }

  // Verify positions (should be sorted lexicographically)
  for (int i = 0; i < 4; i++) {
    int pos = consensus_topology_get_position(parts[i].topology);
    cr_assert_eq(pos, i, "Participant %d has wrong position %d", i, pos);
  }

  // Verify leader is last
  cr_assert_eq(consensus_topology_am_leader(parts[3].topology), true);
  for (int i = 0; i < 3; i++) {
    cr_assert_eq(consensus_topology_am_leader(parts[i].topology), false);
  }

  // Cleanup
  for (int i = 0; i < 4; i++) {
    consensus_topology_destroy(parts[i].topology);
  }
}

/**
 * Test ring navigation (next/prev)
 */
Test(consensus_integration, ring_navigation) {
  // Create 3 participants
  uint8_t participants[3][16];
  participant_t parts[3];

  for (int i = 0; i < 3; i++) {
    make_test_id(participants[i], i + 100);
    memset(&parts[i], 0, sizeof(participant_t));
    memcpy(parts[i].id, participants[i], 16);
  }

  // Create topologies
  for (int i = 0; i < 3; i++) {
    consensus_topology_create(participants, 3, participants[i], &parts[i].topology);
  }

  // Verify ring navigation
  // Sorted order: 100, 101, 102
  // So: 100->101, 101->102, 102->100 (circular)

  uint8_t next_id[16];

  // Participant 0 (100) should have next=101
  consensus_topology_get_next(parts[0].topology, next_id);
  cr_assert_eq(next_id[3], 101);

  // Participant 1 (101) should have next=102
  consensus_topology_get_next(parts[1].topology, next_id);
  cr_assert_eq(next_id[3], 102);

  // Participant 2 (102) should have next=100 (wraparound)
  consensus_topology_get_next(parts[2].topology, next_id);
  cr_assert_eq(next_id[3], 100);

  // Cleanup
  for (int i = 0; i < 3; i++) {
    consensus_topology_destroy(parts[i].topology);
  }
}

/**
 * Test complete election with deterministic scoring
 */
Test(consensus_integration, complete_election_flow) {
  // Create 4 participants with different network characteristics
  uint8_t participants[4][16];
  participant_metrics_t metrics[4];

  make_test_id(participants[0], 1);
  make_test_metrics(&metrics[0], 1, 2, 50000, 40 * 1000000); // Medium NAT, 50Mbps, 40ms

  make_test_id(participants[1], 2);
  make_test_metrics(&metrics[1], 2, 0, 100000, 20 * 1000000); // LAN, 100Mbps, 20ms (BEST)

  make_test_id(participants[2], 3);
  make_test_metrics(&metrics[2], 3, 1, 75000, 30 * 1000000); // Good NAT, 75Mbps, 30ms (SECOND)

  make_test_id(participants[3], 4);
  make_test_metrics(&metrics[3], 4, 3, 10000, 100 * 1000000); // STUN NAT, 10Mbps, 100ms

  // Verify scores are correct
  uint32_t scores[4];
  scores[0] = consensus_election_compute_score(&metrics[0]);
  scores[1] = consensus_election_compute_score(&metrics[1]);
  scores[2] = consensus_election_compute_score(&metrics[2]);
  scores[3] = consensus_election_compute_score(&metrics[3]);

  cr_assert_gt(scores[1], scores[2], "Participant 2 (LAN) should score higher than 3");
  cr_assert_gt(scores[2], scores[0], "Participant 3 should score higher than 1");

  // Run election
  int best_idx, backup_idx;
  asciichat_error_t err = consensus_election_choose_hosts(metrics, 4, &best_idx, &backup_idx);

  cr_assert_eq(err, ASCIICHAT_OK);
  cr_assert_eq(best_idx, 1, "Participant 2 (index 1) should be elected as best host");
  cr_assert_eq(backup_idx, 2, "Participant 3 (index 2) should be elected as backup");

  // Verify all participants would agree on this election
  uint8_t announced_host[16], announced_backup[16];
  memcpy(announced_host, participants[1], 16);
  memcpy(announced_backup, participants[2], 16);

  bool valid = false;
  err = consensus_election_verify(metrics, 4, announced_host, announced_backup, &valid);

  cr_assert_eq(err, ASCIICHAT_OK);
  cr_assert_eq(valid, true, "Election result should be verifiable by all participants");
}

/**
 * Test state machine transitions
 */
Test(consensus_integration, state_machine_transitions) {
  uint8_t participants[2][16];
  make_test_id(participants[0], 10);
  make_test_id(participants[1], 20);

  consensus_topology_t *topo = NULL;
  consensus_state_t *state = NULL;

  // Create topology
  asciichat_error_t err = consensus_topology_create(participants, 2, participants[0], &topo);
  cr_assert_eq(err, ASCIICHAT_OK);

  // Create state machine
  err = consensus_state_create(participants[0], topo, &state);
  cr_assert_eq(err, ASCIICHAT_OK);

  // Initial state should be IDLE
  consensus_state_machine_t current = consensus_state_get_current_state(state);
  cr_assert_eq(current, CONSENSUS_STATE_IDLE);

  // Transition to COLLECTING
  err = consensus_state_start_collection(state);
  cr_assert_eq(err, ASCIICHAT_OK);
  current = consensus_state_get_current_state(state);
  cr_assert_eq(current, CONSENSUS_STATE_COLLECTING);

  // Add some metrics
  participant_metrics_t m1 = {0};
  make_test_id(m1.participant_id, 10);
  m1.nat_tier = 0;
  m1.upload_kbps = 100000;
  m1.rtt_ns = 10 * 1000000; // 10ms in nanoseconds
  m1.stun_probe_success_pct = 100;

  err = consensus_state_add_metrics(state, &m1);
  cr_assert_eq(err, ASCIICHAT_OK);

  participant_metrics_t m2 = {0};
  make_test_id(m2.participant_id, 20);
  m2.nat_tier = 1;
  m2.upload_kbps = 50000;
  m2.rtt_ns = 20 * 1000000; // 20ms in nanoseconds
  m2.stun_probe_success_pct = 95;

  err = consensus_state_add_metrics(state, &m2);
  cr_assert_eq(err, ASCIICHAT_OK);

  // Transition to COLLECTION_COMPLETE
  err = consensus_state_collection_complete(state);
  cr_assert_eq(err, ASCIICHAT_OK);

  // Should transition to ELECTION_START (I am leader, position 0, which is NOT last, but let's check)
  // Actually, position 0 means I'm first when sorted, and last position is leader
  // So I'm not leader, should go back to IDLE
  current = consensus_state_get_current_state(state);
  cr_assert_eq(current, CONSENSUS_STATE_IDLE);

  // Cleanup
  consensus_state_destroy(state);
  consensus_topology_destroy(topo);
}

/**
 * Test coordinator creation and lifecycle
 */
Test(consensus_integration, coordinator_lifecycle) {
  uint8_t participants[3][16];
  participant_t parts[3];

  for (int i = 0; i < 3; i++) {
    make_test_id(participants[i], i + 1000);
    memset(&parts[i], 0, sizeof(participant_t));
    memcpy(parts[i].id, participants[i], 16);
  }

  // Create topology
  for (int i = 0; i < 3; i++) {
    consensus_topology_create(participants, 3, participants[i], &parts[i].topology);
  }

  // Create coordinator (as leader - position 2)
  asciichat_error_t err = consensus_coordinator_create(participants[2], parts[2].topology, test_election_callback,
                                                       &parts[2], &parts[2].coordinator);
  cr_assert_eq(err, ASCIICHAT_OK);

  // Verify initial state
  consensus_state_machine_t coord_state = consensus_coordinator_get_state(parts[2].coordinator);
  cr_assert_eq(coord_state, CONSENSUS_STATE_IDLE);

  // Check that next round is scheduled
  uint64_t time_until_next = consensus_coordinator_time_until_next_round(parts[2].coordinator);
  cr_assert_gt(time_until_next, 0);
  cr_assert(time_until_next <= 5 * 60 * 1000000000ULL);

  // Cleanup
  for (int i = 0; i < 3; i++) {
    destroy_participant(&parts[i]);
  }
}

/**
 * Test multiple rounds with state persistence
 */
Test(consensus_integration, multiple_election_rounds) {
  // Create 3 participants with similar initial metrics
  uint8_t participants[3][16];
  participant_metrics_t metrics[3];

  for (int i = 0; i < 3; i++) {
    make_test_id(participants[i], i + 2000);
    // All participants start with similar metrics
    // Participant 0: LAN, 50Mbps, 40ms
    // Participant 1: LAN, 50Mbps, 40ms
    // Participant 2: STUN, 20Mbps, 100ms (worst)
    int nat_tier = (i < 2) ? 0 : 3;
    uint32_t bw = (i < 2) ? 50000 : 20000;
    uint32_t rtt_ns = (i < 2) ? (40 * 1000000) : (100 * 1000000);
    make_test_metrics(&metrics[i], 2000 + i, nat_tier, bw, rtt_ns);
  }

  // Verify first round election - participants 0 and 1 should compete
  int best_idx_1, backup_idx_1;
  asciichat_error_t err = consensus_election_choose_hosts(metrics, 3, &best_idx_1, &backup_idx_1);
  cr_assert_eq(err, ASCIICHAT_OK);
  cr_assert(best_idx_1 == 0 || best_idx_1 == 1, "Round 1: Participant 0 or 1 should be best");
  cr_assert_eq(backup_idx_1, (best_idx_1 == 0) ? 1 : 0, "Round 1: The other participant should be backup");

  // Modify metrics - make participant 1 significantly better
  metrics[0].nat_tier = 3;         // Participant 0 gets worse NAT
  metrics[0].upload_kbps = 20000;  // Participant 0 gets less bandwidth
  metrics[1].nat_tier = 0;         // Participant 1 stays LAN
  metrics[1].upload_kbps = 100000; // Participant 1 gets more bandwidth

  // Verify second round election (participant 1 should now clearly be best)
  int best_idx_2, backup_idx_2;
  err = consensus_election_choose_hosts(metrics, 3, &best_idx_2, &backup_idx_2);
  cr_assert_eq(err, ASCIICHAT_OK);
  cr_assert_eq(best_idx_2, 1, "Round 2: Participant 1 should now be clearly best");
  cr_assert_neq(best_idx_1, best_idx_2, "Election should change with metric changes");
}
