/**
 * @file session/consensus.c
 * @brief Mode-agnostic ring consensus abstraction implementation
 * @ingroup session
 */

#include "session/consensus.h"
#include <ascii-chat/network/consensus/state.h>
#include <ascii-chat/network/consensus/coordinator.h>
#include <ascii-chat/network/consensus/topology.h>
#include <ascii-chat/network/consensus/election.h>
#include <ascii-chat/network/consensus/metrics.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/util/endian.h>
#include <string.h>

/**
 * @brief Session consensus handle - wraps all consensus modules
 */
typedef struct session_consensus {
  // Consensus modules
  consensus_topology_t *topology;
  consensus_coordinator_t *coordinator;

  // Callbacks for mode integration
  session_consensus_send_packet_fn send_packet;
  session_consensus_on_election_fn on_election;
  session_consensus_get_metrics_fn get_metrics;
  session_consensus_election_fn election;
  void *context;

  // Local identity
  uint8_t my_id[16];
  bool is_leader;
} session_consensus_t;

/**
 * @brief Election callback for coordinator
 *
 * Bridges between coordinator and session callbacks.
 * Calls either custom election or default election algorithm.
 */
static asciichat_error_t session_consensus_election_bridge(void *context, consensus_state_t *state) {
  session_consensus_t *consensus = (session_consensus_t *)context;
  if (!consensus) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "consensus context is NULL");
  }

  // Get collected metrics from state
  int num_metrics = consensus_state_get_metrics_count(state);
  if (num_metrics < 0) {
    return SET_ERRNO(ERROR_INVALID_STATE, "No metrics collected");
  }

  // Allocate array for metrics
  participant_metrics_t *metrics = SAFE_MALLOC(num_metrics * sizeof(participant_metrics_t), participant_metrics_t *);
  if (!metrics) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate metrics array");
  }

  // Copy metrics from state
  for (int i = 0; i < num_metrics; i++) {
    asciichat_error_t err = consensus_state_get_metric_at(state, i, &metrics[i]);
    if (err != ASCIICHAT_OK) {
      SAFE_FREE(metrics);
      return err;
    }
  }

  // Run election (custom or default)
  int best_index = -1;
  int backup_index = -1;

  asciichat_error_t result;
  if (consensus->election) {
    // Custom election
    result = consensus->election(consensus->context, metrics, num_metrics, &best_index, &backup_index);
  } else {
    // Default election
    result = consensus_election_choose_hosts(metrics, num_metrics, &best_index, &backup_index);
  }

  if (result != ASCIICHAT_OK) {
    SAFE_FREE(metrics);
    return result;
  }

  if (best_index < 0 || best_index >= num_metrics) {
    SAFE_FREE(metrics);
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid best host index: %d", best_index);
  }

  if (backup_index < 0 || backup_index >= num_metrics) {
    SAFE_FREE(metrics);
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid backup host index: %d", backup_index);
  }

  // Store in state for later retrieval
  // TODO: Add method to store elected host/backup in state

  log_info("Session consensus election: best=%d, backup=%d", best_index, backup_index);

  SAFE_FREE(metrics);
  return ASCIICHAT_OK;
}

asciichat_error_t session_consensus_create(const uint8_t my_id[16], bool is_leader,
                                           const uint8_t participant_ids[64][16], int num_participants,
                                           const session_consensus_callbacks_t *callbacks,
                                           session_consensus_t **out_consensus) {
  if (!my_id || !callbacks || !out_consensus) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: my_id=%p, callbacks=%p, out_consensus=%p", my_id,
                     callbacks, out_consensus);
  }

  if (!callbacks->send_packet || !callbacks->on_election || !callbacks->get_metrics) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Required callbacks missing: send_packet=%p, on_election=%p, get_metrics=%p",
                     callbacks->send_packet, callbacks->on_election, callbacks->get_metrics);
  }

  if (num_participants < 1 || num_participants > 64) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid participant count: %d", num_participants);
  }

  // Allocate consensus handle
  session_consensus_t *consensus = SAFE_CALLOC(1, sizeof(session_consensus_t), session_consensus_t *);
  if (!consensus) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate session consensus");
  }

  // Copy identity
  memcpy(consensus->my_id, my_id, 16);
  consensus->is_leader = is_leader;

  // Copy callbacks
  consensus->send_packet = callbacks->send_packet;
  consensus->on_election = callbacks->on_election;
  consensus->get_metrics = callbacks->get_metrics;
  consensus->election = callbacks->election; // May be NULL
  consensus->context = callbacks->context;

  // Create topology
  asciichat_error_t err = consensus_topology_create(participant_ids, num_participants, my_id, &consensus->topology);
  if (err != ASCIICHAT_OK) {
    SAFE_FREE(consensus);
    return err;
  }

  // Create coordinator
  err = consensus_coordinator_create(my_id, consensus->topology, session_consensus_election_bridge, consensus,
                                     &consensus->coordinator);
  if (err != ASCIICHAT_OK) {
    consensus_topology_destroy(consensus->topology);
    SAFE_FREE(consensus);
    return err;
  }

  log_debug("Session consensus created: my_id=%.*s, is_leader=%d, participants=%d", 16, my_id, is_leader,
            num_participants);

  *out_consensus = consensus;
  return ASCIICHAT_OK;
}

void session_consensus_destroy(session_consensus_t *consensus) {
  if (!consensus) {
    return;
  }

  if (consensus->coordinator) {
    consensus_coordinator_destroy(consensus->coordinator);
  }

  if (consensus->topology) {
    consensus_topology_destroy(consensus->topology);
  }

  SAFE_FREE(consensus);
}

asciichat_error_t session_consensus_process(session_consensus_t *consensus, uint32_t timeout_ms) {
  if (!consensus) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "consensus is NULL");
  }

  // Call coordinator process
  return consensus_coordinator_process(consensus->coordinator, timeout_ms);
}

asciichat_error_t session_consensus_set_topology(session_consensus_t *consensus, const uint8_t participant_ids[64][16],
                                                 int num_participants) {
  if (!consensus || !participant_ids) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  if (num_participants < 1 || num_participants > 64) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid participant count: %d", num_participants);
  }

  // Destroy old topology
  if (consensus->topology) {
    consensus_topology_destroy(consensus->topology);
  }

  // Create new topology
  asciichat_error_t err =
      consensus_topology_create(participant_ids, num_participants, consensus->my_id, &consensus->topology);
  if (err != ASCIICHAT_OK) {
    consensus->topology = NULL;
    return err;
  }

  // Update coordinator with new topology
  return consensus_coordinator_on_ring_members(consensus->coordinator, consensus->topology);
}

asciichat_error_t session_consensus_on_collection_start(session_consensus_t *consensus, uint32_t round_id,
                                                        uint64_t deadline_ns) {
  if (!consensus) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "consensus is NULL");
  }

  return consensus_coordinator_on_collection_start(consensus->coordinator, round_id, deadline_ns);
}

asciichat_error_t session_consensus_on_stats_update(session_consensus_t *consensus, const uint8_t sender_id[16],
                                                    const participant_metrics_t *metrics, uint8_t num_metrics) {
  if (!consensus || !sender_id || !metrics) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  return consensus_coordinator_on_stats_update(consensus->coordinator, sender_id, metrics, num_metrics);
}

asciichat_error_t session_consensus_on_election_result(session_consensus_t *consensus, const uint8_t host_id[16],
                                                       const char host_address[64], uint16_t host_port,
                                                       const uint8_t backup_id[16], const char backup_address[64],
                                                       uint16_t backup_port) {
  if (!consensus || !host_id || !backup_id) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  // First acknowledge the election in coordinator
  asciichat_error_t err = consensus_coordinator_on_election_result(consensus->coordinator, host_id, backup_id);
  if (err != ASCIICHAT_OK) {
    return err;
  }

  // Then call the mode's election callback
  if (consensus->on_election) {
    return consensus->on_election(consensus->context, host_id, host_address, host_port, backup_id, backup_address,
                                  backup_port);
  }

  return ASCIICHAT_OK;
}

asciichat_error_t session_consensus_get_elected_host(session_consensus_t *consensus, uint8_t out_host_id[16],
                                                     char out_host_address[64], uint16_t *out_host_port,
                                                     uint8_t out_backup_id[16], char out_backup_address[64],
                                                     uint16_t *out_backup_port) {
  if (!consensus || !out_host_id || !out_host_address || !out_host_port || !out_backup_id || !out_backup_address ||
      !out_backup_port) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  // Get from coordinator
  asciichat_error_t err = consensus_coordinator_get_current_host(consensus->coordinator, out_host_id, out_backup_id);
  if (err != ASCIICHAT_OK) {
    return err;
  }

  // NOTE: The current API doesn't return addresses/ports. This is a limitation
  // of the coordinator that should be addressed in a future enhancement.
  // For now, return zeros and indicate this is a placeholder.
  memset(out_host_address, 0, 64);
  *out_host_port = 0;
  memset(out_backup_address, 0, 64);
  *out_backup_port = 0;

  return ASCIICHAT_OK;
}

bool session_consensus_is_ready(session_consensus_t *consensus) {
  if (!consensus) {
    return false;
  }

  // Check if we have a valid elected host
  uint8_t host_id[16];
  uint8_t backup_id[16];
  asciichat_error_t err = consensus_coordinator_get_current_host(consensus->coordinator, host_id, backup_id);

  return err == ASCIICHAT_OK;
}

int session_consensus_get_state(session_consensus_t *consensus) {
  if (!consensus) {
    return -1;
  }

  return (int)consensus_coordinator_get_state(consensus->coordinator);
}

uint64_t session_consensus_time_until_next_round(session_consensus_t *consensus) {
  if (!consensus) {
    return 0;
  }

  return consensus_coordinator_time_until_next_round(consensus->coordinator);
}

int session_consensus_get_metrics_count(session_consensus_t *consensus) {
  if (!consensus) {
    return -1;
  }

  return consensus_coordinator_get_metrics_count(consensus->coordinator);
}
