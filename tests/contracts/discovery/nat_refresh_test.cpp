#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <ascii-chat/options/rcu.h>
#include <libplatform/filesystem.h>
#include "../../../src/discovery/session.h"
#include <ascii-chat/network/webrtc/peer_manager.h>

asciichat_error_t refresh_probe(nat_quality_t *quality, const char *host, uint16_t port);

int refresh_create(platform_thread_t *thread, const char *name, void *(*worker)(void *), void *arg);
int refresh_join(platform_thread_t *thread, void **result, uint64_t timeout);

/* Exercise the production worker lifecycle with deterministic network/thread
 * boundaries. No camera, public STUN server, or scheduler timing is required. */
static bool fail_peer_manager;
static unsigned forwarded_sdp, forwarded_ice;
static const void *forwarded_message;
static webrtc_peer_manager_t *forwarded_manager;
static asciichat_error_t forwarded_result;
static asciichat_error_t refresh_manager_create(const webrtc_peer_manager_config_t *config, const webrtc_signaling_callbacks_t *signaling, webrtc_peer_manager_t **out) {
  if (fail_peer_manager)
    return ERROR_MEMORY;
  return webrtc_peer_manager_create(config, signaling, out);
}
static asciichat_error_t refresh_sdp(webrtc_peer_manager_t *manager, const acip_webrtc_sdp_t *message) {
  ++forwarded_sdp;
  forwarded_manager = manager;
  forwarded_message = message;
  return forwarded_result;
}
static asciichat_error_t refresh_ice(webrtc_peer_manager_t *manager, const acip_webrtc_ice_t *message) {
  ++forwarded_ice;
  forwarded_manager = manager;
  forwarded_message = message;
  return forwarded_result;
}
#define webrtc_peer_manager_create refresh_manager_create
#define webrtc_peer_manager_handle_sdp refresh_sdp
#define webrtc_peer_manager_handle_ice refresh_ice
#define nat_detect_quality refresh_probe
#define platform_thread_create refresh_create
#define thread_join_timeout refresh_join
#include "../../../src/discovery/session.c"
#undef thread_join_timeout
#undef platform_thread_create
#undef nat_detect_quality
#undef webrtc_peer_manager_create
#undef webrtc_peer_manager_handle_sdp
#undef webrtc_peer_manager_handle_ice

static discovery_session_t session;
static uint64_t clock_ms;
static unsigned slept_ms;
static void refresh_sleep(unsigned ms) {
  slept_ms += ms;
}
static unsigned int launches, joins;
static int create_result, join_result;
static asciichat_error_t probe_result;
static void *(*pending_worker)(void *);
static void *pending_arg;

static uint64_t refresh_clock(void) {
  return clock_ms * 1000;
}
asciichat_error_t refresh_probe(nat_quality_t *quality, const char *host, uint16_t port) {
  (void)host;
  (void)port;
  nat_quality_init(quality);
  quality->detection_complete = true;
  quality->has_public_ip = true;
  return probe_result;
}
/** @brief Schedule a controllable fake detector, or inject a thread-start failure. */
int refresh_create(platform_thread_t *thread, const char *name, void *(*worker)(void *), void *arg) {
  (void)thread;
  (void)name;
  launches++;
  if (!create_result) {
    pending_worker = worker;
    pending_arg = arg;
  }
  return create_result;
}
int refresh_join(platform_thread_t *thread, void **result, uint64_t timeout) {
  (void)thread;
  (void)result;
  REQUIRE((timeout) == (0));
  REQUIRE(session.nat_detection_completed);
  joins++;
  return join_result;
}
static void setup(void) {
  REQUIRE((asciichat_shared_init(NULL, false, true)) == (ASCIICHAT_OK));
  log_set_level(LOG_DEV);
  log_set_terminal_output(false);
  REQUIRE((options_state_init()) == (ASCIICHAT_OK));
  static app_callbacks_t callbacks = {.platform_get_monotonic_time_us = refresh_clock, .platform_sleep_ms = refresh_sleep};
  app_callbacks_register(&callbacks);
  memset(&session, 0, sizeof(session));
  REQUIRE((mutex_init(&session.nat_quality_mutex, "nat_refresh_test")) == (0));
  clock_ms = 100000;
  launches = joins = 0;
  create_result = join_result = 0;
  probe_result = ASCIICHAT_OK;
  pending_worker = NULL;
}
static void teardown(void) {
  if (session.participant_ctx)
    session_participant_destroy(session.participant_ctx);
  if (session.host_ctx)
    session_host_destroy(session.host_ctx);
  if (session.peer_manager)
    webrtc_peer_manager_destroy(session.peer_manager);
  SAFE_FREE(session.stun_servers);
  SAFE_FREE(session.turn_servers);
  mutex_destroy(&session.nat_quality_mutex);
  options_state_destroy();
  asciichat_shared_destroy();
}
TEST_CASE("nat_refresh/stale_result_refreshes_without_overlap_or_cache_loss", "[nat_refresh]") {
  start_nat_detection(&session);
  REQUIRE((launches) == (1));
  start_nat_detection(&session);
  REQUIRE((launches) == (1));
  REQUIRE((joins) == (0));
  REQUIRE(pending_worker);
  pending_worker(pending_arg);
  REQUIRE(session.nat_detection_completed && session.nat_quality_cached);
  uint64_t measured = session.nat_quality_measured_ms;
  clock_ms += 5000;
  start_nat_detection(&session);
  REQUIRE((launches) == (1));
  clock_ms = measured + CONSENSUS_HANDOFF_FRESH_MS + 1;
  join_result = -2;
  start_nat_detection(&session);
  REQUIRE(session.nat_detection_thread_started);
  REQUIRE((launches) == (1));
  join_result = 0;
  start_nat_detection(&session);
  REQUIRE((launches) == (2));
  REQUIRE(!session.nat_detection_completed);
  REQUIRE(session.nat_quality_cached && session.cached_nat_quality.has_public_ip);
  REQUIRE((session.nat_quality_measured_ms) == (measured));
  unsigned int previous_joins = joins;
  start_nat_detection(&session);
  REQUIRE((joins) == (previous_joins));
  REQUIRE((launches) == (2));
  pending_worker(pending_arg);
  REQUIRE((session.nat_quality_measured_ms) == (clock_ms));
}

TEST_CASE("nat_refresh/failed_probe_and_failed_start_are_throttled_and_retryable", "[nat_refresh]") {
  start_nat_detection(&session);
  pending_worker(pending_arg);
  uint64_t measured = session.nat_quality_measured_ms;
  clock_ms += CONSENSUS_HANDOFF_FRESH_MS + 1;
  start_nat_detection(&session);
  probe_result = ERROR_NETWORK;
  pending_worker(pending_arg);
  REQUIRE((session.nat_quality_measured_ms) == (measured));
  REQUIRE(session.nat_quality_cached && session.nat_detection_completed);
  start_nat_detection(&session);
  REQUIRE((launches) == (2));
  clock_ms += CONSENSUS_HANDOFF_FRESH_MS + 1;
  create_result = -1;
  start_nat_detection(&session);
  REQUIRE((launches) == (3));
  REQUIRE(!session.nat_detection_thread_started);
  start_nat_detection(&session);
  REQUIRE((launches) == (3));
  clock_ms += CONSENSUS_HANDOFF_FRESH_MS + 1;
  create_result = 0;
  probe_result = ASCIICHAT_OK;
  start_nat_detection(&session);
  REQUIRE((launches) == (4));
  pending_worker(pending_arg);
  REQUIRE((session.nat_quality_measured_ms) == (clock_ms));
}

static options_t *recovery_options(void) {
  options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
  *v = *options_get();
  return v;
}
static void recovery_publish(options_t *v) {
  REQUIRE((options_state_set(v)) == (ASCIICHAT_OK));
  SAFE_FREE(v);
}
static unsigned recovery_packets, recovery_destroyed;
static bool recovery_connected = true;
static asciichat_error_t recovery_send_result = ASCIICHAT_OK;
static packet_type_t recovery_type;
static uint8_t recovery_payload[4096];
static size_t recovery_length;
static asciichat_error_t recovery_send(acip_transport_t *t, const void *data, size_t length) {
  (void)t;
  packet_header_t h;
  REQUIRE((length) >= (sizeof(h)));
  memcpy(&h, data, sizeof(h));
  recovery_type = NET_TO_HOST_U16(h.type);
  recovery_length = length - sizeof(h);
  REQUIRE((recovery_length) <= (sizeof(recovery_payload)));
  memcpy(recovery_payload, (const uint8_t *)data + sizeof(h), recovery_length);
  ++recovery_packets;
  return recovery_send_result;
}
static bool recovery_is_connected(acip_transport_t *t) {
  (void)t;
  return recovery_connected;
}
static acip_transport_type_t recovery_get_type(acip_transport_t *t) {
  (void)t;
  return ACIP_TRANSPORT_WEBRTC;
}
static void recovery_destroy(acip_transport_t *t) {
  (void)t;
  ++recovery_destroyed;
}
static const acip_transport_methods_t recovery_methods = {.send = recovery_send, .is_connected = recovery_is_connected, .get_type = recovery_get_type, .destroy_impl = recovery_destroy};
static acip_transport_t recovery_transport = {.methods = &recovery_methods};
static void recovery_participant(void) {
  session_participant_config_t config = {.address = "127.0.0.1", .port = 27001};
  session.participant_ctx = session_participant_create(&config);
  REQUIRE((session.participant_ctx) != nullptr);
  REQUIRE((session_participant_set_transport(session.participant_ctx, &recovery_transport, ACIP_TRANSPORT_BORROWED)) == (ASCIICHAT_OK));
}
TEST_CASE("nat_refresh/host_liveness_uses_pongs_and_bounded_consecutive_failures", "[nat_refresh]") {
  recovery_participant();
  session.state = DISCOVERY_STATE_ACTIVE;
  session.session_type = SESSION_TYPE_DIRECT_TCP;
  session.liveness.ping_interval_ms = 1000;
  session.liveness.timeout_ms = 2000;
  session.liveness.max_failures = 2;
  REQUIRE((discovery_session_check_host_alive(&session)) == (ASCIICHAT_OK));
  REQUIRE((recovery_packets) == (1));
  REQUIRE((recovery_type) == (PACKET_TYPE_PING));
  REQUIRE(session.liveness.ping_in_flight);
  discovery_session_host_pong(&session);
  REQUIRE((discovery_session_check_host_alive(&session)) == (ASCIICHAT_OK));
  REQUIRE(!(session.liveness.ping_in_flight));
  REQUIRE((session.liveness.consecutive_failures) == (0));
  clock_ms += 1001;
  REQUIRE((discovery_session_check_host_alive(&session)) == (ASCIICHAT_OK));
  clock_ms += 2001;
  REQUIRE((discovery_session_check_host_alive(&session)) == (ASCIICHAT_OK));
  REQUIRE((session.liveness.consecutive_failures) == (1));
  REQUIRE(session.liveness.ping_in_flight);
  clock_ms += 2001;
  REQUIRE((discovery_session_check_host_alive(&session)) == (ERROR_NETWORK));
  REQUIRE((session.liveness.consecutive_failures) == (2));
}
TEST_CASE("nat_refresh/failed_ping_sends_and_disconnected_transport_are_not_mistaken_for_a_live_host", "[nat_refresh]") {
  recovery_participant();
  session.session_type = SESSION_TYPE_DIRECT_TCP;
  session.liveness.max_failures = 2;
  recovery_send_result = ERROR_NETWORK;
  REQUIRE((discovery_session_check_host_alive(&session)) == (ASCIICHAT_OK));
  REQUIRE((discovery_session_check_host_alive(&session)) == (ERROR_NETWORK));
  REQUIRE((session.liveness.consecutive_failures) == (2));
  recovery_connected = false;
  REQUIRE((discovery_session_check_host_alive(&session)) == (ERROR_NETWORK));
  session.state = DISCOVERY_STATE_MIGRATING;
  REQUIRE((discovery_session_check_host_alive(&session)) == (ERROR_NETWORK));
  session.is_host = true;
  REQUIRE((discovery_session_check_host_alive(&session)) == (ASCIICHAT_OK));
  session.is_host = false;
  session.session_type = SESSION_TYPE_WEBRTC;
  REQUIRE((discovery_session_check_host_alive(&session)) == (ERROR_NETWORK));
  session.webrtc_transport_ready = true;
  REQUIRE((discovery_session_check_host_alive(&session)) == (ASCIICHAT_OK));
}
TEST_CASE("nat_refresh/host_loss_reports_identity_and_waits_for_authoritative_handoff", "[nat_refresh]") {
  session.acds_transport = &recovery_transport;
  session.session_id[0] = 1;
  session.participant_id[0] = 2;
  session.host_id[0] = 3;
  REQUIRE((discovery_session_handle_host_disconnect(&session, 7)) == (ASCIICHAT_OK));
  REQUIRE((session.state) == (DISCOVERY_STATE_MIGRATING));
  REQUIRE(!(session.handoff_pending));
  REQUIRE((recovery_type) == (PACKET_TYPE_ACIP_HOST_LOST));
  REQUIRE((recovery_length) == (sizeof(acip_host_lost_t)));
  acip_host_lost_t lost;
  memcpy(&lost, recovery_payload, sizeof(lost));
  REQUIRE(std::memcmp((lost.session_id), (session.session_id), (16)) == 0);
  REQUIRE(std::memcmp((lost.participant_id), (session.participant_id), (16)) == 0);
  REQUIRE(std::memcmp((lost.last_host_id), (session.host_id), (16)) == 0);
  REQUIRE((lost.disconnect_reason) == (7));
  REQUIRE((lost.disconnect_time_ms) == (clock_ms));
  recovery_send_result = ERROR_NETWORK;
  REQUIRE((discovery_session_handle_host_disconnect(&session, 9)) == (ERROR_NETWORK));
  REQUIRE((session.state) == (DISCOVERY_STATE_FAILED));
  REQUIRE((session.error) == (ERROR_NETWORK));
}
TEST_CASE("nat_refresh/committed_tcp_role_change_clears_old_liveness_and_requires_a_new_connection", "[nat_refresh]") {
  recovery_participant();
  session.handoff_pending = true;
  session.session_type = SESSION_TYPE_DIRECT_TCP;
  session.participant_id[0] = 1;
  session.host_id[0] = 2;
  session.webrtc_transport_ready = true;
  session.webrtc_connection_initiated = true;
  session.webrtc_retry_attempt = 3;
  session.webrtc_disconnected_since_ms = 12;
  session.liveness.ping_in_flight = true;
  session.liveness.consecutive_failures = 2;
  atomic_store_u64(&session.liveness.last_pong_received_ms, 13);
  REQUIRE((discovery_session_apply_handoff(&session)) == (ASCIICHAT_OK));
  REQUIRE((session.participant_ctx) == nullptr);
  REQUIRE((recovery_destroyed) == (0));
  REQUIRE(!(session.handoff_pending));
  REQUIRE(!(session.is_host));
  REQUIRE((session.state) == (DISCOVERY_STATE_CONNECTING_HOST));
  REQUIRE(!(session.webrtc_transport_ready));
  REQUIRE(!(session.webrtc_connection_initiated));
  REQUIRE((session.webrtc_retry_attempt) == (0));
  REQUIRE((session.webrtc_disconnected_since_ms) == (0));
  REQUIRE(!(session.liveness.ping_in_flight));
  REQUIRE((atomic_load_u64(&session.liveness.last_pong_received_ms)) == (0));
  session.handoff_pending = true;
  session.host_id[0] = 1;
  REQUIRE((discovery_session_apply_handoff(&session)) == (ASCIICHAT_OK));
  REQUIRE(session.is_host);
  REQUIRE((session.state) == (DISCOVERY_STATE_STARTING_HOST));
}
TEST_CASE("nat_refresh/webrtc_credentials_must_be_complete_bounded_and_compatible_with_relay_policy", "[nat_refresh]") {
  for (unsigned scenario = 0; scenario < 5; ++scenario) {
    options_t *v = recovery_options();
    v->webrtc_relay_only = false;
    v->webrtc_disable_turn = false;
    v->no_webrtc = false;
    v->turn_username[0] = v->turn_credential[0] = 0;
    if (scenario == 0)
      SAFE_STRNCPY(v->turn_username, "only-user", sizeof(v->turn_username));
    if (scenario == 1)
      SAFE_STRNCPY(v->turn_credential, "only-secret", sizeof(v->turn_credential));
    if (scenario == 2) {
      memset(v->turn_username, 'x', 128);
      v->turn_username[128] = 0;
      SAFE_STRNCPY(v->turn_credential, "secret", sizeof(v->turn_credential));
    }
    if (scenario >= 3) {
      v->webrtc_relay_only = true;
      if (scenario == 3)
        v->webrtc_disable_turn = true;
      else
        v->no_webrtc = true;
    }
    recovery_publish(v);
    REQUIRE((initialize_webrtc_peer_manager(&session)) == (ERROR_INVALID_PARAM));
    REQUIRE((session.peer_manager) == nullptr);
    REQUIRE((session.stun_servers) == nullptr);
    REQUIRE((session.turn_servers) == nullptr);
    CLEAR_ERRNO_ALL();
  }
}
TEST_CASE("nat_refresh/custom_turn_configuration_trims_urls_and_retains_full_credentials_without_network_io", "[nat_refresh]") {
  options_t *v = recovery_options();
  v->webrtc_skip_stun = true;
  v->webrtc_disable_turn = false;
  SAFE_STRNCPY(v->turn_username, "local-user", sizeof(v->turn_username));
  SAFE_STRNCPY(v->turn_credential, "local-secret", sizeof(v->turn_credential));
  SAFE_STRNCPY(v->turn_servers, " turn:127.0.0.1:3478 , \tturns:localhost:5349\t ,   ", sizeof(v->turn_servers));
  recovery_publish(v);
  REQUIRE((initialize_webrtc_peer_manager(&session)) == (ASCIICHAT_OK));
  REQUIRE((session.peer_manager) != nullptr);
  REQUIRE((session.stun_count) == (0));
  REQUIRE((session.stun_servers) == nullptr);
  REQUIRE((session.turn_count) == (2));
  REQUIRE(std::strcmp((session.turn_servers[0].url), ("turn:127.0.0.1:3478")) == 0);
  REQUIRE(std::strcmp((session.turn_servers[1].url), ("turns:localhost:5349")) == 0);
  REQUIRE(std::strcmp((session.turn_username), ("local-user")) == 0);
  REQUIRE(std::strcmp((session.turn_password), ("local-secret")) == 0);
}
TEST_CASE("nat_refresh/retry_exhaustion_is_terminal_and_retry_rebuilds_the_peer_manager_after_backoff", "[nat_refresh]") {
  options_t *v = recovery_options();
  v->webrtc_skip_stun = true;
  v->webrtc_disable_turn = true;
  v->webrtc_reconnect_attempts = 2;
  recovery_publish(v);
  session.webrtc_retry_attempt = 0;
  session.webrtc_connection_initiated = true;
  REQUIRE((discovery_session_retry_webrtc(&session, 500)) == (ASCIICHAT_OK));
  REQUIRE((session.peer_manager) != nullptr);
  REQUIRE((session.webrtc_retry_attempt) == (1));
  REQUIRE(!(session.webrtc_connection_initiated));
  REQUIRE((slept_ms) >= (1000));
  REQUIRE((slept_ms) <= (1999));
  REQUIRE((discovery_session_retry_webrtc(&session, 500)) == (ASCIICHAT_OK));
  REQUIRE((session.webrtc_retry_attempt) == (2));
  REQUIRE((discovery_session_retry_webrtc(&session, 500)) == (ERROR_NETWORK_TIMEOUT));
  REQUIRE((session.state) == (DISCOVERY_STATE_FAILED));
  REQUIRE((session.error) == (ERROR_NETWORK_TIMEOUT));
}
TEST_CASE("nat_refresh/backoff_is_saturated_before_shift_overflow_for_extreme_attempt_counts", "[nat_refresh]") {
  const int attempts[] = {-1, 0, 1, 4, 5, 22, 30, INT_MAX};
  const unsigned expected[] = {1000, 1000, 2000, 16000, 30000, 30000, 30000, 30000};
  for (unsigned i = 0; i < ARRAY_SIZE(attempts); ++i) {
    uint32_t delay = calculate_backoff_delay_ms(attempts[i]);
    REQUIRE((delay) >= (expected[i]));
    REQUIRE((delay) <= (expected[i] + 999));
  }
}

static void identity_fixture(char path[PLATFORM_MAX_PATH_LENGTH], uint8_t public_key[32]) {
  int fd = -1;
  REQUIRE((platform_create_temp_file(path, PLATFORM_MAX_PATH_LENGTH, "discovery-identity", &fd)) == (0));
  REQUIRE((platform_close(fd)) == (0));
  uint8_t secret[64];
  REQUIRE((crypto_sign_keypair(public_key, secret)) == (0));
  REQUIRE((acds_identity_save(path, public_key, secret)) == (ASCIICHAT_OK));
  sodium_memzero(secret, sizeof(secret));
}
static unsigned state_calls, error_calls;
static discovery_state_t last_state;
static void state_changed(discovery_state_t state, void *context) {
  REQUIRE((context) == (&session));
  ++state_calls;
  last_state = state;
}
static void discovery_error(asciichat_error_t error, const char *message, void *context) {
  REQUIRE((context) == (&session));
  REQUIRE((error) == (ERROR_NETWORK));
  REQUIRE(std::strcmp((message), ("recovery failed")) == 0);
  ++error_calls;
}
static bool cancel_start(void *context) {
  REQUIRE((context) == (&session));
  return true;
}
TEST_CASE("nat_refresh/explicit_identity_configuration_is_copied_and_cancelled_start_does_not_connect", "[nat_refresh]") {
  char path[PLATFORM_MAX_PATH_LENGTH];
  uint8_t key[32];
  identity_fixture(path, key);
  options_t *v = recovery_options();
  SAFE_STRNCPY(v->encrypt_key, path, sizeof(v->encrypt_key));
  recovery_publish(v);
  discovery_config_t config = {.acds_address = "127.0.0.2",
                               .acds_url = "ws://127.0.0.2:27002",
                               .acds_port = 27002,
                               .session_string = "river-cloud-tree",
                               .password = "local-secret",
                               .on_state_change = state_changed,
                               .on_error = discovery_error,
                               .callback_user_data = &session,
                               .should_exit_callback = cancel_start,
                               .exit_callback_data = &session};
  discovery_session_t *created = discovery_session_create(&config);
  REQUIRE((created) != nullptr);
  REQUIRE(std::memcmp((created->identity_pubkey), (key), (32)) == 0);
  REQUIRE((created->num_identity_keys) == (1));
  REQUIRE(std::strcmp((created->acds_address), (config.acds_address)) == 0);
  REQUIRE(std::strcmp((created->acds_url), (config.acds_url)) == 0);
  REQUIRE((created->acds_port) == (27002));
  REQUIRE(std::strcmp((created->password), (config.password)) == 0);
  REQUIRE(!(created->is_initiator));
  REQUIRE(std::strcmp((discovery_session_get_string(created)), (config.session_string)) == 0);
  REQUIRE((discovery_session_get_state(created)) == (DISCOVERY_STATE_INIT));
  REQUIRE(!(discovery_session_is_active(created)));
  REQUIRE(!(discovery_session_is_host(created)));
  REQUIRE((discovery_session_get_host(created)) == nullptr);
  REQUIRE((discovery_session_get_participant(created)) == nullptr);
  REQUIRE((discovery_session_start(created)) == (ASCIICHAT_OK));
  REQUIRE((created->acds_connection) == nullptr);
  REQUIRE((created->state) == (DISCOVERY_STATE_INIT));
  uint64_t before_waiting = time_get_ns();
  set_state(created, DISCOVERY_STATE_WAITING_PEER);
  uint64_t after_waiting = time_get_ns();
  REQUIRE((last_state) == (DISCOVERY_STATE_WAITING_PEER));
  REQUIRE((created->waiting_peer_start_ns) >= (before_waiting));
  REQUIRE((created->waiting_peer_start_ns) <= (after_waiting));
  set_error(created, ERROR_NETWORK, "recovery failed");
  REQUIRE((error_calls) == (1));
  REQUIRE((last_state) == (DISCOVERY_STATE_FAILED));
  discovery_session_stop(created);
  REQUIRE((discovery_session_get_state(created)) == (DISCOVERY_STATE_ENDED));
  REQUIRE((state_calls) == (3));
  REQUIRE((discovery_session_destroy(created)) == (ASCIICHAT_OK));
  REQUIRE((platform_unlink(path)) == (0));
}
TEST_CASE("nat_refresh/multiple_identity_keys_are_loaded_completely_or_constructor_rejects_them", "[nat_refresh]") {
  char paths[2][PLATFORM_MAX_PATH_LENGTH];
  uint8_t keys[2][32];
  for (unsigned i = 0; i < 2; ++i)
    identity_fixture(paths[i], keys[i]);
  options_t *v = recovery_options();
  SAFE_STRNCPY(v->encrypt_key, paths[0], sizeof(v->encrypt_key));
  v->num_identity_keys = 2;
  for (unsigned i = 0; i < 2; ++i)
    SAFE_STRNCPY(v->identity_keys[i], paths[i], sizeof(v->identity_keys[i]));
  recovery_publish(v);
  discovery_config_t config = {0};
  discovery_session_t *created = discovery_session_create(&config);
  REQUIRE((created) != nullptr);
  REQUIRE(created->is_initiator);
  REQUIRE(std::strcmp((created->acds_address), ("127.0.0.1")) == 0);
  REQUIRE((created->acds_port) == (ACIP_DISCOVERY_DEFAULT_PORT));
  REQUIRE((created->num_identity_keys) == (2));
  REQUIRE(std::memcmp((created->identity_pubkeys), (keys), (sizeof(keys))) == 0);
  REQUIRE(std::memcmp((created->identity_pubkey), (keys[0]), (32)) == 0);
  REQUIRE((discovery_session_get_string(created)) == nullptr);
  REQUIRE((discovery_session_destroy(created)) == (ASCIICHAT_OK));
  REQUIRE((platform_unlink(paths[1])) == (0));
  REQUIRE((discovery_session_create(&config)) == nullptr);
  CLEAR_ERRNO_ALL();
  v = recovery_options();
  v->num_identity_keys = 0;
  SAFE_STRNCPY(v->encrypt_key, paths[1], sizeof(v->encrypt_key));
  recovery_publish(v);
  REQUIRE((discovery_session_create(&config)) == nullptr);
  REQUIRE((platform_unlink(paths[0])) == (0));
}
TEST_CASE("nat_refresh/absent_session_is_rejected_without_allocating_or_publishing_transport_state", "[nat_refresh]") {
  REQUIRE((discovery_session_create(NULL)) == nullptr);
  REQUIRE((discovery_session_destroy(NULL)) == (ERROR_INVALID_PARAM));
  REQUIRE((discovery_session_start(NULL)) == (ERROR_INVALID_PARAM));
  REQUIRE((discovery_session_get_state(NULL)) == (DISCOVERY_STATE_FAILED));
  REQUIRE(!(discovery_session_is_active(NULL)));
  REQUIRE(!(discovery_session_is_host(NULL)));
  REQUIRE((discovery_session_get_string(NULL)) == nullptr);
  REQUIRE((discovery_session_get_host(NULL)) == nullptr);
  REQUIRE((discovery_session_get_participant(NULL)) == nullptr);
  discovery_session_stop(NULL);
  discovery_session_host_pong(NULL);
  set_state(NULL, DISCOVERY_STATE_ACTIVE);
  set_error(NULL, ERROR_NETWORK, NULL);
  REQUIRE((initialize_webrtc_peer_manager(NULL)) == (ERROR_INVALID_PARAM));
  REQUIRE((discovery_session_handle_host_disconnect(NULL, 0)) == (ERROR_INVALID_PARAM));
  REQUIRE((discovery_session_check_host_alive(NULL)) == (ASCIICHAT_OK));
  REQUIRE((session.state) == (DISCOVERY_STATE_INIT));
  REQUIRE((session.participant_ctx) == nullptr);
  REQUIRE((session.peer_manager) == nullptr);
}
TEST_CASE("nat_refresh/late_or_unexpected_webrtc_transport_does_not_change_the_direct_tcp_role", "[nat_refresh]") {
  session.session_type = SESSION_TYPE_DIRECT_TCP;
  session.state = DISCOVERY_STATE_ACTIVE;
  uint8_t peer[16] = {1};
  discovery_on_transport_ready(&recovery_transport, peer, NULL);
  CLEAR_ERRNO_ALL();
  discovery_on_transport_ready(&recovery_transport, peer, &session);
  REQUIRE(!(session.webrtc_transport_ready));
  REQUIRE((session.participant_ctx) == nullptr);
  REQUIRE((session.state) == (DISCOVERY_STATE_ACTIVE));
}
TEST_CASE("nat_refresh/ready_webrtc_channel_can_create_or_replace_a_participant_and_reset_retry_state", "[nat_refresh]") {
  session.session_type = SESSION_TYPE_WEBRTC;
  session.webrtc_retry_attempt = 2;
  session.webrtc_disconnected_since_ms = 3;
  SAFE_STRNCPY(session.host_address, "127.0.0.1", sizeof(session.host_address));
  session.host_port = 27001;
  acip_transport_t *owned = SAFE_CALLOC(1, sizeof(*owned), acip_transport_t *);
  owned->methods = &recovery_methods;
  uint8_t peer[16] = {2};
  discovery_on_transport_ready(owned, peer, &session);
  REQUIRE(session.webrtc_transport_ready);
  REQUIRE((session.participant_ctx) != nullptr);
  REQUIRE((session_participant_get_transport(session.participant_ctx)) == (owned));
  REQUIRE((session.webrtc_retry_attempt) == (0));
  REQUIRE((session.webrtc_disconnected_since_ms) == (0));
  acip_transport_t *replacement = SAFE_CALLOC(1, sizeof(*replacement), acip_transport_t *);
  replacement->methods = &recovery_methods;
  discovery_on_transport_ready(replacement, peer, &session);
  REQUIRE((session_participant_get_transport(session.participant_ctx)) == (replacement));
  REQUIRE((recovery_destroyed) == (1));
  REQUIRE(session.webrtc_transport_ready);
}
TEST_CASE("nat_refresh/host_without_listener_rejects_a_ready_channel_and_frees_its_owned_transport", "[nat_refresh]") {
  session_host_config_t config = {.max_clients = 1};
  session.host_ctx = session_host_create(&config);
  REQUIRE((session.host_ctx) != nullptr);
  session.session_type = SESSION_TYPE_WEBRTC;
  session.is_host = true;
  uint8_t peer[16] = {2};
  acip_transport_t *owned = SAFE_CALLOC(1, sizeof(*owned), acip_transport_t *);
  owned->methods = &recovery_methods;
  discovery_on_transport_ready(owned, peer, &session);
  REQUIRE((recovery_destroyed) == (1));
  REQUIRE((session_host_get_client_count(session.host_ctx)) == (0));
  REQUIRE(!(session.webrtc_transport_ready));
}

static void signaling_options(void) {
  options_t *v = recovery_options();
  v->webrtc_skip_stun = true;
  v->webrtc_disable_turn = true;
  recovery_publish(v);
}
TEST_CASE("nat_refresh/malformed_signaling_is_not_forwarded_and_valid_packets_preserve_the_peer_manager_target", "[nat_refresh]") {
  struct {
    acip_webrtc_sdp_t header;
    char body[4];
  } sdp = {.header = {.sdp_len = HOST_TO_NET_U16(3)}, .body = "v=0"};
  struct {
    acip_webrtc_ice_t header;
    char body[8];
  } ice = {.header = {.candidate_len = HOST_TO_NET_U16(3)}, .body = {'a', 'b', 'c', 0, 'm', 0}};
  session.state = DISCOVERY_STATE_CONNECTING_HOST;
  handle_discovery_webrtc_sdp(&session, NULL, sizeof(sdp.header) - 1);
  CLEAR_ERRNO_ALL();
  handle_discovery_webrtc_sdp(&session, &sdp, sizeof(sdp.header) + 2);
  handle_discovery_webrtc_ice(&session, NULL, sizeof(ice.header) - 1);
  handle_discovery_webrtc_ice(&session, &ice, sizeof(ice.header) + 2);
  REQUIRE((forwarded_sdp) == (0));
  REQUIRE((forwarded_ice) == (0));
  handle_discovery_webrtc_sdp(&session, &sdp, sizeof(sdp.header) + 3);
  handle_discovery_webrtc_ice(&session, &ice, sizeof(ice));
  REQUIRE((forwarded_sdp) == (0));
  REQUIRE((forwarded_ice) == (0));
  signaling_options();
  REQUIRE((initialize_webrtc_peer_manager(&session)) == (ASCIICHAT_OK));
  forwarded_result = ERROR_NETWORK;
  handle_discovery_webrtc_sdp(&session, &sdp, sizeof(sdp.header) + 3);
  REQUIRE((forwarded_sdp) == (1));
  REQUIRE((forwarded_manager) == (session.peer_manager));
  REQUIRE((forwarded_message) == (&sdp));
  handle_discovery_webrtc_ice(&session, &ice, sizeof(ice));
  REQUIRE((forwarded_ice) == (1));
  REQUIRE((forwarded_message) == (&ice));
  REQUIRE((session.state) == (DISCOVERY_STATE_CONNECTING_HOST));
  REQUIRE(!(session.webrtc_transport_ready));
}
TEST_CASE("nat_refresh/peer_manager_creation_failure_releases_stun_and_turn_arrays_before_retry", "[nat_refresh]") {
  options_t *v = recovery_options();
  v->webrtc_skip_stun = false;
  v->webrtc_disable_turn = false;
  SAFE_STRNCPY(v->stun_servers, "stun:127.0.0.1:3478", sizeof(v->stun_servers));
  SAFE_STRNCPY(v->turn_servers, "turn:127.0.0.1:3478", sizeof(v->turn_servers));
  SAFE_STRNCPY(v->turn_username, "user", sizeof(v->turn_username));
  SAFE_STRNCPY(v->turn_credential, "secret", sizeof(v->turn_credential));
  recovery_publish(v);
  fail_peer_manager = true;
  REQUIRE((initialize_webrtc_peer_manager(&session)) == (ERROR_MEMORY));
  REQUIRE((session.peer_manager) == nullptr);
  REQUIRE((session.stun_servers) == nullptr);
  REQUIRE((session.turn_servers) == nullptr);
  REQUIRE((session.stun_count) == (0));
  REQUIRE((session.turn_count) == (0));
  fail_peer_manager = false;
  CLEAR_ERRNO_ALL();
  REQUIRE((initialize_webrtc_peer_manager(&session)) == (ASCIICHAT_OK));
  REQUIRE((session.peer_manager) != nullptr);
  REQUIRE((session.stun_count) == (1));
  REQUIRE((session.turn_count) == (1));
}
TEST_CASE("nat_refresh/webrtc_handoff_releases_old_host_and_peer_and_propagates_reinitialization_failure", "[nat_refresh]") {
  signaling_options();
  session_host_config_t config = {.max_clients = 1};
  session.host_ctx = session_host_create(&config);
  REQUIRE((session.host_ctx) != nullptr);
  REQUIRE((initialize_webrtc_peer_manager(&session)) == (ASCIICHAT_OK));
  session.session_type = SESSION_TYPE_WEBRTC;
  session.state = DISCOVERY_STATE_ACTIVE;
  session.participant_id[0] = 1;
  session.host_id[0] = 2;
  session.handoff_pending = true;
  fail_peer_manager = true;
  REQUIRE((discovery_session_apply_handoff(&session)) == (ERROR_MEMORY));
  REQUIRE((session.host_ctx) == nullptr);
  REQUIRE((session.peer_manager) == nullptr);
  REQUIRE(!(session.handoff_pending));
  REQUIRE(!(session.is_host));
  session.handoff_pending = true;
  fail_peer_manager = false;
  CLEAR_ERRNO_ALL();
  REQUIRE((discovery_session_apply_handoff(&session)) == (ASCIICHAT_OK));
  REQUIRE((session.peer_manager) != nullptr);
  REQUIRE((session.state) == (DISCOVERY_STATE_CONNECTING_HOST));
}
TEST_CASE("nat_refresh/signaling_send_failures_are_reported_and_successful_messages_preserve_identity_and_payload", "[nat_refresh]") {
  uint8_t id[16] = {1}, peer[16] = {2};
  session.participant_id[0] = 3;
  REQUIRE((discovery_send_sdp_via_acds(id, peer, "offer", "v=0", NULL)) == (ERROR_INVALID_PARAM));
  REQUIRE((discovery_send_ice_via_acds(id, peer, "candidate", "m", &session)) == (ERROR_INVALID_PARAM));
  session.acds_transport = &recovery_transport;
  recovery_send_result = ERROR_NETWORK;
  REQUIRE((discovery_send_sdp_via_acds(id, peer, "offer", "v=0", &session)) == (ERROR_NETWORK));
  REQUIRE((discovery_send_ice_via_acds(id, peer, "candidate", "m", &session)) == (ERROR_NETWORK));
  recovery_send_result = ASCIICHAT_OK;
  CLEAR_ERRNO_ALL();
  REQUIRE((discovery_send_sdp_via_acds(id, peer, "answer", "v=0", &session)) == (ASCIICHAT_OK));
  REQUIRE((recovery_type) == (PACKET_TYPE_ACIP_WEBRTC_SDP));
  acip_webrtc_sdp_t sdp;
  memcpy(&sdp, recovery_payload, sizeof(sdp));
  REQUIRE(std::memcmp((sdp.session_id), (id), (16)) == 0);
  REQUIRE(std::memcmp((sdp.recipient_id), (peer), (16)) == 0);
  REQUIRE(std::memcmp((sdp.sender_id), (session.participant_id), (16)) == 0);
  REQUIRE((sdp.sdp_type) == (1));
  REQUIRE((NET_TO_HOST_U16(sdp.sdp_len)) == (3));
  REQUIRE(std::memcmp((recovery_payload + sizeof(sdp)), ("v=0"), (3)) == 0);
  REQUIRE((discovery_send_ice_via_acds(id, peer, "abc", "m", &session)) == (ASCIICHAT_OK));
  REQUIRE((recovery_type) == (PACKET_TYPE_ACIP_WEBRTC_ICE));
  acip_webrtc_ice_t ice;
  memcpy(&ice, recovery_payload, sizeof(ice));
  REQUIRE((NET_TO_HOST_U16(ice.candidate_len)) == (3));
  const char expected[] = {'a', 'b', 'c', 0, 'm', 0};
  REQUIRE(std::memcmp((recovery_payload + sizeof(ice)), (expected), (sizeof(expected))) == 0);
}
