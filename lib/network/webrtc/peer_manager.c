/**
 * @file networking/webrtc/peer_manager.c
 * @brief WebRTC peer connection manager implementation
 * @ingroup webrtc
 *
 * Implements WebRTC peer connection management for session participants.
 * Handles role-based SDP exchange (creator accepts offers, joiner initiates)
 * and integrates WebRTC DataChannels with ACIP transport layer.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <ascii-chat/network/webrtc/peer_manager.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/uthash/uthash.h>
#include <string.h>

/**
 * @brief Per-peer connection state
 */
typedef struct {
  uint8_t participant_id[16];          ///< Remote participant UUID (hash key)
  uint8_t session_id[16];              ///< Session UUID
  webrtc_peer_connection_t *pc;        ///< WebRTC peer connection
  webrtc_data_channel_t *dc;           ///< WebRTC data channel
  bool is_connected;                   ///< DataChannel opened
  struct webrtc_peer_manager *manager; ///< Back-reference to manager
  UT_hash_handle hh;                   ///< uthash handle
} peer_entry_t;

/**
 * @brief WebRTC peer manager structure
 */
typedef struct webrtc_peer_manager {
  webrtc_peer_role_t role;                ///< Session role
  webrtc_peer_manager_config_t config;    ///< Manager configuration
  webrtc_signaling_callbacks_t signaling; ///< Signaling callbacks
  peer_entry_t *peers;                    ///< Hash table of peer connections
  mutex_t peers_mutex;                    ///< Protect peers hash table
} webrtc_peer_manager_t;

// =============================================================================
// Helper Functions
// =============================================================================

/**
 * @brief Find peer by participant_id
 * @note Caller must hold peers_mutex
 */
static peer_entry_t *find_peer_locked(webrtc_peer_manager_t *manager, const uint8_t participant_id[16]) {
  peer_entry_t *peer = NULL;
  HASH_FIND(hh, manager->peers, participant_id, 16, peer);
  return peer;
}

/**
 * @brief Add peer to hash table
 * @note Caller must hold peers_mutex
 */
static void add_peer_locked(webrtc_peer_manager_t *manager, peer_entry_t *peer) {
  HASH_ADD(hh, manager->peers, participant_id, 16, peer);
}

/**
 * @brief Remove and free peer entry
 * @note Caller must hold peers_mutex
 */
static void remove_peer_locked(webrtc_peer_manager_t *manager, peer_entry_t *peer) {
  if (!peer) {
    return;
  }

  HASH_DEL(manager->peers, peer);

  if (peer->dc) {
    webrtc_datachannel_destroy(peer->dc);
  }
  if (peer->pc) {
    webrtc_peer_connection_destroy(peer->pc);
  }

  SAFE_FREE(peer);
}

// =============================================================================
// WebRTC Callbacks
// =============================================================================

/**
 * @brief DataChannel open callback - wrap in ACIP transport
 */
static void on_datachannel_open(webrtc_data_channel_t *dc, void *user_data) {
  peer_entry_t *peer = (peer_entry_t *)user_data;

  log_info("WebRTC DataChannel opened for participant");

  // Get manager to access crypto context
  webrtc_peer_manager_t *manager = peer->manager;
  if (!manager) {
    log_error("No manager found for peer");
    return;
  }

  // For CREATOR role (server), peer->dc is NULL because the DataChannel is received, not created
  // Update peer->dc from the dc parameter passed to this callback
  if (!peer->dc) {
    peer->dc = dc;
    log_debug("Updated peer->dc from DataChannel callback (dc=%p)", (void *)peer->dc);
  }

  // Create ACIP transport wrapper
  acip_transport_t *transport = acip_webrtc_transport_create(peer->pc, peer->dc, manager->config.crypto_ctx);
  if (!transport) {
    log_error("Failed to create ACIP transport for WebRTC DataChannel");
    return;
  }

  log_debug("Transport created, checking callback: on_transport_ready=%p, user_data=%p",
            (void *)manager->config.on_transport_ready, manager->config.user_data);

  // Notify application
  if (manager->config.on_transport_ready) {
    log_debug("Calling on_transport_ready callback");
    manager->config.on_transport_ready(transport, peer->participant_id, manager->config.user_data);
    log_debug("Callback completed");
  } else {
    // No callback - clean up transport
    log_warn("No on_transport_ready callback registered, cleaning up transport");
    acip_transport_destroy(transport);
  }

  peer->is_connected = true;
}

/**
 * @brief Local SDP callback - send to remote peer via ACDS
 */
static void on_local_description(webrtc_peer_connection_t *pc, const char *sdp, const char *type, void *user_data) {
  (void)pc; // Unused
  peer_entry_t *peer = (peer_entry_t *)user_data;
  webrtc_peer_manager_t *manager = peer->manager;

  if (!manager || !manager->signaling.send_sdp) {
    log_error("No signaling callback registered for SDP");
    return;
  }

  log_debug("on_local_description: peer->session_id=%02x%02x%02x%02x..., peer->participant_id=%02x%02x%02x%02x...",
            peer->session_id[0], peer->session_id[1], peer->session_id[2], peer->session_id[3], peer->participant_id[0],
            peer->participant_id[1], peer->participant_id[2], peer->participant_id[3]);

  log_debug("Sending SDP %s to remote peer via ACDS", type);

  asciichat_error_t result =
      manager->signaling.send_sdp(peer->session_id, peer->participant_id, type, sdp, manager->signaling.user_data);

  if (result != ASCIICHAT_OK) {
    log_error("Failed to send SDP via signaling: %s", asciichat_error_string(result));
  }
}

/**
 * @brief Local ICE candidate callback - send to remote peer via ACDS
 */
static void on_local_candidate(webrtc_peer_connection_t *pc, const char *candidate, const char *mid, void *user_data) {
  (void)pc; // Unused
  peer_entry_t *peer = (peer_entry_t *)user_data;
  webrtc_peer_manager_t *manager = peer->manager;

  if (!manager || !manager->signaling.send_ice) {
    log_error("No signaling callback registered for ICE");
    return;
  }

  // Filter out host candidates if --webrtc-skip-host is enabled (for testing STUN/TURN)
  if (GET_OPTION(webrtc_skip_host) && strstr(candidate, "typ host") != NULL) {
    log_debug("Skipping host candidate (--webrtc-skip-host enabled): '%s'", candidate);
    return;
  }

  log_debug("Sending ICE candidate to remote peer via ACDS");
  log_debug("  [1] libdatachannel gave us candidate: '%s' (len=%zu)", candidate, strlen(candidate));
  log_debug("  [1] libdatachannel gave us mid: '%s' (len=%zu)", mid, strlen(mid));

  asciichat_error_t result =
      manager->signaling.send_ice(peer->session_id, peer->participant_id, candidate, mid, manager->signaling.user_data);

  if (result != ASCIICHAT_OK) {
    log_error("Failed to send ICE candidate via signaling: %s", asciichat_error_string(result));
  }
}

/**
 * @brief WebRTC state change callback
 */
static void on_state_change(webrtc_peer_connection_t *pc, webrtc_state_t state, void *user_data) {
  (void)user_data;
  (void)pc;

  const char *state_str = "UNKNOWN";
  switch (state) {
  case WEBRTC_STATE_NEW:
    state_str = "NEW";
    break;
  case WEBRTC_STATE_CONNECTING:
    state_str = "CONNECTING";
    break;
  case WEBRTC_STATE_CONNECTED:
    state_str = "CONNECTED";
    break;
  case WEBRTC_STATE_DISCONNECTED:
    state_str = "DISCONNECTED";
    break;
  case WEBRTC_STATE_FAILED:
    state_str = "FAILED";
    break;
  case WEBRTC_STATE_CLOSED:
    state_str = "CLOSED";
    break;
  }

  log_debug("WebRTC peer connection state: %s", state_str);
}

// =============================================================================
// Peer Connection Creation
// =============================================================================

/**
 * @brief Create peer connection for remote participant
 * @note Caller must hold peers_mutex
 */
static asciichat_error_t create_peer_connection_locked(webrtc_peer_manager_t *manager, const uint8_t session_id[16],
                                                       const uint8_t participant_id[16], peer_entry_t **peer_out) {
  // Check if peer already exists
  peer_entry_t *existing = find_peer_locked(manager, participant_id);
  if (existing) {
    *peer_out = existing;
    return ASCIICHAT_OK;
  }

  // Allocate peer entry
  peer_entry_t *peer = SAFE_MALLOC(sizeof(peer_entry_t), peer_entry_t *);
  if (!peer) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate peer entry");
  }

  memcpy(peer->participant_id, participant_id, 16);
  memcpy(peer->session_id, session_id, 16);
  peer->pc = NULL;
  peer->dc = NULL;
  peer->is_connected = false;
  peer->manager = manager;

  // Create WebRTC configuration
  webrtc_config_t webrtc_config = {
      .stun_servers = manager->config.stun_servers,
      .stun_count = manager->config.stun_count,
      .turn_servers = manager->config.turn_servers,
      .turn_count = manager->config.turn_count,
      .on_state_change = on_state_change,
      .on_local_description = on_local_description,
      .on_local_candidate = on_local_candidate,
      .on_datachannel_open = on_datachannel_open,
      .on_datachannel_message = NULL, // Handled by transport layer
      .on_datachannel_error = NULL,   // Handled by transport layer
      .user_data = peer,              // Pass peer for callbacks
  };

  // Create peer connection
  asciichat_error_t result = webrtc_create_peer_connection(&webrtc_config, &peer->pc);
  if (result != ASCIICHAT_OK) {
    SAFE_FREE(peer);
    return SET_ERRNO(result, "Failed to create WebRTC peer connection");
  }

  // For joiner role, create data channel (creator receives it via callback)
  if (manager->role == WEBRTC_ROLE_JOINER) {
    result = webrtc_create_datachannel(peer->pc, "acip", &peer->dc);
    if (result != ASCIICHAT_OK) {
      webrtc_peer_connection_destroy(peer->pc);
      SAFE_FREE(peer);
      return SET_ERRNO(result, "Failed to create WebRTC data channel");
    }

    // Set datachannel callbacks
    webrtc_datachannel_callbacks_t dc_callbacks = {
        .on_open = on_datachannel_open,
        .on_close = NULL,
        .on_error = NULL,
        .on_message = NULL, // Handled by transport
        .user_data = peer,
    };

    result = webrtc_datachannel_set_callbacks(peer->dc, &dc_callbacks);
    if (result != ASCIICHAT_OK) {
      webrtc_datachannel_destroy(peer->dc);
      webrtc_peer_connection_destroy(peer->pc);
      SAFE_FREE(peer);
      return SET_ERRNO(result, "Failed to set datachannel callbacks");
    }
  }

  // Add to hash table
  add_peer_locked(manager, peer);

  log_debug("Created WebRTC peer connection for participant (role: %s)",
            manager->role == WEBRTC_ROLE_CREATOR ? "creator" : "joiner");

  *peer_out = peer;
  return ASCIICHAT_OK;
}

// =============================================================================
// Public API
// =============================================================================

asciichat_error_t webrtc_peer_manager_create(const webrtc_peer_manager_config_t *config,
                                             const webrtc_signaling_callbacks_t *signaling_callbacks,
                                             webrtc_peer_manager_t **manager_out) {
  if (!config || !signaling_callbacks || !manager_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  if (!signaling_callbacks->send_sdp || !signaling_callbacks->send_ice) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Signaling callbacks required");
  }

  // Allocate manager
  webrtc_peer_manager_t *manager = SAFE_MALLOC(sizeof(webrtc_peer_manager_t), webrtc_peer_manager_t *);
  if (!manager) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate peer manager");
  }

  memcpy(&manager->config, config, sizeof(*config));
  memcpy(&manager->signaling, signaling_callbacks, sizeof(*signaling_callbacks));
  manager->role = config->role;
  manager->peers = NULL;

  if (mutex_init(&manager->peers_mutex, "peers")  != 0) {
    SAFE_FREE(manager);
    return SET_ERRNO(ERROR_INTERNAL, "Failed to initialize peers mutex");
  }

  log_info("Created WebRTC peer manager (role: %s)", manager->role == WEBRTC_ROLE_CREATOR ? "creator" : "joiner");

  *manager_out = manager;
  return ASCIICHAT_OK;
}

void webrtc_peer_manager_destroy(webrtc_peer_manager_t *manager) {
  if (!manager) {
    return;
  }

  mutex_lock(&manager->peers_mutex);

  // Close all peer connections
  peer_entry_t *peer = NULL, *tmp = NULL;
  HASH_ITER(hh, manager->peers, peer, tmp) {
    remove_peer_locked(manager, peer);
  }

  mutex_unlock(&manager->peers_mutex);
  mutex_destroy(&manager->peers_mutex);

  SAFE_FREE(manager);

  log_debug("Destroyed WebRTC peer manager");
}

asciichat_error_t webrtc_peer_manager_handle_sdp(webrtc_peer_manager_t *manager, const acip_webrtc_sdp_t *sdp) {
  if (!manager || !sdp) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  // Extract SDP string and type
  const uint8_t *sdp_data = (const uint8_t *)(sdp + 1); // After header
  const char *sdp_type = (sdp->sdp_type == 0) ? "offer" : "answer";
  uint16_t sdp_len = NET_TO_HOST_U16(sdp->sdp_len);

  // Allocate null-terminated buffer for SDP string (libdatachannel requires C string)
  char *sdp_str = SAFE_MALLOC(sdp_len + 1, char *);
  memcpy(sdp_str, sdp_data, sdp_len);
  sdp_str[sdp_len] = '\0'; // Null-terminate

  log_debug("Handling incoming SDP %s from remote peer (len=%u)", sdp_type, sdp_len);

  mutex_lock(&manager->peers_mutex);

  // Find or create peer connection
  peer_entry_t *peer;

  // Special case: If receiving an answer and we're a joiner, we may have created
  // a peer with broadcast ID (00000000...) and need to update it to the real sender_id
  if (sdp->sdp_type == 1 && manager->role == WEBRTC_ROLE_JOINER) {
    static const uint8_t broadcast_id[16] = {0};
    peer = find_peer_locked(manager, broadcast_id);
    if (peer) {
      log_debug("Updating broadcast peer with real participant_id from answer");
      // Remove from hash with old ID
      HASH_DEL(manager->peers, peer);
      // Update to real participant_id
      memcpy(peer->participant_id, sdp->sender_id, 16);
      // Re-add with new ID
      HASH_ADD(hh, manager->peers, participant_id, 16, peer);
    }
  }

  asciichat_error_t result = create_peer_connection_locked(manager, sdp->session_id, sdp->sender_id, &peer);
  if (result != ASCIICHAT_OK) {
    mutex_unlock(&manager->peers_mutex);
    SAFE_FREE(sdp_str);
    return SET_ERRNO(result, "Failed to create peer connection for SDP");
  }

  mutex_unlock(&manager->peers_mutex);

  // Set remote SDP
  result = webrtc_set_remote_description(peer->pc, sdp_str, sdp_type);
  SAFE_FREE(sdp_str); // Free after use

  if (result != ASCIICHAT_OK) {
    return SET_ERRNO(result, "Failed to set remote SDP");
  }

  // If this is an offer and we're the creator, generate answer automatically
  // (libdatachannel triggers on_local_description callback with answer)
  if (sdp->sdp_type == 0 && manager->role == WEBRTC_ROLE_CREATOR) {
    log_debug("Offer received, answer will be generated automatically");
  }

  return ASCIICHAT_OK;
}

asciichat_error_t webrtc_peer_manager_handle_ice(webrtc_peer_manager_t *manager, const acip_webrtc_ice_t *ice) {
  if (!manager || !ice) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  // Extract ICE candidate and mid (both are null-terminated in the packet)
  const char *candidate = (const char *)(ice + 1); // After header
  size_t candidate_str_len = strlen(candidate);
  const char *mid = candidate + candidate_str_len + 1; // After candidate + null terminator

  log_debug("Handling incoming ICE candidate from remote peer (mid=%s)", mid);
  log_debug("  [3] After ACDS recv - candidate: '%s' (len=%zu)", candidate, strlen(candidate));
  log_debug("  [3] After ACDS recv - mid: '%s' (len=%zu)", mid, strlen(mid));
  log_debug("  [3] After ACDS recv - header.candidate_len=%u", NET_TO_HOST_U16(ice->candidate_len));

  // Hex dump first 100 bytes of payload for debugging
  const uint8_t *payload = (const uint8_t *)(ice + 1);
  log_debug("  [3] Hex dump of payload (first 100 bytes):");
  for (int i = 0; i < 100 && i < (int)candidate_str_len + 20; i += 16) {
    char hex[64] = {0};
    char ascii[20] = {0};
    for (int j = 0; j < 16 && (i + j) < 100; j++) {
      snprintf(hex + j * 3, sizeof(hex) - j * 3, "%02x ", payload[i + j]);
      ascii[j] = (payload[i + j] >= 32 && payload[i + j] < 127) ? payload[i + j] : '.';
    }
    log_debug("    [%04x] %-48s %s", i, hex, ascii);
  }

  mutex_lock(&manager->peers_mutex);

  // Find peer connection
  peer_entry_t *peer = find_peer_locked(manager, ice->sender_id);
  if (!peer) {
    mutex_unlock(&manager->peers_mutex);
    log_warn("ICE candidate for unknown peer, ignoring");
    return ASCIICHAT_OK;
  }

  mutex_unlock(&manager->peers_mutex);

  // Add remote ICE candidate
  asciichat_error_t result = webrtc_add_remote_candidate(peer->pc, candidate, mid);
  if (result != ASCIICHAT_OK) {
    return SET_ERRNO(result, "Failed to add remote ICE candidate");
  }

  return ASCIICHAT_OK;
}

asciichat_error_t webrtc_peer_manager_connect(webrtc_peer_manager_t *manager, const uint8_t session_id[16],
                                              const uint8_t participant_id[16]) {
  if (!manager || !session_id || !participant_id) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  if (manager->role != WEBRTC_ROLE_JOINER) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Only joiners can initiate connections");
  }

  log_debug("webrtc_peer_manager_connect: session_id=%02x%02x%02x%02x..., participant_id=%02x%02x%02x%02x...",
            session_id[0], session_id[1], session_id[2], session_id[3], participant_id[0], participant_id[1],
            participant_id[2], participant_id[3]);

  mutex_lock(&manager->peers_mutex);

  // Create peer connection
  peer_entry_t *peer;
  asciichat_error_t result = create_peer_connection_locked(manager, session_id, participant_id, &peer);
  if (result != ASCIICHAT_OK) {
    mutex_unlock(&manager->peers_mutex);
    return SET_ERRNO(result, "Failed to create peer connection");
  }

  mutex_unlock(&manager->peers_mutex);

  // Note: SDP offer is automatically created by libdatachannel when rtcCreateDataChannel() is called
  // The on_local_description callback will be triggered automatically with the offer
  // No need to manually call webrtc_create_offer() - doing so causes "Unexpected local description" error

  log_info("Initiated WebRTC connection to participant (offer auto-created by DataChannel)");

  return ASCIICHAT_OK;
}

int webrtc_peer_manager_check_gathering_timeouts(webrtc_peer_manager_t *manager, uint32_t timeout_ms) {
  if (!manager) {
    return 0;
  }

  int timeout_count = 0;
  peer_entry_t *peer = NULL, *tmp = NULL;

  mutex_lock(&manager->peers_mutex);

  // Iterate through all peers and check for gathering timeout
  HASH_ITER(hh, manager->peers, peer, tmp) {
    if (!peer || !peer->pc) {
      continue;
    }

    // Check if this peer's ICE gathering has timed out
    if (webrtc_is_gathering_timed_out(peer->pc, timeout_ms)) {
      webrtc_gathering_state_t state = webrtc_get_gathering_state(peer->pc);

      log_error("ICE gathering timeout for peer (participant_id=%02x%02x%02x%02x..., timeout=%ums, state=%d)",
                peer->participant_id[0], peer->participant_id[1], peer->participant_id[2], peer->participant_id[3],
                timeout_ms, state);

      // Call timeout callback if configured
      if (manager->config.on_gathering_timeout) {
        manager->config.on_gathering_timeout(peer->participant_id, timeout_ms, timeout_ms, manager->config.user_data);
      }

      // Remove and close the timed-out peer connection
      remove_peer_locked(manager, peer);
      timeout_count++;

      log_info("Closed and removed timed-out peer connection (count: %d)", timeout_count);
    }
  }

  mutex_unlock(&manager->peers_mutex);

  return timeout_count;
}
