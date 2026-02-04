/**
 * @file network/consensus/coordinator.h
 * @brief Ring consensus coordinator orchestration
 * @ingroup consensus
 *
 * Orchestrates the entire ring consensus flow:
 * - Periodic round scheduling (5 minute intervals)
 * - Metrics collection and relay around the ring
 * - Election computation by leader
 * - Result broadcasting and storage
 *
 * The coordinator manages the state machine and calls appropriate functions
 * based on the current consensus state and participant role.
 */

#pragma once

#include "state.h"
#include "topology.h"
#include "packets.h"
#include <ascii-chat/asciichat_errno.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Election callback function type
 *
 * Called by coordinator when election needs to be computed by leader.
 * The callback can run the deterministic election algorithm.
 *
 * @param context Application context (passed to create)
 * @param state State machine with collected metrics
 * @return ASCIICHAT_OK on success, error code otherwise
 */
typedef asciichat_error_t (*consensus_election_func_t)(void *context, consensus_state_t *state);

/**
 * @brief Opaque consensus coordinator handle
 */
typedef struct consensus_coordinator consensus_coordinator_t;

/**
 * @brief Initialize ring consensus coordinator
 *
 * Creates a new coordinator for managing consensus rounds.
 * The coordinator will automatically schedule rounds every 5 minutes.
 *
 * @param my_id My 16-byte UUID
 * @param topology Topology handle (not owned, must remain valid during coordinator lifetime)
 * @param election_func Callback to run election algorithm (leader only)
 * @param election_context Application context passed to election_func
 * @param out_coordinator Output coordinator handle (caller must destroy)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t consensus_coordinator_create(const uint8_t my_id[16], const consensus_topology_t *topology,
                                               consensus_election_func_t election_func, void *election_context,
                                               consensus_coordinator_t **out_coordinator);

/**
 * @brief Destroy coordinator and free resources
 *
 * @param coordinator Coordinator handle (safe to pass NULL)
 */
void consensus_coordinator_destroy(consensus_coordinator_t *coordinator);

/**
 * @brief Main orchestration loop - call periodically
 *
 * Checks if a new collection round should start and processes the current state.
 * Should be called regularly (at least once per second) to ensure timely round scheduling.
 *
 * Handles:
 * - Round scheduling (triggers every 5 minutes)
 * - Collection deadline enforcement (30 second deadline)
 * - Metrics measurement and relay
 * - Election computation (leader only)
 * - Result storage and broadcasting
 *
 * @param coordinator Coordinator handle
 * @param timeout_ms Timeout for operation in milliseconds
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t consensus_coordinator_process(consensus_coordinator_t *coordinator, uint32_t timeout_ms);

/**
 * @brief Update ring topology when participants change
 *
 * Called when the ring topology changes (participants join/leave).
 * Updates internal topology reference and resets any in-progress round.
 *
 * @param coordinator Coordinator handle
 * @param new_topology New topology handle (not owned, must remain valid)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t consensus_coordinator_on_ring_members(consensus_coordinator_t *coordinator,
                                                        const consensus_topology_t *new_topology);

/**
 * @brief Handle STATS_COLLECTION_START packet
 *
 * Called when receiving a collection start message from the previous ring member.
 * Prepares to collect metrics and transitions state machine.
 *
 * @param coordinator Coordinator handle
 * @param round_id Collection round ID
 * @param deadline_ns Unix nanosecond deadline for this round
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t consensus_coordinator_on_collection_start(consensus_coordinator_t *coordinator, uint32_t round_id,
                                                            uint64_t deadline_ns);

/**
 * @brief Handle STATS_UPDATE packet (metrics relayed from another participant)
 *
 * Called when receiving metrics from previous ring member.
 * Adds metrics to state machine and forwards to next member.
 *
 * @param coordinator Coordinator handle
 * @param sender_id Who is sending this metrics update
 * @param metrics Metrics to relay
 * @param num_metrics Count of metrics in packet
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t consensus_coordinator_on_stats_update(consensus_coordinator_t *coordinator,
                                                        const uint8_t sender_id[16],
                                                        const participant_metrics_t *metrics, uint8_t num_metrics);

/**
 * @brief Handle ELECTION_RESULT packet
 *
 * Called when election result is received from leader.
 * Verifies the election, stores the elected host/backup, and stores for fallback.
 *
 * @param coordinator Coordinator handle
 * @param host_id Elected host UUID
 * @param backup_id Elected backup UUID
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t consensus_coordinator_on_election_result(consensus_coordinator_t *coordinator,
                                                           const uint8_t host_id[16], const uint8_t backup_id[16]);

/**
 * @brief Get currently elected host
 *
 * Retrieves the host and backup elected in the most recent successful round.
 * If no round has completed yet, returns stored previous result if available,
 * or an error if no result exists.
 *
 * @param coordinator Coordinator handle
 * @param out_host_id Output 16-byte host UUID (caller provides buffer)
 * @param out_backup_id Output 16-byte backup UUID (caller provides buffer)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t consensus_coordinator_get_current_host(const consensus_coordinator_t *coordinator,
                                                         uint8_t out_host_id[16], uint8_t out_backup_id[16]);

/**
 * @brief Get current coordinator state
 *
 * @param coordinator Coordinator handle
 * @return Current state from internal state machine
 */
consensus_state_machine_t consensus_coordinator_get_state(const consensus_coordinator_t *coordinator);

/**
 * @brief Get time until next round is due
 *
 * Returns the number of nanoseconds until the next collection round is scheduled.
 *
 * @param coordinator Coordinator handle
 * @return Nanoseconds until next round, or 0 if round is due now
 */
uint64_t consensus_coordinator_time_until_next_round(const consensus_coordinator_t *coordinator);

/**
 * @brief Get count of metrics collected in current round
 *
 * Used for testing and monitoring. Returns the number of metrics currently
 * collected in the state machine.
 *
 * @param coordinator Coordinator handle
 * @return Number of metrics collected, or -1 if invalid
 */
int consensus_coordinator_get_metrics_count(const consensus_coordinator_t *coordinator);
