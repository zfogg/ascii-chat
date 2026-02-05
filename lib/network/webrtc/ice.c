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
#include <pthread.h>
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
 * ICE Candidate Parsing with PCRE2
 * ============================================================================ */

/**
 * @brief PCRE2 regex validator for ICE candidate parsing
 *
 * Validates and extracts ICE candidate fields from standard RFC format.
 * Uses PCRE2 for atomic parsing with single regex match.
 */
typedef struct {
  pcre2_code *candidate_regex; ///< Pattern: Match ICE candidate line
  pcre2_jit_stack *jit_stack;  ///< JIT compilation stack
  bool initialized;            ///< Initialization flag
} ice_candidate_validator_t;

static ice_candidate_validator_t g_ice_candidate_validator = {0};
static pthread_once_t g_ice_candidate_once = PTHREAD_ONCE_INIT;

/**
 * @brief Initialize PCRE2 regex for ICE candidate parsing
 *
 * Pattern matches full ICE candidate line atomically:
 * foundation component protocol priority ip port typ type [raddr X rport Y] [tcptype Z]
 */
static void ice_candidate_regex_init(void) {
  int errornumber;
  PCRE2_SIZE erroroffset;

  // Pattern: Match ICE candidate with all optional extensions
  const char *pattern = "^([^ ]+)\\s+"                            // 1: foundation
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

  g_ice_candidate_validator.candidate_regex =
      pcre2_compile((PCRE2_SPTR8)pattern, PCRE2_ZERO_TERMINATED, PCRE2_CASELESS, &errornumber, &erroroffset, NULL);

  if (!g_ice_candidate_validator.candidate_regex) {
    PCRE2_UCHAR8 error_msg[120];
    pcre2_get_error_message(errornumber, error_msg, sizeof(error_msg));
    log_warn("ice_candidate_regex_init: Failed to compile pattern: %s at offset %zu", error_msg, erroroffset);
    return;
  }

  // Attempt JIT compilation (non-fatal if fails)
  if (pcre2_jit_compile(g_ice_candidate_validator.candidate_regex, PCRE2_JIT_COMPLETE) > 0) {
    g_ice_candidate_validator.jit_stack = pcre2_jit_stack_create(32 * 1024, 512 * 1024, NULL);
  }

  g_ice_candidate_validator.initialized = true;
}

/**
 * @brief Parse ICE candidate using PCRE2 regex
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

  // Initialize regex singleton
  pthread_once(&g_ice_candidate_once, ice_candidate_regex_init);

  // If PCRE2 not available, return error to fall back to manual parser
  if (!g_ice_candidate_validator.initialized || !g_ice_candidate_validator.candidate_regex) {
    return ERROR_MEMORY; // Signal to use fallback
  }

  pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(g_ice_candidate_validator.candidate_regex, NULL);
  if (!match_data) {
    return ERROR_MEMORY;
  }

  int rc =
      pcre2_match(g_ice_candidate_validator.candidate_regex, (PCRE2_SPTR8)line, strlen(line), 0, 0, match_data, NULL);

  if (rc < 7) {
    // Must have at least foundation, component, protocol, priority, ip, port, type (7 groups)
    pcre2_match_data_free(match_data);
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid ICE candidate format");
  }

  memset(candidate, 0, sizeof(ice_candidate_t));
  PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);

  // Extract foundation (group 1)
  size_t start = ovector[2], end = ovector[3];
  if (start < end && end - start < sizeof(candidate->foundation)) {
    memcpy(candidate->foundation, line + start, end - start);
    candidate->foundation[end - start] = '\0';
  }

  // Extract component_id (group 2)
  start = ovector[4], end = ovector[5];
  if (start < end) {
    char component_str[16];
    memcpy(component_str, line + start, end - start);
    component_str[end - start] = '\0';
    candidate->component_id = (uint32_t)strtoul(component_str, NULL, 10);
  }

  // Extract protocol (group 3)
  start = ovector[6], end = ovector[7];
  if (start < end) {
    char protocol_str[8];
    memcpy(protocol_str, line + start, end - start);
    protocol_str[end - start] = '\0';
    if (strcmp(protocol_str, "udp") == 0) {
      candidate->protocol = ICE_PROTOCOL_UDP;
    } else if (strcmp(protocol_str, "tcp") == 0) {
      candidate->protocol = ICE_PROTOCOL_TCP;
    } else {
      pcre2_match_data_free(match_data);
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid protocol");
    }
  }

  // Extract priority (group 4)
  start = ovector[8], end = ovector[9];
  if (start < end) {
    char priority_str[16];
    memcpy(priority_str, line + start, end - start);
    priority_str[end - start] = '\0';
    candidate->priority = (uint32_t)strtoul(priority_str, NULL, 10);
  }

  // Extract IP address (group 5)
  start = ovector[10], end = ovector[11];
  if (start < end && end - start < sizeof(candidate->ip_address)) {
    memcpy(candidate->ip_address, line + start, end - start);
    candidate->ip_address[end - start] = '\0';
  }

  // Extract port (group 6)
  start = ovector[12], end = ovector[13];
  if (start < end) {
    char port_str[8];
    memcpy(port_str, line + start, end - start);
    port_str[end - start] = '\0';
    candidate->port = (uint16_t)strtoul(port_str, NULL, 10);
  }

  // Extract candidate type (group 7)
  start = ovector[14], end = ovector[15];
  if (start < end) {
    char type_str[16];
    memcpy(type_str, line + start, end - start);
    type_str[end - start] = '\0';
    if (strcmp(type_str, "host") == 0) {
      candidate->type = ICE_CANDIDATE_HOST;
    } else if (strcmp(type_str, "srflx") == 0) {
      candidate->type = ICE_CANDIDATE_SRFLX;
    } else if (strcmp(type_str, "prflx") == 0) {
      candidate->type = ICE_CANDIDATE_PRFLX;
    } else if (strcmp(type_str, "relay") == 0) {
      candidate->type = ICE_CANDIDATE_RELAY;
    } else {
      pcre2_match_data_free(match_data);
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid candidate type");
    }
  }

  // Extract optional raddr (group 8)
  start = ovector[16], end = ovector[17];
  if (start < end && start != (size_t)-1 && end - start < sizeof(candidate->raddr)) {
    memcpy(candidate->raddr, line + start, end - start);
    candidate->raddr[end - start] = '\0';
  }

  // Extract optional rport (group 9)
  start = ovector[18], end = ovector[19];
  if (start < end && start != (size_t)-1) {
    char rport_str[8];
    memcpy(rport_str, line + start, end - start);
    rport_str[end - start] = '\0';
    candidate->rport = (uint16_t)strtoul(rport_str, NULL, 10);
  }

  // Extract optional tcptype (group 10)
  start = ovector[20], end = ovector[21];
  if (start < end && start != (size_t)-1) {
    char tcptype_str[16];
    memcpy(tcptype_str, line + start, end - start);
    tcptype_str[end - start] = '\0';
    if (strcmp(tcptype_str, "active") == 0) {
      candidate->tcp_type = ICE_TCP_TYPE_ACTIVE;
    } else if (strcmp(tcptype_str, "passive") == 0) {
      candidate->tcp_type = ICE_TCP_TYPE_PASSIVE;
    } else if (strcmp(tcptype_str, "so") == 0) {
      candidate->tcp_type = ICE_TCP_TYPE_SO;
    }
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
