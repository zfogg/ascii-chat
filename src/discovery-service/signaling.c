/* Discovery signaling through pinned connection-registry visits.
 * Sends run outside the registry mutex while participant state remains alive. */

#include "discovery-service/signaling.h"
#include "discovery-service/server.h"
#include <ascii-chat/discovery/database.h>
#include <ascii-chat/discovery/session.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/network/network.h>
#include <ascii-chat/network/acip/send.h>
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
  packet_type_t type;
  const void *packet;
  size_t length;
  asciichat_error_t result;
  bool found; ///< Whether client was found
} find_client_context_t;

/**
 * @brief Find a participant through a pinned registry visit
 */
static void find_participant_callback(network_connection_t *connection, void *user_arg) {
  void *client_data = network_connection_context(connection);
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
    ctx->found = true;
    // The registry visitor pins the connection through packet admission.
    ctx->result = packet_send_via_transport(acds_data->transport, ctx->type, ctx->packet, ctx->length, 0);
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
 * @brief Send to a participant through a pinned registry visit
 */
static void broadcast_callback(network_connection_t *connection, void *user_arg) {
  void *client_data = network_connection_context(connection);
  broadcast_context_t *ctx = (broadcast_context_t *)user_arg;

  if (!client_data) {
    log_debug("Broadcast: socket has no client_data");
    return; // Client not yet joined a session
  }

  acds_client_data_t *acds_data = (acds_client_data_t *)client_data;
  if (!acds_data->joined_session) {
    log_debug("Broadcast: client not joined (joined_session=false)");
    return; // Client connected but hasn't joined
  }

  if (!acds_data->transport) {
    log_warn("Broadcast: client has no transport");
    return;
  }

  log_debug("Broadcast: checking participant=%02x%02x... (session=%02x%02x...)", acds_data->participant_id[0],
            acds_data->participant_id[1], acds_data->session_id[0], acds_data->session_id[1]);

  // Check if client is in target session
  if (!uuid_equals(acds_data->session_id, ctx->target_session_id)) {
    log_debug("Broadcast: client in different session");
    return; // Different session
  }

  // Skip excluded participant (e.g., sender)
  if (ctx->exclude_participant_id && uuid_equals(acds_data->participant_id, ctx->exclude_participant_id)) {
    log_debug("Broadcast: excluding sender (participant=%02x%02x...)", acds_data->participant_id[0],
              acds_data->participant_id[1]);
    return; // Skip sender
  }

  // Send packet through transport (not raw socket)
  log_debug("★ BROADCAST_CALLBACK: sending packet_type=%u to participant=%02x%02x...", ctx->packet_type,
            acds_data->participant_id[0], acds_data->participant_id[1]);
  asciichat_error_t result =
      packet_send_via_transport(acds_data->transport, ctx->packet_type, ctx->packet, ctx->packet_len, 0);
  if (result == ASCIICHAT_OK) {
    ctx->sent_count++;
    log_debug("★ BROADCAST_CALLBACK: successfully sent to participant=%02x%02x...", acds_data->participant_id[0],
              acds_data->participant_id[1]);
  } else {
    log_warn("★ BROADCAST_CALLBACK_FAILED: Failed to send to participant=%02x%02x...: %s", acds_data->participant_id[0],
             acds_data->participant_id[1], asciichat_error_format(NULL, result));
  }
}

asciichat_error_t signaling_relay_sdp(sqlite3 *db, network_server_t *network_server, const acip_webrtc_sdp_t *sdp,
                                      size_t total_packet_len) {
  if (!db || !network_server || !sdp) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db, network server, or sdp is NULL");
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
    return signaling_broadcast(db, network_server, sdp->session_id, PACKET_TYPE_ACIP_WEBRTC_SDP, sdp, total_packet_len,
                               sdp->sender_id);
  } else {
    // Unicast to specific recipient
    find_client_context_t ctx = {.target_participant_id = sdp->recipient_id,
                                 .found = false,
                                 .type = PACKET_TYPE_ACIP_WEBRTC_SDP,
                                 .packet = sdp,
                                 .length = total_packet_len};

    network_server_foreach(network_server, find_participant_callback, &ctx);

    if (!ctx.found) {
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Recipient participant not found (may be offline)");
    }

    // Send SDP packet to recipient
    asciichat_error_t result = ctx.result;
    if (result != ASCIICHAT_OK) {
      return SET_ERRNO(ERROR_NETWORK, "Failed to send SDP packet to recipient");
    }

    log_debug("Relayed SDP to participant");
  }

  return ASCIICHAT_OK;
}

asciichat_error_t signaling_relay_ice(sqlite3 *db, network_server_t *network_server, const acip_webrtc_ice_t *ice,
                                      size_t total_packet_len) {
  if (!db || !network_server || !ice) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db, network server, or ice is NULL");
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
    return signaling_broadcast(db, network_server, ice->session_id, PACKET_TYPE_ACIP_WEBRTC_ICE, ice, total_packet_len,
                               ice->sender_id);
  } else {
    // Unicast to specific recipient
    find_client_context_t ctx = {.target_participant_id = ice->recipient_id,
                                 .found = false,
                                 .type = PACKET_TYPE_ACIP_WEBRTC_ICE,
                                 .packet = ice,
                                 .length = total_packet_len};

    network_server_foreach(network_server, find_participant_callback, &ctx);

    if (!ctx.found) {
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Recipient participant not found (may be offline)");
    }

    // Send ICE packet to recipient
    asciichat_error_t result = ctx.result;
    if (result != ASCIICHAT_OK) {
      return SET_ERRNO(ERROR_NETWORK, "Failed to send ICE packet to recipient");
    }

    log_debug("Relayed ICE candidate to participant");
  }

  return ASCIICHAT_OK;
}

asciichat_error_t signaling_broadcast(sqlite3 *db, network_server_t *network_server, const uint8_t session_id[16],
                                      packet_type_t packet_type, const void *packet, size_t packet_len,
                                      const uint8_t *exclude_participant_id) {
  if (!db || !network_server || !session_id || !packet) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db, network server, session_id, or packet is NULL");
  }

  log_debug("★ SIGNALING_BROADCAST_START: packet_type=%u, packet_len=%zu, session=%02x%02x...", packet_type, packet_len,
            session_id[0], session_id[1]);
  if (exclude_participant_id) {
    log_debug("★ SIGNALING_BROADCAST: excluding participant=%02x%02x...", exclude_participant_id[0],
              exclude_participant_id[1]);
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

  network_server_foreach(network_server, broadcast_callback, &ctx);

  if (ctx.sent_count == 0) {
    log_warn("★ SIGNALING_BROADCAST_COMPLETE: sent to 0 participants (all offline or not joined yet)");
  } else {
    log_debug("★ SIGNALING_BROADCAST_COMPLETE: successfully sent to %zu participants", ctx.sent_count);
  }

  return ASCIICHAT_OK;
}

asciichat_error_t signaling_relay_network_quality(sqlite3 *db, network_server_t *network_server,
                                                  const acip_nat_quality_t *quality, size_t total_packet_len) {
  if (!db || !network_server || !quality) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db, network server, or quality is NULL");
  }

  // Validate packet size
  if (total_packet_len < sizeof(acip_nat_quality_t)) {
    return SET_ERRNO(ERROR_NETWORK_SIZE, "NETWORK_QUALITY packet too small");
  }

  // Extract session_id and sender_id from the quality packet
  const uint8_t *session_id = quality->session_id;
  const uint8_t *sender_id = quality->participant_id;

  // Find session by UUID using database lookup
  log_debug("NETWORK_QUALITY relay: session_id=%02x%02x..., sender_id=%02x%02x...", session_id[0], session_id[1],
            sender_id[0], sender_id[1]);
  session_entry_t *session = database_session_find_by_id(db, session_id);
  if (!session) {
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Session not found for NETWORK_QUALITY relay");
  }
  session_entry_destroy(session); // We only need to validate existence

  // Broadcast to all participants in session (except sender)
  log_debug("Broadcasting NETWORK_QUALITY to all participants in session (excluding sender)");
  return signaling_broadcast(db, network_server, session_id, PACKET_TYPE_ACIP_NETWORK_QUALITY, quality,
                             total_packet_len, sender_id);
}
