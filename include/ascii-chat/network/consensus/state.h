/**
 * @file network/consensus/state.h
 * @brief Ring consensus state machine
 * @ingroup consensus
 */

#pragma once

#include "packets.h"
#include "topology.h"
#include <ascii-chat/asciichat_errno.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief State machine lifecycle states
 */
typedef enum {
  CONSENSUS_STATE_IDLE = 0,                /**< Waiting for round to start */
  CONSENSUS_STATE_COLLECTION_START = 1,    /**< Round started, prepare collection */
  CONSENSUS_STATE_COLLECTING = 2,          /**< Collecting metrics */
  CONSENSUS_STATE_COLLECTION_COMPLETE = 3, /**< All metrics received */
  CONSENSUS_STATE_ELECTION_START = 4,      /**< Preparing election (leader only) */
  CONSENSUS_STATE_ELECTING = 5,            /**< Running election algorithm */
  CONSENSUS_STATE_ELECTION_COMPLETE = 6,   /**< Election result computed */
  CONSENSUS_STATE_FAILED = 7               /**< Error state */
} consensus_state_machine_t;

/**
 * @brief Opaque consensus state machine handle
 */
typedef struct consensus_state consensus_state_t;

/**
 * @brief Initialize state machine with topology
 *
 * Creates a new state machine for managing consensus rounds.
 * Initial state is CONSENSUS_STATE_IDLE.
 *
 * @param my_id My 16-byte UUID
 * @param topology Topology handle (not owned by state machine)
 * @param out_state Output state machine handle (caller must destroy)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t consensus_state_create(const uint8_t my_id[16], const consensus_topology_t *topology,
                                         consensus_state_t **out_state);

/**
 * @brief Destroy state machine and free resources
 *
 * @param state State machine handle (safe to pass NULL)
 */
void consensus_state_destroy(consensus_state_t *state);

/**
 * @brief Transition to COLLECTION_START state
 *
 * Valid from: IDLE
 * Transitions to: COLLECTING
 *
 * @param state State machine handle
 * @return ASCIICHAT_OK on success, ERROR_INVALID_STATE if transition invalid
 */
asciichat_error_t consensus_state_start_collection(consensus_state_t *state);

/**
 * @brief Add collected metrics from a participant
 *
 * Valid in: COLLECTING state
 * Stores metrics in dynamic array (grows as needed, initial capacity 10)
 *
 * @param state State machine handle
 * @param metrics Participant metrics to store
 * @return ASCIICHAT_OK on success, ERROR_INVALID_STATE if not in COLLECTING state
 */
asciichat_error_t consensus_state_add_metrics(consensus_state_t *state, const participant_metrics_t *metrics);

/**
 * @brief Transition to COLLECTION_COMPLETE state
 *
 * Valid from: COLLECTING
 * Transitions to: ELECTION_START (if leader) or IDLE (if not leader)
 *
 * @param state State machine handle
 * @return ASCIICHAT_OK on success, ERROR_INVALID_STATE if transition invalid
 */
asciichat_error_t consensus_state_collection_complete(consensus_state_t *state);

/**
 * @brief Run election algorithm and transition to ELECTION_COMPLETE
 *
 * Valid from: ELECTION_START or ELECTION_COMPLETE
 * Processes collected metrics to select best host and backup.
 * Election selects host as participant with best score, backup as second-best.
 * Score is computed from metrics: lower RTT + higher bandwidth = better score.
 * Only leader should call this.
 *
 * @param state State machine handle
 * @return ASCIICHAT_OK on success, ERROR_INVALID_STATE if not in ELECTION_START state
 */
asciichat_error_t consensus_state_compute_election(consensus_state_t *state);

/**
 * @brief Transition back to IDLE from ELECTION_COMPLETE
 *
 * Valid from: ELECTION_COMPLETE
 * Transitions to: IDLE
 *
 * @param state State machine handle
 * @return ASCIICHAT_OK on success, ERROR_INVALID_STATE if transition invalid
 */
asciichat_error_t consensus_state_reset_to_idle(consensus_state_t *state);

/**
 * @brief Get current state machine state
 *
 * @param state State machine handle
 * @return Current state (FAILED if state is NULL)
 */
consensus_state_machine_t consensus_state_get_current_state(const consensus_state_t *state);

/**
 * @brief Get elected host ID from last election
 *
 * Valid from: ELECTION_COMPLETE state
 *
 * @param state State machine handle
 * @param out_host_id Output 16-byte host UUID (caller provides buffer)
 * @return ASCIICHAT_OK on success, ERROR_INVALID_STATE if election not complete
 */
asciichat_error_t consensus_state_get_elected_host(const consensus_state_t *state, uint8_t out_host_id[16]);

/**
 * @brief Get elected backup host ID from last election
 *
 * Valid from: ELECTION_COMPLETE state
 *
 * @param state State machine handle
 * @param out_backup_id Output 16-byte backup UUID (caller provides buffer)
 * @return ASCIICHAT_OK on success, ERROR_INVALID_STATE if election not complete
 */
asciichat_error_t consensus_state_get_elected_backup(const consensus_state_t *state, uint8_t out_backup_id[16]);

/**
 * @brief Check if we are the elected leader for this round
 *
 * @param state State machine handle
 * @return true if we are the ring leader, false otherwise
 */
bool consensus_state_is_leader(const consensus_state_t *state);

/**
 * @brief Get count of metrics collected in current round
 *
 * @param state State machine handle
 * @return Number of metrics collected, or -1 if invalid
 */
int consensus_state_get_metrics_count(const consensus_state_t *state);

/**
 * @brief Get collected metrics at index
 *
 * Valid from: COLLECTING, COLLECTION_COMPLETE, or ELECTION states
 *
 * @param state State machine handle
 * @param index Index of metric to retrieve (0-based)
 * @param out_metrics Output metrics structure
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t consensus_state_get_metric_at(const consensus_state_t *state, int index,
                                                participant_metrics_t *out_metrics);
