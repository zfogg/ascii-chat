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

    // Determine connection type
    if (ctx->our_quality.has_public_ip) {
      ctx->connection_type = CONNECTION_TYPE_DIRECT_PUBLIC;
    } else if (ctx->our_quality.upnp_available) {
      ctx->connection_type = CONNECTION_TYPE_UPNP;
    } else {
      ctx->connection_type = CONNECTION_TYPE_STUN;
    }

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

    // Determine connection type based on peer's capabilities
    if (ctx->peer_quality.has_public_ip) {
      ctx->connection_type = CONNECTION_TYPE_DIRECT_PUBLIC;
    } else if (ctx->peer_quality.upnp_available) {
      ctx->connection_type = CONNECTION_TYPE_UPNP;
    } else {
      ctx->connection_type = CONNECTION_TYPE_STUN;
    }

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
