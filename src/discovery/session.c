/**
 * @file discovery/session.c
 * @brief Discovery session flow management
 * @ingroup discovery
 */

#include "session.h"

#include "common.h"
#include "log/logging.h"
#include "network/acip/acds.h"
#include "network/packet.h"
#include "platform/abstraction.h"
#include "platform/socket.h"

#ifdef _WIN32
#include <ws2tcpip.h> // For getaddrinfo on Windows
#else
#include <netdb.h> // For getaddrinfo on POSIX
#endif

#include <string.h>

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

  log_debug("Discovery session created (initiator=%d, acds=%s:%u)", session->is_initiator, session->acds_address,
            session->acds_port);

  return session;
}

void discovery_session_destroy(discovery_session_t *session) {
  if (!session) return;

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

  SAFE_FREE(session);
}

static void set_state(discovery_session_t *session, discovery_state_t new_state) {
  if (!session) return;

  discovery_state_t old_state = session->state;
  session->state = new_state;

  log_debug("Discovery state: %d -> %d", old_state, new_state);

  if (session->on_state_change) {
    session->on_state_change(new_state, session->callback_user_data);
  }
}

static void set_error(discovery_session_t *session, asciichat_error_t error, const char *message) {
  if (!session) return;

  session->error = error;
  set_state(session, DISCOVERY_STATE_FAILED);

  log_error("Discovery error: %s", message ? message : "Unknown error");

  if (session->on_error) {
    session->on_error(error, message, session->callback_user_data);
  }
}

static asciichat_error_t connect_to_acds(discovery_session_t *session) {
  if (!session) return ERROR_INVALID_PARAM;

  set_state(session, DISCOVERY_STATE_CONNECTING_ACDS);
  log_info("Connecting to ACDS at %s:%u...", session->acds_address, session->acds_port);

  // Resolve hostname using getaddrinfo (supports both IP addresses and hostnames)
  struct addrinfo hints, *result = NULL;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;      // IPv4 for now
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  char port_str[8];
  snprintf(port_str, sizeof(port_str), "%u", session->acds_port);

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
  if (!session) return ERROR_INVALID_PARAM;

  set_state(session, DISCOVERY_STATE_CREATING_SESSION);
  log_info("Creating new discovery session...");

  // Build SESSION_CREATE message
  acip_session_create_t create_msg;
  memset(&create_msg, 0, sizeof(create_msg));

  // TODO: Set identity key from options
  // For now, use zero key (anonymous session)
  create_msg.capabilities = 0x03; // Video + Audio
  create_msg.max_participants = 8;
  create_msg.session_type = SESSION_TYPE_DIRECT_TCP;
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
    SAFE_FREE(data);
    return ERROR_NETWORK_PROTOCOL;
  }

  if (type != PACKET_TYPE_ACIP_SESSION_CREATED || len < sizeof(acip_session_created_t)) {
    set_error(session, ERROR_NETWORK_PROTOCOL, "Unexpected response to SESSION_CREATE");
    SAFE_FREE(data);
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

  SAFE_FREE(data);

  // Notify that session is ready
  if (session->on_session_ready) {
    session->on_session_ready(session->session_string, session->callback_user_data);
  }

  // Wait for peer to join
  set_state(session, DISCOVERY_STATE_WAITING_PEER);

  return ASCIICHAT_OK;
}

static asciichat_error_t join_session(discovery_session_t *session) {
  if (!session) return ERROR_INVALID_PARAM;

  set_state(session, DISCOVERY_STATE_JOINING_SESSION);
  log_info("Joining session: %s", session->session_string);

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
    SAFE_FREE(data);
    return ERROR_NETWORK_PROTOCOL;
  }

  if (type != PACKET_TYPE_ACIP_SESSION_JOINED || len < sizeof(acip_session_joined_t)) {
    set_error(session, ERROR_NETWORK_PROTOCOL, "Unexpected response to SESSION_JOIN");
    SAFE_FREE(data);
    return ERROR_NETWORK_PROTOCOL;
  }

  acip_session_joined_t *joined = (acip_session_joined_t *)data;

  if (!joined->success) {
    log_error("Failed to join session: %s", joined->error_message);
    set_error(session, ERROR_NETWORK_PROTOCOL, joined->error_message);
    SAFE_FREE(data);
    return ERROR_NETWORK_PROTOCOL;
  }

  // Store session info
  memcpy(session->session_id, joined->session_id, 16);
  memcpy(session->participant_id, joined->participant_id, 16);

  // Check if host is already established
  if (joined->server_address[0] && joined->server_port > 0) {
    // Host exists - connect directly
    SAFE_STRNCPY(session->host_address, joined->server_address, sizeof(session->host_address));
    session->host_port = joined->server_port;
    session->is_host = false;

    log_info("Host already established: %s:%u", session->host_address, session->host_port);
    SAFE_FREE(data);

    set_state(session, DISCOVERY_STATE_CONNECTING_HOST);
    return ASCIICHAT_OK;
  }

  // No host yet - need to negotiate
  log_info("No host established, starting negotiation...");
  SAFE_FREE(data);

  set_state(session, DISCOVERY_STATE_NEGOTIATING);
  return ASCIICHAT_OK;
}

asciichat_error_t discovery_session_start(discovery_session_t *session) {
  if (!session) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session is NULL");
  }

  // Connect to ACDS
  asciichat_error_t result = connect_to_acds(session);
  if (result != ASCIICHAT_OK) {
    return result;
  }

  // Create or join session
  if (session->is_initiator) {
    return create_session(session);
  } else {
    return join_session(session);
  }
}

asciichat_error_t discovery_session_process(discovery_session_t *session, int timeout_ms) {
  if (!session) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session is NULL");
  }

  // Handle state-specific processing
  switch (session->state) {
  case DISCOVERY_STATE_WAITING_PEER:
    // TODO: Wait for PARTICIPANT_JOINED notification from ACDS
    // For now, just sleep briefly
    platform_sleep_usec(timeout_ms * 1000);
    break;

  case DISCOVERY_STATE_NEGOTIATING:
    // TODO: Handle NAT negotiation
    // 1. Start NAT detection
    // 2. Exchange NAT_QUALITY with peer
    // 3. Determine winner
    // 4. Transition to host or participant
    platform_sleep_usec(timeout_ms * 1000);
    break;

  case DISCOVERY_STATE_STARTING_HOST:
    // TODO: Start host server
    break;

  case DISCOVERY_STATE_CONNECTING_HOST:
    // TODO: Connect to host as participant
    break;

  case DISCOVERY_STATE_ACTIVE:
    // Session is active - process media
    platform_sleep_usec(timeout_ms * 1000);
    break;

  case DISCOVERY_STATE_MIGRATING:
    // TODO: Handle host migration
    break;

  default:
    break;
  }

  return ASCIICHAT_OK;
}

void discovery_session_stop(discovery_session_t *session) {
  if (!session) return;

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
  if (!session) return DISCOVERY_STATE_FAILED;
  return session->state;
}

bool discovery_session_is_active(const discovery_session_t *session) {
  if (!session) return false;
  return session->state == DISCOVERY_STATE_ACTIVE;
}

const char *discovery_session_get_string(const discovery_session_t *session) {
  if (!session || session->session_string[0] == '\0') return NULL;
  return session->session_string;
}

bool discovery_session_is_host(const discovery_session_t *session) {
  if (!session) return false;
  return session->is_host;
}

session_host_t *discovery_session_get_host(discovery_session_t *session) {
  if (!session) return NULL;
  return session->host_ctx;
}

session_participant_t *discovery_session_get_participant(discovery_session_t *session) {
  if (!session) return NULL;
  return session->participant_ctx;
}
