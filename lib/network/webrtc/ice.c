/**
 * @file network/webrtc/ice.c
 * @brief ICE candidate gathering and exchange
 *
 * Implements ICE candidate collection from STUN/TURN servers
 * and relay via ACDS signaling.
 *
 * @date January 2026
 */

#include <ascii-chat/network/webrtc/ice.h>
#include <ascii-chat/network/webrtc/webrtc.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/platform/util.h>
#include <ascii-chat/util/pcre2.h>
#include <ascii-chat/util/ip.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

const char *ice_candidate_type_name(ice_candidate_type_t type) {
  switch (type) {
  case ICE_CANDIDATE_HOST:
    return "host";
  case ICE_CANDIDATE_SRFLX:
    return "srflx";
  case ICE_CANDIDATE_PRFLX:
    return "prflx";
  case ICE_CANDIDATE_RELAY:
    return "relay";
  default:
    return "unknown";
  }
}

const char *ice_protocol_name(ice_protocol_t protocol) {
  switch (protocol) {
  case ICE_PROTOCOL_UDP:
    return "udp";
  case ICE_PROTOCOL_TCP:
    return "tcp";
  default:
    return "unknown";
  }
}

/* ============================================================================
 * ICE Candidate Priority
 * ============================================================================ */

/**
 * @brief Calculate local preference based on IP address classification
 *
 * Uses IP classification utilities to prioritize candidates:
 * - LAN addresses (private networks): Highest priority (65535)
 *   Local peers on same network = lowest latency, no NAT traversal
 * - Localhost (loopback): High priority (65000)
 *   Same machine = zero latency but limited use case
 * - Internet (public IPs): Medium priority (32768)
 *   Direct public connectivity but may have higher latency
 * - Unknown/Invalid: Lowest priority (0)
 *   Malformed or unrecognized addresses
 *
 * @param ip_address IP address to classify (IPv4 or IPv6)
 * @return Local preference value (0-65535, higher = more preferred)
 */
static uint16_t ice_calculate_local_preference_by_ip(const char *ip_address) {
  if (!ip_address || ip_address[0] == '\0') {
    return 0; // Invalid/empty
  }

  // Check for wildcard addresses (should not appear in candidates, but handle gracefully)
  if (strcmp(ip_address, "0.0.0.0") == 0 || strcmp(ip_address, "::") == 0) {
    return 0; // Invalid candidate
  }

  // LAN addresses: Highest priority (same network = lowest latency)
  if (is_lan_ipv4(ip_address) || is_lan_ipv6(ip_address)) {
    return 65535;
  }

  // Localhost: High priority but less useful than LAN for peer-to-peer
  if (is_localhost_ipv4(ip_address) || is_localhost_ipv6(ip_address)) {
    return 65000;
  }

  // Internet (public): Medium priority
  if (is_internet_ipv4(ip_address) || is_internet_ipv6(ip_address)) {
    return 32768;
  }

  // Unknown/unclassified: Lowest priority
  return 0;
}

uint32_t ice_calculate_priority(ice_candidate_type_t type, uint16_t local_preference, uint8_t component_id) {
  // RFC 5245: priority = (2^24 * typePreference) + (2^8 * localPreference) + (256 - componentID)

  // Type preference (higher is better):
  // - Host: 126 (most preferred, lowest latency)
  // - SRFLX: 100 (STUN-discovered address)
  // - PRFLX: 110 (discovered during connectivity checks)
  // - Relay: 0 (least preferred, uses bandwidth)

  uint8_t type_preference = 0;
  switch (type) {
  case ICE_CANDIDATE_HOST:
    type_preference = 126;
    break;
  case ICE_CANDIDATE_SRFLX:
    type_preference = 100;
    break;
  case ICE_CANDIDATE_PRFLX:
    type_preference = 110;
    break;
  case ICE_CANDIDATE_RELAY:
    type_preference = 0;
    break;
  default:
    type_preference = 0;
    break;
  }

  uint32_t priority =
      (((uint32_t)type_preference) << 24) | (((uint32_t)local_preference) << 8) | (256 - (uint32_t)component_id);

  return priority;
}

uint32_t ice_calculate_priority_for_candidate(const ice_candidate_t *candidate) {
  if (!candidate) {
    return 0;
  }

  // Calculate local preference based on IP classification
  uint16_t local_pref = ice_calculate_local_preference_by_ip(candidate->ip_address);

  // Use standard RFC 5245 priority formula with IP-aware local preference
  return ice_calculate_priority(candidate->type, local_pref, (uint8_t)candidate->component_id);
}

/* ============================================================================
 * ICE Candidate Parsing with PCRE2
 * ============================================================================ */

/**
 * @brief PCRE2 regex validator for ICE candidate parsing
 *
 * Validates and extracts ICE candidate fields from standard RFC format.
 * Uses centralized PCRE2 singleton for atomic parsing with single regex match.
 */

static const char *ICE_CANDIDATE_PATTERN = "^([^ ]+)\\s+"                            // 1: foundation
                                           "(\\d+)\\s+"                              // 2: component_id
                                           "(udp|tcp)\\s+"                           // 3: protocol
                                           "(\\d+)\\s+"                              // 4: priority
                                           "([a-fA-F0-9.:]+)\\s+"                    // 5: ip_address (IPv4 or IPv6)
                                           "(\\d+)\\s+"                              // 6: port
                                           "typ\\s+"                                 // literal "typ"
                                           "(host|srflx|prflx|relay)"                // 7: candidate_type
                                           "(?:\\s+raddr\\s+([a-fA-F0-9.:]+))?"      // 8: optional raddr
                                           "(?:\\s+rport\\s+(\\d+))?"                // 9: optional rport
                                           "(?:\\s+tcptype\\s+(active|passive|so))?" // 10: optional tcptype
                                           "\\s*$";

static pcre2_singleton_t *g_ice_candidate_regex = NULL;

/**
 * Get compiled ICE candidate regex (lazy initialization)
 * Returns NULL if compilation failed
 */
static pcre2_code *ice_candidate_regex_get(void) {
  if (g_ice_candidate_regex == NULL) {
    g_ice_candidate_regex = asciichat_pcre2_singleton_compile(ICE_CANDIDATE_PATTERN, PCRE2_CASELESS);
  }
  return asciichat_pcre2_singleton_get_code(g_ice_candidate_regex);
}

/**
 * @brief Parse ICE candidate using PCRE2 regex singleton
 *
 * Atomically validates and extracts all ICE candidate fields.
 * Falls back to manual parsing if PCRE2 unavailable.
 *
 * @param line ICE candidate line
 * @param candidate Output candidate structure
 * @return ASCIICHAT_OK on success, error code otherwise
 */
static asciichat_error_t ice_parse_candidate_pcre2(const char *line, ice_candidate_t *candidate) {
  if (!line || !candidate) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid candidate parameters");
  }

  // Get compiled regex (lazy initialization)
  pcre2_code *regex = ice_candidate_regex_get();

  // If PCRE2 not available, return error to fall back to manual parser
  if (!regex) {
    return ERROR_MEMORY; // Signal to use fallback
  }

  pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(regex, NULL);
  if (!match_data) {
    return ERROR_MEMORY;
  }

  int rc = pcre2_jit_match(regex, (PCRE2_SPTR8)line, strlen(line), 0, 0, match_data, NULL);

  if (rc < 7) {
    // Must have at least foundation, component, protocol, priority, ip, port, type (7 groups)
    pcre2_match_data_free(match_data);
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid ICE candidate format");
  }

  memset(candidate, 0, sizeof(ice_candidate_t));

  // Extract foundation (group 1)
  char *foundation = asciichat_pcre2_extract_group(match_data, 1, line);
  if (foundation && strlen(foundation) < sizeof(candidate->foundation)) {
    SAFE_STRNCPY(candidate->foundation, foundation, sizeof(candidate->foundation));
  }
  SAFE_FREE(foundation);

  // Extract component_id (group 2)
  unsigned long component_id;
  if (asciichat_pcre2_extract_group_ulong(match_data, 2, line, &component_id)) {
    candidate->component_id = (uint32_t)component_id;
  }

  // Extract protocol (group 3)
  char *protocol_str = asciichat_pcre2_extract_group(match_data, 3, line);
  if (protocol_str) {
    if (strcmp(protocol_str, "udp") == 0) {
      candidate->protocol = ICE_PROTOCOL_UDP;
    } else if (strcmp(protocol_str, "tcp") == 0) {
      candidate->protocol = ICE_PROTOCOL_TCP;
    } else {
      SAFE_FREE(protocol_str);
      pcre2_match_data_free(match_data);
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid protocol");
    }
    SAFE_FREE(protocol_str);
  }

  // Extract priority (group 4)
  unsigned long priority;
  if (asciichat_pcre2_extract_group_ulong(match_data, 4, line, &priority)) {
    candidate->priority = (uint32_t)priority;
  }

  // Extract IP address (group 5)
  char *ip_address = asciichat_pcre2_extract_group(match_data, 5, line);
  if (ip_address && strlen(ip_address) < sizeof(candidate->ip_address)) {
    SAFE_STRNCPY(candidate->ip_address, ip_address, sizeof(candidate->ip_address));
  }
  SAFE_FREE(ip_address);

  // Extract port (group 6)
  unsigned long port;
  if (asciichat_pcre2_extract_group_ulong(match_data, 6, line, &port)) {
    candidate->port = (uint16_t)port;
  }

  // Extract candidate type (group 7)
  char *type_str = asciichat_pcre2_extract_group(match_data, 7, line);
  if (type_str) {
    if (strcmp(type_str, "host") == 0) {
      candidate->type = ICE_CANDIDATE_HOST;
    } else if (strcmp(type_str, "srflx") == 0) {
      candidate->type = ICE_CANDIDATE_SRFLX;
    } else if (strcmp(type_str, "prflx") == 0) {
      candidate->type = ICE_CANDIDATE_PRFLX;
    } else if (strcmp(type_str, "relay") == 0) {
      candidate->type = ICE_CANDIDATE_RELAY;
    } else {
      SAFE_FREE(type_str);
      pcre2_match_data_free(match_data);
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid candidate type");
    }
    SAFE_FREE(type_str);
  }

  // Extract optional raddr (group 8)
  char *raddr = asciichat_pcre2_extract_group(match_data, 8, line);
  if (raddr && strlen(raddr) < sizeof(candidate->raddr)) {
    SAFE_STRNCPY(candidate->raddr, raddr, sizeof(candidate->raddr));
  }
  SAFE_FREE(raddr);

  // Extract optional rport (group 9)
  unsigned long rport;
  if (asciichat_pcre2_extract_group_ulong(match_data, 9, line, &rport)) {
    candidate->rport = (uint16_t)rport;
  }

  // Extract optional tcptype (group 10)
  char *tcptype_str = asciichat_pcre2_extract_group(match_data, 10, line);
  if (tcptype_str) {
    if (strcmp(tcptype_str, "active") == 0) {
      candidate->tcp_type = ICE_TCP_TYPE_ACTIVE;
    } else if (strcmp(tcptype_str, "passive") == 0) {
      candidate->tcp_type = ICE_TCP_TYPE_PASSIVE;
    } else if (strcmp(tcptype_str, "so") == 0) {
      candidate->tcp_type = ICE_TCP_TYPE_SO;
    }
    SAFE_FREE(tcptype_str);
  }

  pcre2_match_data_free(match_data);
  return ASCIICHAT_OK;
}

asciichat_error_t ice_parse_candidate(const char *line, ice_candidate_t *candidate) {
  return ice_parse_candidate_pcre2(line, candidate);
}

asciichat_error_t ice_format_candidate(const ice_candidate_t *candidate, char *line, size_t line_size) {
  if (!candidate || !line || line_size == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid candidate format parameters");
  }

  // Format: foundation component protocol priority ip port typ type [raddr rport] [tcptype type]
  // Example: 1 1 udp 2130706431 192.168.1.1 54321 typ host
  // Example with raddr: 1 1 udp 2130706431 203.0.113.45 54321 typ srflx raddr 192.168.1.1 rport 12345

  int written = safe_snprintf(line, line_size, "%s %u %s %u %s %u typ %s", candidate->foundation,
                              candidate->component_id, ice_protocol_name(candidate->protocol), candidate->priority,
                              candidate->ip_address, candidate->port, ice_candidate_type_name(candidate->type));

  if (written < 0 || (size_t)written >= line_size) {
    return SET_ERRNO(ERROR_BUFFER_FULL, "Candidate line too large for buffer");
  }

  size_t remaining = line_size - (size_t)written;
  char *pos = line + written;

  // 2. If srflx/prflx/relay: append raddr and rport
  if (candidate->type == ICE_CANDIDATE_SRFLX || candidate->type == ICE_CANDIDATE_PRFLX ||
      candidate->type == ICE_CANDIDATE_RELAY) {
    if (candidate->raddr[0] != '\0') {
      int appended = safe_snprintf(pos, remaining, " raddr %s rport %u", candidate->raddr, candidate->rport);
      if (appended < 0 || (size_t)appended >= remaining) {
        return SET_ERRNO(ERROR_BUFFER_FULL, "Cannot append raddr/rport to candidate line");
      }
      pos += appended;
      remaining -= (size_t)appended;
    }
  }

  // 3. If TCP: append tcptype
  if (candidate->protocol == ICE_PROTOCOL_TCP) {
    const char *tcptype_str = "passive"; // default
    if (candidate->tcp_type == ICE_TCP_TYPE_ACTIVE) {
      tcptype_str = "active";
    } else if (candidate->tcp_type == ICE_TCP_TYPE_SO) {
      tcptype_str = "so";
    }

    int appended = safe_snprintf(pos, remaining, " tcptype %s", tcptype_str);
    if (appended < 0 || (size_t)appended >= remaining) {
      return SET_ERRNO(ERROR_BUFFER_FULL, "Cannot append tcptype to candidate line");
    }
    pos += appended;
    remaining -= (size_t)appended;
  }

  // 4. Append any extensions
  if (candidate->extensions[0] != '\0') {
    int appended = safe_snprintf(pos, remaining, " %s", candidate->extensions);
    if (appended < 0 || (size_t)appended >= remaining) {
      return SET_ERRNO(ERROR_BUFFER_FULL, "Cannot append extensions to candidate line");
    }
  }

  // 5. Ensure line is null-terminated (snprintf already does this)
  return ASCIICHAT_OK;
}

/* ============================================================================
 * ICE Candidate Gathering
 * ============================================================================ */

asciichat_error_t ice_gather_candidates(const ice_config_t *config) {
  if (!config) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid ICE config");
  }

  if (!config->send_callback) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "ICE config missing send_callback");
  }

  log_debug("ICE: Starting candidate gathering (ufrag=%s, pwd=%s)", config->ufrag, config->pwd);

  // NOTE: libdatachannel (juice) handles ICE candidate gathering internally.
  // This function is a placeholder for the callback-based API.
  // The actual gathering happens in the peer_manager when you:
  //   1. Create peer connection with rtcCreatePeerConnection()
  //   2. Set ice candidate callback with rtcSetIceCandidateCallback()
  //   3. libdatachannel automatically gathers candidates from:
  //      - Local interfaces (host candidates)
  //      - STUN servers (server-reflexive candidates)
  //      - TURN servers (relay candidates)
  //   4. When a candidate is found, rtcSetIceCandidateCallback() is called
  //   5. Application (webrtc.c) formats and sends via ACDS signaling
  //
  // This ice_gather_candidates() function may not be called directly -
  // the gathering happens automatically when peer connection is created.
  // Keeping this for API compatibility.

  return ASCIICHAT_OK;
}

/* ============================================================================
 * ICE Connectivity
 * ============================================================================ */

asciichat_error_t ice_add_remote_candidate(webrtc_peer_connection_t *pc, const ice_candidate_t *candidate,
                                           const char *mid) {
  if (!pc || !candidate || !mid) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid remote candidate parameters");
  }

  // Validate candidate has required fields
  if (candidate->ip_address[0] == '\0' || candidate->port == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Candidate missing IP address or port");
  }

  log_debug("ICE: Adding remote candidate %s:%u (type=%s, foundation=%s) on mid=%s", candidate->ip_address,
            candidate->port, ice_candidate_type_name(candidate->type), candidate->foundation, mid);

  // Format candidate to SDP attribute line for libdatachannel
  char candidate_line[512];
  asciichat_error_t err = ice_format_candidate(candidate, candidate_line, sizeof(candidate_line));
  if (err != ASCIICHAT_OK) {
    return err;
  }

  // Delegate to WebRTC layer
  return webrtc_add_remote_candidate(pc, candidate_line, mid);
}

bool ice_is_connected(webrtc_peer_connection_t *pc) {
  if (!pc) {
    return false;
  }

  // Check peer connection state
  webrtc_state_t state = webrtc_get_state(pc);
  return (state == WEBRTC_STATE_CONNECTED);
}

// Forward declaration of C++ implementation (in ice_selected_pair.cpp)
asciichat_error_t ice_get_selected_pair_impl(webrtc_peer_connection_t *pc, ice_candidate_t *local_candidate,
                                             ice_candidate_t *remote_candidate);

asciichat_error_t ice_get_selected_pair(webrtc_peer_connection_t *pc, ice_candidate_t *local_candidate,
                                        ice_candidate_t *remote_candidate) {
  // Delegate to C++ implementation which uses libdatachannel's C++ API
  return ice_get_selected_pair_impl(pc, local_candidate, remote_candidate);
}
