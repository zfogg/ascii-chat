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

/**
 * @brief Discovery session state
 */
typedef enum {
  DISCOVERY_STATE_INIT,               ///< Initial state
  DISCOVERY_STATE_CONNECTING_ACDS,    ///< Connecting to ACDS
  DISCOVERY_STATE_CREATING_SESSION,   ///< Creating new session
  DISCOVERY_STATE_JOINING_SESSION,    ///< Joining existing session
  DISCOVERY_STATE_WAITING_PEER,       ///< Waiting for peer to join (initiator)
  DISCOVERY_STATE_NEGOTIATING,        ///< NAT negotiation in progress
  DISCOVERY_STATE_STARTING_HOST,      ///< Starting as host
  DISCOVERY_STATE_CONNECTING_HOST,    ///< Connecting to host as participant
  DISCOVERY_STATE_ACTIVE,             ///< Session active (call in progress)
  DISCOVERY_STATE_MIGRATING,          ///< Host migration in progress
  DISCOVERY_STATE_FAILED,             ///< Session failed
  DISCOVERY_STATE_ENDED               ///< Session ended
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

  // Negotiation
  negotiate_ctx_t negotiate;

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
  const char *acds_address;     ///< ACDS address (default: "127.0.0.1")
  uint16_t acds_port;           ///< ACDS port (default: 27225)

  // Session to join (NULL = create new)
  const char *session_string;   ///< Session string to join (or NULL to create)

  // Local server config (if we become host)
  uint16_t local_port;          ///< Local port for hosting (default: 27224)

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

#ifdef __cplusplus
}
#endif
