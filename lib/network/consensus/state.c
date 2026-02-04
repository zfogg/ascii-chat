/**
 * @file network/consensus/state.c
 * @brief Ring consensus state machine implementation
 */

#include <ascii-chat/network/consensus/state.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/asciichat_errno.h>
#include <string.h>

/**
 * @brief Election result storage
 */
typedef struct {
  uint8_t host_id[16];   /**< Elected host participant ID */
  uint8_t backup_id[16]; /**< Elected backup participant ID */
  uint64_t timestamp_ns; /**< Unix ns when elected */
} election_result_t;

/**
 * @brief State machine instance
 */
typedef struct consensus_state {
  consensus_state_machine_t current_state; /**< Current state */
  consensus_topology_t *topology;          /**< Topology (not owned) */
  uint8_t my_id[16];                       /**< My participant ID */

  /* Collected metrics storage */
  participant_metrics_t *metrics; /**< Dynamic array of collected metrics */
  int metrics_count;              /**< Current number of metrics */
  int metrics_capacity;           /**< Allocated capacity */

  /* Election result */
  election_result_t election_result; /**< Last election result */
} consensus_state_t;

/* Static helper: compute score from metrics (lower is better) */
static double compute_metric_score(const participant_metrics_t *m) {
  /* Simple score: prioritize low RTT, then high bandwidth
   * Score = RTT_ns / 1000 + (max_bandwidth - upload_kbps) / 1000
   * Lower RTT = lower score
   * Higher bandwidth = lower score
   */
  double rtt_score = m->rtt_ns / 1000.0; // Convert ns to µs for reasonable scale
  double bandwidth_penalty = (1000000.0 - m->upload_kbps) / 1000.0;
  return rtt_score + bandwidth_penalty;
}

asciichat_error_t consensus_state_create(const uint8_t my_id[16], const consensus_topology_t *topology,
                                         consensus_state_t **out_state) {
  if (!my_id || !topology || !out_state) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid state creation parameters");
  }

  consensus_state_t *state = SAFE_MALLOC(sizeof(*state), consensus_state_t *);
  memset(state, 0, sizeof(*state));

  state->current_state = CONSENSUS_STATE_IDLE;
  state->topology = (consensus_topology_t *)topology; /* Cast away const for storage */
  memcpy(state->my_id, my_id, 16);

  /* Initialize metrics array with initial capacity of 10 */
  state->metrics_capacity = 10;
  state->metrics_count = 0;
  state->metrics = SAFE_MALLOC(state->metrics_capacity * sizeof(participant_metrics_t), participant_metrics_t *);

  memset(&state->election_result, 0, sizeof(election_result_t));

  *out_state = state;
  return ASCIICHAT_OK;
}

void consensus_state_destroy(consensus_state_t *state) {
  if (state) {
    if (state->metrics) {
      SAFE_FREE(state->metrics);
    }
    SAFE_FREE(state);
  }
}

asciichat_error_t consensus_state_start_collection(consensus_state_t *state) {
  if (!state) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "State is NULL");
  }

  if (state->current_state != CONSENSUS_STATE_IDLE) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid transition from state %d", state->current_state);
  }

  /* Reset metrics from previous round */
  state->metrics_count = 0;
  memset(&state->election_result, 0, sizeof(election_result_t));

  state->current_state = CONSENSUS_STATE_COLLECTING;
  return ASCIICHAT_OK;
}

asciichat_error_t consensus_state_add_metrics(consensus_state_t *state, const participant_metrics_t *metrics) {
  if (!state || !metrics) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  if (state->current_state != CONSENSUS_STATE_COLLECTING) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid state for adding metrics: %d", state->current_state);
  }

  /* Grow array if needed */
  if (state->metrics_count >= state->metrics_capacity) {
    int new_capacity = state->metrics_capacity * 2;
    participant_metrics_t *new_metrics =
        SAFE_REALLOC(state->metrics, new_capacity * sizeof(participant_metrics_t), participant_metrics_t *);
    state->metrics = new_metrics;
    state->metrics_capacity = new_capacity;
  }

  /* Copy metrics into array */
  memcpy(&state->metrics[state->metrics_count], metrics, sizeof(participant_metrics_t));
  state->metrics_count++;

  return ASCIICHAT_OK;
}

asciichat_error_t consensus_state_collection_complete(consensus_state_t *state) {
  if (!state) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "State is NULL");
  }

  if (state->current_state != CONSENSUS_STATE_COLLECTING) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid transition from state %d", state->current_state);
  }

  /* Transition: if we're leader, go to ELECTION_START; otherwise go to IDLE */
  if (consensus_topology_am_leader(state->topology)) {
    state->current_state = CONSENSUS_STATE_ELECTION_START;
  } else {
    state->current_state = CONSENSUS_STATE_IDLE;
  }

  return ASCIICHAT_OK;
}

asciichat_error_t consensus_state_compute_election(consensus_state_t *state) {
  if (!state) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "State is NULL");
  }

  if (state->current_state != CONSENSUS_STATE_ELECTION_START) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid transition from state %d", state->current_state);
  }

  if (state->metrics_count < 2) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Need at least 2 metrics for election, have %d", state->metrics_count);
  }

  /* Find best and second-best metrics */
  int best_idx = 0;
  int backup_idx = 1;

  double best_score = compute_metric_score(&state->metrics[0]);
  double backup_score = compute_metric_score(&state->metrics[1]);

  /* Ensure best_idx has lower score */
  if (best_score > backup_score) {
    int tmp = best_idx;
    best_idx = backup_idx;
    backup_idx = tmp;
    double tmp_score = best_score;
    best_score = backup_score;
    backup_score = tmp_score;
  }

  /* Find best and second-best */
  for (int i = 2; i < state->metrics_count; i++) {
    double score = compute_metric_score(&state->metrics[i]);
    if (score < best_score) {
      backup_idx = best_idx;
      backup_score = best_score;
      best_idx = i;
      best_score = score;
    } else if (score < backup_score) {
      backup_idx = i;
      backup_score = score;
    }
  }

  /* Store election result */
  memcpy(state->election_result.host_id, state->metrics[best_idx].participant_id, 16);
  memcpy(state->election_result.backup_id, state->metrics[backup_idx].participant_id, 16);
  state->election_result.timestamp_ns = time_get_realtime_ns();

  state->current_state = CONSENSUS_STATE_ELECTION_COMPLETE;
  return ASCIICHAT_OK;
}

asciichat_error_t consensus_state_reset_to_idle(consensus_state_t *state) {
  if (!state) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "State is NULL");
  }

  if (state->current_state != CONSENSUS_STATE_ELECTION_COMPLETE) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid transition from state %d", state->current_state);
  }

  state->current_state = CONSENSUS_STATE_IDLE;
  return ASCIICHAT_OK;
}

consensus_state_machine_t consensus_state_get_current_state(const consensus_state_t *state) {
  if (!state) {
    return CONSENSUS_STATE_FAILED;
  }
  return state->current_state;
}

asciichat_error_t consensus_state_get_elected_host(const consensus_state_t *state, uint8_t out_host_id[16]) {
  if (!state || !out_host_id) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  if (state->current_state != CONSENSUS_STATE_ELECTION_COMPLETE) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Election not complete, current state: %d", state->current_state);
  }

  memcpy(out_host_id, state->election_result.host_id, 16);
  return ASCIICHAT_OK;
}

asciichat_error_t consensus_state_get_elected_backup(const consensus_state_t *state, uint8_t out_backup_id[16]) {
  if (!state || !out_backup_id) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  if (state->current_state != CONSENSUS_STATE_ELECTION_COMPLETE) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Election not complete, current state: %d", state->current_state);
  }

  memcpy(out_backup_id, state->election_result.backup_id, 16);
  return ASCIICHAT_OK;
}

bool consensus_state_is_leader(const consensus_state_t *state) {
  if (!state) {
    return false;
  }
  return consensus_topology_am_leader(state->topology);
}

int consensus_state_get_metrics_count(const consensus_state_t *state) {
  if (!state) {
    return -1;
  }
  return state->metrics_count;
}

asciichat_error_t consensus_state_get_metric_at(const consensus_state_t *state, int index,
                                                participant_metrics_t *out_metrics) {
  if (!state || !out_metrics || index < 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  if (index >= state->metrics_count) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Index %d out of bounds (count: %d)", index, state->metrics_count);
  }

  /* Valid to retrieve metrics in COLLECTING, COLLECTION_COMPLETE, or ELECTION states */
  if (state->current_state != CONSENSUS_STATE_COLLECTING &&
      state->current_state != CONSENSUS_STATE_COLLECTION_COMPLETE &&
      state->current_state != CONSENSUS_STATE_ELECTION_START && state->current_state != CONSENSUS_STATE_ELECTING &&
      state->current_state != CONSENSUS_STATE_ELECTION_COMPLETE) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Cannot retrieve metrics in state %d", state->current_state);
  }

  memcpy(out_metrics, &state->metrics[index], sizeof(participant_metrics_t));
  return ASCIICHAT_OK;
}
