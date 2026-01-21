/**
 * @file discovery/session.h
 * @brief Discovery session flow management
 * @ingroup discovery
 *
 * Manages the complete discovery session lifecycle:
 * 1. Connect to ACDS
 * 2. Create or join session
 * 3. NAT negotiation (if needed)
 * 4. Transition to host or participant role
 * 5. Handle host migration
 */

#pragma once

#include "asciichat_errno.h"
#include "negotiate.h"
#include "platform/socket.h"
#include "session/host.h"
#include "session/participant.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
#define MAX_PARTICIPANTS 16

/**
 * @brief Ring consensus state (Host-Mediated Proactive Election)
 *
 * **NEW SIMPLIFIED DESIGN**:
 * - Host (not quorum leader) collects NETWORK_QUALITY from all participants every 5 minutes
 * - Participants send metrics TO HOST (not peer-to-peer)
 * - Host elects future host and broadcasts FUTURE_HOST_ELECTED
 * - All participants store future host info locally
 * - When current host dies: instant failover to pre-elected host (no re-election!)
 *
 * Participants just need to store the pre-elected future host info.
 * No ring topology, no quorum leader rotation, no P2P collection.
 */
typedef struct {
  // Future host information (pre-elected by host every 5 minutes)
  uint8_t future_host_id[16];          ///< Who will host if current host dies
  char future_host_address[64];        ///< Where to connect
  uint16_t future_host_port;           ///< Port number
  uint8_t future_host_connection_type; ///< acip_connection_type_t (DIRECT, UPNP, STUN, TURN)
  bool am_future_host;                 ///< Am I the elected future host?
  uint64_t future_host_elected_round;  ///< Which 5-minute round this was elected in

  // Timing for next proactive election
  uint64_t last_ring_round_ms; ///< When host last ran election (for 5-min timer)
} ring_consensus_t;

/**
 * @brief Migration detection and failover state
 *
 * **SIMPLIFIED DESIGN**: No re-election during migration!
 * Future host was pre-elected 5 minutes ago. When current host dies,
 * we already know who takes over and where to find them.
 */
typedef enum {
  MIGRATION_STATE_NONE,     ///< No migration in progress
  MIGRATION_STATE_DETECTED, ///< Host disconnect detected
  MIGRATION_STATE_FAILOVER, ///< Failing over to pre-elected future host
  MIGRATION_STATE_COMPLETE  ///< Failover complete, call resumed
} migration_state_t;

typedef struct {
  migration_state_t state;    ///< Current migration state
  uint64_t detection_time_ms; ///< When host disconnect detected (Unix ms)
  uint8_t last_host_id[16];   ///< The host that died
  uint32_t disconnect_reason; ///< Reason for disconnect (from HOST_LOST packet)
} migration_ctx_t;

/**
 * @brief Discovery session state
 */
typedef enum {
  DISCOVERY_STATE_INIT,             ///< Initial state
  DISCOVERY_STATE_CONNECTING_ACDS,  ///< Connecting to ACDS
  DISCOVERY_STATE_CREATING_SESSION, ///< Creating new session
  DISCOVERY_STATE_JOINING_SESSION,  ///< Joining existing session
  DISCOVERY_STATE_WAITING_PEER,     ///< Waiting for peer to join (initiator)
  DISCOVERY_STATE_NEGOTIATING,      ///< NAT negotiation in progress
  DISCOVERY_STATE_STARTING_HOST,    ///< Starting as host
  DISCOVERY_STATE_CONNECTING_HOST,  ///< Connecting to host as participant
  DISCOVERY_STATE_ACTIVE,           ///< Session active (call in progress)
  DISCOVERY_STATE_MIGRATING,        ///< Host migration in progress
  DISCOVERY_STATE_FAILED,           ///< Session failed
  DISCOVERY_STATE_ENDED             ///< Session ended
} discovery_state_t;

/**
 * @brief Discovery session context
 */
typedef struct {
  // State
  discovery_state_t state;
  asciichat_error_t error;

  // Session info
  uint8_t session_id[16];
  uint8_t participant_id[16];
  uint8_t initiator_id[16];
  char session_string[64];
  bool is_initiator;
  bool is_host;

  // ACDS connection
  socket_t acds_socket;
  char acds_address[64];
  uint16_t acds_port;

  // Host info (when not us)
  uint8_t host_id[16];
  char host_address[64];
  uint16_t host_port;

  // Negotiation (initial host negotiation)
  negotiate_ctx_t negotiate;

  // Ring Consensus (NEW P2P design)
  ring_consensus_t ring;

  // Migration state (host failover)
  migration_ctx_t migration;

  // Role contexts (only one is active at a time)
  session_host_t *host_ctx;
  session_participant_t *participant_ctx;

  // Callbacks for state changes
  void (*on_state_change)(discovery_state_t new_state, void *user_data);
  void (*on_session_ready)(const char *session_string, void *user_data);
  void (*on_error)(asciichat_error_t error, const char *message, void *user_data);
  void *callback_user_data;
} discovery_session_t;

/**
 * @brief Configuration for discovery session
 */
typedef struct {
  // ACDS server
  const char *acds_address; ///< ACDS address (default: "127.0.0.1")
  uint16_t acds_port;       ///< ACDS port (default: 27225)

  // Session to join (NULL = create new)
  const char *session_string; ///< Session string to join (or NULL to create)

  // Local server config (if we become host)
  uint16_t local_port; ///< Local port for hosting (default: 27224)

  // Callbacks
  void (*on_state_change)(discovery_state_t new_state, void *user_data);
  void (*on_session_ready)(const char *session_string, void *user_data);
  void (*on_error)(asciichat_error_t error, const char *message, void *user_data);
  void *callback_user_data;
} discovery_config_t;

/**
 * @brief Create a new discovery session
 * @param config Session configuration
 * @return New session or NULL on error
 */
discovery_session_t *discovery_session_create(const discovery_config_t *config);

/**
 * @brief Destroy discovery session and free resources
 * @param session Session to destroy
 */
void discovery_session_destroy(discovery_session_t *session);

/**
 * @brief Start the discovery session
 *
 * Connects to ACDS and either creates or joins a session.
 *
 * @param session Session context
 * @return ASCIICHAT_OK on success
 */
asciichat_error_t discovery_session_start(discovery_session_t *session);

/**
 * @brief Process session events (call in main loop)
 *
 * Handles incoming ACDS messages, negotiation, and state transitions.
 *
 * @param session Session context
 * @param timeout_ms Max time to wait for events (0 = non-blocking)
 * @return ASCIICHAT_OK on success, error on failure
 */
asciichat_error_t discovery_session_process(discovery_session_t *session, int timeout_ms);

/**
 * @brief Stop the discovery session
 * @param session Session context
 */
void discovery_session_stop(discovery_session_t *session);

/**
 * @brief Get current session state
 * @param session Session context
 * @return Current state
 */
discovery_state_t discovery_session_get_state(const discovery_session_t *session);

/**
 * @brief Check if session is active (call in progress)
 * @param session Session context
 * @return True if active
 */
bool discovery_session_is_active(const discovery_session_t *session);

/**
 * @brief Get session string
 * @param session Session context
 * @return Session string or NULL if not yet assigned
 */
const char *discovery_session_get_string(const discovery_session_t *session);

/**
 * @brief Check if we are the host
 * @param session Session context
 * @return True if we are hosting
 */
bool discovery_session_is_host(const discovery_session_t *session);

/**
 * @brief Get host context (if we are host)
 * @param session Session context
 * @return Host context or NULL
 */
session_host_t *discovery_session_get_host(discovery_session_t *session);

/**
 * @brief Get participant context (if we are participant)
 * @param session Session context
 * @return Participant context or NULL
 */
session_participant_t *discovery_session_get_participant(discovery_session_t *session);

/**
 * @brief Initialize ring consensus state
 *
 * Simplified for host-mediated architecture - just initializes timing,
 * no participant list needed (host runs the election).
 *
 * @param session Session context
 * @return ASCIICHAT_OK on success
 */
asciichat_error_t discovery_session_init_ring(discovery_session_t *session);

/**
 * @brief Start a new ring consensus round (every 5 minutes or on new joiner)
 * @param session Session context
 * @return ASCIICHAT_OK on success
 */
asciichat_error_t discovery_session_start_ring_round(discovery_session_t *session);

/**
 * @brief Detect host disconnect (check connection status)
 * @param session Session context
 * @return ASCIICHAT_OK if host alive, error if disconnected
 */
asciichat_error_t discovery_session_check_host_alive(discovery_session_t *session);

/**
 * @brief Handle host disconnect with automatic failover to future host
 * @param session Session context
 * @param disconnect_reason Reason code for disconnect
 * @return ASCIICHAT_OK on successful failover
 */
asciichat_error_t discovery_session_handle_host_disconnect(discovery_session_t *session, uint32_t disconnect_reason);

/**
 * @brief Become the host (called when elected as future host)
 * @param session Session context
 * @return ASCIICHAT_OK on success
 */
asciichat_error_t discovery_session_become_host(discovery_session_t *session);

/**
 * @brief Connect to pre-elected future host (called when NOT future host)
 * @param session Session context
 * @return ASCIICHAT_OK on success
 */
asciichat_error_t discovery_session_connect_to_future_host(discovery_session_t *session);

/**
 * @brief Get future host information
 * @param session Session context
 * @param out_id Output: Future host participant ID
 * @param out_address Output: Future host address
 * @param out_port Output: Future host port
 * @param out_connection_type Output: Connection type
 * @return ASCIICHAT_OK if future host known, error otherwise
 */
asciichat_error_t discovery_session_get_future_host(const discovery_session_t *session, uint8_t out_id[16],
                                                    char out_address[64], uint16_t *out_port,
                                                    uint8_t *out_connection_type);

/**
 * @brief Check if we are the future host
 * @param session Session context
 * @return True if we will be host if current host dies
 */
bool discovery_session_is_future_host(const discovery_session_t *session);

#ifdef __cplusplus
}
#endif
