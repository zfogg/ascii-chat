/**
 * @file discovery/nat.c
 * @brief NAT quality detection for discovery mode host selection
 * @ingroup discovery
 */

#include <ascii-chat/common/limits.h>
#include "nat.h"

#include <ascii-chat/common.h>
#include <ascii-chat/common/buffer_sizes.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/network/webrtc/stun.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/socket.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/util/time.h>

#include <string.h>
#ifndef _WIN32
#include <netdb.h>
#endif
#include <stdio.h>
#include <errno.h>
#include <limits.h>

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
    if ((uint64_t)ours->upload_kbps >= (uint64_t)theirs->upload_kbps * BANDWIDTH_OVERRIDE_RATIO) {
      log_debug("NAT compare: we win by bandwidth override (%u vs %u kbps)", ours->upload_kbps, theirs->upload_kbps);
      return -1;
    }
    if ((uint64_t)theirs->upload_kbps >= (uint64_t)ours->upload_kbps * BANDWIDTH_OVERRIDE_RATIO) {
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
  if (ours->rtt_to_acds_ns < theirs->rtt_to_acds_ns) {
    log_debug("NAT compare: we win by latency (%lu ns vs %lu ns)", ours->rtt_to_acds_ns, theirs->rtt_to_acds_ns);
    return -1;
  }
  if (ours->rtt_to_acds_ns > theirs->rtt_to_acds_ns) {
    log_debug("NAT compare: they win by latency (%lu ns vs %lu ns)", theirs->rtt_to_acds_ns, ours->rtt_to_acds_ns);
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
        safe_snprintf(reflexive_addr, 64, "%u.%u.%u.%u", (addr >> 24) & 0xFF, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF,
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
  char host_buf[BUFFER_SIZE_SMALL] = {0};
  uint16_t stun_port = STUN_DEFAULT_PORT;

  SAFE_STRNCPY(host_buf, stun_server, sizeof(host_buf));

  char *colon = strchr(host_buf, ':');
  if (colon) {
    *colon = '\0';
    char *endptr;
    errno = 0;
    long port_val = strtol(colon + 1, &endptr, 10);
    if (*endptr == '\0' && errno == 0 && port_val > 0 && port_val <= 65535) {
      stun_port = (uint16_t)port_val;
    } else {
      log_error("Invalid STUN port: %s", colon + 1);
    }
  }

  // Detect address family from STUN server hostname (IPv4 vs IPv6)
  int stun_family = is_valid_ipv6(host_buf) ? AF_INET6 : AF_INET;

  // Create UDP socket for STUN probe
  const char *socket_name = stun_family == AF_INET6 ? "probe_ipv6" : "probe_ipv4";
  socket_t sock = socket_create(socket_name, stun_family, SOCK_DGRAM, 0);
  if (sock == INVALID_SOCKET_VALUE) {
    log_warn("Failed to create UDP socket for STUN probe");
    return SET_ERRNO(ERROR_NETWORK, "Cannot create UDP socket");
  }

  // Set timeout on socket
  socket_set_timeout(sock, 5000 * NS_PER_MS_INT); // 5 second timeout

  // Resolve STUN server hostname
  struct addrinfo hints = {0};
  hints.ai_family = stun_family;
  hints.ai_socktype = SOCK_DGRAM;

  struct addrinfo *result = NULL;
  char port_str[16];
  safe_snprintf(port_str, sizeof(port_str), "%u", stun_port);

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
  char reflexive_addr[IP_ADDRESS_BUFFER_SIZE] = {0};
  uint16_t reflexive_port = 0;
  asciichat_error_t parse_result =
      nat_parse_stun_response(stun_response, (size_t)recv_len, reflexive_addr, &reflexive_port);

  if (parse_result != ASCIICHAT_OK) {
    log_warn("Failed to parse STUN response");
    return parse_result;
  }

  SAFE_STRNCPY(quality->public_address, reflexive_addr, sizeof(quality->public_address));
  quality->public_port = reflexive_port;
  quality->stun_latency_ns = (uint64_t)rtt_ms * NS_PER_MS_INT;

  log_info("STUN probe successful: reflexive_address=%s:%u, rtt=%u ms", reflexive_addr, reflexive_port, rtt_ms);

  return ASCIICHAT_OK;
}

asciichat_error_t nat_detect_quality(nat_quality_t *quality, const char *stun_server, uint16_t local_port) {
  if (!quality) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "quality is NULL");
  }

  nat_quality_init(quality);
  log_info("Starting NAT quality detection (local_port=%u)", local_port);

  // Quality probes run before a listener exists (often with port zero).
  // Only the listening server lifecycle may create, renew, and remove mappings.

  // Probe STUN when configured.
  if (stun_server && stun_server[0]) {
    asciichat_error_t stun_result = nat_stun_probe(quality, stun_server, local_port);
    if (stun_result == ASCIICHAT_OK) {
      // Successfully got reflexive address via STUN
      quality->has_srflx_candidates = true;

      // A server-reflexive address demonstrates STUN reachability, not a public
      // interface or NAT classification. Those require separate observations.
      quality->has_public_ip = false;

    } else {
      log_debug("STUN probe failed, falling back to symmetric NAT assumption");
    }
  }

  // Set ICE candidate flags based on what we found
  quality->has_host_candidates = true; // We always have local IP
  quality->has_srflx_candidates = quality->upnp_available || quality->has_public_ip || quality->has_srflx_candidates;

  // Check if TURN relay servers are available (configured via options or ACDS)
  const options_t *opts = options_get();
  const char *turn_servers = opts ? opts->turn_servers : NULL;
  quality->has_relay_candidates = (turn_servers && turn_servers[0] != '\0');
  if (quality->has_relay_candidates) {
    log_debug("TURN relay servers configured: %s", turn_servers);
  }

  quality->detection_complete = true;
  log_info("NAT detection complete: tier=%d, upnp=%d, has_public_ip=%d, nat_type=%s", nat_compute_tier(quality),
           quality->upnp_available, quality->has_public_ip, nat_type_to_string(quality->nat_type));

  return ASCIICHAT_OK;
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
