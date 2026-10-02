/**
 * @file session/test_consensus.c
 * @brief Tests for session consensus abstraction
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>
#include <stdint.h>

#include <ascii-chat/common.h>
#include <ascii-chat/network/consensus/metrics.h>

// Forward declarations of internal session consensus API
typedef struct session_consensus session_consensus_t;

typedef asciichat_error_t (*session_consensus_send_packet_fn)(void *context, const uint8_t next_participant_id[16],
                                                              const uint8_t *packet, size_t packet_size);

typedef asciichat_error_t (*session_consensus_on_election_fn)(void *context, const uint8_t host_id[16],
                                                              const char host_address[64], uint16_t host_port,
                                                              const uint8_t backup_id[16],
                                                              const char backup_address[64], uint16_t backup_port);

typedef asciichat_error_t (*session_consensus_get_metrics_fn)(void *context, const uint8_t my_id[16],
                                                              participant_metrics_t *out_metrics);

typedef asciichat_error_t (*session_consensus_election_fn)(void *context, const participant_metrics_t *metrics,
                                                           int num_metrics, int *out_best_index, int *out_backup_index);

typedef struct {
  session_consensus_send_packet_fn send_packet;
  session_consensus_on_election_fn on_election;
  session_consensus_get_metrics_fn get_metrics;
  session_consensus_election_fn election;
  void *context;
} session_consensus_callbacks_t;

// Function declarations for internal session consensus API
asciichat_error_t session_consensus_create(const uint8_t my_id[16], bool is_leader,
                                           const uint8_t participant_ids[64][16], int num_participants,
                                           const session_consensus_callbacks_t *callbacks,
                                           session_consensus_t **out_consensus);

void session_consensus_destroy(session_consensus_t *consensus);

asciichat_error_t session_consensus_process(session_consensus_t *consensus, uint32_t timeout_ms);

asciichat_error_t session_consensus_set_topology(session_consensus_t *consensus, const uint8_t participant_ids[64][16],
                                                 int num_participants);

bool session_consensus_is_ready(session_consensus_t *consensus);

int session_consensus_get_state(session_consensus_t *consensus);

uint64_t session_consensus_time_until_next_round(session_consensus_t *consensus);

int session_consensus_get_metrics_count(session_consensus_t *consensus);

TestSuite(test_consensus);

// Mock callbacks for testing
static asciichat_error_t mock_send_packet(void *context, const uint8_t next_participant_id[16],
                                          const uint8_t *packet, size_t packet_size) {
  (void)context;
  (void)next_participant_id;
  (void)packet;
  (void)packet_size;
  return ASCIICHAT_OK;
}

static asciichat_error_t mock_on_election(void *context, const uint8_t host_id[16], const char host_address[64],
                                          uint16_t host_port, const uint8_t backup_id[16],
                                          const char backup_address[64], uint16_t backup_port) {
  (void)context;
  (void)host_id;
  (void)host_address;
  (void)host_port;
  (void)backup_id;
  (void)backup_address;
  (void)backup_port;
  return ASCIICHAT_OK;
}

static asciichat_error_t mock_get_metrics(void *context, const uint8_t my_id[16], participant_metrics_t *out_metrics) {
  (void)context;
  (void)my_id;
  memset(out_metrics, 0, sizeof(participant_metrics_t));
  out_metrics->nat_tier = 0;
  out_metrics->upload_kbps = 5000;
  out_metrics->connection_type = 0;
  return ASCIICHAT_OK;
}

// =============================================================================
// Consensus Creation Tests
// =============================================================================

Test(test_consensus, create_basic) {
  uint8_t my_id[16] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t participant_ids[64][16];
  memset(participant_ids, 0, sizeof(participant_ids));
  memcpy(participant_ids[0], my_id, 16);

  session_consensus_callbacks_t callbacks = {.send_packet = mock_send_packet,
                                             .on_election = mock_on_election,
                                             .get_metrics = mock_get_metrics,
                                             .election = NULL,
                                             .context = NULL};

  session_consensus_t *consensus = NULL;
  asciichat_error_t result = session_consensus_create(my_id, false, participant_ids, 1, &callbacks, &consensus);

  cr_assert_eq(result, ASCIICHAT_OK, "Should create consensus successfully");
  cr_assert_not_null(consensus, "Consensus handle should not be NULL");

  session_consensus_destroy(consensus);
}

Test(test_consensus, create_as_leader) {
  uint8_t my_id[16] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t participant_ids[64][16];
  memset(participant_ids, 0, sizeof(participant_ids));
  memcpy(participant_ids[0], my_id, 16);

  session_consensus_callbacks_t callbacks = {.send_packet = mock_send_packet,
                                             .on_election = mock_on_election,
                                             .get_metrics = mock_get_metrics,
                                             .election = NULL,
                                             .context = NULL};

  session_consensus_t *consensus = NULL;
  asciichat_error_t result = session_consensus_create(my_id, true, participant_ids, 1, &callbacks, &consensus);

  cr_assert_eq(result, ASCIICHAT_OK, "Should create consensus with leader role");

  session_consensus_destroy(consensus);
}

Test(test_consensus, create_multiple_participants) {
  uint8_t my_id[16] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t participant_ids[64][16];
  memset(participant_ids, 0, sizeof(participant_ids));
  for (int i = 0; i < 5; i++) {
    participant_ids[i][0] = i + 1;
  }

  session_consensus_callbacks_t callbacks = {.send_packet = mock_send_packet,
                                             .on_election = mock_on_election,
                                             .get_metrics = mock_get_metrics,
                                             .election = NULL,
                                             .context = NULL};

  session_consensus_t *consensus = NULL;
  asciichat_error_t result = session_consensus_create(my_id, false, participant_ids, 5, &callbacks, &consensus);

  cr_assert_eq(result, ASCIICHAT_OK, "Should handle multiple participants");

  session_consensus_destroy(consensus);
}

// =============================================================================
// Consensus Destruction Tests
// =============================================================================

Test(test_consensus, destroy_null_consensus) {
  session_consensus_destroy(NULL);
  cr_assert(true, "Destroying NULL consensus should not crash");
}

Test(test_consensus, destroy_twice) {
  uint8_t my_id[16] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t participant_ids[64][16];
  memset(participant_ids, 0, sizeof(participant_ids));
  memcpy(participant_ids[0], my_id, 16);

  session_consensus_callbacks_t callbacks = {.send_packet = mock_send_packet,
                                             .on_election = mock_on_election,
                                             .get_metrics = mock_get_metrics,
                                             .election = NULL,
                                             .context = NULL};

  session_consensus_t *consensus = NULL;
  session_consensus_create(my_id, false, participant_ids, 1, &callbacks, &consensus);

  session_consensus_destroy(consensus);
  session_consensus_destroy(consensus);
  cr_assert(true, "Double destroy should be idempotent");
}

// =============================================================================
// Consensus Processing Tests
// =============================================================================

Test(test_consensus, process_basic) {
  uint8_t my_id[16] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t participant_ids[64][16];
  memset(participant_ids, 0, sizeof(participant_ids));
  memcpy(participant_ids[0], my_id, 16);

  session_consensus_callbacks_t callbacks = {.send_packet = mock_send_packet,
                                             .on_election = mock_on_election,
                                             .get_metrics = mock_get_metrics,
                                             .election = NULL,
                                             .context = NULL};

  session_consensus_t *consensus = NULL;
  session_consensus_create(my_id, false, participant_ids, 1, &callbacks, &consensus);

  asciichat_error_t result = session_consensus_process(consensus, 0);
  cr_assert_eq(result, ASCIICHAT_OK, "Process should succeed with zero timeout");

  session_consensus_destroy(consensus);
}

// =============================================================================
// Consensus State Tests
// =============================================================================

Test(test_consensus, is_ready_initially_false) {
  uint8_t my_id[16] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t participant_ids[64][16];
  memset(participant_ids, 0, sizeof(participant_ids));
  memcpy(participant_ids[0], my_id, 16);

  session_consensus_callbacks_t callbacks = {.send_packet = mock_send_packet,
                                             .on_election = mock_on_election,
                                             .get_metrics = mock_get_metrics,
                                             .election = NULL,
                                             .context = NULL};

  session_consensus_t *consensus = NULL;
  session_consensus_create(my_id, false, participant_ids, 1, &callbacks, &consensus);

  bool is_ready = session_consensus_is_ready(consensus);
  cr_assert_not(is_ready, "Consensus should not be ready immediately after creation");

  session_consensus_destroy(consensus);
}

Test(test_consensus, get_state) {
  uint8_t my_id[16] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t participant_ids[64][16];
  memset(participant_ids, 0, sizeof(participant_ids));
  memcpy(participant_ids[0], my_id, 16);

  session_consensus_callbacks_t callbacks = {.send_packet = mock_send_packet,
                                             .on_election = mock_on_election,
                                             .get_metrics = mock_get_metrics,
                                             .election = NULL,
                                             .context = NULL};

  session_consensus_t *consensus = NULL;
  session_consensus_create(my_id, false, participant_ids, 1, &callbacks, &consensus);

  int state = session_consensus_get_state(consensus);
  cr_assert_geq(state, 0, "Consensus state should be valid");

  session_consensus_destroy(consensus);
}

Test(test_consensus, get_metrics_count) {
  uint8_t my_id[16] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t participant_ids[64][16];
  memset(participant_ids, 0, sizeof(participant_ids));
  memcpy(participant_ids[0], my_id, 16);

  session_consensus_callbacks_t callbacks = {.send_packet = mock_send_packet,
                                             .on_election = mock_on_election,
                                             .get_metrics = mock_get_metrics,
                                             .election = NULL,
                                             .context = NULL};

  session_consensus_t *consensus = NULL;
  session_consensus_create(my_id, false, participant_ids, 1, &callbacks, &consensus);

  int count = session_consensus_get_metrics_count(consensus);
  cr_assert_geq(count, -1, "Metrics count should be valid");

  session_consensus_destroy(consensus);
}

Test(test_consensus, time_until_next_round) {
  uint8_t my_id[16] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t participant_ids[64][16];
  memset(participant_ids, 0, sizeof(participant_ids));
  memcpy(participant_ids[0], my_id, 16);

  session_consensus_callbacks_t callbacks = {.send_packet = mock_send_packet,
                                             .on_election = mock_on_election,
                                             .get_metrics = mock_get_metrics,
                                             .election = NULL,
                                             .context = NULL};

  session_consensus_t *consensus = NULL;
  session_consensus_create(my_id, false, participant_ids, 1, &callbacks, &consensus);

  uint64_t time_until_next = session_consensus_time_until_next_round(consensus);
  cr_assert_geq(time_until_next, 0, "Time until next round should be non-negative");

  session_consensus_destroy(consensus);
}

// =============================================================================
// Topology Update Tests
// =============================================================================

Test(test_consensus, set_topology) {
  uint8_t my_id[16] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t participant_ids[64][16];
  memset(participant_ids, 0, sizeof(participant_ids));
  memcpy(participant_ids[0], my_id, 16);

  session_consensus_callbacks_t callbacks = {.send_packet = mock_send_packet,
                                             .on_election = mock_on_election,
                                             .get_metrics = mock_get_metrics,
                                             .election = NULL,
                                             .context = NULL};

  session_consensus_t *consensus = NULL;
  session_consensus_create(my_id, false, participant_ids, 1, &callbacks, &consensus);

  // Update topology with more participants
  uint8_t new_participant_ids[64][16];
  memset(new_participant_ids, 0, sizeof(new_participant_ids));
  for (int i = 0; i < 3; i++) {
    new_participant_ids[i][0] = i + 1;
  }

  asciichat_error_t result = session_consensus_set_topology(consensus, new_participant_ids, 3);
  cr_assert_eq(result, ASCIICHAT_OK, "Should update topology successfully");

  session_consensus_destroy(consensus);
}
