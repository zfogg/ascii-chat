/**
 * @file acds/signaling.c
 * @brief 🎬 WebRTC SDP/ICE signaling relay implementation
 *
 * TODO: Implement SDP/ICE relay logic
 */

#include "acds/signaling.h"
#include "log/logging.h"
#include <string.h>

asciichat_error_t signaling_relay_sdp(session_registry_t *registry, const acip_webrtc_sdp_t *sdp) {
  if (!registry || !sdp) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "registry or sdp is NULL");
  }

  // TODO: Implement SDP relay
  // - Find session by session_id
  // - Find recipient by recipient_id (or broadcast if all-zeros)
  // - Send SDP packet to recipient(s)

  log_debug("SDP relay not yet implemented");
  return SET_ERRNO(ERROR_GENERAL, "SDP relay not yet implemented");
}

asciichat_error_t signaling_relay_ice(session_registry_t *registry, const acip_webrtc_ice_t *ice) {
  if (!registry || !ice) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "registry or ice is NULL");
  }

  // TODO: Implement ICE relay
  // - Find session by session_id
  // - Find recipient by recipient_id
  // - Send ICE packet to recipient(s)

  log_debug("ICE relay not yet implemented");
  return SET_ERRNO(ERROR_GENERAL, "ICE relay not yet implemented");
}

asciichat_error_t signaling_broadcast(session_registry_t *registry, const uint8_t session_id[16], const void *packet,
                                      size_t packet_len) {
  if (!registry || !session_id || !packet) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "registry, session_id, or packet is NULL");
  }

  // TODO: Implement broadcast
  // - Find session by session_id
  // - Send packet to all participants in session

  log_debug("Broadcast not yet implemented (packet size: %zu)", packet_len);
  return SET_ERRNO(ERROR_GENERAL, "Broadcast not yet implemented");
}
