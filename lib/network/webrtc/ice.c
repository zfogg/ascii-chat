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
#include <ascii-chat/log/logging.h>
#include <ascii-chat/platform/util.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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

/* ============================================================================
 * ICE Candidate Parsing
 * ============================================================================ */

asciichat_error_t ice_parse_candidate(const char *line, ice_candidate_t *candidate) {
  if (!line || !candidate) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid candidate parse parameters");
  }

  memset(candidate, 0, sizeof(ice_candidate_t));

  // Parse: foundation component protocol priority ip port typ type [raddr rport] [extensions]
  // Example: 1 1 udp 2130706431 192.168.1.1 54321 typ host

  char line_copy[512];
  SAFE_STRNCPY(line_copy, line, sizeof(line_copy));

  char *saveptr = NULL;
  char *token = NULL;

  // 1. Parse foundation
  token = platform_strtok_r(line_copy, " ", &saveptr);
  if (!token) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing foundation in candidate");
  }
  SAFE_STRNCPY(candidate->foundation, token, sizeof(candidate->foundation));

  // 2. Parse component ID
  token = platform_strtok_r(NULL, " ", &saveptr);
  if (!token || sscanf(token, "%u", &candidate->component_id) != 1) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid component ID");
  }

  // 3. Parse protocol (udp/tcp)
  token = platform_strtok_r(NULL, " ", &saveptr);
  if (!token) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing protocol");
  }
  if (strcmp(token, "udp") == 0) {
    candidate->protocol = ICE_PROTOCOL_UDP;
  } else if (strcmp(token, "tcp") == 0) {
    candidate->protocol = ICE_PROTOCOL_TCP;
  } else {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid protocol: %s", token);
  }

  // 4. Parse priority
  token = platform_strtok_r(NULL, " ", &saveptr);
  if (!token || sscanf(token, "%u", &candidate->priority) != 1) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid priority");
  }

  // 5. Parse IP address
  token = platform_strtok_r(NULL, " ", &saveptr);
  if (!token) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing IP address");
  }
  SAFE_STRNCPY(candidate->ip_address, token, sizeof(candidate->ip_address));

  // 6. Parse port
  token = platform_strtok_r(NULL, " ", &saveptr);
  if (!token || sscanf(token, "%hu", &candidate->port) != 1) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid port");
  }

  // 7. Expect "typ" keyword
  token = platform_strtok_r(NULL, " ", &saveptr);
  if (!token || strcmp(token, "typ") != 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing 'typ' keyword");
  }

  // 8. Parse candidate type
  token = platform_strtok_r(NULL, " ", &saveptr);
  if (!token) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing candidate type");
  }

  if (strcmp(token, "host") == 0) {
    candidate->type = ICE_CANDIDATE_HOST;
  } else if (strcmp(token, "srflx") == 0) {
    candidate->type = ICE_CANDIDATE_SRFLX;
  } else if (strcmp(token, "prflx") == 0) {
    candidate->type = ICE_CANDIDATE_PRFLX;
  } else if (strcmp(token, "relay") == 0) {
    candidate->type = ICE_CANDIDATE_RELAY;
  } else {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid candidate type: %s", token);
  }

  // 9. If srflx/prflx/relay: parse raddr and rport
  if (candidate->type == ICE_CANDIDATE_SRFLX || candidate->type == ICE_CANDIDATE_PRFLX ||
      candidate->type == ICE_CANDIDATE_RELAY) {
    token = platform_strtok_r(NULL, " ", &saveptr);
    if (token && strcmp(token, "raddr") == 0) {
      token = platform_strtok_r(NULL, " ", &saveptr);
      if (!token) {
        return SET_ERRNO(ERROR_INVALID_PARAM, "Missing raddr value");
      }
      SAFE_STRNCPY(candidate->raddr, token, sizeof(candidate->raddr));

      token = platform_strtok_r(NULL, " ", &saveptr);
      if (!token || strcmp(token, "rport") != 0) {
        return SET_ERRNO(ERROR_INVALID_PARAM, "Missing 'rport' keyword");
      }

      token = platform_strtok_r(NULL, " ", &saveptr);
      if (!token || sscanf(token, "%hu", &candidate->rport) != 1) {
        return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid rport value");
      }
    }
  }

  // 10. Parse any extensions (tcptype for TCP candidates)
  if (candidate->protocol == ICE_PROTOCOL_TCP) {
    token = platform_strtok_r(NULL, " ", &saveptr);
    while (token) {
      if (strcmp(token, "tcptype") == 0) {
        token = platform_strtok_r(NULL, " ", &saveptr);
        if (token) {
          if (strcmp(token, "active") == 0) {
            candidate->tcp_type = ICE_TCP_TYPE_ACTIVE;
          } else if (strcmp(token, "passive") == 0) {
            candidate->tcp_type = ICE_TCP_TYPE_PASSIVE;
          } else if (strcmp(token, "so") == 0) {
            candidate->tcp_type = ICE_TCP_TYPE_SO;
          }
        }
      }
      token = platform_strtok_r(NULL, " ", &saveptr);
    }
  }

  return ASCIICHAT_OK;
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
