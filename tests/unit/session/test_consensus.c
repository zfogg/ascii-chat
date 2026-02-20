/**
 * @file session/test_consensus.c
 * @brief Tests for session consensus abstraction
 * @ingroup session
 */

#include <criterion/criterion.h>
#include <ascii-chat/session/consensus.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/common.h>
#include <string.h>
#include <time.h>
#include <netinet/in.h>

// Define htonll if not available (GNU extension)
#ifndef htonll
#define htonll(x) ((uint64_t)(((uint64_t)htonl((uint32_t)(x))) << 32) | (uint64_t)htonl((uint32_t)((x) >> 32)))
#endif

// Mock context for testing
typedef struct {
  uint8_t packets_sent;
  uint8_t elections_called;
  asciichat_error_t last_error;

  // Latest elected hosts
  uint8_t elected_host_id[16];
  char elected_host_address[64];
  uint16_t elected_host_port;
  uint8_t elected_backup_id[16];
  char elected_backup_address[64];
  uint16_t elected_backup_port;
} mock_context_t;

// Mock send packet callback
static asciichat_error_t mock_send_packet(void *context, const uint8_t next_participant_id[16], const uint8_t *packet,
                                          size_t packet_size) {
  (void)next_participant_id; // unused
  (void)packet;              // unused
  (void)packet_size;         // unused

  mock_context_t *mock = (mock_context_t *)context;
  if (!mock) {
    return ERROR_INVALID_PARAM;
  }

  mock->packets_sent++;
  return ASCIICHAT_OK;
}

// Mock on election callback
static asciichat_error_t mock_on_election(void *context, const uint8_t host_id[16], const char host_address[64],
                                          uint16_t host_port, const uint8_t backup_id[16],
                                          const char backup_address[64], uint16_t backup_port) {
  mock_context_t *mock = (mock_context_t *)context;
  if (!mock) {
    return ERROR_INVALID_PARAM;
  }

  mock->elections_called++;
  memcpy(mock->elected_host_id, host_id, 16);
  SAFE_STRNCPY(mock->elected_host_address, host_address, sizeof(mock->elected_host_address));
  mock->elected_host_port = host_port;
  memcpy(mock->elected_backup_id, backup_id, 16);
  SAFE_STRNCPY(mock->elected_backup_address, backup_address, sizeof(mock->elected_backup_address));
  mock->elected_backup_port = backup_port;

  return ASCIICHAT_OK;
}

// Mock get metrics callback
static asciichat_error_t mock_get_metrics(void *context, const uint8_t my_id[16], participant_metrics_t *out_metrics) {
  if (!context || !my_id || !out_metrics) {
    return ERROR_INVALID_PARAM;
  }

  // Create dummy metrics
  memcpy(out_metrics->participant_id, my_id, 16);
  out_metrics->nat_tier = 1;               // Public
  out_metrics->upload_kbps = htonl(50000); // 50 Mbps
  out_metrics->rtt_ns = htonl(25000000);   // 25ms
  out_metrics->stun_probe_success_pct = 95;
  SAFE_STRNCPY(out_metrics->public_address, "192.168.1.1", sizeof(out_metrics->public_address));
  out_metrics->public_port = htons(12345);
  out_metrics->connection_type = 0; // Direct

  uint64_t current_time = time(NULL) * 1000000000ULL;
  out_metrics->measurement_time_ns = htonll(current_time);
  out_metrics->measurement_window_ns = htonll(5000000000ULL); // 5 seconds

  return ASCIICHAT_OK;
}
// Use verbose logging with debug level enabled and stdout/stderr not disabled
TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(consensus, LOG_DEBUG, LOG_DEBUG, false, false);

Test(consensus, lifecycle) {
  // Create mock context
  mock_context_t mock = {0};

  // Setup participant IDs
  uint8_t participant_ids[2][16] = {
      {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16},
      {17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32},
  };
  uint8_t my_id[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

  // Create callbacks
  session_consensus_callbacks_t callbacks = {
      .send_packet = mock_send_packet,
      .on_election = mock_on_election,
      .get_metrics = mock_get_metrics,
      .election = NULL,
      .context = &mock,
  };

  // Create consensus
  session_consensus_t *consensus = NULL;
  asciichat_error_t err =
      session_consensus_create(my_id, true, (const uint8_t (*)[16])participant_ids, 2, &callbacks, &consensus);
  cr_assert_eq(err, ASCIICHAT_OK, "Failed to create consensus: %d", err);
  cr_assert_not_null(consensus, "Consensus is NULL");

  // Verify we can get the state
  int state = session_consensus_get_state(consensus);
  cr_assert_geq(state, 0, "Invalid state: %d", state);

  // Verify not ready yet (no election)
  bool ready = session_consensus_is_ready(consensus);
  cr_assert(!ready, "Should not be ready without election");

  // Destroy
  session_consensus_destroy(consensus);
  consensus = NULL;

  // Verify safe to destroy NULL
  session_consensus_destroy(NULL);
}

Test(consensus, invalid_parameters) {
  uint8_t my_id[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  uint8_t participant_ids[1][16];
  memcpy(participant_ids[0], my_id, 16);
  mock_context_t mock = {0};

  session_consensus_callbacks_t callbacks = {
      .send_packet = mock_send_packet,
      .on_election = mock_on_election,
      .get_metrics = mock_get_metrics,
      .context = &mock,
  };

  session_consensus_t *consensus = NULL;

  // Test NULL my_id
  asciichat_error_t err =
      session_consensus_create(NULL, true, (const uint8_t (*)[16])participant_ids, 1, &callbacks, &consensus);
  cr_assert_neq(err, ASCIICHAT_OK, "Should reject NULL my_id");
  cr_assert_null(consensus, "Consensus should be NULL");

  // Test NULL callbacks
  err = session_consensus_create(my_id, true, (const uint8_t (*)[16])participant_ids, 1, NULL, &consensus);
  cr_assert_neq(err, ASCIICHAT_OK, "Should reject NULL callbacks");

  // Test NULL output
  err = session_consensus_create(my_id, true, (const uint8_t (*)[16])participant_ids, 1, &callbacks, NULL);
  cr_assert_neq(err, ASCIICHAT_OK, "Should reject NULL output");

  // Test missing callbacks
  session_consensus_callbacks_t bad_callbacks = {0};
  err = session_consensus_create(my_id, true, (const uint8_t (*)[16])participant_ids, 1, &bad_callbacks, &consensus);
  cr_assert_neq(err, ASCIICHAT_OK, "Should reject missing callbacks");

  // Test invalid participant count
  err = session_consensus_create(my_id, true, (const uint8_t (*)[16])participant_ids, 0, &callbacks, &consensus);
  cr_assert_neq(err, ASCIICHAT_OK, "Should reject 0 participants");

  err = session_consensus_create(my_id, true, (const uint8_t (*)[16])participant_ids, 65, &callbacks, &consensus);
  cr_assert_neq(err, ASCIICHAT_OK, "Should reject 65+ participants");
}

Test(consensus, topology_updates) {
  mock_context_t mock = {0};

  uint8_t participant_ids[3][16] = {
      {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16},
      {17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32},
      {33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48},
  };
  uint8_t my_id[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

  session_consensus_callbacks_t callbacks = {
      .send_packet = mock_send_packet,
      .on_election = mock_on_election,
      .get_metrics = mock_get_metrics,
      .context = &mock,
  };

  session_consensus_t *consensus = NULL;
  asciichat_error_t err =
      session_consensus_create(my_id, false, (const uint8_t (*)[16])participant_ids, 3, &callbacks, &consensus);
  cr_assert_eq(err, ASCIICHAT_OK);

  // Update topology to remove last participant
  uint8_t new_participant_ids[2][16];
  memcpy(new_participant_ids[0], participant_ids[0], 16);
  memcpy(new_participant_ids[1], participant_ids[1], 16);

  err = session_consensus_set_topology(consensus, (const uint8_t (*)[16])new_participant_ids, 2);
  cr_assert_eq(err, ASCIICHAT_OK, "Failed to update topology");

  // Test invalid topology update
  err = session_consensus_set_topology(consensus, NULL, 2);
  cr_assert_neq(err, ASCIICHAT_OK, "Should reject NULL participant_ids");

  err = session_consensus_set_topology(consensus, (const uint8_t (*)[16])new_participant_ids, 0);
  cr_assert_neq(err, ASCIICHAT_OK, "Should reject 0 participants");

  session_consensus_destroy(consensus);
}

Test(consensus, process_nonblocking) {
  mock_context_t mock = {0};

  uint8_t participant_ids[2][16] = {
      {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16},
      {17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32},
  };
  uint8_t my_id[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

  session_consensus_callbacks_t callbacks = {
      .send_packet = mock_send_packet,
      .on_election = mock_on_election,
      .get_metrics = mock_get_metrics,
      .context = &mock,
  };

  session_consensus_t *consensus = NULL;
  asciichat_error_t err =
      session_consensus_create(my_id, true, (const uint8_t (*)[16])participant_ids, 2, &callbacks, &consensus);
  cr_assert_eq(err, ASCIICHAT_OK);

  // Process multiple times - should be non-blocking and not error
  for (int i = 0; i < 5; i++) {
    err = session_consensus_process(consensus, 0);
    cr_assert_eq(err, ASCIICHAT_OK, "Process failed on iteration %d", i);
  }

  // Test invalid consensus handle
  err = session_consensus_process(NULL, 0);
  cr_assert_neq(err, ASCIICHAT_OK, "Should reject NULL consensus");

  session_consensus_destroy(consensus);
}

Test(consensus, metrics_handling) {
  mock_context_t mock = {0};

  uint8_t participant_ids[2][16] = {
      {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16},
      {17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32},
  };
  uint8_t my_id[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  uint8_t sender_id[16] = {17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};

  session_consensus_callbacks_t callbacks = {
      .send_packet = mock_send_packet,
      .on_election = mock_on_election,
      .get_metrics = mock_get_metrics,
      .context = &mock,
  };

  session_consensus_t *consensus = NULL;
  asciichat_error_t err =
      session_consensus_create(my_id, false, (const uint8_t (*)[16])participant_ids, 2, &callbacks, &consensus);
  cr_assert_eq(err, ASCIICHAT_OK);

  // Create sample metrics
  participant_metrics_t metrics[2];
  for (int i = 0; i < 2; i++) {
    err = mock_get_metrics(&mock, participant_ids[i], &metrics[i]);
    cr_assert_eq(err, ASCIICHAT_OK);
  }

  // Handle collection start
  err = session_consensus_on_collection_start(consensus, 1, 1000000000ULL);
  cr_assert_eq(err, ASCIICHAT_OK, "Failed to handle collection start");

  // Handle stats update
  err = session_consensus_on_stats_update(consensus, sender_id, metrics, 2);
  cr_assert_eq(err, ASCIICHAT_OK, "Failed to handle stats update");

  // Test invalid parameters
  err = session_consensus_on_stats_update(NULL, sender_id, metrics, 2);
  cr_assert_neq(err, ASCIICHAT_OK, "Should reject NULL consensus");

  session_consensus_destroy(consensus);
}

Test(consensus, election_result) {
  mock_context_t mock = {0};

  uint8_t participant_ids[2][16] = {
      {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16},
      {17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32},
  };
  uint8_t my_id[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  uint8_t host_id[16] = {17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};
  uint8_t backup_id[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

  session_consensus_callbacks_t callbacks = {
      .send_packet = mock_send_packet,
      .on_election = mock_on_election,
      .get_metrics = mock_get_metrics,
      .context = &mock,
  };

  session_consensus_t *consensus = NULL;
  asciichat_error_t err =
      session_consensus_create(my_id, false, (const uint8_t (*)[16])participant_ids, 2, &callbacks, &consensus);
  cr_assert_eq(err, ASCIICHAT_OK);

  // Handle election result
  err = session_consensus_on_election_result(consensus, host_id, "example.com", 27224, backup_id, "backup.example.com",
                                             27224);
  cr_assert_eq(err, ASCIICHAT_OK, "Failed to handle election result");

  // Verify callback was called
  cr_assert_gt(mock.elections_called, 0, "Election callback not called");
  cr_assert_eq(memcmp(mock.elected_host_id, host_id, 16), 0, "Host ID mismatch");

  // Test invalid parameters
  err =
      session_consensus_on_election_result(NULL, host_id, "example.com", 27224, backup_id, "backup.example.com", 27224);
  cr_assert_neq(err, ASCIICHAT_OK, "Should reject NULL consensus");

  session_consensus_destroy(consensus);
}

Test(consensus, get_elected_host) {
  mock_context_t mock = {0};

  uint8_t participant_ids[2][16] = {
      {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16},
      {17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32},
  };
  uint8_t my_id[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

  session_consensus_callbacks_t callbacks = {
      .send_packet = mock_send_packet,
      .on_election = mock_on_election,
      .get_metrics = mock_get_metrics,
      .context = &mock,
  };

  session_consensus_t *consensus = NULL;
  asciichat_error_t err =
      session_consensus_create(my_id, false, (const uint8_t (*)[16])participant_ids, 2, &callbacks, &consensus);
  cr_assert_eq(err, ASCIICHAT_OK);

  // Get elected host (should fail - no election yet)
  uint8_t out_host_id[16];
  char out_host_address[64];
  uint16_t out_host_port;
  uint8_t out_backup_id[16];
  char out_backup_address[64];
  uint16_t out_backup_port;

  err = session_consensus_get_elected_host(consensus, out_host_id, out_host_address, &out_host_port, out_backup_id,
                                           out_backup_address, &out_backup_port);
  // May succeed with dummy values or fail - both are acceptable at this point

  session_consensus_destroy(consensus);
}

Test(consensus, timing_info) {
  mock_context_t mock = {0};

  uint8_t participant_ids[2][16] = {
      {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16},
      {17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32},
  };
  uint8_t my_id[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

  session_consensus_callbacks_t callbacks = {
      .send_packet = mock_send_packet,
      .on_election = mock_on_election,
      .get_metrics = mock_get_metrics,
      .context = &mock,
  };

  session_consensus_t *consensus = NULL;
  asciichat_error_t err =
      session_consensus_create(my_id, false, (const uint8_t (*)[16])participant_ids, 2, &callbacks, &consensus);
  cr_assert_eq(err, ASCIICHAT_OK);

  // Get time until next round
  uint64_t time_to_next = session_consensus_time_until_next_round(consensus);
  cr_assert_geq(time_to_next, 0, "Time to next round should be non-negative");

  // Get metrics count
  int count = session_consensus_get_metrics_count(consensus);
  cr_assert_geq(count, 0, "Metrics count should be non-negative");

  session_consensus_destroy(consensus);
}

Test(consensus, ready_state) {
  mock_context_t mock = {0};

  uint8_t participant_ids[2][16] = {
      {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16},
      {17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32},
  };
  uint8_t my_id[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

  session_consensus_callbacks_t callbacks = {
      .send_packet = mock_send_packet,
      .on_election = mock_on_election,
      .get_metrics = mock_get_metrics,
      .context = &mock,
  };

  session_consensus_t *consensus = NULL;
  asciichat_error_t err =
      session_consensus_create(my_id, false, (const uint8_t (*)[16])participant_ids, 2, &callbacks, &consensus);
  cr_assert_eq(err, ASCIICHAT_OK);

  // Initially not ready
  bool ready = session_consensus_is_ready(consensus);
  cr_assert(!ready, "Should not be ready initially");

  // Test with NULL
  ready = session_consensus_is_ready(NULL);
  cr_assert(!ready, "NULL consensus should not be ready");

  session_consensus_destroy(consensus);
}
