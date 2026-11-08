
/* Storage capacities in elements; text buffers include the terminating NUL. */
#define DISCOVERY_SESSION_IDENTITY_PATH_BUFFER_SIZE 256
#define DISCOVERY_SESSION_ERROR_MSG_BUFFER_SIZE 256

#include <ascii-chat/crypto/sizes.h>
#include <ascii-chat/tui/input.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/network/negotiation.h>
#include <ascii-chat/network/consensus/handoff.h>
#include <ascii-chat/crypto/policy.h>
#include <ascii-chat/network/acip/acds_protocol.h>
#include <ascii-chat/stats/runtime.h>
#include <ascii-chat/otel.h>
/**
 * @file discovery/session.c
 * @brief Discovery session flow management
 * @ingroup discovery
 */

#include <ascii-chat/tui/notice.h>

#include "session.h"

#include <ascii-chat/common.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/network/acip/acds.h>
#include <ascii-chat/network/acip/acds_client.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/network/connection_endpoint.h>
#include <ascii-chat/network/connection.h>
#include <ascii-chat/network/webrtc/stun.h>
#include <ascii-chat/network/webrtc/config.h>
#include <ascii-chat/network/webrtc/peer_manager.h>
#include <ascii-chat/discovery/identity.h>
#include "ascii-chat/common/error_codes.h"
#include "negotiate.h"
#include "nat.h"
#include <ascii-chat/app_callbacks.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/crypto/keys.h>
#include <ascii-chat/crypto/handshake/common.h>
#include <ascii-chat/crypto/handshake/client.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/acip/client.h>
#include <ascii-chat/network/tailscale.h>

#ifdef _WIN32
#include <time.h>
#else
#include <sys/time.h>
#include <time.h>
#endif

#include <string.h>

/**
 * @brief Get current time in milliseconds (portable)
 */
static uint64_t session_get_current_time_ms(void) {
  return APP_CALLBACK_UINT64(platform_get_monotonic_time_us) / 1000;
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
  session->handoff_generation = 1; // Match consensus_handoff_init().
  session->acds_transport = NULL;
  session->peer_manager = NULL;
  session->webrtc_transport_ready = false;
  session->webrtc_connection_initiated = false;
  session->webrtc_retry_attempt = 0;
  session->webrtc_last_attempt_time_ms = 0;
  session->webrtc_disconnected_since_ms = 0;
  session->stun_servers = NULL;
  session->stun_count = 0;
  session->turn_servers = NULL;
  session->turn_count = 0;

  // Initialize host liveness detection
  session->liveness.last_ping_sent_ms = 0;
  atomic_store_u64(&session->liveness.last_pong_received_ms, 0);
  session->liveness.consecutive_failures = 0;
  session->liveness.max_failures = 3;
  session->liveness.ping_interval_ms = 3 * MS_PER_SEC_INT; // Ping every 3 seconds
  session->liveness.timeout_ms = 10 * MS_PER_SEC_INT;      // 10 second timeout
  session->liveness.ping_in_flight = false;

  log_info("After CALLOC - participant_ctx=%p, host_ctx=%p", session->participant_ctx, session->host_ctx);

  // Explicit identity credentials apply to signaling as well as media authentication.
  const options_t *opts = options_get();
  bool explicit_identity = opts && opts->encrypt_key[0];
  char identity_path[DISCOVERY_SESSION_IDENTITY_PATH_BUFFER_SIZE];
  asciichat_error_t id_result;
  if (explicit_identity) {
    SAFE_STRNCPY(identity_path, opts->encrypt_key, sizeof(identity_path));
    id_result = ASCIICHAT_OK;
  } else {
    id_result = acds_identity_default_path(identity_path, sizeof(identity_path));
  }
  if (id_result == ASCIICHAT_OK) {
    asciichat_errno_scope_t load_scope = {0};
    bool suppressed = false;
    if (!explicit_identity) {
      load_scope = errno_scope_begin();
      suppressed = errno_notifications_suppress(true);
    }
    if (!explicit_identity && platform_access(identity_path, PLATFORM_ACCESS_EXISTS) != 0)
      id_result = ERROR_CONFIG;
    else
      id_result = acds_identity_load(identity_path, session->identity_pubkey, session->identity_seckey);
    if (!explicit_identity) {
      errno_notifications_suppress(suppressed);
      errno_scope_end(load_scope, ASCIICHAT_ERRNO_HANDLED);
    }
    if (id_result != ASCIICHAT_OK && !explicit_identity) {
      // File doesn't exist or is corrupted - generate new key
      log_info("Identity key not found, generating new key");
      id_result = acds_identity_generate(session->identity_pubkey, session->identity_seckey);
      if (id_result == ASCIICHAT_OK) {
        // Save for future use
        acds_identity_save(identity_path, session->identity_pubkey, session->identity_seckey);
      }
    }
  }

  if (id_result != ASCIICHAT_OK && explicit_identity) {
    SAFE_FREE(session);
    return NULL;
  }
  if (id_result != ASCIICHAT_OK) {
    NOTICE(DANGER, "DISCOVERY IDENTITY UNAVAILABLE", "Failed to load/generate identity key, using zero key");
    memset(session->identity_pubkey, 0, sizeof(session->identity_pubkey));
    memset(session->identity_seckey, 0, sizeof(session->identity_seckey));
  } else {
    char fingerprint[HEX_STRING_SIZE_32];
    acds_identity_fingerprint(session->identity_pubkey, fingerprint);
    log_info("Using identity key with fingerprint: %.16s...", fingerprint);
  }

  // Load multiple identity keys from options (multi-key protocol support)
  if (opts && opts->num_identity_keys > 0) {
    // Load all identity keys from options
    for (size_t i = 0; i < opts->num_identity_keys && i < MAX_SESSION_KEYS; i++) {
      uint8_t pubkey[ED25519_PUBLIC_KEY_SIZE] = {0};
      uint8_t seckey[ED25519_PRIVATE_KEY_SIZE] = {0};
      asciichat_error_t key_result = acds_identity_load(opts->identity_keys[i], pubkey, seckey);
      if (key_result == ASCIICHAT_OK) {
        memcpy(session->identity_pubkeys[session->num_identity_keys], pubkey, sizeof(pubkey));
        memcpy(session->identity_seckeys[session->num_identity_keys++], seckey, sizeof(seckey));
        sodium_memzero(seckey, sizeof(seckey));
        char fingerprint[HEX_STRING_SIZE_32];
        acds_identity_fingerprint(pubkey, fingerprint);
        log_info("Loaded identity key #%zu with fingerprint: %.16s...", i + 1, fingerprint);
      } else {
        log_warn("Failed to load identity key #%zu from %s", i + 1, opts->identity_keys[i]);
      }
    }
    if (session->num_identity_keys != opts->num_identity_keys) {
      sodium_memzero(session->identity_seckey, sizeof(session->identity_seckey));
      sodium_memzero(session->identity_seckeys, sizeof(session->identity_seckeys));
      SAFE_FREE(session);
      SET_ERRNO(ERROR_CRYPTO, "Could not load the complete discovery identity key set");
      return NULL;
    }
    memcpy(session->identity_pubkey, session->identity_pubkeys[0], sizeof(session->identity_pubkey));
    memcpy(session->identity_seckey, session->identity_seckeys[0], sizeof(session->identity_seckey));
    log_info("Loaded %zu identity key(s) for multi-key protocol", session->num_identity_keys);
  } else {
    // Single-key mode: use only the primary identity key
    memcpy(session->identity_pubkeys[0], session->identity_pubkey, ED25519_PUBLIC_KEY_SIZE);
    memcpy(session->identity_seckeys[0], session->identity_seckey, sizeof(session->identity_seckey));
    session->num_identity_keys = 1;
  }

  // ACDS connection info
  if (config->acds_url && config->acds_url[0]) {
    SAFE_STRNCPY(session->acds_url, config->acds_url, sizeof(session->acds_url));
  }
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
  if (config->password && config->password[0]) {
    SAFE_STRNCPY(session->password, config->password, sizeof(session->password));
  }

  // Callbacks
  session->on_state_change = config->on_state_change;
  session->on_session_ready = config->on_session_ready;
  session->on_error = config->on_error;
  session->callback_user_data = config->callback_user_data;
  session->should_exit_callback = config->should_exit_callback;
  session->exit_callback_data = config->exit_callback_data;

  // Initialize mutex for NAT quality protection
  if (mutex_init(&session->nat_quality_mutex, "discovery_nat_quality") != 0) {
    log_warn("Failed to initialize NAT quality mutex");
  }

  log_debug("Discovery session created (initiator=%d, acds=%s:%u)", session->is_initiator, session->acds_address,
            session->acds_port);

  NAMED_REGISTER_ATOMIC(&session->liveness.last_pong_received_ms, "discovery.last_pong_received_ms", NULL);
  return session;
}

// Join media before releasing the active transport; failed joins retain ownership.
static asciichat_error_t release_participant_transport(discovery_session_t *session) {
  session_participant_t *participant = session->participant_ctx;
  if (!participant)
    return ASCIICHAT_OK;
  session_participant_disconnect(participant);
  if (session_participant_has_transport(participant))
    return SET_ERRNO(ERROR_THREAD, "Participant connection is still stopping");
  return ASCIICHAT_OK;
}

static void release_host_control_transport(discovery_session_t *session) {
  network_connection_destroy(&session->host_control_connection);
  session->host_control_transport = NULL;
}

// Discovery retains the host control connection when media switches to WebRTC.
static asciichat_error_t select_participant_transport(discovery_session_t *session, acip_transport_t *transport) {
  session_participant_t *participant = session->participant_ctx;
  bool restart_video = session_participant_is_video_active(participant);
  bool restart_audio = session_participant_is_audio_active(participant);
  session_participant_stop_audio(participant);
  session_participant_stop_video(participant);
  acip_transport_t *previous = session_participant_get_transport(participant);
  if (previous && acip_transport_get_type(previous) == ACIP_TRANSPORT_TCP && !session->host_control_transport) {
    asciichat_error_t result = session_participant_take_connection(participant, &session->host_control_connection);
    if (result != ASCIICHAT_OK)
      return result;
    session->host_control_transport = network_connection_transport(session->host_control_connection);
  }
  asciichat_error_t result = session_participant_set_transport(participant, transport, ACIP_TRANSPORT_OWNED);
  if (result != ASCIICHAT_OK)
    return result;
  // Ownership has transferred; a capture failure must not make the caller free the transport.
  if (restart_video && session_participant_start_video(participant) != ASCIICHAT_OK)
    log_warn("Failed to restart video after switching participant transport");
  if (restart_audio && session_participant_start_audio(participant) != ASCIICHAT_OK)
    log_warn("Failed to restart audio after switching participant transport");
  return ASCIICHAT_OK;
}

asciichat_error_t discovery_session_destroy(discovery_session_t *session) {
  ASCII_OTEL_SCOPE(DISCOVERY_SHUTDOWN);
  if (!session) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session is NULL");
  }

  asciichat_error_t result = release_participant_transport(session);
  if (result != ASCIICHAT_OK)
    return result;
  // The detector borrows the session and mutex; retain both on a timed join failure.
  if (session->nat_detection_thread_started) {
    asciichat_error_t join_result = thread_join_timeout(&session->nat_detection_thread, NULL, 500 * NS_PER_MS_INT);
    if (join_result != ASCIICHAT_OK)
      return SET_ERRNO(ERROR_THREAD, "NAT detection worker still owns discovery session");
    session->nat_detection_thread_started = false;
  }
  release_host_control_transport(session);

  // Close ACDS connection
  if (session->acds_transport) {
    network_connection_destroy(&session->acds_connection);
    session->acds_transport = NULL;
  }

  // Destroy host context if active
  if (session->host_ctx) {
    asciichat_error_t result = session_host_destroy(session->host_ctx);
    if (result != ASCIICHAT_OK)
      return result;
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

  // Destroy NAT quality mutex
  mutex_destroy(&session->nat_quality_mutex);

  NAMED_UNREGISTER(&session->liveness.last_pong_received_ms);
  sodium_memzero(session->password, sizeof(session->password));
  sodium_memzero(session->identity_seckey, sizeof(session->identity_seckey));
  sodium_memzero(session->identity_seckeys, sizeof(session->identity_seckeys));
  SAFE_FREE(session);
  return ASCIICHAT_OK;
}

static void set_state(discovery_session_t *session, discovery_state_t new_state) {
  if (!session) {
    SET_ERRNO(ERROR_INVALID_PARAM, "session is NULL");
    return;
  }

  discovery_state_t old_state = session->state;
  session->state = new_state;

  // A host enters ACTIVE when it starts listening, before a peer connects.
  if (new_state == DISCOVERY_STATE_ACTIVE && old_state != DISCOVERY_STATE_ACTIVE && !session->is_host &&
      session->session_string[0]) {
    ascii_otel_call_session_used(session->session_string);
  }

  // Initialize timing for WAITING_PEER state (to timeout if peer doesn't join)
  if (new_state == DISCOVERY_STATE_WAITING_PEER) {
    session->waiting_peer_start_ns = time_get_ns();
  }

  log_debug("Discovery state: %d -> %d", old_state, new_state);

  if (session->on_state_change) {
    session->on_state_change(new_state, session->callback_user_data);
  }
}

static void set_error(discovery_session_t *session, asciichat_error_t error, const char *message) {
  if (!session) {
    SET_ERRNO(ERROR_INVALID_PARAM, "session is NULL");
    return;
  }

  session->error = error;
  set_state(session, DISCOVERY_STATE_FAILED);

  log_error("Discovery error: %s", message ? message : "Unknown error");

  if (session->on_error) {
    session->on_error(error, message, session->callback_user_data);
  }
}

/**
 * @brief Background thread function for NAT quality detection
 * Runs in a separate thread to avoid blocking the main thread
 */
static void *nat_detection_thread_fn(void *arg) {
  if (!arg) {
    return NULL;
  }

  discovery_session_t *session = (discovery_session_t *)arg;

  stun_server_t probe = {0};
  const char *probe_host = NULL;
  if (!GET_OPTION(webrtc_skip_stun) &&
      stun_servers_parse(GET_OPTION(stun_servers), OPT_ENDPOINT_STUN_SERVERS_DEFAULT, &probe, 1) > 0) {
    probe_host = probe.host;
    if (strncmp(probe_host, "stun:", 5) == 0)
      probe_host += 5;
  }

  // Run blocking NAT detection in this thread (not main thread)
  nat_quality_t quality;
  nat_quality_init(&quality);
  asciichat_error_t result = nat_detect_quality(&quality, probe_host, 0);

  if (result == ASCIICHAT_OK) {
    // Lock and store result
    mutex_lock(&session->nat_quality_mutex);
    memcpy(&session->cached_nat_quality, &quality, sizeof(nat_quality_t));
    session->nat_quality_cached = true;
    session->nat_quality_measured_ms = session_get_current_time_ms();
    mutex_unlock(&session->nat_quality_mutex);
    log_debug("★ NAT_DETECTION_COMPLETE (thread): tier=%d, nat_type=%s", nat_compute_tier(&quality),
              nat_type_to_string(quality.nat_type));
  } else {
    log_warn("NAT detection failed in background thread; retaining the previous cache");
  }

  mutex_lock(&session->nat_quality_mutex);
  session->nat_detection_completed = true;
  mutex_unlock(&session->nat_quality_mutex);
  return NULL;
}

/**
 * @brief Refresh an expired NAT hint without overlapping detector workers.
 * Only the controlling thread calls this function. Completion/cache fields are
 * read under the mutex, and a completed worker must join before its handle is reused.
 * Failed starts/detections are throttled; the previous cache remains available.
 */
static void start_nat_detection(discovery_session_t *session) {
  uint64_t now = session_get_current_time_ms();
  mutex_lock(&session->nat_quality_mutex);
  bool fresh = session->nat_quality_cached && now - session->nat_quality_measured_ms <= CONSENSUS_HANDOFF_FRESH_MS;
  bool completed = session->nat_detection_completed;
  mutex_unlock(&session->nat_quality_mutex);
  if (fresh)
    return;
  if (session->nat_detection_thread_started) {
    // Never join a detector that may still be doing network I/O. Poll a completed
    // worker before reusing its handle; a failed join retains ownership for retry.
    if (!completed || thread_join_timeout(&session->nat_detection_thread, NULL, 0) != 0)
      return;
    session->nat_detection_thread_started = false;
  }
  if (session->nat_detection_attempted && now - session->nat_detection_last_attempt_ms <= CONSENSUS_HANDOFF_FRESH_MS)
    return;
  session->nat_detection_attempted = true;
  session->nat_detection_last_attempt_ms = now;
  mutex_lock(&session->nat_quality_mutex);
  session->nat_detection_completed = false;
  mutex_unlock(&session->nat_quality_mutex);
  if (acthread_create(&session->nat_detection_thread, "nat_detection", nat_detection_thread_fn, session) == 0) {
    session->nat_detection_thread_started = true;
  } else {
    log_warn("Could not start NAT detection; handoff will use unknown NAT tier");
  }
}

static asciichat_error_t connect_to_acds(discovery_session_t *session) {
  ASCII_OTEL_SCOPE(DISCOVERY_CONNECT);
  if (!session) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "null session");
  }

  set_state(session, DISCOVERY_STATE_CONNECTING_ACDS);

  const char *endpoint_input = session->acds_url[0] != '\0' ? session->acds_url : session->acds_address;
  uint16_t endpoint_default_port = session->acds_port > 0 ? session->acds_port : ACIP_DISCOVERY_DEFAULT_PORT;
  connection_endpoint_t endpoint = {0};
  asciichat_error_t endpoint_result = network_connection_open("discovery_acds", endpoint_input, endpoint_default_port,
                                                              NULL, &session->acds_connection, &endpoint);
  if (endpoint_result != ASCIICHAT_OK) {
    set_error(session, endpoint_result, "Failed to connect to ACDS");
    return endpoint_result;
  }

  session->acds_transport = network_connection_transport(session->acds_connection);
  SAFE_STRNCPY(session->acds_address, endpoint.host, sizeof(session->acds_address));
  session->acds_port = endpoint.port;

  if (endpoint.protocol == CONNECTION_ENDPOINT_WEBSOCKET) {
    log_info("Connected to ACDS via WebSocket: %s", endpoint.input);
  } else {
    log_info("Connected to ACDS at %s:%u", session->acds_address, session->acds_port);
  }

  return ASCIICHAT_OK;
}

static asciichat_error_t discovery_protocol_send(void *transport, packet_type_t type, const void *payload,
                                                 size_t length) {
  return packet_send_via_transport(transport, type, payload, length, 0);
}
static asciichat_error_t discovery_protocol_receive(discovery_session_t *session, acds_protocol_t *protocol) {
  packet_type_t type;
  void *payload = NULL, *allocation = NULL;
  size_t length = 0;
  asciichat_error_t result = acds_receive_packet(session->acds_transport, &type, &payload, &length, &allocation);
  if (result == ASCIICHAT_OK)
    result = acds_protocol_receive(protocol, type, payload, length);
  buffer_release(allocation);
  if (result != ASCIICHAT_OK &&
      !(result == ERROR_INVALID_PASSWORD && session->state == DISCOVERY_STATE_JOINING_SESSION))
    set_error(session, result, protocol->error[0] ? protocol->error : "Discovery receive failed");
  return result;
}

static asciichat_error_t create_session(discovery_session_t *session) {
  ASCII_OTEL_SCOPE(DISCOVERY_CREATE);
  if (!session) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "null session");
  }

  set_state(session, DISCOVERY_STATE_CREATING_SESSION);
  log_info("Creating new discovery session...");

  acds_protocol_t protocol;
  acds_protocol_init(&protocol, discovery_protocol_send, session->acds_transport);
  for (size_t key_idx = 0; key_idx < session->num_identity_keys; ++key_idx) {
    acds_session_create_params_t params = {0};
    memcpy(params.identity_pubkey, session->identity_pubkeys[key_idx], sizeof(params.identity_pubkey));
    memcpy(params.identity_seckey, session->identity_seckeys[key_idx], sizeof(params.identity_seckey));
    params.capabilities = 3;
    params.max_participants = MAX_SESSION_PARTICIPANTS;
    params.session_type = SESSION_TYPE_WEBRTC;
    params.has_password = session->password[0] != 0;
    SAFE_STRNCPY(params.password, session->password, sizeof(params.password));
    // An empty address lets ACDS use the creator's observed source address.
    params.acds_expose_ip = GET_OPTION(discovery_expose_ip);
    params.server_port = (uint16_t)GET_OPTION(port);
    if (GET_OPTION(quic_bind_address)[0] ||
        (GET_OPTION(webtransport_bind_address)[0] || GET_OPTION(webtransport_origin)[0]))
      params.quic_port = (uint16_t)(GET_OPTION(quic_port) ? GET_OPTION(quic_port) : GET_OPTION(port));
    if ((GET_OPTION(webtransport_bind_address)[0] || GET_OPTION(webtransport_origin)[0])) {
      if (strlen(GET_OPTION(webtransport_public_url)) >= sizeof(params.webtransport_url)) {
        acds_protocol_destroy(&protocol);
        return SET_ERRNO(ERROR_USAGE, "WebTransport public URL exceeds the discovery endpoint limit");
      }
      SAFE_STRNCPY(params.webtransport_url, GET_OPTION(webtransport_public_url), sizeof(params.webtransport_url));
    }
    asciichat_error_t result =
        acds_protocol_create(&protocol, &params, (uint8_t)session->num_identity_keys, (uint8_t)key_idx);
    sodium_memzero(&params, sizeof(params));
    if (result != ASCIICHAT_OK) {
      acds_protocol_destroy(&protocol);
      return result;
    }
  }
  asciichat_error_t result = discovery_protocol_receive(session, &protocol);
  if (result != ASCIICHAT_OK) {
    acds_protocol_destroy(&protocol);
    return result;
  }
  acip_session_created_t created_value = protocol.created;
  acip_session_created_t *created = &created_value;
  void *alloc_buffer = NULL;
  acds_protocol_destroy(&protocol);

  // Store session info
  memcpy(session->session_id, created->session_id, SESSION_ID_SIZE);
  SAFE_STRNCPY(session->session_string, created->session_string, sizeof(session->session_string));

  // We're also participant 0 (initiator)
  memcpy(session->participant_id, created->session_id,
         SESSION_ID_SIZE); // Use session ID as participant ID for initiator
  memcpy(session->initiator_id, session->participant_id, SESSION_ID_SIZE);

  log_info("Session created: %s", session->session_string);

  buffer_release(alloc_buffer);

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
static asciichat_error_t discovery_send_sdp_via_acds(const uint8_t session_id[SESSION_ID_SIZE],
                                                     const uint8_t recipient_id[SESSION_ID_SIZE], const char *sdp_type,
                                                     const char *sdp, void *user_data) {
  discovery_session_t *session = (discovery_session_t *)user_data;

  if (!session || !session->acds_transport) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid session or ACDS transport");
  }

  // Build ACIP SDP packet
  size_t sdp_len = strlen(sdp);
  size_t total_len = sizeof(acip_webrtc_sdp_t) + sdp_len;

  uint8_t *packet_data = SAFE_MALLOC(total_len, uint8_t *);
  if (!packet_data) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate SDP packet");
  }
  acip_webrtc_sdp_t *sdp_msg = (acip_webrtc_sdp_t *)packet_data;

  memcpy(sdp_msg->session_id, session_id, SESSION_ID_SIZE);
  memcpy(sdp_msg->sender_id, session->participant_id, SESSION_ID_SIZE);
  memcpy(sdp_msg->recipient_id, recipient_id, SESSION_ID_SIZE);
  sdp_msg->sdp_type = (strcmp(sdp_type, "offer") == 0) ? 0 : 1;
  sdp_msg->sdp_len = HOST_TO_NET_U16(sdp_len);
  // NOLINTNEXTLINE(bugprone-not-null-terminated-result) - binary packet data
  memcpy(packet_data + sizeof(acip_webrtc_sdp_t), sdp, sdp_len);

  // Send to ACDS
  asciichat_error_t result =
      packet_send_via_transport(session->acds_transport, PACKET_TYPE_ACIP_WEBRTC_SDP, packet_data, total_len, 0);

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
static asciichat_error_t discovery_send_ice_via_acds(const uint8_t session_id[SESSION_ID_SIZE],
                                                     const uint8_t recipient_id[SESSION_ID_SIZE], const char *candidate,
                                                     const char *mid, void *user_data) {
  discovery_session_t *session = (discovery_session_t *)user_data;

  if (!session || !session->acds_transport) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid session or ACDS transport");
  }

  // Calculate payload length (candidate + null + mid + null)
  size_t candidate_len = strlen(candidate);
  size_t mid_len = strlen(mid);
  size_t payload_len = candidate_len + 1 + mid_len + 1;

  // Allocate packet buffer (header + payload)
  size_t total_len = sizeof(acip_webrtc_ice_t) + payload_len;
  uint8_t *packet_data = SAFE_MALLOC(total_len, uint8_t *);
  if (!packet_data) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate ICE packet");
  }

  // Fill header
  acip_webrtc_ice_t *ice_msg = (acip_webrtc_ice_t *)packet_data;
  memcpy(ice_msg->session_id, session_id, SESSION_ID_SIZE);
  memcpy(ice_msg->sender_id, session->participant_id, SESSION_ID_SIZE);
  memcpy(ice_msg->recipient_id, recipient_id, SESSION_ID_SIZE);
  ice_msg->candidate_len = HOST_TO_NET_U16((uint16_t)candidate_len);

  // Copy candidate and mid after header
  uint8_t *payload = packet_data + sizeof(acip_webrtc_ice_t);
  memcpy(payload, candidate, candidate_len);
  payload[candidate_len] = '\0';
  memcpy(payload + candidate_len + 1, mid, mid_len);
  payload[candidate_len + 1 + mid_len] = '\0';

  log_debug("Sending ICE candidate to ACDS (candidate_len=%zu, mid=%s)", candidate_len, mid);

  // Send to ACDS
  asciichat_error_t result =
      packet_send_via_transport(session->acds_transport, PACKET_TYPE_ACIP_WEBRTC_ICE, packet_data, total_len, 0);

  SAFE_FREE(packet_data);

  if (result != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to send ICE to ACDS");
  }

  log_debug("Sent ICE candidate to ACDS (candidate_len=%zu, mid=%s)", candidate_len, mid);
  return ASCIICHAT_OK;
}

/**
 * @brief Calculate exponential backoff delay with jitter
 * @param attempt Attempt number (0 = first retry)
 * @return Delay in milliseconds
 *
 * Formula: min(1000 * (2^attempt), 30000) + random(0-1000)
 * Examples: 0→1s, 1→2s, 2→4s, 3→8s, 4→16s, 5→30s (capped)
 */
static uint32_t calculate_backoff_delay_ms(int attempt) {
  // Base delay: 2^attempt seconds (clamp attempt to valid range)
  if (attempt < 0) {
    attempt = 0;
  } else if (attempt > 30) {
    attempt = 30; // Prevent overflow
  }
  // Saturate before shifting: a large attempt can wrap a 32-bit delay to zero.
  uint32_t base_delay_ms = attempt >= 5 ? 30 * MS_PER_SEC_INT : 1000U << attempt;

  // Cap at 30 seconds
  if (base_delay_ms > 30 * MS_PER_SEC_INT) {
    base_delay_ms = 30 * MS_PER_SEC_INT;
  }

  // Add jitter (0-1000ms) to prevent thundering herd
  // Use monotonic time as pseudo-random seed
  uint32_t jitter_ms = (uint32_t)(APP_CALLBACK_UINT64(platform_get_monotonic_time_us) % 1000);

  return base_delay_ms + jitter_ms;
}

/**
 * @brief Callback when ICE gathering times out for a peer
 *
 * This callback is invoked when a peer connection's ICE gathering exceeds
 * the configured timeout. Trickle ICE can still establish a connection using
 * candidates gathered so far, so this is diagnostic rather than fatal.
 */
static void discovery_on_gathering_timeout(const uint8_t participant_id[SESSION_ID_SIZE], uint32_t timeout_ms,
                                           uint64_t elapsed_ms, void *user_data) {
  (void)user_data; // Session context not needed for logging

  log_warn("ICE candidate gathering is slower than expected; continuing trickle ICE");
  log_debug("  Participant: %02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", participant_id[0],
            participant_id[1], participant_id[2], participant_id[3], participant_id[4], participant_id[5],
            participant_id[6], participant_id[7], participant_id[8], participant_id[9], participant_id[10],
            participant_id[11], participant_id[12], participant_id[13], participant_id[14], participant_id[15]);
  log_warn("  Gathering timeout: %u ms (elapsed: %llu ms)", timeout_ms, (unsigned long long)elapsed_ms);
  log_debug("Possible causes include unreachable STUN/TURN servers or network filtering");
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
static void discovery_on_transport_ready(acip_transport_t *transport, const uint8_t participant_id[SESSION_ID_SIZE],
                                         void *user_data) {
  discovery_session_t *session = (discovery_session_t *)user_data;

  if (!session) {
    SET_ERRNO(ERROR_INVALID_STATE, "session is NULL");
    return;
  }

  log_info("========== WebRTC DataChannel READY ==========");
  log_info("WebRTC DataChannel successfully established and wrapped in ACIP transport");
  log_debug("  Remote participant: %02x%02x%02x%02x-...", participant_id[0], participant_id[1], participant_id[2],
            participant_id[3]);
  log_info("  Transport status: READY for media transmission");
  log_info("  Encryption: DTLS (automatic) + optional ACIP layer encryption");

  // Mark transport as ready

  if (session->session_type != SESSION_TYPE_WEBRTC) {
    log_warn("WebRTC transport ready but session type is not WebRTC!");
    return;
  }

  log_info("========== WebRTC Media Channel ACTIVE ==========");
  log_info("Switching to WebRTC transport for media transmission...");

  // Implement transport switching for participant/host
  if (session->is_host && session->host_ctx) {
    asciichat_error_t result = session_host_accept_peer(session->host_ctx, transport);
    if (result != ASCIICHAT_OK) {
      log_error("Cannot admit host peer: %s", asciichat_error_format(NULL, result));
      acip_transport_destroy(transport);
      return;
    }
  } else if (session->participant_ctx) {
    // We are a participant - set transport for ourselves
    asciichat_error_t result = select_participant_transport(session, transport);
    if (result == ASCIICHAT_OK) {
      log_info("✓ Participant: Successfully switched to WebRTC transport");
      log_info("  TCP connection remains open as control/signaling channel");
      log_info("  Media frames will now be transmitted over WebRTC DataChannel");
    } else {
      log_error("✗ Participant: Failed to set WebRTC transport: %d", result);
      acip_transport_destroy(transport);
      return;
    }
  } else {
    session_participant_config_t pconfig = {
        .address = session->host_address,
        .port = session->host_port,
        .enable_video = true,
        .enable_audio = GET_OPTION(audio_enabled),
        .encryption_enabled = false,
    };
    session->participant_ctx = session_participant_create(&pconfig);
    if (!session->participant_ctx || select_participant_transport(session, transport) != ASCIICHAT_OK) {
      acip_transport_destroy(transport);
      set_error(session, ERROR_INVALID_STATE, "Failed to attach WebRTC participant transport");
      return;
    }
  }
  session->webrtc_transport_ready = true;
  session->webrtc_disconnected_since_ms = 0;
  session->webrtc_retry_attempt = 0;
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

  const char *custom_username = GET_OPTION(turn_username);
  const char *custom_credential = GET_OPTION(turn_credential);
  bool custom_turn = custom_username[0] || custom_credential[0];
  if (custom_turn && (!custom_username[0] || !custom_credential[0])) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Provide both --turn-username and --turn-credential");
  }
  const char *turn_username = custom_turn ? custom_username : session->turn_username;
  const char *turn_credential = custom_turn ? custom_credential : session->turn_password;
  if (custom_turn && (strlen(turn_username) >= sizeof(session->turn_username) ||
                      strlen(turn_credential) >= sizeof(session->turn_password))) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "TURN username and credential must each fit 127 bytes");
  }
  if (custom_turn) {
    SAFE_STRNCPY(session->turn_username, custom_username, sizeof(session->turn_username));
    SAFE_STRNCPY(session->turn_password, custom_credential, sizeof(session->turn_password));
  }
  if (GET_OPTION(webrtc_relay_only) && (GET_OPTION(webrtc_disable_turn) || GET_OPTION(no_webrtc))) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "--webrtc-relay-only conflicts with disabled TURN or WebRTC");
  }

  const char *stun_servers_option = GET_OPTION(stun_servers);
  bool default_stun_servers = !stun_servers_option || stun_servers_option[0] == '\0' ||
                              strcmp(stun_servers_option, OPT_ENDPOINT_STUN_SERVERS_DEFAULT) == 0;
  bool tailscale_direct = default_stun_servers && is_tailscale_host(GET_OPTION(discovery_server));

  // A Tailscale discovery service already provides a private mesh path. Avoid
  // sharing its ICE socket with public STUN when the default servers are used:
  // some macOS networks reject those UDP sends and libjuice then abandons the
  // otherwise reachable Tailscale host candidate.
  if (GET_OPTION(webrtc_skip_stun) || tailscale_direct) {
    if (tailscale_direct && !GET_OPTION(webrtc_skip_stun)) {
      log_info("Skipping default STUN for Tailscale discovery service; using mesh host candidates");
    } else {
      log_info("Skipping STUN (--webrtc-skip-stun)");
    }
    session->stun_servers = NULL;
    session->stun_count = 0;
  } else {
    // Parse STUN servers from options (supports up to 4 servers for redundancy)
    const int max_stun_servers = 4;
    session->stun_servers = SAFE_MALLOC(max_stun_servers * sizeof(stun_server_t), stun_server_t *);
    if (!session->stun_servers) {
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate STUN server array");
    }

    // Parse comma-separated STUN server list from options
    session->stun_count = stun_servers_parse(GET_OPTION(stun_servers), OPT_ENDPOINT_STUN_SERVERS_DEFAULT,
                                             session->stun_servers, max_stun_servers);

    if (session->stun_count == 0) {
      log_warn("No STUN servers configured, WebRTC may fail with symmetric NAT");
      SAFE_FREE(session->stun_servers);
      session->stun_servers = NULL;
    } else {
      log_info("Configured %d STUN server(s) for WebRTC", session->stun_count);
    }
  }

  // Set up TURN servers (if credentials available from ACDS and not disabled)
  if (GET_OPTION(webrtc_disable_turn)) {
    log_info("TURN disabled (--webrtc-disable-turn)");
    session->turn_servers = NULL;
    session->turn_count = 0;
  } else if (turn_username[0] != '\0' && turn_credential[0] != '\0') {
    // Parse TURN servers from options and apply ACDS credentials to each
    const char *turn_servers_str = GET_OPTION(turn_servers);
    if (!turn_servers_str || turn_servers_str[0] == '\0') {
      turn_servers_str = OPT_ENDPOINT_TURN_SERVERS_DEFAULT;
    }

    // Allocate array for up to 4 TURN servers
    const int max_turn_servers = 4;
    session->turn_servers = SAFE_MALLOC(max_turn_servers * sizeof(turn_server_t), turn_server_t *);
    if (!session->turn_servers) {
      SAFE_FREE(session->stun_servers);
      session->stun_servers = NULL;
      session->stun_count = 0;
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate TURN server array");
    }

    asciichat_error_t parse_result =
        webrtc_turn_servers_parse(turn_servers_str, turn_username, turn_credential, session->turn_servers,
                                  max_turn_servers, &session->turn_count);
    if (parse_result != ASCIICHAT_OK) {
      SAFE_FREE(session->turn_servers);
      session->turn_servers = NULL;
      session->turn_count = 0;
      return parse_result;
    }

    if (session->turn_count > 0) {
      log_info("Configured %zu TURN server(s) with %s credentials", session->turn_count,
               custom_turn ? "custom" : "ACDS");
    } else {
      log_warn("No valid TURN servers configured");
      SAFE_FREE(session->turn_servers);
      session->turn_servers = NULL;
    }
  } else {
    log_info("No TURN credentials from ACDS - will rely on direct P2P + STUN (sufficient for most NAT scenarios)");
    session->turn_servers = NULL;
    session->turn_count = 0;
  }

  // The session host accepts offers and generates answers. A newly created
  // session has not set is_host yet when this runs, but its initiator will
  // become the host during NAT negotiation. Treat that initiator as the
  // responder now so it does not generate a competing offer for a browser.
  webrtc_peer_role_t role = (session->is_host || session->is_initiator) ? WEBRTC_ROLE_CREATOR : WEBRTC_ROLE_JOINER;

  log_info("Initializing WebRTC peer manager (role=%s, stun_count=%zu, turn_count=%zu)",
           role == WEBRTC_ROLE_JOINER ? "JOINER" : "CREATOR", session->stun_count, session->turn_count);

  // ICE needs its own interface selection: the TCP listener and signaling
  // service addresses do not determine where a peer can receive UDP.
  const char *ice_bind_address = GET_OPTION(webrtc_bind_address);
  // Create peer manager configuration
  webrtc_peer_manager_config_t pm_config = {.role = role,
                                            .bind_address = ice_bind_address[0] ? ice_bind_address : NULL,
                                            .stun_servers = session->stun_servers,
                                            .stun_count = session->stun_count,
                                            .turn_servers = session->turn_servers,
                                            .turn_count = session->turn_count,
                                            .relay_only = GET_OPTION(webrtc_relay_only),
                                            .turn_username = session->turn_username,
                                            .turn_credential = session->turn_password,
                                            .on_transport_ready = discovery_on_transport_ready,
                                            .on_gathering_timeout = discovery_on_gathering_timeout,
                                            .user_data = session};

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
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid SDP packet size: %zu", len);
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
  if (!sdp_str) {
    log_error("Failed to allocate SDP string");
    return;
  }
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
 * @brief Encode and send a handoff control frame or measured probe body
 */
static asciichat_error_t discovery_handoff_send(discovery_session_t *session, consensus_handoff_message_t *message,
                                                bool probe) {
  size_t length = CONSENSUS_HANDOFF_HEADER + (probe ? CONSENSUS_HANDOFF_PROBE_BYTES : 0);
  uint8_t *bytes = SAFE_CALLOC(1, length, uint8_t *);
  consensus_handoff_encode(message, bytes);
  asciichat_error_t result =
      packet_send_via_transport(session->acds_transport, (packet_type_t)CONSENSUS_HANDOFF_PACKET, bytes, length, 0);
  SAFE_FREE(bytes);
  return result;
}
// Membership generation and committed round together order host decisions.
static int discovery_compare_host_decision(const discovery_session_t *session, uint32_t generation, uint32_t sequence) {
  if (!session->host_authority_received)
    return 1;
  if (generation != session->host_authority_generation)
    return generation > session->host_authority_generation ? 1 : -1;
  if (sequence != session->host_authority_sequence)
    return sequence > session->host_authority_sequence ? 1 : -1;
  return 0;
}

static void discovery_record_host_decision(discovery_session_t *session, uint32_t generation, uint32_t sequence) {
  session->host_authority_received = true;
  session->host_authority_generation = generation;
  session->host_authority_sequence = sequence;
}

static void discovery_commit_host(discovery_session_t *session, const consensus_handoff_message_t *message) {
  ASCII_OTEL_SCOPE(DISCOVERY_HOST_RECONCILE);
  ASCII_OTEL_ATTR_STRING("host.update.source", "commit");
  ASCII_OTEL_ATTR_INT("host.generation", message->generation);
  ASCII_OTEL_ATTR_STRING("host.update.outcome", "ignored");
  ASCII_OTEL_ATTR_INT("host.decision.sequence", message->sequence);
  if (discovery_compare_host_decision(session, message->generation, message->sequence) <= 0)
    return;
  ASCII_OTEL_ATTR_STRING("host.update.outcome", "queued");
  discovery_record_host_decision(session, message->generation, message->sequence);
  session->host_loss_detected_ms = session_get_current_time_ms();
  if (memcmp(session->host_id, message->participant, SESSION_ID_SIZE))
    NOTICE_ANNOUNCE("Session host changed", "The session has a new host.");
  memcpy(session->host_id, message->participant, SESSION_ID_SIZE);
  session->host_port = message->port;
  session->host_address[0] = 0;
  session->handoff_pending = session->session_type == SESSION_TYPE_WEBRTC ||
                             !memcmp(session->host_id, session->participant_id, SESSION_ID_SIZE);
  set_state(session, DISCOVERY_STATE_MIGRATING);
  log_info("Committed successor %02x%02x generation=%u", message->participant[0], message->participant[1],
           message->generation);
}

/**
 * @brief Apply a validated ACDS handoff action on the controlling thread.
 * PREPARE acknowledges actual host startup. COMMIT defers transport/role replacement
 * until media workers stop; FAILED records a terminal session error.
 */
static void discovery_handoff_receive(discovery_session_t *session, const void *data, size_t length) {
  consensus_handoff_message_t message;
  if (consensus_handoff_decode(data, length, &message) != ASCIICHAT_OK ||
      memcmp(message.session, session->session_id, SESSION_ID_SIZE))
    return;
  if (message.generation < session->handoff_generation)
    return;
  session->handoff_generation = message.generation;
  if (message.kind == HANDOFF_PREPARE && !memcmp(message.participant, session->participant_id, SESSION_ID_SIZE)) {
    asciichat_error_t result = ASCIICHAT_OK;
    if (!session->host_ctx) {
      session_host_config_t config = {.port = GET_OPTION(port),
                                      .ipv4_address = "0.0.0.0",
                                      .max_clients = MAX_SESSION_PARTICIPANTS,
                                      .encryption_enabled = true,
                                      .configured_quic_listeners = true,
                                      .password = session->password,
                                      .key_path = GET_OPTION(encrypt_key)};
      session->host_ctx = session_host_create(&config);
      result = session->host_ctx ? session_host_start(session->host_ctx) : ERROR_MEMORY;
    }
    if (result != ASCIICHAT_OK && session->host_ctx) {
      session_host_destroy(session->host_ctx);
      session->host_ctx = NULL;
    }
    message.kind = result == ASCIICHAT_OK ? HANDOFF_READY : HANDOFF_REJECT;
    message.capabilities = session->session_type == SESSION_TYPE_WEBRTC ? 2 : 1;
    message.port = (uint16_t)GET_OPTION(port);
    if (discovery_handoff_send(session, &message, false) != ASCIICHAT_OK)
      log_warn("Could not acknowledge successor preparation");
  } else if (message.kind == HANDOFF_PLAN) {
    log_info("Prepared successor %02x%02x%02x%02x round=%u", message.participant[0], message.participant[1],
             message.participant[2], message.participant[3], message.sequence);
  } else if (message.kind == HANDOFF_FAILED) {
    set_error(session, ERROR_NETWORK, "No eligible successor has fresh measurements and a prepared host");
  } else if (message.kind == HANDOFF_COMMIT) {
    discovery_commit_host(session, &message);
  }
}
/**
 * @brief Replace the old role after a committed handoff and media-worker shutdown.
 * Releases borrowed-role resources, resets liveness, and initializes the committed
 * successor connection. Callers must stop media workers before entering this function.
 * @return ASCIICHAT_OK if no handoff is pending or initialization succeeds.
 */
asciichat_error_t discovery_session_apply_handoff(discovery_session_t *session) {
  if (!session || !session->handoff_pending)
    return ASCIICHAT_OK;
  session->handoff_pending = false;
  if (session->participant_ctx) {
    session_participant_destroy(session->participant_ctx);
    session->participant_ctx = NULL;
  }
  if (session->peer_manager) {
    webrtc_peer_manager_destroy(session->peer_manager);
    session->peer_manager = NULL;
  }
  session->is_host = !memcmp(session->host_id, session->participant_id, SESSION_ID_SIZE);
  if (!session->is_host && session->host_ctx) {
    session_host_destroy(session->host_ctx);
    session->host_ctx = NULL;
  }
  session->webrtc_transport_ready = false;
  session->webrtc_connection_initiated = false;
  session->webrtc_retry_attempt = 0;
  session->webrtc_disconnected_since_ms = 0;
  session->liveness.ping_in_flight = false;
  session->liveness.consecutive_failures = 0;
  session->liveness.last_ping_sent_ms = session_get_current_time_ms();
  atomic_store_u64(&session->liveness.last_pong_received_ms, 0);
  if (session->session_type == SESSION_TYPE_WEBRTC) {
    asciichat_error_t result = initialize_webrtc_peer_manager(session);
    if (result != ASCIICHAT_OK)
      return result;
  }
  ascii_otel_metric(ASCII_OTEL_METRIC_HOST_CHANGES_APPLIED, 1);
  session->host_loss_detected_ms = 0;

  set_state(session, session->is_host ? DISCOVERY_STATE_STARTING_HOST : DISCOVERY_STATE_CONNECTING_HOST);
  return ASCIICHAT_OK;
}

/**
 * @brief Send a measured probe at most every five seconds, including during recovery.
 * Refreshes expired native NAT hints asynchronously and advertises only fresh hints.
 * ACDS measures the probe body rather than trusting a sender-supplied throughput value.
 * @return The transport send error, or ASCIICHAT_OK when sent or not yet due.
 */
static asciichat_error_t discovery_handoff_sample(discovery_session_t *session) {
  uint64_t now = session_get_current_time_ms();
  if (session->handoff_last_sample_ms && now - session->handoff_last_sample_ms < 5000)
    return ASCIICHAT_OK;
  session->handoff_last_sample_ms = now;
  start_nat_detection(session);
  if (!session->handoff_sequence)
    log_info("Handoff identity=%02x%02x%02x%02x", session->participant_id[0], session->participant_id[1],
             session->participant_id[2], session->participant_id[3]);
  consensus_handoff_message_t message = {.kind = HANDOFF_SAMPLE_START,
                                         .sequence = ++session->handoff_sequence,
                                         .capabilities = session->session_type == SESSION_TYPE_WEBRTC ? 2 : 0,
                                         .nat_tier = 255,
                                         .port = (uint16_t)GET_OPTION(port)};
  mutex_lock(&session->nat_quality_mutex);
  if (session->nat_quality_cached && now - session->nat_quality_measured_ms <= CONSENSUS_HANDOFF_FRESH_MS &&
      session->cached_nat_quality.detection_complete &&
      (session->cached_nat_quality.lan_reachable || session->cached_nat_quality.has_public_ip ||
       session->cached_nat_quality.upnp_available))
    message.nat_tier = (uint8_t)nat_compute_tier(&session->cached_nat_quality);
  mutex_unlock(&session->nat_quality_mutex);
  if ((GET_OPTION(quic_bind_address)[0] ||
       (GET_OPTION(webtransport_bind_address)[0] || GET_OPTION(webtransport_origin)[0])) &&
       GET_OPTION(tls_cert)[0] && GET_OPTION(tls_key)[0])
    message.capabilities |= 1; // A configured native listener can prepare a direct successor.
  memcpy(message.session, session->session_id, SESSION_ID_SIZE);
  memcpy(message.participant, session->participant_id, SESSION_ID_SIZE);
  asciichat_error_t result = discovery_handoff_send(session, &message, false);
  if (result != ASCIICHAT_OK)
    return result;
  message.kind = HANDOFF_SAMPLE_DATA;
  return discovery_handoff_send(session, &message, true);
}

static bool discovery_host_designation_is_current(const discovery_session_t *session,
                                                  const acip_host_designated_t *host, uint32_t generation) {
  if (memcmp(host->session_id, session->session_id, SESSION_ID_SIZE) ||
      !memchr(host->host_address, 0, sizeof(host->host_address)) || !host->host_port ||
      host->connection_type > ACIP_CONNECTION_TYPE_TURN || generation < session->handoff_generation)
    return false;
  int order = discovery_compare_host_decision(session, generation, NET_TO_HOST_U32(host->sequence));
  if (order != 0)
    return order > 0;
  // The same authority may supply an endpoint that was not available at join time.
  return !memcmp(session->host_id, host->host_id, SESSION_ID_SIZE) && !session->host_address[0] &&
         host->host_address[0];
}

static bool discovery_host_endpoint_ready(const discovery_session_t *session, const acip_host_designated_t *host) {
  return session->session_type == SESSION_TYPE_WEBRTC || host->host_address[0] ||
         !memcmp(host->host_id, session->participant_id, SESSION_ID_SIZE);
}

static void discovery_receive_host_designated(discovery_session_t *session, const void *data, size_t len) {
  ASCII_OTEL_SCOPE(DISCOVERY_HOST_RECONCILE);
  ASCII_OTEL_ATTR_STRING("host.update.source", "designation");
  ASCII_OTEL_ATTR_STRING("host.update.outcome", "malformed");
  if (!data || len < sizeof(acip_host_designated_t))
    return;
  const acip_host_designated_t *host = data;
  uint16_t quic_port = 0;
  char webtransport_url[512];
  asciichat_errno_scope_t metadata_scope = errno_scope_begin();
  asciichat_error_t metadata_result =
      acds_transport_metadata_decode((const uint8_t *)data + sizeof(*host), len - sizeof(*host), &quic_port,
                                     webtransport_url, sizeof(webtransport_url));
  errno_scope_end(metadata_scope, ASCIICHAT_ERRNO_DISMISSED);
  if (metadata_result != ASCIICHAT_OK)
    return;
  uint32_t generation = NET_TO_HOST_U32(host->generation);
  ASCII_OTEL_ATTR_INT("host.generation", generation);
  ASCII_OTEL_ATTR_INT("host.decision.sequence", NET_TO_HOST_U32(host->sequence));
  if (!discovery_host_designation_is_current(session, host, generation)) {
    ASCII_OTEL_ATTR_STRING("host.update.outcome", "ignored");
    ascii_otel_metric(ASCII_OTEL_METRIC_HOST_DESIGNATIONS_IGNORED, 1);
    return;
  }
  ASCII_OTEL_ATTR_STRING("host.update.outcome", "accepted");
  bool changed = memcmp(session->host_id, host->host_id, SESSION_ID_SIZE) != 0;
  ASCII_OTEL_ATTR_INT("host.update.changed", changed);
  ASCII_OTEL_ATTR_STRING("host.update.role",
                         !memcmp(host->host_id, session->participant_id, SESSION_ID_SIZE) ? "host" : "participant");
  bool active = session->state == DISCOVERY_STATE_ACTIVE;
  bool connecting = session->state == DISCOVERY_STATE_CONNECTING_HOST;
  discovery_record_host_decision(session, generation, NET_TO_HOST_U32(host->sequence));
  session->handoff_generation = generation;
  memcpy(session->host_id, host->host_id, SESSION_ID_SIZE);
  if (changed)
    session->host_address[0] = 0;
  if (host->host_address[0])
    SAFE_STRNCPY(session->host_address, host->host_address, sizeof(session->host_address));
  session->host_port = GET_OPTION(discovery_quic) && quic_port ? quic_port : host->host_port;
  if (changed && active)
    NOTICE_ANNOUNCE("Session host changed", "The session has a new host.");
  if ((changed && (active || connecting) && discovery_host_endpoint_ready(session, host)) ||
      (session->state == DISCOVERY_STATE_MIGRATING && session->session_type != SESSION_TYPE_WEBRTC &&
       host->host_address[0])) {
    // Worker teardown remains deferred to the existing role dispatcher.
    session->handoff_pending = true;
    set_state(session, DISCOVERY_STATE_MIGRATING);
  } else if (session->state == DISCOVERY_STATE_NEGOTIATING && host->host_address[0]) {
    session->is_host = !memcmp(host->host_id, session->participant_id, SESSION_ID_SIZE);
    set_state(session, session->is_host ? DISCOVERY_STATE_STARTING_HOST : DISCOVERY_STATE_CONNECTING_HOST);
  }
}

static void handle_acds_webrtc_packet(discovery_session_t *session, packet_type_t type, void *data, size_t len) {
  if ((unsigned)type == CONSENSUS_HANDOFF_PACKET) {
    discovery_handoff_receive(session, data, len);
    return;
  }
  switch (type) {
  case PACKET_TYPE_ACIP_HOST_DESIGNATED:
    discovery_receive_host_designated(session, data, len);
    break;
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
  ASCII_OTEL_SCOPE(DISCOVERY_JOIN);
  log_info("participant_ctx=%p", session->participant_ctx);

  if (!session)
    return ERROR_INVALID_PARAM;

  set_state(session, DISCOVERY_STATE_JOINING_SESSION);
  log_info("Joining session: %s (participant_ctx still=%p)", session->session_string, session->participant_ctx);

  acds_protocol_t protocol;
  asciichat_error_t result;
  for (;;) {
    acds_protocol_init(&protocol, discovery_protocol_send, session->acds_transport);
    acds_session_join_params_t params = {.session_string = session->session_string,
                                         .has_password = session->password[0] != 0};
    memcpy(params.identity_pubkey, session->identity_pubkey, sizeof(params.identity_pubkey));
    memcpy(params.identity_seckey, session->identity_seckey, sizeof(params.identity_seckey));
    SAFE_STRNCPY(params.password, session->password, sizeof(params.password));
    result = acds_protocol_join(&protocol, &params);
    sodium_memzero(&params, sizeof(params));
    if (result == ASCIICHAT_OK)
      result = discovery_protocol_receive(session, &protocol);
    if (result != ERROR_INVALID_PASSWORD)
      break;
    acds_protocol_destroy(&protocol);
    bool incorrect = session->password[0] != 0;
    CLEAR_ERRNO();
    result = ui_input_password(session->session_string, session->password, sizeof(session->password), incorrect);
    if (result != ASCIICHAT_OK) {
      if (!shutdown_is_requested())
        set_error(session, result, "Password required to join session");
      return result;
    }
  }
  if (result != ASCIICHAT_OK) {
    acds_protocol_destroy(&protocol);
    return result;
  }
  uint16_t advertised_quic_port = protocol.quic_port;
  acip_session_joined_t joined_value = protocol.joined;
  acip_session_joined_t *joined = &joined_value;
  void *alloc_buffer = NULL;
  acds_protocol_destroy(&protocol);

  // Store session info
  memcpy(session->session_id, joined->session_id, SESSION_ID_SIZE);
  memcpy(session->participant_id, joined->participant_id, SESSION_ID_SIZE);

  memcpy(session->host_id, joined->host_established ? joined->host_id : joined->initiator_id, SESSION_ID_SIZE);
  session->is_host = memcmp(session->host_id, session->participant_id, sizeof(session->host_id)) == 0;

  // Store session type and WebRTC credentials (if WebRTC)
  session->session_type = joined->session_type;
  if (GET_OPTION(no_webrtc) || GET_OPTION(discovery_quic)) {
    if (!session->is_host && (!joined->server_address[0] || joined->server_port == 0)) {
      const char *message = GET_OPTION(discovery_quic)
                                ? "Direct QUIC requires a disclosed host endpoint"
                                : "Direct TCP requires a disclosed host endpoint; WebRTC is disabled";
      set_error(session, ERROR_NETWORK, message);
      buffer_release(alloc_buffer);
      return SET_ERRNO(ERROR_NETWORK, "%s", message);
    }
    session->session_type = SESSION_TYPE_DIRECT_TCP;
    if (GET_OPTION(discovery_quic))
      log_info("Discovery connection route: direct QUIC (--discovery-quic)");
    else
      log_info("Discovery connection route: direct TCP (--no-webrtc)");
  }
  if (session->session_type == SESSION_TYPE_WEBRTC) {
    // WebRTC signaling addresses peers by participant ID, so a private server
    // address must not send the client through TCP/NAT host election. The
    // session initiator is the host for server-created sessions and is already
    // available even when IP details are withheld.
    session->is_host = memcmp(session->host_id, session->participant_id, sizeof(session->host_id)) == 0;
  }
  if (GET_OPTION(webrtc_relay_only) && session->session_type != SESSION_TYPE_WEBRTC) {
    buffer_release(alloc_buffer);
    return SET_ERRNO(ERROR_INVALID_STATE, "Relay only requires a WebRTC session");
  }
  if (session->session_type == SESSION_TYPE_WEBRTC) {
    SAFE_STRNCPY(session->turn_username, joined->turn_username, sizeof(session->turn_username));
    SAFE_STRNCPY(session->turn_password, joined->turn_password, sizeof(session->turn_password));
    log_info("WebRTC session detected - TURN username: %s", session->turn_username);

    // Initialize WebRTC peer manager for WebRTC sessions (NEW)
    asciichat_error_t webrtc_result = initialize_webrtc_peer_manager(session);
    if (webrtc_result != ASCIICHAT_OK) {
      log_error("Failed to initialize WebRTC peer manager: %d", webrtc_result);
      buffer_release(alloc_buffer);
      return webrtc_result;
    }
    log_info("WebRTC peer manager initialized successfully");
  }

  // The initiator also joins its own session. Start its listeners instead of
  // treating its newly advertised endpoint as an already running remote host.
  if (session->is_host) {
    buffer_release(alloc_buffer);
    set_state(session, DISCOVERY_STATE_STARTING_HOST);
    return ASCIICHAT_OK;
  }

  // Check if host is already established
  if (session->session_type != SESSION_TYPE_WEBRTC && joined->server_address[0] && joined->server_port > 0) {
    // Host exists - connect directly
    SAFE_STRNCPY(session->host_address, joined->server_address, sizeof(session->host_address));
    session->host_port =
        GET_OPTION(discovery_quic) && advertised_quic_port ? advertised_quic_port : joined->server_port;
    session->is_host = false;

    log_info("Host already established: %s:%u (session_type=%u, participant_ctx=%p before transition)",
             session->host_address, session->host_port, session->session_type, session->participant_ctx);
    buffer_release(alloc_buffer);

    log_info("About to transition to CONNECTING_HOST - participant_ctx=%p", session->participant_ctx);
    set_state(session, DISCOVERY_STATE_CONNECTING_HOST);
    log_info("Transitioned to CONNECTING_HOST - participant_ctx=%p (returning ASCIICHAT_OK)", session->participant_ctx);
    return ASCIICHAT_OK;
  }

  // WebRTC peers connect through ACDS signaling and never need the host's IP
  // address. Use the host participant ID returned above when the address is
  // intentionally hidden.
  if (session->session_type == SESSION_TYPE_WEBRTC && session->host_id[0]) {
    if (session->is_host) {
      log_info("WebRTC participant is the session host; starting host transport");
      set_state(session, DISCOVERY_STATE_STARTING_HOST);
    } else {
      log_info("WebRTC host identified by participant ID; connecting without revealing its IP");
      set_state(session, DISCOVERY_STATE_CONNECTING_HOST);
    }
    buffer_release(alloc_buffer);
    return ASCIICHAT_OK;
  }

  // No host yet - need to negotiate
  log_info("No host established (server_address='%s' port=%u), starting negotiation...", joined->server_address,
           joined->server_port);
  buffer_release(alloc_buffer);

  set_state(session, DISCOVERY_STATE_NEGOTIATING);
  return ASCIICHAT_OK;
}

asciichat_error_t discovery_session_start(discovery_session_t *session) {
  log_info("session=%p", session);

  if (!session) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session is NULL");
  }

  log_info("is_initiator=%d, session_string='%s'", session->is_initiator, session->session_string);

  // Check if we should exit before starting blocking operations
  if (session->should_exit_callback && session->should_exit_callback(session->exit_callback_data)) {
    log_info("Exiting early due to should_exit_callback (before ACDS connect)");
    return ASCIICHAT_OK; // Return success to allow clean shutdown
  }

  log_info("Calling connect_to_acds...");
  // Connect to ACDS
  asciichat_error_t result = connect_to_acds(session);
  if (result != ASCIICHAT_OK) {
    log_error("connect_to_acds FAILED %s", asciichat_error_format(NULL, result));
    set_state(session, DISCOVERY_STATE_FAILED);
    return result;
  }
  log_info("connect_to_acds succeeded");

  // Check again after connection
  if (session->should_exit_callback && session->should_exit_callback(session->exit_callback_data)) {
    log_info("Exiting early due to should_exit_callback (after ACDS connect)");
    return ASCIICHAT_OK; // Return success to allow clean shutdown
  }

  session->acds_transport->receive_timeout_ns = 10 * NS_PER_SEC_INT;

  // Perform crypto handshake with ACDS (same as client-server handshake)
  log_info("Performing crypto handshake with ACDS...");
  crypto_handshake_context_t *handshake_ctx = network_connection_handshake(session->acds_connection);

  result = crypto_handshake_init("discovery_client", handshake_ctx, false);
  if (result != ASCIICHAT_OK) {
    log_error("Handshake init failed - %s", asciichat_error_format(NULL, result));
    set_error(session, result, "Crypto handshake init failed");
    return result;
  }

  bool has_identity = memcmp(session->identity_pubkey, (uint8_t[32]){0}, ED25519_PUBLIC_KEY_SIZE) != 0;
  // Store identity key in handshake context
  handshake_ctx->client_public_key.type = has_identity ? KEY_TYPE_ED25519 : KEY_TYPE_UNKNOWN;
  handshake_ctx->client_private_key.type = handshake_ctx->client_public_key.type;
  memcpy(handshake_ctx->client_public_key.key, session->identity_pubkey, ED25519_PUBLIC_KEY_SIZE);
  memcpy(handshake_ctx->client_private_key.key.ed25519, session->identity_seckey, ED25519_PRIVATE_KEY_SIZE);
  memcpy(handshake_ctx->client_private_key.public_key, session->identity_pubkey, sizeof(session->identity_pubkey));

  crypto_policy_t signaling_policy = {
      .transport_secure = strncmp(session->acds_url, "wss://", 6) == 0,
      .has_identity = has_identity,
  };
  uint8_t mode;
  result = crypto_policy_resolve(&signaling_policy, &mode);
  if (result == ASCIICHAT_OK)
    result = network_client_negotiate(session->acds_transport, handshake_ctx, mode, NETWORK_PROTOCOL_DISCOVERY);
  if (result != ASCIICHAT_OK) {
    set_error(session, result, "Discovery handshake failed");
    return result;
  }

  // Check again after handshake
  if (session->should_exit_callback && session->should_exit_callback(session->exit_callback_data)) {
    log_info("Exiting early due to should_exit_callback (after handshake)");
    return ASCIICHAT_OK; // Return success to allow clean shutdown
  }

  // Create or join session
  if (session->is_initiator) {
    log_info("Calling create_session - participant_ctx before=%p", session->participant_ctx);
    result = create_session(session);
    log_info("create_session returned - participant_ctx after=%p, result=%d", session->participant_ctx, result);

    if (result == ASCIICHAT_OK) {
      // Initiator also needs to register itself as a participant so it can receive PARTICIPANT_JOINED notifications
      // when other peers join. This makes the initiator discoverable to other participants.
      log_info("Initiator: now registering self as participant in created session");
      result = join_session(session);
      log_info("Initiator: join_session result=%d", result);
    }
    return result;
  } else {
    log_info("Calling join_session - participant_ctx before=%p", session->participant_ctx);
    asciichat_error_t result = join_session(session);
    log_info("join_session returned - participant_ctx after=%p, state=%d, result=%d", session->participant_ctx,
             session->state, result);
    return result;
  }
}

static asciichat_error_t discovery_session_send_keepalive(discovery_session_t *session) {
  // Media traffic uses a separate transport; keep the signaling connection alive.
  if (session->acds_transport && acip_transport_is_connected(session->acds_transport)) {
    uint64_t now_ns = time_get_ns();
    if (now_ns - session->last_acds_keepalive_ns >= 5 * NS_PER_SEC_INT) {
      asciichat_error_t ping_result = acip_send_ping(session->acds_transport);
      if (ping_result != ASCIICHAT_OK)
        return ping_result;
      session->last_acds_keepalive_ns = now_ns;
    }
  }

  return ASCIICHAT_OK;
}

static asciichat_error_t discovery_session_wait_for_peer(discovery_session_t *session, int64_t timeout_ns) {
  // Wait for PARTICIPANT_JOINED notification from ACDS
  // Note: don't use a hard timeout here - let the outer process timeout (snapshot delay) control max wait
  // The discovery_run() loop will keep calling this until session becomes active or process exits
  uint64_t now_ns = time_get_ns();
  uint64_t wait_duration = now_ns - session->waiting_peer_start_ns;

  log_debug("★ WAITING_PEER: duration=%.3fs (no timeout, waiting for peer...)", wait_duration / 1e9);

  if (session->acds_transport && timeout_ns > 0) {
    // Do not set socket timeout - let receive_packet_secure use its own timeout
    // Setting a short socket timeout conflicts with its hardcoded 1-second timeout
    packet_type_t type;
    void *data = NULL;
    size_t len = 0;
    void *alloc_buffer = NULL;

    // Try to receive a packet
    asciichat_error_t recv_result = acds_receive_packet(session->acds_transport, &type, &data, &len, &alloc_buffer);
    if (recv_result == ASCIICHAT_OK) {
      if (type == PACKET_TYPE_ACIP_PARTICIPANT_JOINED) {
        log_info("Peer joined session, transitioning to negotiation");
        set_state(session, DISCOVERY_STATE_NEGOTIATING);
        buffer_release(alloc_buffer);
      } else {
        // Got some other packet, handle it or ignore
        log_debug("Received packet type %d while waiting for peer", type);
        buffer_release(alloc_buffer);
      }
    } else if (recv_result == ERROR_NETWORK_TIMEOUT) {
      // Timeout is normal, just return
    } else {
      log_debug("Packet receive error while waiting for peer %s", asciichat_error_format(NULL, recv_result));
    }
  } else {
    // Fallback to sleep if no timeout or invalid transport
    platform_sleep_us(timeout_ns / 1000);
  }
  return ASCIICHAT_OK;
}

/** @brief Bootstrap the initial TCP role and start asynchronous NAT hint detection. */
static void discovery_session_begin_negotiation(discovery_session_t *session) {
  // Initialize negotiation
  negotiate_init(&session->negotiate, session->session_id, session->participant_id, session->is_initiator);
  log_info("Starting NAT negotiation (initiator=%d)", session->is_initiator);

  start_nat_detection(session);

  // Bootstrap the initial TCP host. Subsequent host recovery is decided by ACDS.
  session->negotiate.our_quality.detection_complete = true;
  session->negotiate.peer_quality_received = true;
  memset(&session->negotiate.peer_quality, 0, sizeof(session->negotiate.peer_quality));
  // Mark peer quality as detected but with worst NAT type so we win host election
  session->negotiate.peer_quality.detection_complete = true;
  session->negotiate.peer_quality.nat_type = ACIP_NAT_TYPE_SYMMETRIC;
}

static void discovery_session_receive_host_designation(discovery_session_t *session) {
  if (session->acds_transport && !session->is_initiator && !session->host_address[0]) {
    packet_type_t type;
    void *data = NULL;
    size_t len = 0;
    void *alloc_buffer = NULL;

    asciichat_error_t recv_result = acds_receive_packet(session->acds_transport, &type, &data, &len, &alloc_buffer);
    if (recv_result == ASCIICHAT_OK) {
      handle_acds_webrtc_packet(session, type, data, len);
      buffer_release(alloc_buffer);
    }
  }
}

static asciichat_error_t discovery_session_select_host(discovery_session_t *session) {
  // Determine host role (now that peer_quality is set)
  if (session->negotiate.peer_quality_received && session->negotiate.state != NEGOTIATE_STATE_COMPARING) {
    log_info("Determining host role based on NAT qualities...");
    ASCII_OTEL_SCOPE(DISCOVERY_HOST_SELECTION);
    ASCII_OTEL_ATTR_STRING("host.selection.phase", "initial");
    asciichat_error_t result = negotiate_determine_result(&session->negotiate);
    ASCII_OTEL_ATTR_INT("host.selection.error_code", result);
    ASCII_OTEL_ATTR_STRING("host.selection.outcome", result == ASCIICHAT_OK ? "selected" : "failed");
    if (result == ASCIICHAT_OK) {
      ASCII_OTEL_ATTR_STRING("host.selection.role", session->negotiate.we_are_host ? "host" : "participant");
      ASCII_OTEL_ATTR_INT("host.selection.local_nat_tier", nat_compute_tier(&session->negotiate.our_quality));
      ASCII_OTEL_ATTR_INT("host.selection.peer_nat_tier", nat_compute_tier(&session->negotiate.peer_quality));
      ascii_otel_metric(ASCII_OTEL_METRIC_HOST_SELECTIONS, 1);
      session->is_host = session->negotiate.we_are_host;
      stats_runtime_connection_state(session->is_host ? "discovery host" : "discovery participant");

      if (session->is_host) {
        // We become the host
        log_info("🏠 NAT negotiation complete: WE ARE HOST (tier:%d vs tier:%d)",
                 nat_compute_tier(&session->negotiate.our_quality), nat_compute_tier(&session->negotiate.peer_quality));
        memcpy(session->host_id, session->participant_id, sizeof(session->host_id));
        SAFE_STRNCPY(session->host_address, session->negotiate.host_address, sizeof(session->host_address));
        session->host_port = session->negotiate.host_port;

        // Fallback to localhost if NAT detection failed to determine address
        if (!session->host_address[0]) {
          log_warn("NAT address empty, using localhost fallback");
          SAFE_STRNCPY(session->host_address, "127.0.0.1", sizeof(session->host_address));
        }

        set_state(session, DISCOVERY_STATE_STARTING_HOST);
      } else {
        // They are host (shouldn't happen in solo mode, but handle it)
        log_info("👤 NAT negotiation complete: THEY ARE HOST at %s:%u (tier:%d vs tier:%d)",
                 session->negotiate.host_address, session->negotiate.host_port,
                 nat_compute_tier(&session->negotiate.our_quality), nat_compute_tier(&session->negotiate.peer_quality));
        SAFE_STRNCPY(session->host_address, session->negotiate.host_address, sizeof(session->host_address));
        session->host_port = session->negotiate.host_port;

        // If host address is empty, wait for HOST_DESIGNATED from ACDS instead of using fallback
        if (!session->host_address[0]) {
          log_info("Waiting for HOST_DESIGNATED from ACDS to discover host address...");
          // Stay in NEGOTIATING state to continue waiting for HOST_DESIGNATED
          return ASCIICHAT_OK;
        }

        set_state(session, DISCOVERY_STATE_CONNECTING_HOST);
      }
    } else {
      log_error("Failed to determine negotiation result %s", asciichat_error_format(NULL, result));
      set_error(session, result, "Host election failed");
    }
  }
  return ASCIICHAT_OK;
}

static asciichat_error_t discovery_session_negotiate(discovery_session_t *session) {
  if (session->negotiate.state == NEGOTIATE_STATE_INIT) {
    discovery_session_begin_negotiation(session);
  }
  discovery_session_receive_host_designation(session);
  if (session->state != DISCOVERY_STATE_NEGOTIATING)
    return ASCIICHAT_OK;
  return discovery_session_select_host(session);
}

static asciichat_error_t discovery_session_start_host(discovery_session_t *session) {
  ASCII_OTEL_SCOPE(DISCOVERY_START_HOST);
  // Start hosting
  if (!session->host_ctx) {
    // Create host context with configured port
    int host_port = GET_OPTION(port);

    session_host_config_t hconfig = {
        .port = host_port,
        .ipv4_address = "0.0.0.0",
        .max_clients = MAX_SESSION_PARTICIPANTS,
        .encryption_enabled = true,
        .configured_quic_listeners = true,
        .password = session->password,
        .key_path = GET_OPTION(encrypt_key),
    };

    session->host_ctx = session_host_create(&hconfig);
    if (!session->host_ctx) {
      log_error("Failed to create host context");
      set_error(session, ERROR_MEMORY, "Failed to create host context");
      return ASCIICHAT_OK;
    }

    // Start listening
    asciichat_error_t hstart = session_host_start(session->host_ctx);
    if (hstart != ASCIICHAT_OK) {
      log_error("Failed to start host: %d", hstart);
      set_error(session, hstart, "Failed to start host");
      return ASCIICHAT_OK;
    }

    log_info("Host started, listening for connections");
  }

  // Notify listeners that we're ready
  if (session->on_session_ready) {
    session->on_session_ready(session->session_string, session->callback_user_data);
  }

  // Send HOST_ANNOUNCEMENT to ACDS so participants can discover us
  int host_port = GET_OPTION(port);
  acip_host_announcement_t announcement = {0};
  memcpy(announcement.session_id, session->session_id, sizeof(announcement.session_id));
  memcpy(announcement.host_id, session->host_id, sizeof(announcement.host_id));
  SAFE_STRNCPY(announcement.host_address, session->host_address, sizeof(announcement.host_address));
  announcement.host_port = host_port;
  announcement.connection_type = ACIP_CONNECTION_TYPE_DIRECT_PUBLIC;
  announcement.generation = HOST_TO_NET_U32(session->handoff_generation);
  announcement.sequence = HOST_TO_NET_U32(session->host_authority_sequence);

  if (session->acds_transport) {
    uint8_t payload[sizeof(announcement) + ACDS_TRANSPORT_METADATA_MAX];
    size_t metadata_length = 0;
    uint16_t quic_port = (GET_OPTION(quic_bind_address)[0] ||
                          (GET_OPTION(webtransport_bind_address)[0] || GET_OPTION(webtransport_origin)[0]))
                             ? (uint16_t)(GET_OPTION(quic_port) ? GET_OPTION(quic_port) : host_port)
                             : 0;
    const char *url = (GET_OPTION(webtransport_bind_address)[0] || GET_OPTION(webtransport_origin)[0])
                          ? GET_OPTION(webtransport_public_url)
                          : "";
    asciichat_error_t sent = ASCIICHAT_OK;
    if (quic_port || *url)
      sent = acds_transport_metadata_encode(quic_port, url, payload + sizeof(announcement), ACDS_TRANSPORT_METADATA_MAX,
                                            &metadata_length);
    if (sent != ASCIICHAT_OK)
      return sent;
    memcpy(payload, &announcement, sizeof(announcement));
    sent = packet_send_via_transport(session->acds_transport, PACKET_TYPE_ACIP_HOST_ANNOUNCEMENT, payload,
                                     sizeof(announcement) + metadata_length, 0);
    if (sent != ASCIICHAT_OK)
      return sent;
    log_info("Sent HOST_ANNOUNCEMENT to ACDS at %s:%u", announcement.host_address, announcement.host_port);
  }

  set_state(session, DISCOVERY_STATE_ACTIVE);
  return ASCIICHAT_OK;
}

static asciichat_error_t discovery_session_retry_webrtc(discovery_session_t *session, int timeout_ms) {
  int max_attempts = GET_OPTION(webrtc_reconnect_attempts);
  if (session->webrtc_retry_attempt >= max_attempts) {
    log_error("WebRTC connection failed after %d attempts (timeout: %dms)", max_attempts + 1, timeout_ms);
    char error_msg[DISCOVERY_SESSION_ERROR_MSG_BUFFER_SIZE];
    snprintf(error_msg, sizeof(error_msg), "WebRTC connection failed after %d attempts (%dms timeout)",
             max_attempts + 1, timeout_ms);
    set_error(session, ERROR_NETWORK_TIMEOUT, error_msg);
    return SET_ERRNO(ERROR_NETWORK_TIMEOUT, "%s", error_msg);
  }

  uint32_t backoff_ms = calculate_backoff_delay_ms(session->webrtc_retry_attempt);
  log_warn("WebRTC connection attempt %d/%d failed, retrying in %ums...", session->webrtc_retry_attempt + 1,
           max_attempts + 1, backoff_ms);
  APP_CALLBACK_VOID(platform_pump_events);
  APP_CALLBACK_VOID_UINT(platform_sleep_ms, backoff_ms);

  webrtc_peer_manager_destroy(session->peer_manager);
  session->peer_manager = NULL;
  asciichat_error_t result = initialize_webrtc_peer_manager(session);
  if (result != ASCIICHAT_OK) {
    log_error("Failed to re-initialize WebRTC for retry: %d", result);
    set_error(session, result, "Failed to re-initialize WebRTC for retry");
    return result;
  }

  session->webrtc_connection_initiated = false;
  session->webrtc_retry_attempt++;
  log_info("WebRTC peer manager re-initialized for attempt %d/%d", session->webrtc_retry_attempt + 1, max_attempts + 1);
  return ASCIICHAT_OK;
}

/**
 * @brief Dispatch signaling until the host DataChannel opens or bounded retries fail.
 * Each attempt has an end-to-end deadline, including the time after ICE gathering
 * completes; configured retry limits determine when the connection becomes terminal.
 */
static asciichat_error_t discovery_session_connect_webrtc(discovery_session_t *session) {
  // Check if transport is ready (DataChannel opened)
  if (session->webrtc_transport_ready) {
    log_info("WebRTC DataChannel established, transitioning to ACTIVE state");
    set_state(session, DISCOVERY_STATE_ACTIVE);
    return ASCIICHAT_OK;
  }

  // If not ready and we haven't initiated connection yet, initiate it
  if (session->peer_manager && !session->webrtc_connection_initiated) {
    log_info("Initiating WebRTC connection to host (attempt %d/%d)...", session->webrtc_retry_attempt + 1,
             GET_OPTION(webrtc_reconnect_attempts) + 1);

    // Initiate connection (generates offer, triggers SDP exchange)
    asciichat_error_t conn_result =
        webrtc_peer_manager_connect(session->peer_manager, session->session_id, session->host_id);

    if (conn_result != ASCIICHAT_OK) {
      log_error("Failed to initiate WebRTC connection: %d", conn_result);
      set_error(session, conn_result, "Failed to initiate WebRTC connection");
      return ASCIICHAT_OK;
    }

    log_info("WebRTC connection initiated, waiting for DataChannel...");
    session->webrtc_connection_initiated = true;
    session->webrtc_last_attempt_time_ms = APP_CALLBACK_UINT64(platform_get_monotonic_time_us) / 1000;
  }

  // Check for ICE gathering timeout on all peers
  if (session->webrtc_connection_initiated && session->peer_manager) {
    int timeout_ms = GET_OPTION(webrtc_ice_timeout_ms);
    int timed_out_count = webrtc_peer_manager_check_gathering_timeouts(session->peer_manager, timeout_ms);

    bool attempt_expired = session_get_current_time_ms() - session->webrtc_last_attempt_time_ms >= (uint64_t)timeout_ms;
    if (timed_out_count > 0 || attempt_expired) {
      log_error("WebRTC negotiation failed: %d peer(s) failed or exceeded the %dms gathering timeout", timed_out_count,
                timeout_ms);

      asciichat_error_t retry_result = discovery_session_retry_webrtc(session, timeout_ms);
      if (retry_result != ASCIICHAT_OK) {
        if (GET_OPTION(prefer_webrtc) || GET_OPTION(webrtc_relay_only)) {
          set_state(session, DISCOVERY_STATE_FAILED);
          return SET_ERRNO(ERROR_NETWORK_TIMEOUT, "WebRTC connection failed and direct fallback is disabled");
        }
        return ASCIICHAT_OK;
      }
    }
  }

  // Poll ACDS for WebRTC signaling messages (SDP answer, ICE candidates)
  // while waiting for DataChannel to open
  if (session->acds_transport && acip_transport_has_pending_data(session->acds_transport)) {
    packet_type_t type;
    void *data = NULL;
    size_t len = 0;
    void *alloc_buffer = NULL;

    asciichat_error_t recv_result = acds_receive_packet(session->acds_transport, &type, &data, &len, &alloc_buffer);

    if (recv_result == ASCIICHAT_OK) {
      // Dispatch to appropriate handler
      handle_acds_webrtc_packet(session, type, data, len);
      buffer_release(alloc_buffer);
    } else if (!acip_transport_is_connected(session->acds_transport)) {
      set_error(session, recv_result, "Discovery signaling connection closed during WebRTC negotiation");
      set_state(session, DISCOVERY_STATE_FAILED);
      return recv_result;
    } else {
      log_debug("Failed to receive ACDS packet: %d", recv_result);
    }
  }

  // Stay in CONNECTING_HOST until DataChannel opens (webrtc_transport_ready becomes true)
  // The discovery_on_transport_ready callback will set webrtc_transport_ready=true
  return ASCIICHAT_OK;
}

static asciichat_error_t discovery_session_connect_tcp(discovery_session_t *session) {
  ASCII_OTEL_SCOPE(CONNECTION_ATTEMPT);
  if (!session->participant_ctx) {
    char quic_address[ACIP_ADDRESS_SIZE + 16];
    const char *address = session->host_address;
    if (GET_OPTION(discovery_quic)) {
      if (strchr(address, ':'))
        SAFE_SNPRINTF(quic_address, sizeof(quic_address), "quic://[%s]", address);
      else
        SAFE_SNPRINTF(quic_address, sizeof(quic_address), "quic://%s", address);
      address = quic_address;
    }
    session_participant_config_t config = {.address = address,
                                           .port = session->host_port,
                                           .enable_audio = true,
                                           .enable_video = true,
                                           .encryption_enabled = true,
                                           .password = session->password,
                                           .server_key = GET_OPTION(server_key),
                                           .tls_ca_file = GET_OPTION(tls_ca_file),
                                           .quic_audio_datagrams = GET_OPTION(quic_audio_datagrams),
                                           .quic_visual_datagrams = GET_OPTION(quic_visual_datagrams)};
    session->participant_ctx = session_participant_create(&config);
    if (!session->participant_ctx)
      return SET_ERRNO(ERROR_MEMORY, "Cannot allocate discovery participant");
  }
  asciichat_error_t result = session_participant_connect(session->participant_ctx);
  if (result != ASCIICHAT_OK) {
    set_error(session, result, "Cannot establish authenticated host connection");
    return result;
  }
  set_state(session, DISCOVERY_STATE_ACTIVE);
  return ASCIICHAT_OK;
}

/**
 * @brief Dispatch ACDS actions, then check liveness only while the session stays active.
 * Terminal failures and deferred commitments return before any host-loss handling,
 * preventing liveness checks from reviving a failed session or replacing a commitment.
 */
static asciichat_error_t discovery_session_process_active(discovery_session_t *session, int64_t timeout_ns) {
  // All session types dispatch ACDS recovery decisions without a blocking receive.

  if (session->acds_transport && acip_transport_has_pending_data(session->acds_transport)) {
    packet_type_t type;
    void *data = NULL;
    size_t len = 0;
    void *alloc_buffer = NULL;

    asciichat_error_t recv_result = acds_receive_packet(session->acds_transport, &type, &data, &len, &alloc_buffer);

    if (recv_result == ASCIICHAT_OK) {
      // Dispatch to appropriate handler
      handle_acds_webrtc_packet(session, type, data, len);
      buffer_release(alloc_buffer);
    }
  }

  if (session->handoff_pending || session->state != DISCOVERY_STATE_ACTIVE)
    return ASCIICHAT_OK;

  if (session->session_type == SESSION_TYPE_WEBRTC && !session->is_host && session->peer_manager) {
    webrtc_state_t peer_state = WEBRTC_STATE_CLOSED;
    bool peer_found = webrtc_peer_manager_get_state(session->peer_manager, session->host_id, &peer_state);
    uint64_t now_ms = session_get_current_time_ms();
    bool disconnected_too_long = false;

    if (peer_found && peer_state == WEBRTC_STATE_DISCONNECTED) {
      if (session->webrtc_disconnected_since_ms == 0) {
        session->webrtc_disconnected_since_ms = now_ms;
      } else {
        disconnected_too_long = now_ms - session->webrtc_disconnected_since_ms >= 5000;
      }
    } else {
      session->webrtc_disconnected_since_ms = 0;
    }

    bool peer_failed = !peer_found || peer_state == WEBRTC_STATE_FAILED || peer_state == WEBRTC_STATE_CLOSED;
    if (session->webrtc_transport_ready && (peer_failed || disconnected_too_long)) {
      // Media workers still borrow this transport. The role dispatcher joins them
      // before applying the elected successor and releasing the old participant.
      return discovery_session_handle_host_disconnect(session, 1);
    }
  }

  // Session is active - check for host disconnect
  if (!session->is_host) {
    // We are a participant - check if host is still alive
    asciichat_error_t host_check = discovery_session_check_host_alive(session);
    if (host_check != ASCIICHAT_OK) {
      // Report host loss and await the measured ACDS recovery decision.
      log_warn("Host disconnect detected during ACTIVE state");

      // If --prefer-webrtc is set, WebRTC failure is fatal (no TCP fallback)
      const options_t *opts = options_get();
      if (opts && (opts->prefer_webrtc || opts->webrtc_relay_only)) {
        set_state(session, DISCOVERY_STATE_FAILED);
        session->error = ERROR_NETWORK;
        return SET_ERRNO(ERROR_NETWORK, "WebRTC connection failed");
      }

      set_state(session, DISCOVERY_STATE_MIGRATING);
      // Reason code: 1 = timeout/disconnect detected during ACTIVE session
      discovery_session_handle_host_disconnect(session, 1);
    }
  }
  platform_sleep_us(timeout_ns / 1000);
  return ASCIICHAT_OK;
}

/**
 * @brief Continue signaling while awaiting ACDS commitment or terminal failure.
 * A 30-second local deadline also bounds recovery when the signaling decision is lost.
 */
static asciichat_error_t discovery_session_process_migration(discovery_session_t *session, int64_t timeout_ns) {
  if (session->acds_transport && acip_transport_has_pending_data(session->acds_transport)) {
    packet_type_t type;
    void *data = NULL, *allocated = NULL;
    size_t length = 0;
    asciichat_error_t result = packet_receive_via_transport(session->acds_transport, &type, &data, &length, &allocated);
    if (result == ASCIICHAT_OK)
      handle_acds_webrtc_packet(session, type, data, length);
    buffer_release(allocated);
  }
  if (session->handoff_pending || session->state == DISCOVERY_STATE_FAILED)
    return ASCIICHAT_OK;
  // ACDS owns successor selection; this deadline also bounds a lost signaling decision.
  if (session_get_current_time_ms() - session->host_loss_detected_ms > 30 * MS_PER_SEC_INT) {
    set_error(session, ERROR_NETWORK_TIMEOUT, "Timed out waiting for ACDS host recovery");
  }
  platform_sleep_us(timeout_ns / 1000);
  return ASCIICHAT_OK;
}

asciichat_error_t discovery_session_process(discovery_session_t *session, int64_t timeout_ns) {
  if (!session) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session is NULL");
  }

  // Transport callbacks publish readiness; ownership changes and worker joins
  // execute here on the session's controlling thread.
  asciichat_error_t result = webrtc_peer_manager_dispatch_ready(session->peer_manager);
  if (result != ASCIICHAT_OK)
    return result;

  result = discovery_session_send_keepalive(session);
  if (result != ASCIICHAT_OK) {
    return result;
  }

  if (session->state == DISCOVERY_STATE_ACTIVE || session->state == DISCOVERY_STATE_CONNECTING_HOST ||
      session->state == DISCOVERY_STATE_MIGRATING) {
    result = discovery_handoff_sample(session);
    if (result != ASCIICHAT_OK)
      return result;
  }

  // State handlers preserve deferred failures: recording a failed state does not
  // necessarily return an error until the next iteration processes that state.
  switch (session->state) {
  case DISCOVERY_STATE_WAITING_PEER:
    return discovery_session_wait_for_peer(session, timeout_ns);
  case DISCOVERY_STATE_NEGOTIATING:
    return discovery_session_negotiate(session);
  case DISCOVERY_STATE_STARTING_HOST:
    return discovery_session_start_host(session);
  case DISCOVERY_STATE_CONNECTING_HOST:
    return session->session_type == SESSION_TYPE_WEBRTC ? discovery_session_connect_webrtc(session)
                                                        : discovery_session_connect_tcp(session);
  case DISCOVERY_STATE_ACTIVE:
    return discovery_session_process_active(session, timeout_ns);
  case DISCOVERY_STATE_MIGRATING:
    return discovery_session_process_migration(session, timeout_ns);
  case DISCOVERY_STATE_FAILED:
    // Session has failed - return error on further processing
    return session->error != ASCIICHAT_OK ? session->error : ERROR_NETWORK;

  case DISCOVERY_STATE_ENDED:
    // Session has ended gracefully - return OK
    return ASCIICHAT_OK;

  case DISCOVERY_STATE_INIT:
  case DISCOVERY_STATE_CONNECTING_ACDS:
  case DISCOVERY_STATE_CREATING_SESSION:
  case DISCOVERY_STATE_JOINING_SESSION:
    // These states are handled during discovery_session_start(), not in the process loop
    // If we reach here, something went wrong - sleep and wait
    log_debug("State %d should be handled by discovery_session_start", session->state);
    platform_sleep_us(timeout_ns / 1000);
    break;

  default:
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid session state: %d", session->state);
  }

  return ASCIICHAT_OK;
}

void discovery_session_stop(discovery_session_t *session) {
  ASCII_OTEL_SCOPE(DISCOVERY_SHUTDOWN);
  if (!session) {
    SET_ERRNO(ERROR_INVALID_PARAM, "session is NULL");
    return;
  }

  log_info("Stopping discovery session...");

  // Send SESSION_LEAVE if connected
  if (session->acds_transport && session->session_id[0] != 0) {
    acip_session_leave_t leave_msg;
    memset(&leave_msg, 0, sizeof(leave_msg));
    memcpy(leave_msg.session_id, session->session_id, SESSION_ID_SIZE);
    memcpy(leave_msg.participant_id, session->participant_id, SESSION_ID_SIZE);

    packet_send_via_transport(session->acds_transport, PACKET_TYPE_ACIP_SESSION_LEAVE, &leave_msg, sizeof(leave_msg),
                              0);
  }

  set_state(session, DISCOVERY_STATE_ENDED);
}

discovery_state_t discovery_session_get_state(const discovery_session_t *session) {
  if (!session) {
    SET_ERRNO(ERROR_INVALID_PARAM, "null session");
    return DISCOVERY_STATE_FAILED;
  }
  return session->state;
}

bool discovery_session_is_active(const discovery_session_t *session) {
  if (!session) {
    SET_ERRNO(ERROR_INVALID_PARAM, "null session");
    return false;
  }
  return session->state == DISCOVERY_STATE_ACTIVE;
}

const char *discovery_session_get_string(const discovery_session_t *session) {
  if (!session || session->session_string[0] == '\0') {
    SET_ERRNO(ERROR_INVALID_PARAM, "null session or session string is empty");
    return NULL;
  }
  return session->session_string;
}

bool discovery_session_is_host(const discovery_session_t *session) {
  if (!session) {
    SET_ERRNO(ERROR_INVALID_PARAM, "null session");
    return false;
  }
  return session->is_host;
}

session_host_t *discovery_session_get_host(discovery_session_t *session) {
  if (!session) {
    SET_ERRNO(ERROR_INVALID_PARAM, "null session");
    return NULL;
  }
  return session->host_ctx;
}

session_participant_t *discovery_session_get_participant(discovery_session_t *session) {
  if (!session) {
    SET_ERRNO(ERROR_INVALID_PARAM, "null session");
    return NULL;
  }
  return session->participant_ctx;
}

void discovery_session_host_pong(discovery_session_t *session) {
  if (session)
    atomic_store_u64(&session->liveness.last_pong_received_ms, session_get_current_time_ms());
}

/**
 * @brief Check host transport availability and advance TCP ping/pong liveness.
 * Uses receiver-clock pong timestamps and bounded consecutive-failure tracking.
 * @return ASCIICHAT_OK for a live host, or a network error after loss is detected.
 */
asciichat_error_t discovery_session_check_host_alive(discovery_session_t *session) {
  if (!session || session->is_host) {
    // We are the host, so "host" is always alive
    return ASCIICHAT_OK;
  }

  // For WebRTC sessions, check the WebRTC transport instead of participant_ctx
  if (session->session_type == SESSION_TYPE_WEBRTC) {
    // If we have a WebRTC transport and it's marked as ready, host is alive
    if (session->webrtc_transport_ready) {
      return ASCIICHAT_OK;
    }
    // WebRTC transport not ready yet - might still be connecting
    return ERROR_NETWORK;
  }

  // For TCP sessions, check participant context
  // If we don't have a participant context yet, we're not connected
  if (!session->participant_ctx) {
    return ERROR_NETWORK;
  }

  // Check migration state - if we're already migrating, host is definitely not alive
  if (session->state == DISCOVERY_STATE_MIGRATING) {
    return ERROR_NETWORK; // Already detected disconnect
  }

  uint64_t now_ms = session_get_current_time_ms();

  if (session->liveness.ping_in_flight &&
      atomic_load_u64(&session->liveness.last_pong_received_ms) >= session->liveness.last_ping_sent_ms) {
    session->liveness.ping_in_flight = false;
    session->liveness.consecutive_failures = 0;
  }
  // Check if we have a ping in flight that timed out
  if (session->liveness.ping_in_flight) {
    uint64_t ping_age_ms = now_ms - session->liveness.last_ping_sent_ms;
    if (ping_age_ms > session->liveness.timeout_ms) {
      // Ping timed out
      session->liveness.consecutive_failures++;
      session->liveness.ping_in_flight = false;
      log_warn("Host ping timeout (attempt %u/%u, age=%llu ms)", session->liveness.consecutive_failures,
               session->liveness.max_failures, (unsigned long long)ping_age_ms);

      // Check if we've exceeded failure threshold
      if (session->liveness.consecutive_failures >= session->liveness.max_failures) {
        log_error("Host declared dead after %u consecutive ping failures", session->liveness.consecutive_failures);
        return ERROR_NETWORK;
      }
    }
  }

  // Check if it's time to send a new ping
  uint64_t time_since_last_ping = now_ms - session->liveness.last_ping_sent_ms;
  if (!session->liveness.ping_in_flight && time_since_last_ping >= session->liveness.ping_interval_ms) {
    // Send ping to host via participant connection
    acip_transport_t *transport = session_participant_get_transport(session->participant_ctx);
    if (!transport || !acip_transport_is_connected(transport))
      return SET_ERRNO(ERROR_NETWORK, "Host transport disconnected");
    asciichat_error_t result = acip_send_ping(transport);
    if (result == ASCIICHAT_OK) {
      session->liveness.last_ping_sent_ms = now_ms;
      session->liveness.ping_in_flight = true;
      log_debug("Sent ping to host (attempt %u/%u)", session->liveness.consecutive_failures + 1,
                session->liveness.max_failures);
    } else {
      log_warn("Failed to send ping to host: %d", result);
      session->liveness.consecutive_failures++;
      if (session->liveness.consecutive_failures >= session->liveness.max_failures) {
        log_error("Host declared dead after %u consecutive send failures", session->liveness.consecutive_failures);
        return ERROR_NETWORK;
      }
    }
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Report host loss and await the authoritative ACDS handoff decision.
 * Records the local recovery deadline origin. Advisory successor plans never
 * authorize immediate promotion or reconnection from this function.
 * @return ASCIICHAT_OK if the report was sent; a send error also fails the session.
 */
asciichat_error_t discovery_session_handle_host_disconnect(discovery_session_t *session, uint32_t disconnect_reason) {
  if (!session) {
    SET_ERRNO(ERROR_INVALID_PARAM, "session is NULL");
    return ERROR_INVALID_PARAM;
  }

  log_warn("Host disconnect detected: reason=%u", disconnect_reason);

  session->host_loss_detected_ms = session_get_current_time_ms();

  // Notify ACDS of host loss (lightweight notification, no NAT data exchanged!)
  acip_host_lost_t host_lost = {0};
  memcpy(host_lost.session_id, session->session_id, SESSION_ID_SIZE);
  memcpy(host_lost.participant_id, session->participant_id, SESSION_ID_SIZE);
  memcpy(host_lost.last_host_id, session->host_id, SESSION_ID_SIZE);
  host_lost.disconnect_reason = disconnect_reason;
  host_lost.disconnect_time_ms = session->host_loss_detected_ms;

  asciichat_error_t result =
      packet_send_via_transport(session->acds_transport, PACKET_TYPE_ACIP_HOST_LOST, &host_lost, sizeof(host_lost), 0);
  if (result != ASCIICHAT_OK) {
    set_error(session, result, "Could not report host loss to ACDS");
    return result;
  }

  // A PLAN is advisory. Only ACDS COMMIT authorizes a role change after fresh
  // measurement and preparation. Continue dispatching while awaiting that decision.
  set_state(session, DISCOVERY_STATE_MIGRATING);

  return ASCIICHAT_OK;
}
