/**
 * @file discovery/nat.c
 * @brief NAT quality detection for discovery mode host selection
 * @ingroup discovery
 */

#include "nat.h"

#include "common.h"
#include "log/logging.h"
#include "network/nat/upnp.h"
#include "network/webrtc/stun.h"
#include "platform/abstraction.h"
#include "platform/socket.h"
#include "util/time.h"

#include <string.h>
#include <netdb.h>
#include <stdio.h>

// Bandwidth override threshold: 10x difference can override NAT priority
#define BANDWIDTH_OVERRIDE_RATIO 10

void nat_quality_init(nat_quality_t *quality) {
  if (!quality)
    return;
  memset(quality, 0, sizeof(nat_quality_t));
  quality->nat_type = ACIP_NAT_TYPE_SYMMETRIC; // Assume worst case
}

int nat_compute_tier(const nat_quality_t *quality) {
  if (!quality)
    return 4;

  if (quality->lan_reachable)
    return 0; // LAN - best
  if (quality->has_public_ip)
    return 1; // Public IP
  if (quality->upnp_available)
    return 2; // UPnP mapping
  if (quality->nat_type <= ACIP_NAT_TYPE_RESTRICTED)
    return 3; // STUN hole-punchable
  return 4;   // TURN relay required
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

/**
 * @brief Parse STUN binding response to extract reflexive address
 * @param response STUN response packet
 * @param response_len Response packet length
 * @param reflexive_addr Output buffer for reflexive address (dotted quad)
 * @param reflexive_port Output for reflexive port
 * @return ASCIICHAT_OK on success, error code if address not found
 */
static asciichat_error_t nat_parse_stun_response(const uint8_t *response, size_t response_len, char *reflexive_addr,
                                                 uint16_t *reflexive_port) {
  if (!response || response_len < 20 || !reflexive_addr || !reflexive_port) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid STUN response parameters");
  }

  // STUN packets have a 20-byte header followed by attributes
  // Attributes are TLV (Type-Length-Value) format
  // Look for XOR_MAPPED_ADDRESS (0x0020) or MAPPED_ADDRESS (0x0001)

  const uint8_t *current = response + 20;
  const uint8_t *end = response + response_len;

  while (current + 4 <= end) {
    uint16_t attr_type = (current[0] << 8) | current[1];
    uint16_t attr_len = (current[2] << 8) | current[3];

    if (current + 4 + attr_len > end) {
      break;
    }

    // Look for MAPPED_ADDRESS (0x0001)
    if (attr_type == 0x0001 && attr_len >= 8) {
      // Format: reserved byte, family (1=IPv4, 2=IPv6), port (2 bytes), address
      uint8_t family = current[5];
      if (family == 1) { // IPv4
        uint16_t port = ((uint16_t)current[6] << 8) | current[7];
        uint32_t addr =
            ((uint32_t)current[8] << 24) | ((uint32_t)current[9] << 16) | ((uint32_t)current[10] << 8) | current[11];

        // Convert to dotted quad notation
        snprintf(reflexive_addr, 64, "%u.%u.%u.%u", (addr >> 24) & 0xFF, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF,
                 addr & 0xFF);
        *reflexive_port = port;
        return ASCIICHAT_OK;
      }
    }

    // Move to next attribute (with 4-byte alignment padding)
    uint16_t padded_len = (attr_len + 3) & ~3;
    current += 4 + padded_len;
  }

  return SET_ERRNO(ERROR_FORMAT, "STUN response missing MAPPED_ADDRESS");
}

/**
 * @brief Perform a STUN probe to detect NAT characteristics
 * @param quality Output structure for results
 * @param stun_server STUN server URL (format: "stun.example.com:3478")
 * @param local_port Local port to use for probe
 * @return ASCIICHAT_OK on success, error code on failure
 */
static asciichat_error_t nat_stun_probe(nat_quality_t *quality, const char *stun_server, uint16_t local_port) {
  if (!quality || !stun_server || !stun_server[0]) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for STUN probe");
  }

  log_debug("Starting STUN probe to %s (local_port=%u)", stun_server, local_port);

  // Parse stun_server for hostname and port
  char host_buf[256] = {0};
  uint16_t stun_port = STUN_DEFAULT_PORT;

  SAFE_STRNCPY(host_buf, stun_server, sizeof(host_buf));

  char *colon = strchr(host_buf, ':');
  if (colon) {
    *colon = '\0';
    stun_port = (uint16_t)atoi(colon + 1);
  }

  // Create UDP socket for STUN probe
  socket_t sock = socket_create(AF_INET, SOCK_DGRAM, 0);
  if (sock == INVALID_SOCKET_VALUE) {
    log_warn("Failed to create UDP socket for STUN probe");
    return SET_ERRNO(ERROR_NETWORK, "Cannot create UDP socket");
  }

  // Set timeout on socket
  socket_set_timeout(sock, 5000); // 5 second timeout

  // Resolve STUN server hostname
  struct addrinfo hints = {0};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;

  struct addrinfo *result = NULL;
  char port_str[16];
  snprintf(port_str, sizeof(port_str), "%u", stun_port);

  int ret = getaddrinfo(host_buf, port_str, &hints, &result);
  if (ret != 0) {
    log_warn("Failed to resolve STUN server %s: %s", host_buf, gai_strerror(ret));
    socket_close(sock);
    return SET_ERRNO(ERROR_NETWORK_CONNECT, "Cannot resolve STUN server hostname");
  }

  // Build minimal STUN binding request (RFC 5389)
  // Header: Magic (0x2112A442) + Transaction ID (96 bits)
  uint8_t stun_request[20];
  stun_request[0] = 0x00; // Message type: Binding Request (0x0001), bits split
  stun_request[1] = 0x01;
  stun_request[2] = 0x00; // Message length: 0 (no attributes for simple probe)
  stun_request[3] = 0x00;

  // Magic cookie (STUN magic number)
  stun_request[4] = 0x21;
  stun_request[5] = 0x12;
  stun_request[6] = 0xa4;
  stun_request[7] = 0x42;

  // Transaction ID (96 bits) - use simple pattern for MVP
  memset(stun_request + 8, 0xAA, 12);

  // Send STUN binding request
  uint64_t start_time = time_get_ns();
  ssize_t sent = socket_sendto(sock, stun_request, sizeof(stun_request), 0, result->ai_addr, result->ai_addrlen);

  if (sent < 0) {
    log_warn("Failed to send STUN request");
    freeaddrinfo(result);
    socket_close(sock);
    return SET_ERRNO(ERROR_NETWORK, "Cannot send STUN request");
  }

  // Receive STUN response
  uint8_t stun_response[512];
  struct sockaddr_in peer_addr;
  socklen_t peer_addr_len = sizeof(peer_addr);

  ssize_t recv_len =
      socket_recvfrom(sock, stun_response, sizeof(stun_response), 0, (struct sockaddr *)&peer_addr, &peer_addr_len);

  uint64_t end_time = time_get_ns();
  uint32_t rtt_ms = (uint32_t)(time_ns_to_ms(end_time - start_time));

  freeaddrinfo(result);
  socket_close(sock);

  if (recv_len < 20) {
    log_warn("STUN response too short or timeout");
    return SET_ERRNO(ERROR_NETWORK_TIMEOUT, "STUN server did not respond");
  }

  // Parse STUN response to extract reflexive address
  char reflexive_addr[64] = {0};
  uint16_t reflexive_port = 0;
  asciichat_error_t parse_result =
      nat_parse_stun_response(stun_response, (size_t)recv_len, reflexive_addr, &reflexive_port);

  if (parse_result != ASCIICHAT_OK) {
    log_warn("Failed to parse STUN response");
    return parse_result;
  }

  SAFE_STRNCPY(quality->public_address, reflexive_addr, sizeof(quality->public_address));
  quality->public_port = reflexive_port;
  quality->stun_latency_ms = rtt_ms;

  log_info("STUN probe successful: reflexive_address=%s:%u, rtt=%u ms", reflexive_addr, reflexive_port, rtt_ms);

  return ASCIICHAT_OK;
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
    quality->nat_type = ACIP_NAT_TYPE_FULL_CONE; // UPnP implies at least full-cone equivalent
  } else {
    log_debug("UPnP: not available or mapping failed");
  }

  // Try STUN probe if provided and UPnP didn't succeed
  if (!quality->upnp_available && stun_server && stun_server[0]) {
    asciichat_error_t stun_result = nat_stun_probe(quality, stun_server, local_port);
    if (stun_result == ASCIICHAT_OK) {
      // Successfully got reflexive address via STUN
      quality->has_srflx_candidates = true;

      // If reflexive address is not private range, we have public IP
      // Private ranges: 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16
      if (quality->public_address[0] && strncmp(quality->public_address, "10.", 3) != 0 &&
          strncmp(quality->public_address, "172.16.", 7) != 0 && strncmp(quality->public_address, "192.168.", 8) != 0) {
        quality->has_public_ip = true;
        quality->nat_type = ACIP_NAT_TYPE_OPEN;
      } else {
        // Reflexive address is still private - behind symmetric NAT
        quality->nat_type = ACIP_NAT_TYPE_SYMMETRIC;
      }
    } else {
      log_debug("STUN probe failed, falling back to symmetric NAT assumption");
    }
  }

  // Set ICE candidate flags based on what we found
  quality->has_host_candidates = true; // We always have local IP
  quality->has_srflx_candidates = quality->upnp_available || quality->has_public_ip || quality->has_srflx_candidates;
  quality->has_relay_candidates = false; // Set true if TURN is available

  quality->detection_complete = true;
  log_info("NAT detection complete: tier=%d, upnp=%d, has_public_ip=%d, nat_type=%s", nat_compute_tier(quality),
           quality->upnp_available, quality->has_public_ip, nat_type_to_string(quality->nat_type));

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

void nat_quality_to_acip(const nat_quality_t *quality, const uint8_t session_id[16], const uint8_t participant_id[16],
                         acip_nat_quality_t *out) {
  if (!quality || !out)
    return;

  memset(out, 0, sizeof(acip_nat_quality_t));

  if (session_id)
    memcpy(out->session_id, session_id, 16);
  if (participant_id)
    memcpy(out->participant_id, participant_id, 16);

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
  if (quality->has_host_candidates)
    out->ice_candidate_types |= 1;
  if (quality->has_srflx_candidates)
    out->ice_candidate_types |= 2;
  if (quality->has_relay_candidates)
    out->ice_candidate_types |= 4;
}

void nat_quality_from_acip(const acip_nat_quality_t *acip, nat_quality_t *out) {
  if (!acip || !out)
    return;

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
  case ACIP_NAT_TYPE_OPEN:
    return "Open (Public IP)";
  case ACIP_NAT_TYPE_FULL_CONE:
    return "Full Cone";
  case ACIP_NAT_TYPE_RESTRICTED:
    return "Restricted Cone";
  case ACIP_NAT_TYPE_PORT_RESTRICTED:
    return "Port Restricted";
  case ACIP_NAT_TYPE_SYMMETRIC:
    return "Symmetric";
  default:
    return "Unknown";
  }
}
