/**
 * @file session/consensus.h
 * @brief Mode-agnostic ring consensus abstraction for session discovery
 * @ingroup session
 *
 * Provides a clean abstraction over the ring consensus protocol that any mode
 * (server, client, acds, discovery) can use. The abstraction:
 *
 * - Wraps all consensus modules (state, coordinator, topology, election, metrics)
 * - Uses callbacks to decouple consensus logic from transport/metrics specifics
 * - Provides non-blocking periodic processing
 * - Handles automatic packet generation and state transitions
 *
 * Design Principles:
 * 1. Consensus algorithm is mode-agnostic - doesn't know about TCP/WebRTC/etc
 * 2. Modes provide callbacks for:
 *    - Sending packets to next participant
 *    - Measuring network metrics (NAT quality, bandwidth, etc)
 *    - Handling elected host results
 * 3. No tight coupling to specific transports or capture mechanisms
 * 4. Each mode can opt-in or opt-out independently
 */

#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/network/consensus/metrics.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque session consensus handle
 */
typedef struct session_consensus session_consensus_t;

/**
 * @brief Callback: Send a consensus packet to the next ring participant
 *
 * Called by consensus to send packets around the ring. The mode is responsible
 * for serializing and transmitting to the appropriate transport.
 *
 * @param context Opaque pointer provided by caller during create
 * @param next_participant_id 16-byte UUID of next participant in ring
 * @param packet Packet data to send (including header)
 * @param packet_size Size of packet in bytes
 * @return ASCIICHAT_OK on success, error code if sending failed
 */
typedef asciichat_error_t (*session_consensus_send_packet_fn)(void *context, const uint8_t next_participant_id[16],
                                                              const uint8_t *packet, size_t packet_size);

/**
 * @brief Callback: Handle elected host result
 *
 * Called by consensus when a new host election is complete and announced.
 * The mode should:
 * - Store the elected host and backup host info
 * - Update connection targets if participant
 * - Initiate host role if elected as new host
 *
 * @param context Opaque pointer provided by caller during create
 * @param host_id 16-byte UUID of elected host
 * @param host_address Address to connect to (hostname or IP)
 * @param host_port Port to connect to
 * @param backup_id 16-byte UUID of backup host
 * @param backup_address Backup address
 * @param backup_port Backup port
 * @return ASCIICHAT_OK on success, error code otherwise
 */
typedef asciichat_error_t (*session_consensus_on_election_fn)(void *context, const uint8_t host_id[16],
                                                              const char host_address[64], uint16_t host_port,
                                                              const uint8_t backup_id[16],
                                                              const char backup_address[64], uint16_t backup_port);

/**
 * @brief Callback: Measure current participant's network metrics
 *
 * Called by consensus to collect metrics for this participant. Should measure:
 * - NAT tier (0=LAN, 1=Public, 2=UPnP, 3=STUN, 4=TURN)
 * - Upload bandwidth (Kbps)
 * - RTT to current host (nanoseconds)
 * - STUN probe success rate (0-100%)
 * - Public address and port
 * - Connection type (Direct, UPnP, STUN, TURN)
 *
 * @param context Opaque pointer provided by caller during create
 * @param my_id 16-byte UUID of this participant
 * @param out_metrics Output metrics structure (caller-allocated)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
typedef asciichat_error_t (*session_consensus_get_metrics_fn)(void *context, const uint8_t my_id[16],
                                                              participant_metrics_t *out_metrics);

/**
 * @brief Callback type for custom election algorithm
 *
 * If provided, consensus calls this instead of built-in election.
 * Allows modes to implement custom host selection logic.
 *
 * @param context Opaque pointer provided by caller during create
 * @param metrics Array of participant metrics
 * @param num_metrics Count of metrics
 * @param out_best_index Output: Index of best host (not ID)
 * @param out_backup_index Output: Index of backup host
 * @return ASCIICHAT_OK on success, error code otherwise
 */
typedef asciichat_error_t (*session_consensus_election_fn)(void *context, const participant_metrics_t *metrics,
                                                           int num_metrics, int *out_best_index, int *out_backup_index);

/**
 * @brief Callbacks for consensus operations
 *
 * Modes provide these to integrate consensus with their specific transport
 * and metric collection mechanisms.
 */
typedef struct {
  session_consensus_send_packet_fn send_packet; ///< Send packet callback
  session_consensus_on_election_fn on_election; ///< Election result callback
  session_consensus_get_metrics_fn get_metrics; ///< Measure metrics callback
  session_consensus_election_fn election;       ///< Custom election callback (optional, NULL = use default)
  void *context;                                ///< Opaque context passed to all callbacks
} session_consensus_callbacks_t;

/**
 * @brief Create a new session consensus instance
 *
 * Initializes consensus for a participant in a session. The consensus will
 * manage ring topology, metrics collection, and host election according to
 * the configured ring parameters.
 *
 * @param my_id 16-byte UUID of this participant
 * @param is_leader Whether this participant should run elections (typically last in ring)
 * @param participant_ids Array of participant UUIDs (up to 64)
 * @param num_participants Count of participants
 * @param callbacks Callback functions for mode integration
 * @param out_consensus Output consensus handle (caller must destroy)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t session_consensus_create(const uint8_t my_id[16], bool is_leader,
                                           const uint8_t participant_ids[64][16], int num_participants,
                                           const session_consensus_callbacks_t *callbacks,
                                           session_consensus_t **out_consensus);

/**
 * @brief Destroy consensus instance and free all resources
 *
 * @param consensus Consensus handle (safe to pass NULL)
 */
void session_consensus_destroy(session_consensus_t *consensus);

/**
 * @brief Main consensus processing loop - call periodically
 *
 * Handles:
 * - Round scheduling (every 5 minutes)
 * - Collection deadline enforcement (30 seconds)
 * - Metrics measurement and relay around ring
 * - Election computation (leader only)
 * - Result broadcasting
 *
 * Non-blocking: returns immediately if no action needed.
 * Modes should call this regularly (at least once per second) to ensure
 * timely round scheduling and deadline enforcement.
 *
 * @param consensus Consensus handle
 * @param timeout_ms Timeout for this call in milliseconds (0 = non-blocking)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t session_consensus_process(session_consensus_t *consensus, uint32_t timeout_ms);

/**
 * @brief Update ring topology when participants change
 *
 * Called when the ring topology changes (participants join/leave).
 * Updates the consensus topology and resets any in-progress round.
 *
 * @param consensus Consensus handle
 * @param participant_ids Updated array of participant UUIDs
 * @param num_participants New count of participants
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t session_consensus_set_topology(session_consensus_t *consensus, const uint8_t participant_ids[64][16],
                                                 int num_participants);

/**
 * @brief Handle incoming STATS_COLLECTION_START packet
 *
 * Consensus calls this when it receives a collection start from the
 * previous ring participant. Should be called from packet handler in mode.
 *
 * @param consensus Consensus handle
 * @param round_id Collection round ID
 * @param deadline_ns Unix nanosecond deadline for collection
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t session_consensus_on_collection_start(session_consensus_t *consensus, uint32_t round_id,
                                                        uint64_t deadline_ns);

/**
 * @brief Handle incoming STATS_UPDATE packet (metrics relayed around ring)
 *
 * Consensus calls this when it receives metrics from the previous participant.
 * Should be called from packet handler in mode.
 *
 * @param consensus Consensus handle
 * @param sender_id 16-byte UUID of who sent this update
 * @param metrics Array of metrics in this packet
 * @param num_metrics Count of metrics
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t session_consensus_on_stats_update(session_consensus_t *consensus, const uint8_t sender_id[16],
                                                    const participant_metrics_t *metrics, uint8_t num_metrics);

/**
 * @brief Handle incoming ELECTION_RESULT packet from leader
 *
 * Consensus calls this when leader announces the elected host.
 * Should be called from packet handler in mode.
 *
 * @param consensus Consensus handle
 * @param host_id 16-byte UUID of elected host
 * @param host_address Address of elected host
 * @param host_port Port of elected host
 * @param backup_id 16-byte UUID of backup host
 * @param backup_address Address of backup host
 * @param backup_port Port of backup host
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t session_consensus_on_election_result(session_consensus_t *consensus, const uint8_t host_id[16],
                                                       const char host_address[64], uint16_t host_port,
                                                       const uint8_t backup_id[16], const char backup_address[64],
                                                       uint16_t backup_port);

/**
 * @brief Get the currently elected host
 *
 * Returns the most recently elected host and backup. If no election has
 * completed yet, returns the previous stored result if available, or error.
 *
 * @param consensus Consensus handle
 * @param out_host_id Output: 16-byte UUID of host (caller provides buffer)
 * @param out_host_address Output: Host address (caller provides buffer, 64 bytes)
 * @param out_host_port Output: Host port
 * @param out_backup_id Output: 16-byte UUID of backup (caller provides buffer)
 * @param out_backup_address Output: Backup address (caller provides buffer, 64 bytes)
 * @param out_backup_port Output: Backup port
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t session_consensus_get_elected_host(session_consensus_t *consensus, uint8_t out_host_id[16],
                                                     char out_host_address[64], uint16_t *out_host_port,
                                                     uint8_t out_backup_id[16], char out_backup_address[64],
                                                     uint16_t *out_backup_port);

/**
 * @brief Check if consensus has completed at least one round
 *
 * Returns true if at least one complete consensus round has finished with
 * a valid elected host and backup.
 *
 * @param consensus Consensus handle
 * @return true if consensus reached, false otherwise
 */
bool session_consensus_is_ready(session_consensus_t *consensus);

/**
 * @brief Get current consensus state
 *
 * @param consensus Consensus handle
 * @return Current state from internal state machine
 */
int session_consensus_get_state(session_consensus_t *consensus);

/**
 * @brief Get time until next consensus round in nanoseconds
 *
 * Useful for scheduling when to call process() next.
 *
 * @param consensus Consensus handle
 * @return Nanoseconds until next round, or 0 if round is due now
 */
uint64_t session_consensus_time_until_next_round(session_consensus_t *consensus);

/**
 * @brief Get count of metrics collected in current round
 *
 * Useful for monitoring collection progress.
 *
 * @param consensus Consensus handle
 * @return Number of metrics collected, or -1 if invalid
 */
int session_consensus_get_metrics_count(session_consensus_t *consensus);

#ifdef __cplusplus
}
#endif
