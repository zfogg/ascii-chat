/**
 * @file discovery/negotiate.c
 * @brief Host negotiation logic for discovery mode
 * @ingroup discovery
 */

#include "negotiate.h"

#include "common.h"
#include "log/logging.h"
#include "network/acip/acds.h"

#include <string.h>

// Maximum participants in a session (must match session.h definition)
#define MAX_PARTICIPANTS 16

void negotiate_init(negotiate_ctx_t *ctx, const uint8_t session_id[16], const uint8_t participant_id[16],
                    bool is_initiator) {
  if (!ctx) return;

  memset(ctx, 0, sizeof(negotiate_ctx_t));

  if (session_id) memcpy(ctx->session_id, session_id, 16);
  if (participant_id) memcpy(ctx->participant_id, participant_id, 16);

  ctx->is_initiator = is_initiator;
  ctx->state = NEGOTIATE_STATE_INIT;
  ctx->peer_quality_received = false;
  ctx->we_are_host = false;

  nat_quality_init(&ctx->our_quality);
  nat_quality_init(&ctx->peer_quality);

  log_debug("Negotiation initialized (initiator=%d)", is_initiator);
}

asciichat_error_t negotiate_start_detection(negotiate_ctx_t *ctx, const char *stun_server, uint16_t local_port) {
  if (!ctx) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "ctx is NULL");
  }

  ctx->state = NEGOTIATE_STATE_DETECTING_NAT;
  log_info("Starting NAT detection for host negotiation...");

  asciichat_error_t result = nat_detect_quality(&ctx->our_quality, stun_server, local_port);
  if (result != ASCIICHAT_OK) {
    ctx->state = NEGOTIATE_STATE_FAILED;
    ctx->error = result;
    log_error("NAT detection failed");
    return result;
  }

  // If we already have peer quality, we can determine result
  if (ctx->peer_quality_received) {
    ctx->state = NEGOTIATE_STATE_COMPARING;
    return negotiate_determine_result(ctx);
  }

  ctx->state = NEGOTIATE_STATE_WAITING_PEER;
  log_info("NAT detection complete, waiting for peer quality...");
  return ASCIICHAT_OK;
}

asciichat_error_t negotiate_receive_peer_quality(negotiate_ctx_t *ctx, const acip_nat_quality_t *peer_quality) {
  if (!ctx || !peer_quality) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "ctx or peer_quality is NULL");
  }

  nat_quality_from_acip(peer_quality, &ctx->peer_quality);
  ctx->peer_quality_received = true;

  log_info("Received peer NAT quality: tier=%d, upload=%u kbps", nat_compute_tier(&ctx->peer_quality),
           ctx->peer_quality.upload_kbps);

  // If our detection is complete, we can determine result
  if (ctx->our_quality.detection_complete) {
    ctx->state = NEGOTIATE_STATE_COMPARING;
    return negotiate_determine_result(ctx);
  }

  // Otherwise, keep waiting for our detection to complete
  return ASCIICHAT_OK;
}

/**
 * @brief Determine connection type for a NAT quality result
 *
 * Helper function to avoid code duplication. Returns the most direct connection
 * type supported: DIRECT_PUBLIC > UPNP > STUN.
 *
 * @param quality NAT quality to evaluate
 * @return Connection type (acip_connection_type_t)
 */
static uint8_t determine_connection_type(const nat_quality_t *quality) {
  if (!quality) {
    return ACIP_CONNECTION_TYPE_STUN; // Default fallback
  }

  if (quality->has_public_ip) {
    return ACIP_CONNECTION_TYPE_DIRECT_PUBLIC;
  } else if (quality->upnp_available) {
    return ACIP_CONNECTION_TYPE_UPNP;
  } else {
    return ACIP_CONNECTION_TYPE_STUN;
  }
}

asciichat_error_t negotiate_determine_result(negotiate_ctx_t *ctx) {
  if (!ctx) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "ctx is NULL");
  }

  if (!ctx->our_quality.detection_complete || !ctx->peer_quality_received) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Cannot determine result: detection incomplete");
  }

  ctx->state = NEGOTIATE_STATE_COMPARING;

  // Compare NAT qualities
  int result = nat_compare_quality(&ctx->our_quality, &ctx->peer_quality, ctx->is_initiator);

  if (result <= 0) {
    // We should host
    ctx->we_are_host = true;
    ctx->state = NEGOTIATE_STATE_WE_HOST;

    // Set our address as host address
    if (ctx->our_quality.public_address[0]) {
      SAFE_STRNCPY(ctx->host_address, ctx->our_quality.public_address, sizeof(ctx->host_address));
    } else {
      // Fall back to localhost for testing
      SAFE_STRNCPY(ctx->host_address, "127.0.0.1", sizeof(ctx->host_address));
    }

    ctx->host_port = ctx->our_quality.upnp_available ? ctx->our_quality.upnp_mapped_port : ACIP_HOST_DEFAULT_PORT;
    ctx->connection_type = determine_connection_type(&ctx->our_quality);

    log_info("Negotiation result: WE ARE HOST (addr=%s:%u, type=%d)", ctx->host_address, ctx->host_port,
             ctx->connection_type);
  } else {
    // They should host
    ctx->we_are_host = false;
    ctx->state = NEGOTIATE_STATE_THEY_HOST;

    // Set peer address as host address
    if (ctx->peer_quality.public_address[0]) {
      SAFE_STRNCPY(ctx->host_address, ctx->peer_quality.public_address, sizeof(ctx->host_address));
    }

    ctx->host_port = ctx->peer_quality.upnp_available ? ctx->peer_quality.upnp_mapped_port : ACIP_HOST_DEFAULT_PORT;
    ctx->connection_type = determine_connection_type(&ctx->peer_quality);

    log_info("Negotiation result: THEY ARE HOST (addr=%s:%u, type=%d)", ctx->host_address, ctx->host_port,
             ctx->connection_type);
  }

  ctx->state = NEGOTIATE_STATE_COMPLETE;
  return ASCIICHAT_OK;
}

negotiate_state_t negotiate_get_state(const negotiate_ctx_t *ctx) {
  if (!ctx) return NEGOTIATE_STATE_FAILED;
  return ctx->state;
}

bool negotiate_is_complete(const negotiate_ctx_t *ctx) {
  if (!ctx) return true;
  return ctx->state == NEGOTIATE_STATE_COMPLETE || ctx->state == NEGOTIATE_STATE_FAILED;
}

asciichat_error_t negotiate_get_error(const negotiate_ctx_t *ctx) {
  if (!ctx) return ERROR_INVALID_PARAM;
  return ctx->error;
}

asciichat_error_t negotiate_elect_future_host(
    const acip_nat_quality_t collected_quality[],
    const uint8_t participant_ids[][16],
    size_t num_participants,
    uint8_t out_future_host_id[16]) {
  if (!collected_quality || !participant_ids || !out_future_host_id) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
    return ERROR_INVALID_PARAM;
  }

  if (num_participants == 0 || num_participants > MAX_PARTICIPANTS) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid participant count");
    return ERROR_INVALID_PARAM;
  }

  // Special case: only one participant
  if (num_participants == 1) {
    memcpy(out_future_host_id, participant_ids[0], 16);
    log_info("Only one participant, electing as future host");
    return ASCIICHAT_OK;
  }

  // Convert all acip_nat_quality_t to nat_quality_t for comparison
  nat_quality_t qualities[MAX_PARTICIPANTS];
  for (size_t i = 0; i < num_participants; i++) {
    nat_quality_from_acip(&collected_quality[i], &qualities[i]);
  }

  // Find the best candidate by comparing all pairs
  // Winner is the one who "wins" most comparisons
  size_t best_idx = 0;
  int best_wins = 0;

  for (size_t i = 0; i < num_participants; i++) {
    int wins = 0;

    // Compare against all others
    for (size_t j = 0; j < num_participants; j++) {
      if (i == j) continue;

      // Compare: -1 means i wins, 1 means j wins, 0 means tie
      // Note: we_are_initiator parameter doesn't matter for multiparty comparison
      // We use false consistently for deterministic results
      int result = nat_compare_quality(&qualities[i], &qualities[j], false);

      if (result <= 0) {
        wins++; // i wins or tie
      }
    }

    // Select as winner if has more wins than current best
    // In case of tie, lexicographically smaller participant_id wins (as tiebreaker)
    if (wins > best_wins ||
        (wins == best_wins && memcmp(participant_ids[i], participant_ids[best_idx], 16) < 0)) {
      best_wins = wins;
      best_idx = i;
    }
  }

  // Copy elected host ID
  memcpy(out_future_host_id, participant_ids[best_idx], 16);

  log_info("Future host elected (participant index %zu with %d wins)", best_idx, best_wins);
  return ASCIICHAT_OK;
}
