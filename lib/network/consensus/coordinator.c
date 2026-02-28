/**
 * @file network/consensus/coordinator.c
 * @brief Ring consensus coordinator implementation
 */

#include <ascii-chat/network/consensus/coordinator.h>
#include <ascii-chat/network/consensus/election.h>
#include <ascii-chat/network/consensus/metrics.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/common.h>
#include <string.h>

/* Round scheduling constants */
#define CONSENSUS_ROUND_INTERVAL_NS (5ULL * 60 * NS_PER_SEC_INT)  /* 5 minutes */
#define CONSENSUS_COLLECTION_DEADLINE_NS (30ULL * NS_PER_SEC_INT) /* 30 seconds */

/**
 * @brief Internal coordinator structure
 */
typedef struct consensus_coordinator {
  uint8_t my_id[16];
  const consensus_topology_t *topology;
  consensus_state_t *state;
  consensus_election_func_t election_func;
  void *election_context;

  /* Round scheduling */
  uint64_t last_round_start_ns;    /* When the last round was initiated */
  uint32_t next_round_id;          /* Next round ID to use */
  uint64_t collection_deadline_ns; /* Deadline for current collection */

  /* Elected hosts - fallback storage */
  uint8_t stored_host_id[16];
  uint8_t stored_backup_id[16];
  bool has_stored_result;
} consensus_coordinator_t;

/**
 * @brief Initialize coordinator with given parameters
 */
asciichat_error_t consensus_coordinator_create(const uint8_t my_id[16], const consensus_topology_t *topology,
                                               consensus_election_func_t election_func, void *election_context,
                                               consensus_coordinator_t **out_coordinator) {
  if (!my_id || !topology || !election_func || !out_coordinator) {
    return SET_ERRNO(
        ERROR_INVALID_PARAM, "Invalid parameter: my_id=%p, topology=%p, election_func=%p, out_coordinator=%p",
        (const void *)my_id, (const void *)topology, (const void *)election_func, (const void *)out_coordinator);
  }

  consensus_coordinator_t *coordinator = SAFE_MALLOC(sizeof(consensus_coordinator_t), consensus_coordinator_t *);
  if (!coordinator) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate coordinator");
  }

  memset(coordinator, 0, sizeof(*coordinator));
  memcpy(coordinator->my_id, my_id, 16);
  coordinator->topology = topology;
  coordinator->election_func = election_func;
  coordinator->election_context = election_context;

  /* Initialize state machine */
  asciichat_error_t err = consensus_state_create(my_id, topology, &coordinator->state);
  if (err != ASCIICHAT_OK) {
    SAFE_FREE(coordinator);
    return err;
  }

  /* Initialize round scheduling */
  coordinator->last_round_start_ns = time_get_ns();
  coordinator->next_round_id = 1;
  coordinator->has_stored_result = false;

  log_debug("Coordinator created for node %u, first round in 5 minutes", my_id[0]);

  *out_coordinator = coordinator;
  return ASCIICHAT_OK;
}

/**
 * @brief Clean up coordinator resources
 */
void consensus_coordinator_destroy(consensus_coordinator_t *coordinator) {
  if (!coordinator) {
    return;
  }
  consensus_state_destroy(coordinator->state);
  SAFE_FREE(coordinator);
}

/**
 * @brief Check if we should start a new collection round
 */
static bool should_start_new_round(consensus_coordinator_t *coordinator, uint64_t now_ns) {
  if (!coordinator || !coordinator->state) {
    return false;
  }

  consensus_state_machine_t current_state = consensus_state_get_current_state(coordinator->state);
  if (current_state != CONSENSUS_STATE_IDLE) {
    return false;
  }

  uint64_t time_since_last_round = time_elapsed_ns(coordinator->last_round_start_ns, now_ns);
  return time_since_last_round >= CONSENSUS_ROUND_INTERVAL_NS;
}

/**
 * @brief Measure our own metrics and add to state
 */
static asciichat_error_t measure_and_add_metrics(consensus_coordinator_t *coordinator) {
  participant_metrics_t metrics;
  asciichat_error_t err = consensus_metrics_measure(coordinator->my_id, &metrics);
  if (err != ASCIICHAT_OK) {
    return err;
  }

  err = consensus_state_add_metrics(coordinator->state, &metrics);
  if (err != ASCIICHAT_OK) {
    return err;
  }

  log_debug("Added own metrics to collection");
  return ASCIICHAT_OK;
}

/**
 * @brief Start a new collection round (leader only)
 */
static asciichat_error_t start_collection_round(consensus_coordinator_t *coordinator, uint64_t now_ns) {
  /* Transition state machine */
  asciichat_error_t err = consensus_state_start_collection(coordinator->state);
  if (err != ASCIICHAT_OK) {
    return err;
  }

  coordinator->last_round_start_ns = now_ns;
  coordinator->collection_deadline_ns = now_ns + CONSENSUS_COLLECTION_DEADLINE_NS;
  coordinator->next_round_id++;

  log_info("Starting collection round %u, deadline in 30 seconds", coordinator->next_round_id - 1);

  /* Measure our own metrics immediately */
  err = measure_and_add_metrics(coordinator);
  if (err != ASCIICHAT_OK) {
    return err;
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Check if collection has timed out
 */
static bool has_collection_timed_out(consensus_coordinator_t *coordinator, uint64_t now_ns) {
  consensus_state_machine_t current_state = consensus_state_get_current_state(coordinator->state);
  if (current_state != CONSENSUS_STATE_COLLECTING) {
    return false;
  }

  return now_ns >= coordinator->collection_deadline_ns;
}

/**
 * @brief Complete collection and transition to election (if leader)
 */
static asciichat_error_t complete_collection(consensus_coordinator_t *coordinator) {
  asciichat_error_t err = consensus_state_collection_complete(coordinator->state);
  if (err != ASCIICHAT_OK) {
    return err;
  }

  consensus_state_machine_t current_state = consensus_state_get_current_state(coordinator->state);

  if (current_state == CONSENSUS_STATE_ELECTION_START) {
    /* Leader: Run election */
    log_info("Leader: Computing election from %d metrics", consensus_state_get_metrics_count(coordinator->state));

    err = consensus_state_compute_election(coordinator->state);
    if (err != ASCIICHAT_OK) {
      return err;
    }

    /* Optionally run election callback */
    if (coordinator->election_func) {
      err = coordinator->election_func(coordinator->election_context, coordinator->state);
      if (err != ASCIICHAT_OK) {
        log_warn("Election callback returned error: %d", err);
      }
    }

    /* Store elected hosts */
    uint8_t host_id[16], backup_id[16];
    err = consensus_state_get_elected_host(coordinator->state, host_id);
    if (err == ASCIICHAT_OK) {
      err = consensus_state_get_elected_backup(coordinator->state, backup_id);
    }
    if (err == ASCIICHAT_OK) {
      memcpy(coordinator->stored_host_id, host_id, 16);
      memcpy(coordinator->stored_backup_id, backup_id, 16);
      coordinator->has_stored_result = true;
      log_info("Election complete: host=%u, backup=%u", host_id[0], backup_id[0]);
    }
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Main orchestration loop
 */
asciichat_error_t consensus_coordinator_process(consensus_coordinator_t *coordinator, uint32_t timeout_ms) {
  (void)timeout_ms; /* Currently unused, but kept for future use */

  if (!coordinator) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Coordinator is NULL");
  }

  uint64_t now_ns = time_get_ns();

  /* Check if we should start a new round */
  if (should_start_new_round(coordinator, now_ns)) {
    /* Only leader starts rounds */
    if (consensus_topology_am_leader(coordinator->topology)) {
      asciichat_error_t err = start_collection_round(coordinator, now_ns);
      if (err != ASCIICHAT_OK) {
        log_warn("Failed to start collection round: %d", err);
        return err;
      }
    }
  }

  consensus_state_machine_t current_state = consensus_state_get_current_state(coordinator->state);

  /* Check for collection timeout */
  if (current_state == CONSENSUS_STATE_COLLECTING && has_collection_timed_out(coordinator, now_ns)) {
    log_warn("Collection round timed out, completing early");
    asciichat_error_t err = complete_collection(coordinator);
    if (err != ASCIICHAT_OK) {
      log_error("Failed to complete collection on timeout: %d", err);
    }
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Update ring topology when participants change
 */
asciichat_error_t consensus_coordinator_on_ring_members(consensus_coordinator_t *coordinator,
                                                        const consensus_topology_t *new_topology) {
  if (!coordinator || !new_topology) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameter");
  }

  /* Update topology reference */
  coordinator->topology = new_topology;

  /* Reset state if we're in the middle of a round */
  consensus_state_machine_t current_state = consensus_state_get_current_state(coordinator->state);
  if (current_state != CONSENSUS_STATE_IDLE) {
    log_warn("Ring topology changed during round, resetting state");
    consensus_state_destroy(coordinator->state);
    coordinator->state = NULL;

    asciichat_error_t err = consensus_state_create(coordinator->my_id, new_topology, &coordinator->state);
    if (err != ASCIICHAT_OK) {
      return err;
    }
  }

  log_info("Ring topology updated");
  return ASCIICHAT_OK;
}

/**
 * @brief Handle STATS_COLLECTION_START packet
 */
asciichat_error_t consensus_coordinator_on_collection_start(consensus_coordinator_t *coordinator, uint32_t round_id,
                                                            uint64_t deadline_ns) {
  if (!coordinator) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Coordinator is NULL");
  }

  consensus_state_machine_t current_state = consensus_state_get_current_state(coordinator->state);
  if (current_state != CONSENSUS_STATE_IDLE) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Cannot start collection, state is %d", current_state);
  }

  /* Start collection */
  asciichat_error_t err = consensus_state_start_collection(coordinator->state);
  if (err != ASCIICHAT_OK) {
    return err;
  }

  coordinator->next_round_id = round_id;
  coordinator->collection_deadline_ns = deadline_ns;

  /* Measure and add our metrics */
  err = measure_and_add_metrics(coordinator);
  if (err != ASCIICHAT_OK) {
    log_warn("Failed to measure metrics: %d", err);
  }

  log_dev("Collection started: round_id=%u, deadline in %u seconds", round_id,
          (unsigned int)((deadline_ns - time_get_ns()) / NS_PER_SEC_INT));

  return ASCIICHAT_OK;
}

/**
 * @brief Handle STATS_UPDATE packet
 */
asciichat_error_t consensus_coordinator_on_stats_update(consensus_coordinator_t *coordinator,
                                                        const uint8_t sender_id[16],
                                                        const participant_metrics_t *metrics, uint8_t num_metrics) {
  if (!coordinator || !sender_id || !metrics) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameter");
  }

  consensus_state_machine_t current_state = consensus_state_get_current_state(coordinator->state);
  if (current_state != CONSENSUS_STATE_COLLECTING) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Cannot accept metrics, state is %d", current_state);
  }

  /* Add all metrics to state */
  for (int i = 0; i < num_metrics; i++) {
    asciichat_error_t err = consensus_state_add_metrics(coordinator->state, &metrics[i]);
    if (err != ASCIICHAT_OK) {
      log_warn("Failed to add metric %d: %d", i, err);
    }
  }

  log_debug("Received %d metrics from sender %u", num_metrics, sender_id[0]);

  return ASCIICHAT_OK;
}

/**
 * @brief Handle ELECTION_RESULT packet
 */
asciichat_error_t consensus_coordinator_on_election_result(consensus_coordinator_t *coordinator,
                                                           const uint8_t host_id[16], const uint8_t backup_id[16]) {
  if (!coordinator || !host_id || !backup_id) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameter");
  }

  /* Store the elected host and backup */
  memcpy(coordinator->stored_host_id, host_id, 16);
  memcpy(coordinator->stored_backup_id, backup_id, 16);
  coordinator->has_stored_result = true;

  log_info("Election result received: host=%u, backup=%u", host_id[0], backup_id[0]);

  /* Transition state back to IDLE */
  consensus_state_machine_t current_state = consensus_state_get_current_state(coordinator->state);
  if (current_state == CONSENSUS_STATE_ELECTION_COMPLETE) {
    asciichat_error_t err = consensus_state_reset_to_idle(coordinator->state);
    if (err != ASCIICHAT_OK) {
      log_warn("Failed to reset state to IDLE: %d", err);
    }
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Get currently elected host
 */
asciichat_error_t consensus_coordinator_get_current_host(const consensus_coordinator_t *coordinator,
                                                         uint8_t out_host_id[16], uint8_t out_backup_id[16]) {
  if (!coordinator || !out_host_id || !out_backup_id) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameter");
  }

  /* Try to get from current state first */
  consensus_state_machine_t current_state = consensus_state_get_current_state(coordinator->state);
  if (current_state == CONSENSUS_STATE_ELECTION_COMPLETE) {
    asciichat_error_t err = consensus_state_get_elected_host(coordinator->state, out_host_id);
    if (err == ASCIICHAT_OK) {
      err = consensus_state_get_elected_backup(coordinator->state, out_backup_id);
    }
    if (err == ASCIICHAT_OK) {
      return ASCIICHAT_OK;
    }
  }

  /* Fall back to stored result */
  if (coordinator->has_stored_result) {
    memcpy(out_host_id, coordinator->stored_host_id, 16);
    memcpy(out_backup_id, coordinator->stored_backup_id, 16);
    return ASCIICHAT_OK;
  }

  return SET_ERRNO(ERROR_INVALID_STATE, "No election result available");
}

/**
 * @brief Get current coordinator state
 */
consensus_state_machine_t consensus_coordinator_get_state(const consensus_coordinator_t *coordinator) {
  if (!coordinator || !coordinator->state) {
    return CONSENSUS_STATE_FAILED;
  }
  return consensus_state_get_current_state(coordinator->state);
}

/**
 * @brief Get time until next round
 */
uint64_t consensus_coordinator_time_until_next_round(const consensus_coordinator_t *coordinator) {
  if (!coordinator) {
    return 0;
  }

  uint64_t now_ns = time_get_ns();
  uint64_t next_round_ns = coordinator->last_round_start_ns + CONSENSUS_ROUND_INTERVAL_NS;

  if (now_ns >= next_round_ns) {
    return 0;
  }

  return next_round_ns - now_ns;
}

/**
 * @brief Get count of metrics in current round
 */
int consensus_coordinator_get_metrics_count(const consensus_coordinator_t *coordinator) {
  if (!coordinator || !coordinator->state) {
    return -1;
  }
  return consensus_state_get_metrics_count(coordinator->state);
}
