/**
 * @file acds/signaling.c
 * @brief 🎬 WebRTC SDP/ICE signaling relay implementation
 *
 * Pure relay server for WebRTC signaling - no media processing.
 * Relays SDP offers/answers and ICE candidates between participants
 * using participant_id → socket mapping from tcp_server client registry.
 *
 * Session validation uses SQLite database lookups.
 */

#include "discovery-service/signaling.h"
#include "discovery-service/server.h"
#include <ascii-chat/discovery/database.h>
#include <ascii-chat/discovery/session.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/network/network.h>
#include <string.h>

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
  const uint8_t *target_session_id;      ///< Session to broadcast to
  const uint8_t *exclude_participant_id; ///< Participant to exclude (NULL = no exclusion)
  packet_type_t packet_type;             ///< Packet type to send
  const void *packet;                    ///< Packet data
  size_t packet_len;                     ///< Packet length
  size_t sent_count;                     ///< Number of successful sends
} broadcast_context_t;

/**
 * @brief Callback for tcp_server_foreach_client to broadcast to session
 */
static void broadcast_callback(socket_t socket, void *client_data, void *user_arg) {
  broadcast_context_t *ctx = (broadcast_context_t *)user_arg;

  if (!client_data) {
    log_debug("Broadcast: socket=%d has no client_data", socket);
    return; // Client not yet joined a session
  }

  acds_client_data_t *acds_data = (acds_client_data_t *)client_data;
  if (!acds_data->joined_session) {
    log_debug("Broadcast: socket=%d not joined (joined_session=false)", socket);
    return; // Client connected but hasn't joined
  }

  log_debug("Broadcast: checking socket=%d (session=%02x%02x..., participant=%02x%02x...)", socket,
            acds_data->session_id[0], acds_data->session_id[1], acds_data->participant_id[0],
            acds_data->participant_id[1]);

  // Check if client is in target session
  if (!uuid_equals(acds_data->session_id, ctx->target_session_id)) {
    log_debug("Broadcast: socket=%d in different session", socket);
    return; // Different session
  }

  // Skip excluded participant (e.g., sender)
  if (ctx->exclude_participant_id && uuid_equals(acds_data->participant_id, ctx->exclude_participant_id)) {
    log_debug("Broadcast: socket=%d is excluded sender (participant=%02x%02x...)", socket, acds_data->participant_id[0],
              acds_data->participant_id[1]);
    return; // Skip sender
  }

  // Send packet to this participant
  log_debug("Broadcast: sending to socket=%d (participant=%02x%02x...)", socket, acds_data->participant_id[0],
            acds_data->participant_id[1]);
  int result = send_packet(socket, ctx->packet_type, ctx->packet, ctx->packet_len);
  if (result == 0) {
    ctx->sent_count++;
  } else {
    log_warn("Failed to send packet to participant (socket=%d)", socket);
  }
}

asciichat_error_t signaling_relay_sdp(sqlite3 *db, tcp_server_t *tcp_server, const acip_webrtc_sdp_t *sdp,
                                      size_t total_packet_len) {
  if (!db || !tcp_server || !sdp) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db, tcp_server, or sdp is NULL");
  }

  /* Find session by UUID using database lookup */
  log_debug("SDP relay: Looking up session_id=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
            sdp->session_id[0], sdp->session_id[1], sdp->session_id[2], sdp->session_id[3], sdp->session_id[4],
            sdp->session_id[5], sdp->session_id[6], sdp->session_id[7], sdp->session_id[8], sdp->session_id[9],
            sdp->session_id[10], sdp->session_id[11], sdp->session_id[12], sdp->session_id[13], sdp->session_id[14],
            sdp->session_id[15]);
  session_entry_t *session = database_session_find_by_id(db, sdp->session_id);
  if (!session) {
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Session not found for SDP relay");
  }
  session_entry_destroy(session); // We only need to validate existence

  // Check if broadcast (recipient_id all zeros) or unicast
  if (is_broadcast_uuid(sdp->recipient_id)) {
    // Broadcast to all participants in session (except sender)
    log_debug("Broadcasting SDP to all participants in session (excluding sender)");
    return signaling_broadcast(db, tcp_server, sdp->session_id, PACKET_TYPE_ACIP_WEBRTC_SDP, sdp, total_packet_len,
                               sdp->sender_id);
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

asciichat_error_t signaling_relay_ice(sqlite3 *db, tcp_server_t *tcp_server, const acip_webrtc_ice_t *ice,
                                      size_t total_packet_len) {
  if (!db || !tcp_server || !ice) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db, tcp_server, or ice is NULL");
  }

  /* Find session by UUID using database lookup */
  session_entry_t *session = database_session_find_by_id(db, ice->session_id);
  if (!session) {
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Session not found for ICE relay");
  }
  session_entry_destroy(session); // We only need to validate existence

  // Check if broadcast (recipient_id all zeros) or unicast
  if (is_broadcast_uuid(ice->recipient_id)) {
    // Broadcast to all participants in session (except sender)
    log_debug("Broadcasting ICE candidate to all participants in session (excluding sender)");
    return signaling_broadcast(db, tcp_server, ice->session_id, PACKET_TYPE_ACIP_WEBRTC_ICE, ice, total_packet_len,
                               ice->sender_id);
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

asciichat_error_t signaling_broadcast(sqlite3 *db, tcp_server_t *tcp_server, const uint8_t session_id[16],
                                      packet_type_t packet_type, const void *packet, size_t packet_len,
                                      const uint8_t *exclude_participant_id) {
  if (!db || !tcp_server || !session_id || !packet) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db, tcp_server, session_id, or packet is NULL");
  }

  /* Find session by UUID using database lookup */
  session_entry_t *session = database_session_find_by_id(db, session_id);
  if (!session) {
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Session not found for broadcast");
  }
  session_entry_destroy(session); // We only need to validate existence

  // Broadcast to all participants in session (excluding sender if specified)
  broadcast_context_t ctx = {.target_session_id = session_id,
                             .exclude_participant_id = exclude_participant_id,
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
