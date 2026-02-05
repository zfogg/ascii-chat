/**
 * @file discovery/session.c
 * @brief Discovery session flow management
 * @ingroup discovery
 */

#include "session.h"

#include <ascii-chat/common.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/network/acip/acds.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/packet.h>
#include <ascii-chat/network/webrtc/stun.h>
#include <ascii-chat/network/webrtc/peer_manager.h>
#include "negotiate.h"
#include "nat.h"
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/socket.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/crypto/keys.h>

#ifdef _WIN32
#include <ws2tcpip.h> // For getaddrinfo on Windows
#include <time.h>
#else
#include <netdb.h> // For getaddrinfo on POSIX
#include <sys/time.h>
#include <time.h>
#endif

#include <string.h>

/**
 * @brief Get current time in milliseconds (portable)
 */
static uint64_t session_get_current_time_ms(void) {
  uint64_t current_time_ns = time_get_ns();
  return time_ns_to_ms(current_time_ns);
}

discovery_session_t *discovery_session_create(const discovery_config_t *config) {
  if (!config) {
    SET_ERRNO(ERROR_INVALID_PARAM, "config is NULL");
    return NULL;
  }

  discovery_session_t *session = SAFE_CALLOC(1, sizeof(discovery_session_t), discovery_session_t *);
  if (!session) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate discovery session");
    return NULL;
  }

  session->state = DISCOVERY_STATE_INIT;
  session->acds_socket = INVALID_SOCKET_VALUE;
  session->peer_manager = NULL;
  session->webrtc_transport_ready = false;
  session->stun_servers = NULL;
  session->stun_count = 0;
  session->turn_servers = NULL;
  session->turn_count = 0;

  log_info("discovery_session_create: After CALLOC - participant_ctx=%p, host_ctx=%p", session->participant_ctx,
           session->host_ctx);

  // ACDS connection info
  if (config->acds_address && config->acds_address[0]) {
    SAFE_STRNCPY(session->acds_address, config->acds_address, sizeof(session->acds_address));
  } else {
    SAFE_STRNCPY(session->acds_address, "127.0.0.1", sizeof(session->acds_address));
  }
  session->acds_port = config->acds_port > 0 ? config->acds_port : ACIP_DISCOVERY_DEFAULT_PORT;

  // Session string (if joining)
  if (config->session_string && config->session_string[0]) {
    SAFE_STRNCPY(session->session_string, config->session_string, sizeof(session->session_string));
    session->is_initiator = false;
  } else {
    session->is_initiator = true;
  }

  // Callbacks
  session->on_state_change = config->on_state_change;
  session->on_session_ready = config->on_session_ready;
  session->on_error = config->on_error;
  session->callback_user_data = config->callback_user_data;
  session->should_exit_callback = config->should_exit_callback;
  session->exit_callback_data = config->exit_callback_data;

  log_debug("Discovery session created (initiator=%d, acds=%s:%u)", session->is_initiator, session->acds_address,
            session->acds_port);

  return session;
}

void discovery_session_destroy(discovery_session_t *session) {
  if (!session)
    return;

  // Close ACDS connection
  if (session->acds_socket != INVALID_SOCKET_VALUE) {
    socket_close(session->acds_socket);
    session->acds_socket = INVALID_SOCKET_VALUE;
  }

  // Destroy host context if active
  if (session->host_ctx) {
    session_host_destroy(session->host_ctx);
    session->host_ctx = NULL;
  }

  // Destroy participant context if active
  if (session->participant_ctx) {
    session_participant_destroy(session->participant_ctx);
    session->participant_ctx = NULL;
  }

  // Clean up WebRTC peer manager (NEW)
  if (session->peer_manager) {
    webrtc_peer_manager_destroy(session->peer_manager);
    session->peer_manager = NULL;
  }

  // Clean up STUN/TURN server arrays (NEW)
  if (session->stun_servers) {
    SAFE_FREE(session->stun_servers);
    session->stun_servers = NULL;
    session->stun_count = 0;
  }

  if (session->turn_servers) {
    SAFE_FREE(session->turn_servers);
    session->turn_servers = NULL;
    session->turn_count = 0;
  }

  SAFE_FREE(session);
}

static void set_state(discovery_session_t *session, discovery_state_t new_state) {
  if (!session)
    return;

  discovery_state_t old_state = session->state;
  session->state = new_state;

  log_debug("Discovery state: %d -> %d", old_state, new_state);

  if (session->on_state_change) {
    session->on_state_change(new_state, session->callback_user_data);
  }
}

static void set_error(discovery_session_t *session, asciichat_error_t error, const char *message) {
  if (!session)
    return;

  session->error = error;
  set_state(session, DISCOVERY_STATE_FAILED);

  log_error("Discovery error: %s", message ? message : "Unknown error");

  if (session->on_error) {
    session->on_error(error, message, session->callback_user_data);
  }
}

/**
 * @brief Gather NAT quality information from local system
 * @param quality Output quality structure
 * @return ASCIICHAT_OK on success
 *
 * Collects NAT detection results using STUN and other methods.
 * This is non-blocking and may not have all data immediately.
 */
static asciichat_error_t gather_nat_quality(nat_quality_t *quality) {
  if (!quality)
    return ERROR_INVALID_PARAM;

  // Initialize with defaults
  nat_quality_init(quality);

  // Parse STUN servers from options (use fallback endpoint for NAT probe)
  // Extract just the hostname:port without the "stun:" prefix for nat_detect_quality
  // If custom servers are configured, use the first one; otherwise use fallback
  const char *stun_servers_option = GET_OPTION(stun_servers);
  const char *stun_server_for_probe = OPT_ENDPOINT_STUN_FALLBACK; // Default fallback

  if (stun_servers_option && stun_servers_option[0] != '\0') {
    // Use the first configured server
    // nat_detect_quality expects just "host:port" without "stun:" prefix
    // For now, we'll use the fallback and rely on ACDS to provide custom servers
    stun_server_for_probe = OPT_ENDPOINT_STUN_FALLBACK;
  }

  // Strip "stun:" prefix if present
  const char *probe_host = stun_server_for_probe;
  if (strncmp(probe_host, "stun:", 5) == 0) {
    probe_host = stun_server_for_probe + 5;
  }

  // Run NAT detection (timeout 2 seconds)
  // This will probe STUN, check UPnP, etc.
  asciichat_error_t result = nat_detect_quality(quality, probe_host, 0);
  if (result != ASCIICHAT_OK) {
    log_warn("NAT detection had issues, using partial data");
    // Don't fail - we have partial data that's better than nothing
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Send NETWORK_QUALITY packet to ACDS
 * @param session Discovery session
 * @return ASCIICHAT_OK on success
 *
 * Sends our NAT quality metrics to ACDS so peers can receive them.
 */
static asciichat_error_t send_network_quality_to_acds(discovery_session_t *session) {
  if (!session || session->acds_socket == INVALID_SOCKET_VALUE) {
    return ERROR_INVALID_PARAM;
  }

  // Gather our NAT quality
  nat_quality_t our_quality;
  asciichat_error_t result = gather_nat_quality(&our_quality);
  if (result != ASCIICHAT_OK) {
    log_warn("Failed to gather NAT quality: %d", result);
    return result;
  }

  // Convert to wire format
  acip_nat_quality_t wire_quality;
  nat_quality_to_acip(&our_quality, session->session_id, session->participant_id, &wire_quality);

  // Send via ACDS
  asciichat_error_t send_result =
      packet_send(session->acds_socket, PACKET_TYPE_ACIP_NETWORK_QUALITY, &wire_quality, sizeof(wire_quality));

  if (send_result == ASCIICHAT_OK) {
    log_debug("Sent NETWORK_QUALITY to ACDS (NAT tier: %d, upload: %u Kbps)", nat_compute_tier(&our_quality),
              our_quality.upload_kbps);
  } else {
    log_error("Failed to send NETWORK_QUALITY: %d", send_result);
  }

  return send_result;
}

/**
 * @brief Receive NETWORK_QUALITY packet from ACDS relay
 * @param session Discovery session
 * @param timeout_ms Timeout in milliseconds
 * @return ASCIICHAT_OK if received, ERROR_NETWORK_TIMEOUT if no data
 *
 * Attempts to receive peer's NETWORK_QUALITY from ACDS.
 * Stores result in session->negotiate.peer_quality
 */
static asciichat_error_t receive_network_quality_from_acds(discovery_session_t *session) {
  if (!session || session->acds_socket == INVALID_SOCKET_VALUE) {
    return ERROR_INVALID_PARAM;
  }

  // Receive packet
  packet_type_t ptype;
  void *data = NULL;
  size_t len = 0;
  asciichat_error_t result = packet_receive(session->acds_socket, &ptype, &data, &len);

  if (result != ASCIICHAT_OK) {
    if (data)
      SAFE_FREE(data);
    return result;
  }

  // Check if it's a NETWORK_QUALITY packet
  if (ptype != PACKET_TYPE_ACIP_NETWORK_QUALITY) {
    log_debug("Received packet type %u (expected NETWORK_QUALITY)", ptype);
    if (data)
      SAFE_FREE(data);
    return ERROR_NETWORK_PROTOCOL;
  }

  // Parse NETWORK_QUALITY
  if (len < sizeof(acip_nat_quality_t)) {
    log_error("NETWORK_QUALITY packet too small: %zu bytes", len);
    if (data)
      SAFE_FREE(data);
    return ERROR_NETWORK_SIZE;
  }

  acip_nat_quality_t *wire_quality = (acip_nat_quality_t *)data;

  // Convert from wire format
  nat_quality_from_acip(wire_quality, &session->negotiate.peer_quality);
  session->negotiate.peer_quality_received = true;

  log_debug("Received NETWORK_QUALITY from peer (NAT tier: %d, upload: %u Kbps)",
            nat_compute_tier(&session->negotiate.peer_quality), session->negotiate.peer_quality.upload_kbps);

  if (data)
    SAFE_FREE(data);
  return ASCIICHAT_OK;
}

static asciichat_error_t connect_to_acds(discovery_session_t *session) {
  if (!session)
    return ERROR_INVALID_PARAM;

  set_state(session, DISCOVERY_STATE_CONNECTING_ACDS);
  log_info("Connecting to ACDS at %s:%u...", session->acds_address, session->acds_port);

  // Resolve hostname using getaddrinfo (supports both IP addresses and hostnames)
  struct addrinfo hints, *result = NULL;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET; // IPv4 for now
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  char port_str[8];
  safe_snprintf(port_str, sizeof(port_str), "%u", session->acds_port);

  int gai_err = getaddrinfo(session->acds_address, port_str, &hints, &result);
  if (gai_err != 0) {
    set_error(session, ERROR_NETWORK_CONNECT, "Failed to resolve ACDS address");
    log_error("getaddrinfo failed: %s", gai_strerror(gai_err));
    return ERROR_NETWORK_CONNECT;
  }

  // Create socket
  session->acds_socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
  if (session->acds_socket == INVALID_SOCKET_VALUE) {
    set_error(session, ERROR_NETWORK, "Failed to create socket");
    freeaddrinfo(result);
    return ERROR_NETWORK;
  }

  // Connect to ACDS
  if (connect(session->acds_socket, result->ai_addr, (socklen_t)result->ai_addrlen) < 0) {
    set_error(session, ERROR_NETWORK_CONNECT, "Failed to connect to ACDS");
    socket_close(session->acds_socket);
    session->acds_socket = INVALID_SOCKET_VALUE;
    freeaddrinfo(result);
    return ERROR_NETWORK_CONNECT;
  }

  freeaddrinfo(result);
  log_info("Connected to ACDS");
  return ASCIICHAT_OK;
}

static asciichat_error_t create_session(discovery_session_t *session) {
  if (!session)
    return ERROR_INVALID_PARAM;

  set_state(session, DISCOVERY_STATE_CREATING_SESSION);
  log_info("Creating new discovery session...");

  // Build SESSION_CREATE message
  acip_session_create_t create_msg;
  memset(&create_msg, 0, sizeof(create_msg));

  // TODO: Set identity key from options
  // For now, use zero key (anonymous session)
  create_msg.capabilities = 0x03; // Video + Audio
  create_msg.max_participants = 8;

  // Check if WebRTC is preferred
  const options_t *opts = options_get();
  if (opts && opts->prefer_webrtc) {
    log_info("DISCOVERY: WebRTC preferred, using SESSION_TYPE_WEBRTC");
    create_msg.session_type = SESSION_TYPE_WEBRTC;
  } else {
    log_info("DISCOVERY: Using direct TCP (SESSION_TYPE_DIRECT_TCP)");
    create_msg.session_type = SESSION_TYPE_DIRECT_TCP;
  }

  create_msg.has_password = 0;
  create_msg.expose_ip_publicly = 0;
  create_msg.reserved_string_len = 0;

  // Set server address (will be updated after NAT detection)
  SAFE_STRNCPY(create_msg.server_address, "127.0.0.1", sizeof(create_msg.server_address));
  create_msg.server_port = ACIP_HOST_DEFAULT_PORT;

  // Send SESSION_CREATE
  asciichat_error_t result =
      packet_send(session->acds_socket, PACKET_TYPE_ACIP_SESSION_CREATE, &create_msg, sizeof(create_msg));
  if (result != ASCIICHAT_OK) {
    set_error(session, result, "Failed to send SESSION_CREATE");
    return result;
  }

  // Receive SESSION_CREATED response
  packet_type_t type;
  void *data = NULL;
  size_t len = 0;

  result = packet_receive(session->acds_socket, &type, &data, &len);
  if (result != ASCIICHAT_OK) {
    set_error(session, result, "Failed to receive SESSION_CREATED");
    return result;
  }

  if (type == PACKET_TYPE_ACIP_ERROR) {
    acip_error_t *error = (acip_error_t *)data;
    log_error("ACDS error: %s", error->error_message);
    set_error(session, ERROR_NETWORK_PROTOCOL, error->error_message);
    POOL_FREE(data, len);
    return ERROR_NETWORK_PROTOCOL;
  }

  if (type != PACKET_TYPE_ACIP_SESSION_CREATED || len < sizeof(acip_session_created_t)) {
    set_error(session, ERROR_NETWORK_PROTOCOL, "Unexpected response to SESSION_CREATE");
    POOL_FREE(data, len);
    return ERROR_NETWORK_PROTOCOL;
  }

  acip_session_created_t *created = (acip_session_created_t *)data;

  // Store session info
  memcpy(session->session_id, created->session_id, 16);
  SAFE_STRNCPY(session->session_string, created->session_string, sizeof(session->session_string));

  // We're also participant 0 (initiator)
  memcpy(session->participant_id, created->session_id, 16); // Use session ID as participant ID for initiator
  memcpy(session->initiator_id, session->participant_id, 16);

  log_info("Session created: %s", session->session_string);

  POOL_FREE(data, len);

  // Notify that session is ready
  if (session->on_session_ready) {
    session->on_session_ready(session->session_string, session->callback_user_data);
  }

  // Wait for peer to join
  set_state(session, DISCOVERY_STATE_WAITING_PEER);

  return ASCIICHAT_OK;
}

// ============================================================================
// WebRTC Support Functions (NEW)
// ============================================================================

/**
 * @brief Send SDP via ACDS signaling
 */
static asciichat_error_t discovery_send_sdp_via_acds(const uint8_t session_id[16], const uint8_t recipient_id[16],
                                                     const char *sdp_type, const char *sdp, void *user_data) {
  discovery_session_t *session = (discovery_session_t *)user_data;

  if (!session || session->acds_socket == INVALID_SOCKET_VALUE) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid session or ACDS socket");
  }

  // Build ACIP SDP packet
  size_t sdp_len = strlen(sdp);
  size_t total_len = sizeof(acip_webrtc_sdp_t) + sdp_len;

  uint8_t *packet_data = SAFE_MALLOC(total_len, uint8_t *);
  acip_webrtc_sdp_t *sdp_msg = (acip_webrtc_sdp_t *)packet_data;

  memcpy(sdp_msg->session_id, session_id, 16);
  memcpy(sdp_msg->sender_id, session->participant_id, 16);
  memcpy(sdp_msg->recipient_id, recipient_id, 16);
  sdp_msg->sdp_type = (strcmp(sdp_type, "offer") == 0) ? 0 : 1;
  sdp_msg->sdp_len = HOST_TO_NET_U16(sdp_len);
  memcpy(packet_data + sizeof(acip_webrtc_sdp_t), sdp, sdp_len);

  // Send to ACDS
  asciichat_error_t result = packet_send(session->acds_socket, PACKET_TYPE_ACIP_WEBRTC_SDP, packet_data, total_len);

  SAFE_FREE(packet_data);

  if (result != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to send SDP to ACDS");
  }

  log_info("Sent SDP %s to ACDS (len=%zu)", sdp_type, sdp_len);
  return ASCIICHAT_OK;
}

/**
 * @brief Send ICE candidate via ACDS signaling
 */
static asciichat_error_t discovery_send_ice_via_acds(const uint8_t session_id[16], const uint8_t recipient_id[16],
                                                     const char *candidate, const char *mid __attribute__((unused)),
                                                     void *user_data) {
  discovery_session_t *session = (discovery_session_t *)user_data;

  if (!session || session->acds_socket == INVALID_SOCKET_VALUE) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid session or ACDS socket");
  }

  // Build ACIP ICE packet
  size_t candidate_len = strlen(candidate);
  size_t total_len = sizeof(acip_webrtc_ice_t) + candidate_len;

  uint8_t *packet_data = SAFE_MALLOC(total_len, uint8_t *);
  acip_webrtc_ice_t *ice_msg = (acip_webrtc_ice_t *)packet_data;

  memcpy(ice_msg->session_id, session_id, 16);
  memcpy(ice_msg->sender_id, session->participant_id, 16);
  memcpy(ice_msg->recipient_id, recipient_id, 16);
  ice_msg->candidate_len = HOST_TO_NET_U16(candidate_len);
  memcpy(packet_data + sizeof(acip_webrtc_ice_t), candidate, candidate_len);

  // Send to ACDS
  asciichat_error_t result = packet_send(session->acds_socket, PACKET_TYPE_ACIP_WEBRTC_ICE, packet_data, total_len);

  SAFE_FREE(packet_data);

  if (result != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to send ICE to ACDS");
  }

  log_debug("Sent ICE candidate to ACDS (len=%zu)", candidate_len);
  return ASCIICHAT_OK;
}

/**
 * @brief Callback when WebRTC DataChannel is ready
 *
 * This callback is invoked by the WebRTC peer manager when a DataChannel
 * successfully opens and is wrapped in an ACIP transport. At this point,
 * frames can be transmitted over the WebRTC DataChannel instead of TCP.
 *
 * IMPLEMENTATION NOTES:
 * - The transport is ready for use immediately
 * - Current implementation keeps TCP connection open as fallback
 * - Future: Could implement graceful TCP->WebRTC switchover
 * - Security: DataChannel uses DTLS encryption (automatic from libdatachannel)
 * - Encryption: ACIP layer may add additional encryption on top if needed
 */
static void discovery_on_transport_ready(acip_transport_t *transport, const uint8_t participant_id[16],
                                         void *user_data) {
  discovery_session_t *session = (discovery_session_t *)user_data;

  if (!session) {
    log_error("discovery_on_transport_ready: session is NULL");
    return;
  }

  log_info("========== WebRTC DataChannel READY ==========");
  log_info("WebRTC DataChannel successfully established and wrapped in ACIP transport");
  log_debug("  Remote participant: %02x%02x%02x%02x-...", participant_id[0], participant_id[1], participant_id[2],
            participant_id[3]);
  log_info("  Transport status: READY for media transmission");
  log_info("  Encryption: DTLS (automatic) + optional ACIP layer encryption");

  // Mark transport as ready
  session->webrtc_transport_ready = true;

  if (session->session_type != SESSION_TYPE_WEBRTC) {
    log_warn("WebRTC transport ready but session type is not WebRTC!");
    return;
  }

  log_info("========== WebRTC Media Channel ACTIVE ==========");
  log_info("Switching to WebRTC transport for media transmission...");

  // Implement transport switching for participant/host
  if (session->is_host && session->host_ctx) {
    // We are the host - set transport for the remote client
    // participant_id contains the client's ID
    uint32_t client_id = 0;
    // Extract client ID from participant_id (use first 4 bytes as uint32)
    memcpy(&client_id, participant_id, sizeof(uint32_t));

    asciichat_error_t result = session_host_set_client_transport(session->host_ctx, client_id, transport);
    if (result == ASCIICHAT_OK) {
      log_info("✓ Host: Successfully switched client %u to WebRTC transport", client_id);
    } else {
      log_error("✗ Host: Failed to set WebRTC transport for client %u: %d", client_id, result);
    }
  } else if (session->participant_ctx) {
    // We are a participant - set transport for ourselves
    asciichat_error_t result = session_participant_set_transport(session->participant_ctx, transport);
    if (result == ASCIICHAT_OK) {
      log_info("✓ Participant: Successfully switched to WebRTC transport");
      log_info("  TCP connection remains open as control/signaling channel");
      log_info("  Media frames will now be transmitted over WebRTC DataChannel");
    } else {
      log_error("✗ Participant: Failed to set WebRTC transport: %d", result);
    }
  } else {
    log_warn("transport_ready: No participant or host context available for transport switching");
  }
}

/**
 * @brief Initialize WebRTC peer manager for a discovery session
 *
 * Creates STUN/TURN server arrays and stores them in the session.
 * These arrays must persist for the lifetime of the peer manager.
 */
static asciichat_error_t initialize_webrtc_peer_manager(discovery_session_t *session) {
  if (!session) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session is NULL");
  }

  // Initialize WebRTC library (idempotent)
  asciichat_error_t init_result = webrtc_init();
  if (init_result != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to initialize WebRTC library");
  }

  // Set up STUN servers
  // Allocate array for 1 STUN server (can be expanded for redundancy)
  session->stun_servers = SAFE_MALLOC(sizeof(stun_server_t), stun_server_t *);
  if (!session->stun_servers) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate STUN server array");
  }
  session->stun_count = 1;

  // Configure primary STUN server
  // STUN URL format: "stun:hostname:port"
  const char *stun_url = "stun:stun.l.google.com:19302";
  session->stun_servers[0].host_len = strlen(stun_url);
  SAFE_STRNCPY(session->stun_servers[0].host, stun_url, sizeof(session->stun_servers[0].host));

  // Set up TURN servers (if credentials available from ACDS)
  if (session->turn_username[0] != '\0' && session->turn_password[0] != '\0') {
    // Allocate array for 1 TURN server
    session->turn_servers = SAFE_MALLOC(sizeof(turn_server_t), turn_server_t *);
    if (!session->turn_servers) {
      SAFE_FREE(session->stun_servers);
      session->stun_servers = NULL;
      session->stun_count = 0;
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate TURN server array");
    }
    session->turn_count = 1;

    // Configure TURN server with ACDS-provided credentials
    const char *turn_url = "turn:turn.ascii-chat.com:3478";
    session->turn_servers[0].url_len = strlen(turn_url);
    SAFE_STRNCPY(session->turn_servers[0].url, turn_url, sizeof(session->turn_servers[0].url));

    session->turn_servers[0].username_len = strlen(session->turn_username);
    SAFE_STRNCPY(session->turn_servers[0].username, session->turn_username, sizeof(session->turn_servers[0].username));

    session->turn_servers[0].credential_len = strlen(session->turn_password);
    SAFE_STRNCPY(session->turn_servers[0].credential, session->turn_password,
                 sizeof(session->turn_servers[0].credential));

    log_info("Using ACDS-provided TURN credentials for symmetric NAT relay (username_len=%d, credential_len=%d)",
             session->turn_servers[0].username_len, session->turn_servers[0].credential_len);
  } else {
    log_info("No TURN credentials from ACDS - will rely on direct P2P + STUN (sufficient for most NAT scenarios)");
    session->turn_servers = NULL;
    session->turn_count = 0;
  }

  // Determine our role for WebRTC peer connection
  // Participants always use JOINER role (they initiate connections)
  webrtc_peer_role_t role = WEBRTC_ROLE_JOINER;

  log_info("Initializing WebRTC peer manager (role=%s, stun_count=%zu, turn_count=%zu)",
           role == WEBRTC_ROLE_JOINER ? "JOINER" : "CREATOR", session->stun_count, session->turn_count);

  // Create peer manager configuration
  webrtc_peer_manager_config_t pm_config = {
      .role = role,
      .stun_servers = session->stun_servers,
      .stun_count = session->stun_count,
      .turn_servers = session->turn_servers,
      .turn_count = session->turn_count,
      .on_transport_ready = discovery_on_transport_ready,
      .user_data = session,
      .crypto_ctx = NULL // Crypto happens at ACIP layer
  };

  // Create signaling callbacks
  webrtc_signaling_callbacks_t signaling = {
      .send_sdp = discovery_send_sdp_via_acds, .send_ice = discovery_send_ice_via_acds, .user_data = session};

  // Create peer manager
  asciichat_error_t result = webrtc_peer_manager_create(&pm_config, &signaling, &session->peer_manager);
  if (result != ASCIICHAT_OK) {
    // Clean up on error
    SAFE_FREE(session->stun_servers);
    session->stun_servers = NULL;
    session->stun_count = 0;
    if (session->turn_servers) {
      SAFE_FREE(session->turn_servers);
      session->turn_servers = NULL;
    }
    session->turn_count = 0;
  }

  return result;
}

/**
 * @brief Handle incoming WebRTC SDP from ACDS
 */
static void handle_discovery_webrtc_sdp(discovery_session_t *session, void *data, size_t len) {
  if (len < sizeof(acip_webrtc_sdp_t)) {
    log_error("Invalid SDP packet size: %zu", len);
    return;
  }

  const acip_webrtc_sdp_t *sdp_msg = (const acip_webrtc_sdp_t *)data;
  uint16_t sdp_len = NET_TO_HOST_U16(sdp_msg->sdp_len);

  if (len != sizeof(acip_webrtc_sdp_t) + sdp_len) {
    log_error("SDP packet size mismatch: expected %zu, got %zu", sizeof(acip_webrtc_sdp_t) + sdp_len, len);
    return;
  }

  // Extract SDP string
  char *sdp_str = SAFE_MALLOC(sdp_len + 1, char *);
  memcpy(sdp_str, (const uint8_t *)data + sizeof(acip_webrtc_sdp_t), sdp_len);
  sdp_str[sdp_len] = '\0';

  const char *sdp_type = (sdp_msg->sdp_type == 0) ? "offer" : "answer";
  log_info("Received SDP %s from ACDS (len=%u)", sdp_type, sdp_len);

  // Forward to peer manager
  if (session->peer_manager) {
    asciichat_error_t result = webrtc_peer_manager_handle_sdp(session->peer_manager, sdp_msg);
    if (result != ASCIICHAT_OK) {
      log_error("Failed to handle SDP: %d", result);
    }
  } else {
    log_error("Received SDP but peer manager not initialized");
  }

  SAFE_FREE(sdp_str);
}

/**
 * @brief Handle incoming WebRTC ICE candidate from ACDS
 */
static void handle_discovery_webrtc_ice(discovery_session_t *session, void *data, size_t len) {
  if (len < sizeof(acip_webrtc_ice_t)) {
    log_error("Invalid ICE packet size: %zu", len);
    return;
  }

  const acip_webrtc_ice_t *ice_msg = (const acip_webrtc_ice_t *)data;
  uint16_t candidate_len = NET_TO_HOST_U16(ice_msg->candidate_len);

  log_debug("★ ICE VALIDATION: len=%zu, sizeof(header)=%zu, candidate_len=%u, expected=%zu", len,
            sizeof(acip_webrtc_ice_t), candidate_len, sizeof(acip_webrtc_ice_t) + candidate_len);

  // FIXED: The payload contains candidate + null + mid + null, not just candidate
  // So we can't validate the exact size without parsing the strings first
  if (len < sizeof(acip_webrtc_ice_t) + candidate_len) {
    log_error("ICE packet too small: expected at least %zu, got %zu", sizeof(acip_webrtc_ice_t) + candidate_len, len);
    return;
  }

  log_debug("Received ICE candidate from ACDS (len=%u)", candidate_len);

  // Forward to peer manager
  if (session->peer_manager) {
    asciichat_error_t result = webrtc_peer_manager_handle_ice(session->peer_manager, ice_msg);
    if (result != ASCIICHAT_OK) {
      log_error("Failed to handle ICE candidate: %d", result);
    }
  } else {
    log_error("Received ICE but peer manager not initialized");
  }
}

/**
 * @brief Dispatch incoming ACDS packets for WebRTC signaling
 */
static void handle_acds_webrtc_packet(discovery_session_t *session, packet_type_t type, void *data, size_t len) {
  switch (type) {
  case PACKET_TYPE_ACIP_WEBRTC_SDP:
    handle_discovery_webrtc_sdp(session, data, len);
    break;
  case PACKET_TYPE_ACIP_WEBRTC_ICE:
    handle_discovery_webrtc_ice(session, data, len);
    break;
  default:
    log_debug("Unhandled WebRTC packet type: 0x%04x", type);
  }
}

/**
 * @brief Join an existing discovery session
 */
static asciichat_error_t join_session(discovery_session_t *session) {
  log_info("join_session: START - participant_ctx=%p", session->participant_ctx);

  if (!session)
    return ERROR_INVALID_PARAM;

  set_state(session, DISCOVERY_STATE_JOINING_SESSION);
  log_info("Joining session: %s (participant_ctx still=%p)", session->session_string, session->participant_ctx);

  // Build SESSION_JOIN message
  acip_session_join_t join_msg;
  memset(&join_msg, 0, sizeof(join_msg));

  join_msg.session_string_len = (uint8_t)strlen(session->session_string);
  SAFE_STRNCPY(join_msg.session_string, session->session_string, sizeof(join_msg.session_string));

  // TODO: Set identity key from options
  join_msg.has_password = 0;

  // Send SESSION_JOIN
  asciichat_error_t result =
      packet_send(session->acds_socket, PACKET_TYPE_ACIP_SESSION_JOIN, &join_msg, sizeof(join_msg));
  if (result != ASCIICHAT_OK) {
    set_error(session, result, "Failed to send SESSION_JOIN");
    return result;
  }

  // Receive SESSION_JOINED response
  packet_type_t type;
  void *data = NULL;
  size_t len = 0;

  result = packet_receive(session->acds_socket, &type, &data, &len);
  if (result != ASCIICHAT_OK) {
    set_error(session, result, "Failed to receive SESSION_JOINED");
    return result;
  }

  if (type == PACKET_TYPE_ACIP_ERROR) {
    acip_error_t *error = (acip_error_t *)data;
    log_error("ACDS error: %s", error->error_message);
    set_error(session, ERROR_NETWORK_PROTOCOL, error->error_message);
    POOL_FREE(data, len);
    return ERROR_NETWORK_PROTOCOL;
  }

  if (type != PACKET_TYPE_ACIP_SESSION_JOINED || len < sizeof(acip_session_joined_t)) {
    set_error(session, ERROR_NETWORK_PROTOCOL, "Unexpected response to SESSION_JOIN");
    POOL_FREE(data, len);
    return ERROR_NETWORK_PROTOCOL;
  }

  acip_session_joined_t *joined = (acip_session_joined_t *)data;

  if (!joined->success) {
    log_error("Failed to join session: %s", joined->error_message);
    set_error(session, ERROR_NETWORK_PROTOCOL, joined->error_message);
    POOL_FREE(data, len);
    return ERROR_NETWORK_PROTOCOL;
  }

  // Store session info
  memcpy(session->session_id, joined->session_id, 16);
  memcpy(session->participant_id, joined->participant_id, 16);

  // Store session type and WebRTC credentials (if WebRTC)
  session->session_type = joined->session_type;
  if (joined->session_type == 1) { // SESSION_TYPE_WEBRTC
    SAFE_STRNCPY(session->turn_username, joined->turn_username, sizeof(session->turn_username));
    SAFE_STRNCPY(session->turn_password, joined->turn_password, sizeof(session->turn_password));
    log_info("WebRTC session detected - TURN username: %s", session->turn_username);

    // Initialize WebRTC peer manager for WebRTC sessions (NEW)
    asciichat_error_t webrtc_result = initialize_webrtc_peer_manager(session);
    if (webrtc_result != ASCIICHAT_OK) {
      log_error("Failed to initialize WebRTC peer manager: %d", webrtc_result);
      POOL_FREE(data, len);
      return webrtc_result;
    }
    log_info("WebRTC peer manager initialized successfully");
  }

  // Check if host is already established
  if (joined->server_address[0] && joined->server_port > 0) {
    // Host exists - connect directly
    SAFE_STRNCPY(session->host_address, joined->server_address, sizeof(session->host_address));
    session->host_port = joined->server_port;
    session->is_host = false;

    log_info("Host already established: %s:%u (session_type=%u, participant_ctx=%p before transition)",
             session->host_address, session->host_port, session->session_type, session->participant_ctx);
    POOL_FREE(data, len);

    log_info("join_session: About to transition to CONNECTING_HOST - participant_ctx=%p", session->participant_ctx);
    set_state(session, DISCOVERY_STATE_CONNECTING_HOST);
    log_info("join_session: Transitioned to CONNECTING_HOST - participant_ctx=%p (returning ASCIICHAT_OK)",
             session->participant_ctx);
    return ASCIICHAT_OK;
  }

  // No host yet - need to negotiate
  log_info("No host established (server_address='%s' port=%u), starting negotiation...", joined->server_address,
           joined->server_port);
  POOL_FREE(data, len);

  set_state(session, DISCOVERY_STATE_NEGOTIATING);
  return ASCIICHAT_OK;
}

asciichat_error_t discovery_session_start(discovery_session_t *session) {
  if (!session) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session is NULL");
  }

  // Check if we should exit before starting blocking operations
  if (session->should_exit_callback && session->should_exit_callback(session->exit_callback_data)) {
    return ASCIICHAT_OK; // Return success to allow clean shutdown
  }

  // Connect to ACDS
  asciichat_error_t result = connect_to_acds(session);
  if (result != ASCIICHAT_OK) {
    return result;
  }

  // Check again after connection
  if (session->should_exit_callback && session->should_exit_callback(session->exit_callback_data)) {
    return ASCIICHAT_OK; // Return success to allow clean shutdown
  }

  // Create or join session
  if (session->is_initiator) {
    log_info("discovery_session_start: Calling create_session - participant_ctx before=%p", session->participant_ctx);
    asciichat_error_t result = create_session(session);
    log_info("discovery_session_start: create_session returned - participant_ctx after=%p, result=%d",
             session->participant_ctx, result);
    return result;
  } else {
    log_info("discovery_session_start: Calling join_session - participant_ctx before=%p", session->participant_ctx);
    asciichat_error_t result = join_session(session);
    log_info("discovery_session_start: join_session returned - participant_ctx after=%p, state=%d, result=%d",
             session->participant_ctx, session->state, result);
    return result;
  }
}

asciichat_error_t discovery_session_process(discovery_session_t *session, int64_t timeout_ns) {
  if (!session) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session is NULL");
  }

  // Handle state-specific processing
  switch (session->state) {
  case DISCOVERY_STATE_WAITING_PEER:
    // TODO: Wait for PARTICIPANT_JOINED notification from ACDS
    // For now, just sleep briefly
    platform_sleep_us(timeout_ns / 1000);
    break;

  case DISCOVERY_STATE_NEGOTIATING: {
    // Handle NAT negotiation
    if (session->negotiate.state == NEGOTIATE_STATE_INIT) {
      // Initialize negotiation
      negotiate_init(&session->negotiate, session->session_id, session->participant_id, session->is_initiator);
      log_info("Starting NAT negotiation (initiator=%d)", session->is_initiator);

      // Send our NETWORK_QUALITY to ACDS immediately (let peer receive it)
      asciichat_error_t send_result = send_network_quality_to_acds(session);
      if (send_result != ASCIICHAT_OK) {
        log_warn("Failed to send initial NETWORK_QUALITY: %d", send_result);
        // Don't fail - we'll try again next iteration
      }
    }

    // Try to receive peer's NETWORK_QUALITY from ACDS (non-blocking with short timeout)
    if (!session->negotiate.peer_quality_received) {
      asciichat_error_t recv_result = receive_network_quality_from_acds(session);
      if (recv_result == ASCIICHAT_OK) {
        // Got peer quality - proceed to election
        log_info("Received peer NETWORK_QUALITY, proceeding to host election");
      } else if (recv_result != ERROR_NETWORK_TIMEOUT) {
        // Real error (not just timeout)
        log_debug("Error receiving NETWORK_QUALITY: %d (this might be normal if peer not ready)", recv_result);
      }
    }

    // Check if we have both qualities and can determine host
    if (session->negotiate.peer_quality_received && session->negotiate.state != NEGOTIATE_STATE_COMPARING) {
      // Both sides have exchanged NAT quality - determine the winner
      log_info("Both NAT qualities received, determining host...");
      asciichat_error_t result = negotiate_determine_result(&session->negotiate);
      if (result == ASCIICHAT_OK) {
        // Store result
        session->is_host = session->negotiate.we_are_host;

        if (session->is_host) {
          // We won the negotiation - become the host
          log_info("🏠 NAT negotiation complete: WE ARE HOST (tier:%d vs tier:%d)",
                   nat_compute_tier(&session->negotiate.our_quality),
                   nat_compute_tier(&session->negotiate.peer_quality));
          memcpy(session->host_id, session->participant_id, 16);
          SAFE_STRNCPY(session->host_address, session->negotiate.host_address, sizeof(session->host_address));
          session->host_port = session->negotiate.host_port;
          set_state(session, DISCOVERY_STATE_STARTING_HOST);
        } else {
          // They won - we'll be a participant
          log_info("👤 NAT negotiation complete: THEY ARE HOST at %s:%u (tier:%d vs tier:%d)",
                   session->negotiate.host_address, session->negotiate.host_port,
                   nat_compute_tier(&session->negotiate.our_quality),
                   nat_compute_tier(&session->negotiate.peer_quality));
          SAFE_STRNCPY(session->host_address, session->negotiate.host_address, sizeof(session->host_address));
          session->host_port = session->negotiate.host_port;
          set_state(session, DISCOVERY_STATE_CONNECTING_HOST);
        }
      } else {
        log_error("Failed to determine negotiation result: %d", result);
        set_error(session, result, "Host election failed");
      }
    } else {
      // Still waiting for peer quality - sleep briefly to avoid busy loop
      platform_sleep_us(timeout_ns / 1000);
    }
    break;
  }

  case DISCOVERY_STATE_STARTING_HOST: {
    // Start hosting
    if (!session->host_ctx) {
      // Create host context with configured port
      int host_port = GET_OPTION(port);

      session_host_config_t hconfig = {
          .port = host_port,
          .ipv4_address = "0.0.0.0",
          .max_clients = 32,
          .encryption_enabled = true,
      };

      session->host_ctx = session_host_create(&hconfig);
      if (!session->host_ctx) {
        log_error("Failed to create host context");
        set_error(session, ERROR_MEMORY, "Failed to create host context");
        break;
      }

      // Start listening
      asciichat_error_t hstart = session_host_start(session->host_ctx);
      if (hstart != ASCIICHAT_OK) {
        log_error("Failed to start host: %d", hstart);
        set_error(session, hstart, "Failed to start host");
        break;
      }

      log_info("Host started, listening for connections");
    }

    // Notify listeners that we're ready
    if (session->on_session_ready) {
      session->on_session_ready(session->session_string, session->callback_user_data);
    }

    set_state(session, DISCOVERY_STATE_ACTIVE);
    break;
  }

  case DISCOVERY_STATE_CONNECTING_HOST: {
    // Connect to host as participant

    // For WebRTC sessions, wait for DataChannel to open
    if (session->session_type == SESSION_TYPE_WEBRTC) {
      // Check if transport is ready (DataChannel opened)
      if (session->webrtc_transport_ready) {
        log_info("WebRTC DataChannel established, transitioning to ACTIVE state");
        set_state(session, DISCOVERY_STATE_ACTIVE);
        break;
      }

      // Use a per-session flag to track if we've initiated WebRTC connection
      // (We can't use participant_ctx because that's only for TCP connections)
      static bool webrtc_initiated = false;

      // If not ready and we haven't initiated connection yet, initiate it once
      if (session->peer_manager && !webrtc_initiated) {
        log_info("Initiating WebRTC connection to host...");

        // Initiate connection (generates offer, triggers SDP exchange)
        asciichat_error_t conn_result =
            webrtc_peer_manager_connect(session->peer_manager, session->session_id, session->host_id);

        if (conn_result != ASCIICHAT_OK) {
          log_error("Failed to initiate WebRTC connection: %d", conn_result);
          set_error(session, conn_result, "Failed to initiate WebRTC connection");
          break;
        }

        log_info("WebRTC connection initiated, waiting for DataChannel...");
        webrtc_initiated = true; // Mark as initiated
      }

      // Stay in CONNECTING_HOST until DataChannel opens (webrtc_transport_ready becomes true)
      // The discovery_on_transport_ready callback will set webrtc_transport_ready=true
      break;
    }

    if (!session->participant_ctx) {
      // Create participant context for this session
      session_participant_config_t pconfig = {
          .address = session->host_address,
          .port = session->host_port,
          .enable_audio = true,
          .enable_video = true,
          .encryption_enabled = true,
      };

      session->participant_ctx = session_participant_create(&pconfig);
      if (!session->participant_ctx) {
        log_error("Failed to create participant context");
        set_error(session, ERROR_MEMORY, "Failed to create participant context");
        break;
      }

      // Attempt connection
      asciichat_error_t pconn = session_participant_connect(session->participant_ctx);
      if (pconn != ASCIICHAT_OK) {
        log_error("Failed to connect as participant: %d", pconn);
        // Stay in this state, will retry on next process() call
        break;
      }

      log_info("Connected to host, performing crypto handshake...");

      // Perform crypto handshake with server
      // Get the socket from participant context
      socket_t participant_socket = session_participant_get_socket(session->participant_ctx);
      if (participant_socket == INVALID_SOCKET_VALUE) {
        log_error("Failed to get socket from participant context");
        set_error(session, ERROR_NETWORK, "Failed to get participant socket");
        session_participant_disconnect(session->participant_ctx);
        break;
      }

      // Step 1: Send client protocol version
      log_debug("Sending protocol version to server...");
      protocol_version_packet_t client_version = {0};
      client_version.protocol_version = HOST_TO_NET_U16(1);  // Protocol version 1
      client_version.protocol_revision = HOST_TO_NET_U16(0); // Revision 0
      client_version.supports_encryption = 1;                // We support encryption
      client_version.compression_algorithms = 0;
      client_version.compression_threshold = 0;
      client_version.feature_flags = 0;

      int result = send_protocol_version_packet(participant_socket, &client_version);
      if (result != 0) {
        log_error("Failed to send protocol version to server");
        set_error(session, ERROR_NETWORK, "Failed to send protocol version");
        session_participant_disconnect(session->participant_ctx);
        break;
      }

      // Step 2: Receive server protocol version
      log_debug("Receiving server protocol version...");
      packet_type_t packet_type;
      void *payload = NULL;
      size_t payload_len = 0;

      int recv_result = receive_packet(participant_socket, &packet_type, &payload, &payload_len);
      if (recv_result != ASCIICHAT_OK || packet_type != PACKET_TYPE_PROTOCOL_VERSION) {
        log_error("Failed to receive server protocol version (got type 0x%x)", packet_type);
        if (payload) {
          buffer_pool_free(NULL, payload, payload_len);
        }
        set_error(session, ERROR_NETWORK, "Failed to receive protocol version from server");
        session_participant_disconnect(session->participant_ctx);
        break;
      }

      if (payload_len != sizeof(protocol_version_packet_t)) {
        log_error("Invalid protocol version packet size: %zu", payload_len);
        buffer_pool_free(NULL, payload, payload_len);
        set_error(session, ERROR_NETWORK, "Invalid protocol version packet");
        session_participant_disconnect(session->participant_ctx);
        break;
      }

      protocol_version_packet_t server_version;
      memcpy(&server_version, payload, sizeof(protocol_version_packet_t));
      buffer_pool_free(NULL, payload, payload_len);

      if (!server_version.supports_encryption) {
        log_error("Server does not support encryption");
        set_error(session, ERROR_NETWORK, "Server does not support encryption");
        session_participant_disconnect(session->participant_ctx);
        break;
      }

      log_info("Server protocol version: %u.%u (encryption: yes)", NET_TO_HOST_U16(server_version.protocol_version),
               NET_TO_HOST_U16(server_version.protocol_revision));

      // Step 3: Send crypto capabilities
      log_debug("Sending crypto capabilities...");
      crypto_capabilities_packet_t client_caps = {0};
      client_caps.supported_kex_algorithms = HOST_TO_NET_U16(0x0001);    // KEX_ALGO_X25519
      client_caps.supported_auth_algorithms = HOST_TO_NET_U16(0x0003);   // AUTH_ALGO_ED25519 | AUTH_ALGO_NONE
      client_caps.supported_cipher_algorithms = HOST_TO_NET_U16(0x0001); // CIPHER_ALGO_XSALSA20_POLY1305
      client_caps.requires_verification = 0;
      client_caps.preferred_kex = 0x0001;    // KEX_ALGO_X25519
      client_caps.preferred_auth = 0x0001;   // AUTH_ALGO_ED25519
      client_caps.preferred_cipher = 0x0001; // CIPHER_ALGO_XSALSA20_POLY1305

      result = send_crypto_capabilities_packet(participant_socket, &client_caps);
      if (result != 0) {
        log_error("Failed to send crypto capabilities");
        set_error(session, ERROR_NETWORK, "Failed to send crypto capabilities");
        session_participant_disconnect(session->participant_ctx);
        break;
      }

      log_info("Crypto handshake initiated successfully");
      log_warn("*** Connected to host as participant - transitioning to ACTIVE ***");
    }

    // Check if connection is established
    bool ctx_valid = session->participant_ctx != NULL;
    bool is_connected = ctx_valid ? session_participant_is_connected(session->participant_ctx) : false;
    log_debug("discovery_session_process: CONNECTING_HOST - participant_ctx=%p, ctx_valid=%d, is_connected=%d",
              session->participant_ctx, ctx_valid, is_connected);

    if (ctx_valid && !is_connected) {
      // Log more details about why it's not connected
      log_debug("discovery_session_process: CONNECTING_HOST - context exists but not connected yet");
    }

    if (is_connected) {
      log_info("Participant connection confirmed, transitioning to ACTIVE state");
      set_state(session, DISCOVERY_STATE_ACTIVE);
    } else {
      log_debug("discovery_session_process: CONNECTING_HOST - awaiting connection establishment (ctx_valid=%d, "
                "is_connected=%d)",
                ctx_valid, is_connected);
    }
    break;
  }

  case DISCOVERY_STATE_ACTIVE:
    // Receive and handle ACDS packets for WebRTC signaling (NEW)
    if (session->session_type == SESSION_TYPE_WEBRTC && session->acds_socket != INVALID_SOCKET_VALUE) {
      // Use non-blocking polling to check for incoming ACDS packets
      struct pollfd pfd = {.fd = session->acds_socket, .events = POLLIN, .revents = 0};

      int poll_result = socket_poll(&pfd, 1, 0); // Non-blocking poll (timeout=0)

      if (poll_result > 0 && (pfd.revents & POLLIN)) {
        // Data available to read
        packet_type_t type;
        void *data = NULL;
        size_t len = 0;

        asciichat_error_t recv_result = packet_receive(session->acds_socket, &type, &data, &len);

        if (recv_result == ASCIICHAT_OK) {
          // Dispatch to appropriate handler
          handle_acds_webrtc_packet(session, type, data, len);
          POOL_FREE(data, len);
        }
      }
    }

    // Session is active - check for host disconnect
    if (!session->is_host) {
      // We are a participant - check if host is still alive
      asciichat_error_t host_check = discovery_session_check_host_alive(session);
      if (host_check != ASCIICHAT_OK) {
        // Host is down! Trigger automatic failover to pre-elected future host
        log_warn("Host disconnect detected during ACTIVE state");

        // If --prefer-webrtc is set, WebRTC failure is fatal (no TCP fallback)
        const options_t *opts = options_get();
        if (opts && opts->prefer_webrtc) {
          log_fatal("WebRTC connection failed and --prefer-webrtc is set - exiting");
          set_state(session, DISCOVERY_STATE_FAILED);
          session->error = ERROR_NETWORK;
          return ERROR_NETWORK;
        }

        set_state(session, DISCOVERY_STATE_MIGRATING);
        // Reason code: 1 = timeout/disconnect detected during ACTIVE session
        discovery_session_handle_host_disconnect(session, 1);
      }
    }
    platform_sleep_us(timeout_ns / 1000);
    break;

  case DISCOVERY_STATE_MIGRATING:
    // Host migration in progress
    // Check if migration has timed out (if it takes >30 seconds, give up)
    if (session->migration.state == MIGRATION_STATE_COMPLETE) {
      // Migration completed successfully
      set_state(session, DISCOVERY_STATE_ACTIVE);
      log_info("Migration complete, session resumed");
    } else if (session_get_current_time_ms() - session->migration.detection_time_ms > 30000) {
      // Migration timed out after 30 seconds
      log_error("Host migration timeout - session cannot recover");
      set_state(session, DISCOVERY_STATE_FAILED);
      session->error = ERROR_NETWORK;
    }
    platform_sleep_us(timeout_ns / 1000);
    break;

  default:
    break;
  }

  return ASCIICHAT_OK;
}

void discovery_session_stop(discovery_session_t *session) {
  if (!session)
    return;

  log_info("Stopping discovery session...");

  // Send SESSION_LEAVE if connected
  if (session->acds_socket != INVALID_SOCKET_VALUE && session->session_id[0] != 0) {
    acip_session_leave_t leave_msg;
    memset(&leave_msg, 0, sizeof(leave_msg));
    memcpy(leave_msg.session_id, session->session_id, 16);
    memcpy(leave_msg.participant_id, session->participant_id, 16);

    packet_send(session->acds_socket, PACKET_TYPE_ACIP_SESSION_LEAVE, &leave_msg, sizeof(leave_msg));
  }

  set_state(session, DISCOVERY_STATE_ENDED);
}

discovery_state_t discovery_session_get_state(const discovery_session_t *session) {
  if (!session)
    return DISCOVERY_STATE_FAILED;
  return session->state;
}

bool discovery_session_is_active(const discovery_session_t *session) {
  if (!session)
    return false;
  return session->state == DISCOVERY_STATE_ACTIVE;
}

const char *discovery_session_get_string(const discovery_session_t *session) {
  if (!session || session->session_string[0] == '\0')
    return NULL;
  return session->session_string;
}

bool discovery_session_is_host(const discovery_session_t *session) {
  if (!session)
    return false;
  return session->is_host;
}

session_host_t *discovery_session_get_host(discovery_session_t *session) {
  if (!session)
    return NULL;
  return session->host_ctx;
}

session_participant_t *discovery_session_get_participant(discovery_session_t *session) {
  if (!session)
    return NULL;
  return session->participant_ctx;
}

// ============================================================================
// Ring Consensus & Migration Implementation (Host-Mediated Proactive Election)
// ============================================================================

/**
 * @brief Collect NETWORK_QUALITY from all connected participants and run host election
 *
 * This is called by the host every 5 minutes to:
 * 1. Collect fresh NETWORK_QUALITY from all connected participants
 * 2. Measure the host's own NAT quality
 * 3. Run the deterministic election algorithm
 * 4. Broadcast FUTURE_HOST_ELECTED to all participants
 *
 * @param session Discovery session (must be host)
 * @return ASCIICHAT_OK on success
 *
 * NOTE: For MVP, participants' quality data comes from when they connected.
 * TODO: Extend to request fresh NETWORK_QUALITY from participants periodically.
 */
static asciichat_error_t discovery_session_run_election(discovery_session_t *session) {
  if (!session || !session->is_host) {
    return ERROR_INVALID_STATE;
  }

  log_debug("Running host election with collected NETWORK_QUALITY");

  // For MVP: Just use host's own quality + placeholder for participants
  // TODO: When participants start sending NETWORK_QUALITY, collect those instead

  // Measure host's own NAT quality (fresh measurement)
  nat_quality_t host_quality = {0};
  nat_quality_init(&host_quality);

  // TODO: Run NAT detection to get fresh quality data
  // nat_detect_quality(&host_quality, stun_server_url, local_port);
  // For now, assume reasonable defaults (we're the host!)
  host_quality.has_public_ip = true;
  host_quality.upload_kbps = 100000; // Placeholder: 100 Mbps
  host_quality.detection_complete = true;

  // For now, just elect ourselves as the future host (we're already the host!)
  // TODO: When collecting participant data, run proper election:
  // negotiate_elect_future_host(collected_qualities, participant_ids, num_participants, &future_host_id);

  uint8_t future_host_id[16];
  memcpy(future_host_id, session->participant_id, 16); // For MVP, host stays host

  log_info("Election result: Future host elected (round %llu)",
           (unsigned long long)(session->ring.future_host_elected_round + 1));

  // Store locally
  session->ring.future_host_elected_round++;
  memcpy(session->ring.future_host_id, future_host_id, 16);
  session->ring.am_future_host = (memcmp(future_host_id, session->participant_id, 16) == 0);
  memcpy(session->ring.future_host_address, session->host_address, 64);
  session->ring.future_host_port = session->host_port;
  session->ring.future_host_connection_type = 0; // DIRECT_PUBLIC (we're the host)

  // Broadcast FUTURE_HOST_ELECTED to all participants via ACDS
  acip_future_host_elected_t msg = {0};
  memcpy(msg.session_id, session->session_id, 16);
  memcpy(msg.future_host_id, future_host_id, 16);
  memcpy(msg.future_host_address, session->ring.future_host_address, 64);
  msg.future_host_port = session->ring.future_host_port;
  msg.connection_type = session->ring.future_host_connection_type;
  msg.elected_at_round = session->ring.future_host_elected_round;

  packet_send(session->acds_socket, PACKET_TYPE_ACIP_FUTURE_HOST_ELECTED, &msg, sizeof(msg));

  log_info("Broadcasted FUTURE_HOST_ELECTED: participant_id=%.*s, round=%llu", 16, (char *)future_host_id,
           (unsigned long long)msg.elected_at_round);

  return ASCIICHAT_OK;
}

asciichat_error_t discovery_session_init_ring(discovery_session_t *session) {
  if (!session) {
    SET_ERRNO(ERROR_INVALID_PARAM, "session is NULL");
    return ERROR_INVALID_PARAM;
  }

  // Simplified for new architecture: just initialize timing
  // No participant list needed - host runs the election
  session->ring.last_ring_round_ms = session_get_current_time_ms();
  session->ring.am_future_host = false;
  memset(session->ring.future_host_id, 0, 16);

  log_info("Ring consensus initialized (host-mediated model)");
  return ASCIICHAT_OK;
}

asciichat_error_t discovery_session_start_ring_round(discovery_session_t *session) {
  if (!session) {
    SET_ERRNO(ERROR_INVALID_PARAM, "session is NULL");
    return ERROR_INVALID_PARAM;
  }

  session->ring.last_ring_round_ms = session_get_current_time_ms();

  if (session->is_host) {
    log_info("Starting 5-minute proactive election round (collecting NETWORK_QUALITY from participants)");

    // HOST: Run proactive host election
    // 1. Collect NETWORK_QUALITY from all connected participants (TODO: request fresh data)
    // 2. Measure the host's own NAT quality
    // 3. Run deterministic election to determine future host
    // 4. Broadcast FUTURE_HOST_ELECTED to all participants via ACDS
    asciichat_error_t result = discovery_session_run_election(session);
    if (result != ASCIICHAT_OK) {
      log_error("Host election failed: %d", result);
      return result;
    }

    return ASCIICHAT_OK;
  } else {
    log_debug("Ring round timer triggered (waiting for host to broadcast FUTURE_HOST_ELECTED)");
    return ASCIICHAT_OK;
  }
}

asciichat_error_t discovery_session_check_host_alive(discovery_session_t *session) {
  if (!session || session->is_host) {
    // We are the host, so "host" is always alive
    return ASCIICHAT_OK;
  }

  // If we don't have a participant context yet, we're not connected
  if (!session->participant_ctx) {
    return ERROR_NETWORK;
  }

  // Check if connection to host is still alive
  // TODO: This requires access to the participant socket, which may need to be exposed
  // For now, implement a simple check: if we haven't detected a disconnect yet, assume alive
  // A more robust implementation would:
  // 1. Try MSG_PEEK on the participant socket (non-blocking)
  // 2. If read returns 0 bytes with no error, connection closed (RST/FIN detected)
  // 3. If read returns -1 with ECONNRESET/EPIPE, connection reset
  // 4. If read returns 0 with error, connection dropped

  // For now, check migration state - if we're already migrating, host is definitely not alive
  if (session->migration.state != MIGRATION_STATE_NONE) {
    return ERROR_NETWORK; // Already detected disconnect
  }

  // Assume host is alive if we haven't detected otherwise
  return ASCIICHAT_OK;
}

asciichat_error_t discovery_session_handle_host_disconnect(discovery_session_t *session, uint32_t disconnect_reason) {
  if (!session) {
    SET_ERRNO(ERROR_INVALID_PARAM, "session is NULL");
    return ERROR_INVALID_PARAM;
  }

  log_warn("Host disconnect detected: reason=%u", disconnect_reason);

  // Initialize migration context
  session->migration.state = MIGRATION_STATE_DETECTED;
  session->migration.detection_time_ms = session_get_current_time_ms();
  memcpy(session->migration.last_host_id, session->host_id, 16);
  session->migration.disconnect_reason = disconnect_reason;

  // Notify ACDS of host loss (lightweight notification, no NAT data exchanged!)
  acip_host_lost_t host_lost = {0};
  memcpy(host_lost.session_id, session->session_id, 16);
  memcpy(host_lost.participant_id, session->participant_id, 16);
  memcpy(host_lost.last_host_id, session->host_id, 16);
  host_lost.disconnect_reason = disconnect_reason;
  host_lost.disconnect_time_ms = session->migration.detection_time_ms;

  packet_send(session->acds_socket, PACKET_TYPE_ACIP_HOST_LOST, &host_lost, sizeof(host_lost));

  // Check if we have a pre-elected future host (from last 5-minute ring round)
  if (session->ring.future_host_id[0] == 0) {
    // No future host known! This shouldn't happen if we just received FUTURE_HOST_ELECTED
    // within the last 5 minutes. Fall back to treating this as fatal.
    log_error("No future host pre-elected! Session cannot recover from host disconnect.");
    session->migration.state = MIGRATION_STATE_COMPLETE;
    return ERROR_NETWORK; // Session should end
  }

  // **SIMPLIFIED**: No metrics exchange needed! Future host was already elected 5 minutes ago.
  // Just failover to the pre-elected host.
  session->migration.state = MIGRATION_STATE_FAILOVER;

  // Check if I am the future host
  if (session->ring.am_future_host) {
    // I become the new host immediately! (no election delay!)
    log_info("Becoming host (I'm the pre-elected future host)");
    return discovery_session_become_host(session);
  } else {
    // Connect to pre-elected future host (address already stored!)
    log_info("Connecting to pre-elected future host: %s:%u", session->ring.future_host_address,
             session->ring.future_host_port);
    return discovery_session_connect_to_future_host(session);
  }
}

asciichat_error_t discovery_session_become_host(discovery_session_t *session) {
  if (!session) {
    SET_ERRNO(ERROR_INVALID_PARAM, "session is NULL");
    return ERROR_INVALID_PARAM;
  }

  log_info("Starting as new host after migration (participant ID: %02x%02x...)", session->participant_id[0],
           session->participant_id[1]);

  // Get configured port (or use default)
  int host_port = GET_OPTION(port);

  // Mark ourselves as host
  session->is_host = true;

  // Create host context if needed with configured port
  if (!session->host_ctx) {
    session_host_config_t hconfig = {
        .port = host_port,
        .ipv4_address = "0.0.0.0",
        .max_clients = 32,
        .encryption_enabled = true,
    };

    session->host_ctx = session_host_create(&hconfig);
    if (!session->host_ctx) {
      log_error("Failed to create host context for migration");
      return ERROR_MEMORY;
    }

    // Start listening for new participant connections
    asciichat_error_t hstart = session_host_start(session->host_ctx);
    if (hstart != ASCIICHAT_OK) {
      log_error("Failed to start host after migration: %d", hstart);
      return hstart;
    }

    log_info("Host restarted after migration, listening on port %d", host_port);
  }

  // Send HOST_ANNOUNCEMENT to ACDS so other participants can reconnect
  acip_host_announcement_t announcement = {0};
  memcpy(announcement.session_id, session->session_id, 16);
  memcpy(announcement.host_id, session->participant_id, 16);
  SAFE_STRNCPY(announcement.host_address, "127.0.0.1", sizeof(announcement.host_address));
  // TODO: Use actual determined host address instead of 127.0.0.1
  announcement.host_port = host_port;
  announcement.connection_type = ACIP_CONNECTION_TYPE_DIRECT_PUBLIC;
  // TODO: Use actual connection type based on NAT quality

  if (session->acds_socket != INVALID_SOCKET_VALUE) {
    packet_send(session->acds_socket, PACKET_TYPE_ACIP_HOST_ANNOUNCEMENT, &announcement, sizeof(announcement));
    log_info("Sent HOST_ANNOUNCEMENT to ACDS for new host %02x%02x... at %s:%u", announcement.host_id[0],
             announcement.host_id[1], announcement.host_address, announcement.host_port);
  }

  // Mark migration complete
  session->migration.state = MIGRATION_STATE_COMPLETE;

  // Transition to ACTIVE state (host is ready)
  set_state(session, DISCOVERY_STATE_ACTIVE);

  return ASCIICHAT_OK;
}

asciichat_error_t discovery_session_connect_to_future_host(discovery_session_t *session) {
  if (!session) {
    SET_ERRNO(ERROR_INVALID_PARAM, "session is NULL");
    return ERROR_INVALID_PARAM;
  }

  log_info("Reconnecting to future host: %s:%u (connection type: %u)", session->ring.future_host_address,
           session->ring.future_host_port, session->ring.future_host_connection_type);

  // Destroy old participant context if present
  if (session->participant_ctx) {
    session_participant_destroy(session->participant_ctx);
    session->participant_ctx = NULL;
  }

  // Create new participant context for the future host
  session_participant_config_t pconfig = {
      .address = session->ring.future_host_address,
      .port = session->ring.future_host_port,
      .enable_audio = true,
      .enable_video = true,
      .encryption_enabled = true,
  };

  session->participant_ctx = session_participant_create(&pconfig);
  if (!session->participant_ctx) {
    log_error("Failed to create participant context for future host");
    return ERROR_MEMORY;
  }

  // Attempt connection to future host
  asciichat_error_t pconn = session_participant_connect(session->participant_ctx);
  if (pconn != ASCIICHAT_OK) {
    log_error("Failed to connect to future host: %d", pconn);
    return pconn;
  }

  log_info("Connected to future host after migration");

  // Update our host info with the new future host details
  memcpy(session->host_id, session->ring.future_host_id, 16);
  SAFE_STRNCPY(session->host_address, session->ring.future_host_address, sizeof(session->host_address));
  session->host_port = session->ring.future_host_port;

  log_info("Updated host info to: %s:%u (id: %02x%02x...)", session->host_address, session->host_port,
           session->host_id[0], session->host_id[1]);

  // Mark migration complete (in real implementation, this would be done after successful connection)
  session->migration.state = MIGRATION_STATE_COMPLETE;

  // Transition to ACTIVE state (participant is ready)
  set_state(session, DISCOVERY_STATE_ACTIVE);

  return ASCIICHAT_OK;
}

asciichat_error_t discovery_session_get_future_host(const discovery_session_t *session, uint8_t out_id[16],
                                                    char out_address[64], uint16_t *out_port,
                                                    uint8_t *out_connection_type) {
  if (!session || !out_id || !out_address || !out_port || !out_connection_type) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid output parameters");
    return ERROR_INVALID_PARAM;
  }

  if (session->ring.future_host_id[0] == 0) {
    SET_ERRNO(ERROR_NOT_FOUND, "Future host not elected yet");
    return ERROR_NOT_FOUND;
  }

  memcpy(out_id, session->ring.future_host_id, 16);
  SAFE_STRNCPY(out_address, session->ring.future_host_address, 64);
  *out_port = session->ring.future_host_port;
  *out_connection_type = session->ring.future_host_connection_type;

  return ASCIICHAT_OK;
}

bool discovery_session_is_future_host(const discovery_session_t *session) {
  if (!session)
    return false;
  return session->ring.am_future_host;
}
