/**
 * @file discovery/nat.c
 * @brief NAT quality detection for discovery mode host selection
 * @ingroup discovery
 */

#include "nat.h"

#include "common.h"
#include "log/logging.h"
#include "network/nat/upnp.h"
#include "platform/abstraction.h"

#include <string.h>

// Bandwidth override threshold: 10x difference can override NAT priority
#define BANDWIDTH_OVERRIDE_RATIO 10

void nat_quality_init(nat_quality_t *quality) {
  if (!quality) return;
  memset(quality, 0, sizeof(nat_quality_t));
  quality->nat_type = NAT_TYPE_SYMMETRIC; // Assume worst case
}

int nat_compute_tier(const nat_quality_t *quality) {
  if (!quality) return 4;

  if (quality->lan_reachable) return 0;           // LAN - best
  if (quality->has_public_ip) return 1;           // Public IP
  if (quality->upnp_available) return 2;          // UPnP mapping
  if (quality->nat_type <= NAT_TYPE_RESTRICTED) return 3; // STUN hole-punchable
  return 4;                                        // TURN relay required
}

int nat_compare_quality(const nat_quality_t *ours, const nat_quality_t *theirs, bool we_are_initiator) {
  if (!ours || !theirs) {
    return we_are_initiator ? -1 : 1;
  }

  int our_tier = nat_compute_tier(ours);
  int their_tier = nat_compute_tier(theirs);

  // Check for bandwidth override: massive bandwidth advantage can override NAT tier
  if (ours->upload_kbps > 0 && theirs->upload_kbps > 0) {
    if (ours->upload_kbps >= theirs->upload_kbps * BANDWIDTH_OVERRIDE_RATIO) {
      log_debug("NAT compare: we win by bandwidth override (%u vs %u kbps)", ours->upload_kbps, theirs->upload_kbps);
      return -1;
    }
    if (theirs->upload_kbps >= ours->upload_kbps * BANDWIDTH_OVERRIDE_RATIO) {
      log_debug("NAT compare: they win by bandwidth override (%u vs %u kbps)", theirs->upload_kbps, ours->upload_kbps);
      return 1;
    }
  }

  // Compare NAT tiers (lower = better)
  if (our_tier < their_tier) {
    log_debug("NAT compare: we win by tier (%d vs %d)", our_tier, their_tier);
    return -1;
  }
  if (our_tier > their_tier) {
    log_debug("NAT compare: they win by tier (%d vs %d)", our_tier, their_tier);
    return 1;
  }

  // Same tier - bandwidth is tiebreaker
  if (ours->upload_kbps > theirs->upload_kbps) {
    log_debug("NAT compare: we win by bandwidth (%u vs %u kbps)", ours->upload_kbps, theirs->upload_kbps);
    return -1;
  }
  if (ours->upload_kbps < theirs->upload_kbps) {
    log_debug("NAT compare: they win by bandwidth (%u vs %u kbps)", theirs->upload_kbps, ours->upload_kbps);
    return 1;
  }

  // Same bandwidth - latency is tiebreaker
  if (ours->rtt_to_acds_ms < theirs->rtt_to_acds_ms) {
    log_debug("NAT compare: we win by latency (%u vs %u ms)", ours->rtt_to_acds_ms, theirs->rtt_to_acds_ms);
    return -1;
  }
  if (ours->rtt_to_acds_ms > theirs->rtt_to_acds_ms) {
    log_debug("NAT compare: they win by latency (%u vs %u ms)", theirs->rtt_to_acds_ms, ours->rtt_to_acds_ms);
    return 1;
  }

  // Everything equal - initiator hosts
  log_debug("NAT compare: equal quality, initiator wins (we_are_initiator=%d)", we_are_initiator);
  return we_are_initiator ? -1 : 1;
}

asciichat_error_t nat_detect_quality(nat_quality_t *quality, const char *stun_server, uint16_t local_port) {
  if (!quality) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "quality is NULL");
  }

  nat_quality_init(quality);
  log_info("Starting NAT quality detection (local_port=%u)", local_port);

  // Try UPnP/NAT-PMP first
  nat_upnp_context_t *upnp = NULL;
  asciichat_error_t upnp_result = nat_upnp_open(local_port, "ascii-chat", &upnp);
  if (upnp_result == ASCIICHAT_OK && upnp && nat_upnp_is_active(upnp)) {
    quality->upnp_available = true;
    quality->upnp_mapped_port = upnp->mapped_port;

    char addr_buf[64];
    if (nat_upnp_get_address(upnp, addr_buf, sizeof(addr_buf)) == ASCIICHAT_OK) {
      // Parse IP:port
      char *colon = strchr(addr_buf, ':');
      if (colon) {
        size_t ip_len = (size_t)(colon - addr_buf);
        if (ip_len < sizeof(quality->public_address)) {
          memcpy(quality->public_address, addr_buf, ip_len);
          quality->public_address[ip_len] = '\0';
        }
      } else {
        SAFE_STRNCPY(quality->public_address, addr_buf, sizeof(quality->public_address));
      }
    }

    log_info("UPnP: mapped port %u, external IP %s", quality->upnp_mapped_port, quality->public_address);
    quality->nat_type = NAT_TYPE_FULL_CONE; // UPnP implies at least full-cone equivalent
  } else {
    log_debug("UPnP: not available or mapping failed");
  }

  // TODO: Implement STUN probe for more accurate NAT type detection
  // For now, use UPnP results or assume symmetric NAT
  if (stun_server && stun_server[0]) {
    log_debug("STUN probe to %s (not yet implemented)", stun_server);
    // Future: Send STUN binding request, analyze response
    // - If reflexive address == local address, we have public IP
    // - Measure RTT for stun_latency_ms
    // - Determine NAT type from response patterns
  }

  // Set ICE candidate flags based on what we found
  quality->has_host_candidates = true; // We always have local IP
  quality->has_srflx_candidates = quality->upnp_available || quality->has_public_ip;
  quality->has_relay_candidates = false; // Set true if TURN is available

  quality->detection_complete = true;
  log_info("NAT detection complete: tier=%d, upnp=%d, public_ip=%s", nat_compute_tier(quality), quality->upnp_available,
           quality->public_address[0] ? quality->public_address : "(none)");

  if (upnp) {
    // Keep UPnP mapping active for the session
    // Don't close it here - caller is responsible for cleanup
  }

  return ASCIICHAT_OK;
}

asciichat_error_t nat_measure_bandwidth(nat_quality_t *quality, socket_t acds_socket) {
  if (!quality) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "quality is NULL");
  }
  if (acds_socket == INVALID_SOCKET_VALUE) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "invalid socket");
  }

  // TODO: Implement bandwidth test
  // 1. Send BANDWIDTH_TEST packet with 64KB payload
  // 2. ACDS measures receive rate
  // 3. ACDS responds with measured bandwidth
  // For now, use reasonable defaults

  quality->upload_kbps = 10000;   // Assume 10 Mbps upload
  quality->download_kbps = 50000; // Assume 50 Mbps download
  quality->rtt_to_acds_ms = 50;   // Assume 50ms RTT
  quality->jitter_ms = 5;
  quality->packet_loss_pct = 0;

  log_debug("Bandwidth measurement: upload=%u kbps, download=%u kbps, rtt=%u ms", quality->upload_kbps,
            quality->download_kbps, quality->rtt_to_acds_ms);

  return ASCIICHAT_OK;
}

void nat_quality_to_acip(const nat_quality_t *quality, const uint8_t session_id[16],
                         const uint8_t participant_id[16], acip_nat_quality_t *out) {
  if (!quality || !out) return;

  memset(out, 0, sizeof(acip_nat_quality_t));

  if (session_id) memcpy(out->session_id, session_id, 16);
  if (participant_id) memcpy(out->participant_id, participant_id, 16);

  out->has_public_ip = quality->has_public_ip ? 1 : 0;
  out->upnp_available = quality->upnp_available ? 1 : 0;
  out->upnp_mapped_port[0] = (uint8_t)(quality->upnp_mapped_port >> 8);
  out->upnp_mapped_port[1] = (uint8_t)(quality->upnp_mapped_port & 0xFF);
  out->stun_nat_type = (uint8_t)quality->nat_type;
  out->lan_reachable = quality->lan_reachable ? 1 : 0;
  out->stun_latency_ms = quality->stun_latency_ms;

  out->upload_kbps = quality->upload_kbps;
  out->download_kbps = quality->download_kbps;
  out->rtt_to_acds_ms = quality->rtt_to_acds_ms;
  out->jitter_ms = quality->jitter_ms;
  out->packet_loss_pct = quality->packet_loss_pct;

  SAFE_STRNCPY(out->public_address, quality->public_address, sizeof(out->public_address));
  out->public_port = quality->public_port;

  out->ice_candidate_types = 0;
  if (quality->has_host_candidates) out->ice_candidate_types |= 1;
  if (quality->has_srflx_candidates) out->ice_candidate_types |= 2;
  if (quality->has_relay_candidates) out->ice_candidate_types |= 4;
}

void nat_quality_from_acip(const acip_nat_quality_t *acip, nat_quality_t *out) {
  if (!acip || !out) return;

  nat_quality_init(out);

  out->has_public_ip = acip->has_public_ip != 0;
  out->upnp_available = acip->upnp_available != 0;
  out->upnp_mapped_port = ((uint16_t)acip->upnp_mapped_port[0] << 8) | acip->upnp_mapped_port[1];
  out->nat_type = (acip_nat_type_t)acip->stun_nat_type;
  out->lan_reachable = acip->lan_reachable != 0;
  out->stun_latency_ms = acip->stun_latency_ms;

  out->upload_kbps = acip->upload_kbps;
  out->download_kbps = acip->download_kbps;
  out->rtt_to_acds_ms = acip->rtt_to_acds_ms;
  out->jitter_ms = acip->jitter_ms;
  out->packet_loss_pct = acip->packet_loss_pct;

  SAFE_STRNCPY(out->public_address, acip->public_address, sizeof(out->public_address));
  out->public_port = acip->public_port;

  out->has_host_candidates = (acip->ice_candidate_types & 1) != 0;
  out->has_srflx_candidates = (acip->ice_candidate_types & 2) != 0;
  out->has_relay_candidates = (acip->ice_candidate_types & 4) != 0;

  out->detection_complete = true;
}

const char *nat_type_to_string(acip_nat_type_t type) {
  switch (type) {
  case NAT_TYPE_OPEN:
    return "Open (Public IP)";
  case NAT_TYPE_FULL_CONE:
    return "Full Cone";
  case NAT_TYPE_RESTRICTED:
    return "Restricted Cone";
  case NAT_TYPE_PORT_RESTRICTED:
    return "Port Restricted";
  case NAT_TYPE_SYMMETRIC:
    return "Symmetric";
  default:
    return "Unknown";
  }
}
