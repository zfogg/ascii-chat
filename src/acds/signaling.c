/**
 * @file acds/signaling.c
 * @brief 🎬 WebRTC SDP/ICE signaling relay implementation
 *
 * Pure relay server for WebRTC signaling - no media processing.
 * Relays SDP offers/answers and ICE candidates between participants
 * using participant_id → socket mapping from tcp_server client registry.
 *
 * RCU Integration:
 * - Session lookups use RCU read-side critical sections (no locks)
 * - Lock-free hash table access with cds_lfht_for_each_entry
 */

#include "acds/signaling.h"
#include "acds/server.h"
#include "acds/session.h"
#include "log/logging.h"
#include "network/network.h"
#include <string.h>
#include <urcu.h>
#include <urcu/rculfhash.h>

/**
 * @brief Helper: Check if UUID is all zeros (broadcast indicator)
 */
static bool is_broadcast_uuid(const uint8_t uuid[16]) {
  for (int i = 0; i < 16; i++) {
    if (uuid[i] != 0) {
      return false;
    }
  }
  return true;
}

/**
 * @brief Helper: Compare two UUIDs for equality
 */
static bool uuid_equals(const uint8_t a[16], const uint8_t b[16]) {
  return memcmp(a, b, 16) == 0;
}

/**
 * @brief Context for finding client by participant_id
 */
typedef struct {
  const uint8_t *target_participant_id; ///< Participant to find
  socket_t found_socket;                ///< Socket if found
  bool found;                           ///< Whether client was found
} find_client_context_t;

/**
 * @brief Callback for tcp_server_foreach_client to find participant socket
 */
static void find_participant_callback(socket_t socket, void *client_data, void *user_arg) {
  find_client_context_t *ctx = (find_client_context_t *)user_arg;

  if (!client_data) {
    return; // Client not yet joined a session
  }

  acds_client_data_t *acds_data = (acds_client_data_t *)client_data;
  if (!acds_data->joined_session) {
    return; // Client connected but hasn't joined
  }

  // Check if participant_id matches
  if (uuid_equals(acds_data->participant_id, ctx->target_participant_id)) {
    ctx->found_socket = socket;
    ctx->found = true;
  }
}

/**
 * @brief Context for broadcasting to session participants
 */
typedef struct {
  const uint8_t *target_session_id; ///< Session to broadcast to
  packet_type_t packet_type;        ///< Packet type to send
  const void *packet;               ///< Packet data
  size_t packet_len;                ///< Packet length
  size_t sent_count;                ///< Number of successful sends
} broadcast_context_t;

/**
 * @brief Callback for tcp_server_foreach_client to broadcast to session
 */
static void broadcast_callback(socket_t socket, void *client_data, void *user_arg) {
  broadcast_context_t *ctx = (broadcast_context_t *)user_arg;

  if (!client_data) {
    return; // Client not yet joined a session
  }

  acds_client_data_t *acds_data = (acds_client_data_t *)client_data;
  if (!acds_data->joined_session) {
    return; // Client connected but hasn't joined
  }

  // Check if client is in target session
  if (!uuid_equals(acds_data->session_id, ctx->target_session_id)) {
    return; // Different session
  }

  // Send packet to this participant
  int result = send_packet(socket, ctx->packet_type, ctx->packet, ctx->packet_len);
  if (result == 0) {
    ctx->sent_count++;
  } else {
    log_warn("Failed to send packet to participant (socket=%d)", socket);
  }
}

asciichat_error_t signaling_relay_sdp(session_registry_t *registry, tcp_server_t *tcp_server,
                                      const acip_webrtc_sdp_t *sdp, size_t total_packet_len) {
  if (!registry || !tcp_server || !sdp) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "registry, tcp_server, or sdp is NULL");
  }

  /* Find session by UUID - uses RCU lock-free iteration (no lock needed!) */
  rcu_read_lock();

  session_entry_t *session = NULL;
  session_entry_t *iter;
  struct cds_lfht_iter iter_ctx;

  /* Iterate through hash table using RCU-safe iterator */
  cds_lfht_for_each_entry(registry->sessions, &iter_ctx, iter, hash_node) {
    if (uuid_equals(iter->session_id, sdp->session_id)) {
      session = iter;
      break;
    }
  }

  if (!session) {
    rcu_read_unlock();
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Session not found for SDP relay");
  }

  rcu_read_unlock();

  // Check if broadcast (recipient_id all zeros) or unicast
  if (is_broadcast_uuid(sdp->recipient_id)) {
    // Broadcast to all participants in session
    log_debug("Broadcasting SDP to all participants in session");
    return signaling_broadcast(registry, tcp_server, sdp->session_id, PACKET_TYPE_ACIP_WEBRTC_SDP, sdp,
                               total_packet_len);
  } else {
    // Unicast to specific recipient
    find_client_context_t ctx = {
        .target_participant_id = sdp->recipient_id, .found_socket = INVALID_SOCKET_VALUE, .found = false};

    tcp_server_foreach_client(tcp_server, find_participant_callback, &ctx);

    if (!ctx.found) {
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Recipient participant not found (may be offline)");
    }

    // Send SDP packet to recipient
    int result = send_packet(ctx.found_socket, PACKET_TYPE_ACIP_WEBRTC_SDP, sdp, total_packet_len);
    if (result != 0) {
      return SET_ERRNO(ERROR_NETWORK, "Failed to send SDP packet to recipient");
    }

    log_debug("Relayed SDP from sender to recipient (socket=%d)", ctx.found_socket);
  }

  return ASCIICHAT_OK;
}

asciichat_error_t signaling_relay_ice(session_registry_t *registry, tcp_server_t *tcp_server,
                                      const acip_webrtc_ice_t *ice, size_t total_packet_len) {
  if (!registry || !tcp_server || !ice) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "registry, tcp_server, or ice is NULL");
  }

  /* Find session by UUID - uses RCU lock-free iteration (no lock needed!) */
  rcu_read_lock();

  session_entry_t *session = NULL;
  session_entry_t *iter;
  struct cds_lfht_iter iter_ctx;

  /* Iterate through hash table using RCU-safe iterator */
  cds_lfht_for_each_entry(registry->sessions, &iter_ctx, iter, hash_node) {
    if (uuid_equals(iter->session_id, ice->session_id)) {
      session = iter;
      break;
    }
  }

  if (!session) {
    rcu_read_unlock();
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Session not found for ICE relay");
  }

  rcu_read_unlock();

  // Check if broadcast (recipient_id all zeros) or unicast
  if (is_broadcast_uuid(ice->recipient_id)) {
    // Broadcast to all participants in session
    log_debug("Broadcasting ICE candidate to all participants in session");
    return signaling_broadcast(registry, tcp_server, ice->session_id, PACKET_TYPE_ACIP_WEBRTC_ICE, ice,
                               total_packet_len);
  } else {
    // Unicast to specific recipient
    find_client_context_t ctx = {
        .target_participant_id = ice->recipient_id, .found_socket = INVALID_SOCKET_VALUE, .found = false};

    tcp_server_foreach_client(tcp_server, find_participant_callback, &ctx);

    if (!ctx.found) {
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Recipient participant not found (may be offline)");
    }

    // Send ICE packet to recipient
    int result = send_packet(ctx.found_socket, PACKET_TYPE_ACIP_WEBRTC_ICE, ice, total_packet_len);
    if (result != 0) {
      return SET_ERRNO(ERROR_NETWORK, "Failed to send ICE packet to recipient");
    }

    log_debug("Relayed ICE candidate from sender to recipient (socket=%d)", ctx.found_socket);
  }

  return ASCIICHAT_OK;
}

asciichat_error_t signaling_broadcast(session_registry_t *registry, tcp_server_t *tcp_server,
                                      const uint8_t session_id[16], packet_type_t packet_type, const void *packet,
                                      size_t packet_len) {
  if (!registry || !tcp_server || !session_id || !packet) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "registry, tcp_server, session_id, or packet is NULL");
  }

  /* Find session by UUID - uses RCU lock-free iteration (no lock needed!) */
  rcu_read_lock();

  session_entry_t *session = NULL;
  session_entry_t *iter;
  struct cds_lfht_iter iter_ctx;

  /* Iterate through hash table using RCU-safe iterator */
  cds_lfht_for_each_entry(registry->sessions, &iter_ctx, iter, hash_node) {
    if (uuid_equals(iter->session_id, session_id)) {
      session = iter;
      break;
    }
  }

  if (!session) {
    rcu_read_unlock();
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Session not found for broadcast");
  }

  rcu_read_unlock();

  // Broadcast to all participants in session
  broadcast_context_t ctx = {.target_session_id = session_id,
                             .packet_type = packet_type,
                             .packet = packet,
                             .packet_len = packet_len,
                             .sent_count = 0};

  tcp_server_foreach_client(tcp_server, broadcast_callback, &ctx);

  if (ctx.sent_count == 0) {
    log_warn("Broadcast sent to 0 participants (all offline or not joined yet)");
  } else {
    log_debug("Broadcast sent to %zu participants", ctx.sent_count);
  }

  return ASCIICHAT_OK;
}
