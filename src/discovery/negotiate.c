/**
 * @file discovery/negotiate.c
 * @brief Host negotiation logic for discovery mode
 * @ingroup discovery
 */

#include <ascii-chat/crypto/sizes.h>
#include "negotiate.h"

#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/network/acip/acds.h>

#include <string.h>

void negotiate_init(negotiate_ctx_t *ctx, const uint8_t session_id[SESSION_ID_SIZE],
                    const uint8_t participant_id[SESSION_ID_SIZE], bool is_initiator) {
  if (!ctx)
    return;

  memset(ctx, 0, sizeof(negotiate_ctx_t));

  if (session_id)
    memcpy(ctx->session_id, session_id, SESSION_ID_SIZE);
  if (participant_id)
    memcpy(ctx->participant_id, participant_id, SESSION_ID_SIZE);

  ctx->is_initiator = is_initiator;
  ctx->state = NEGOTIATE_STATE_INIT;
  ctx->peer_quality_received = false;
  ctx->we_are_host = false;

  nat_quality_init(&ctx->our_quality);
  nat_quality_init(&ctx->peer_quality);

  log_debug("Negotiation initialized (initiator=%d)", is_initiator);
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
