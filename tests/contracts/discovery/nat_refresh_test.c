#include <criterion/criterion.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/platform/filesystem.h>

int refresh_create(asciichat_thread_t *thread, const char *name, void *(*worker)(void *), void *arg);
int refresh_join(asciichat_thread_t *thread, void **result, uint64_t timeout);

/* Exercise the production worker lifecycle with deterministic network/thread
 * boundaries. No camera, public STUN server, or scheduler timing is required. */
#define nat_detect_quality refresh_probe
#define acthread_create refresh_create
#define thread_join_timeout refresh_join
#include "../../../src/discovery/session.c"
#undef thread_join_timeout
#undef acthread_create
#undef nat_detect_quality

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
int refresh_create(asciichat_thread_t *thread, const char *name, void *(*worker)(void *), void *arg) {
  (void)thread;
  (void)name;
  launches++;
  if (!create_result) {
    pending_worker = worker;
    pending_arg = arg;
  }
  return create_result;
}
int refresh_join(asciichat_thread_t *thread, void **result, uint64_t timeout) {
  (void)thread;
  (void)result;
  cr_assert_eq(timeout, 0, "Refresh must not block on a detector doing network I/O");
  cr_assert(session.nat_detection_completed);
  joins++;
  return join_result;
}
static void setup(void) {
  cr_assert_eq(asciichat_shared_init(NULL, false, true), ASCIICHAT_OK);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  static app_callbacks_t callbacks = {.platform_get_monotonic_time_us = refresh_clock,
                                      .platform_sleep_ms = refresh_sleep};
  app_callbacks_register(&callbacks);
  memset(&session, 0, sizeof(session));
  cr_assert_eq(mutex_init(&session.nat_quality_mutex, "nat_refresh_test"), 0);
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
TestSuite(nat_refresh, .init = setup, .fini = teardown);

Test(nat_refresh, stale_result_refreshes_without_overlap_or_cache_loss) {
  start_nat_detection(&session);
  cr_assert_eq(launches, 1);
  start_nat_detection(&session);
  cr_assert_eq(launches, 1);
  cr_assert_eq(joins, 0, "Running worker must not be joined");
  cr_assert(pending_worker);
  pending_worker(pending_arg);
  cr_assert(session.nat_detection_completed && session.nat_quality_cached);
  uint64_t measured = session.nat_quality_measured_ms;
  clock_ms += 5000;
  start_nat_detection(&session);
  cr_assert_eq(launches, 1, "Fresh cache must not cause another measurement");
  clock_ms = measured + CONSENSUS_HANDOFF_FRESH_MS + 1;
  join_result = -2;
  start_nat_detection(&session);
  cr_assert(session.nat_detection_thread_started);
  cr_assert_eq(launches, 1, "Failed join must retain the previous handle");
  join_result = 0;
  start_nat_detection(&session);
  cr_assert_eq(launches, 2);
  cr_assert(!session.nat_detection_completed);
  cr_assert(session.nat_quality_cached && session.cached_nat_quality.has_public_ip);
  cr_assert_eq(session.nat_quality_measured_ms, measured);
  unsigned int previous_joins = joins;
  start_nat_detection(&session);
  cr_assert_eq(joins, previous_joins);
  cr_assert_eq(launches, 2);
  pending_worker(pending_arg);
  cr_assert_eq(session.nat_quality_measured_ms, clock_ms);
}

Test(nat_refresh, failed_probe_and_failed_start_are_throttled_and_retryable) {
  start_nat_detection(&session);
  pending_worker(pending_arg);
  uint64_t measured = session.nat_quality_measured_ms;
  clock_ms += CONSENSUS_HANDOFF_FRESH_MS + 1;
  start_nat_detection(&session);
  probe_result = ERROR_NETWORK;
  pending_worker(pending_arg);
  cr_assert_eq(session.nat_quality_measured_ms, measured);
  cr_assert(session.nat_quality_cached && session.nat_detection_completed);
  start_nat_detection(&session);
  cr_assert_eq(launches, 2, "Failed probe must not retry in a tight loop");
  clock_ms += CONSENSUS_HANDOFF_FRESH_MS + 1;
  create_result = -1;
  start_nat_detection(&session);
  cr_assert_eq(launches, 3);
  cr_assert(!session.nat_detection_thread_started);
  start_nat_detection(&session);
  cr_assert_eq(launches, 3, "Failed thread start must also be throttled");
  clock_ms += CONSENSUS_HANDOFF_FRESH_MS + 1;
  create_result = 0;
  probe_result = ASCIICHAT_OK;
  start_nat_detection(&session);
  cr_assert_eq(launches, 4);
  pending_worker(pending_arg);
  cr_assert_eq(session.nat_quality_measured_ms, clock_ms);
}

static options_t *recovery_options(void) {
  options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
  *v = *options_get();
  return v;
}
static void recovery_publish(options_t *v) {
  cr_assert_eq(options_state_set(v), ASCIICHAT_OK);
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
  cr_assert_geq(length, sizeof(h));
  memcpy(&h, data, sizeof(h));
  recovery_type = NET_TO_HOST_U16(h.type);
  recovery_length = length - sizeof(h);
  cr_assert_leq(recovery_length, sizeof(recovery_payload));
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
static const acip_transport_methods_t recovery_methods = {.send = recovery_send,
                                                          .is_connected = recovery_is_connected,
                                                          .get_type = recovery_get_type,
                                                          .destroy_impl = recovery_destroy};
static acip_transport_t recovery_transport = {.methods = &recovery_methods};
static void recovery_participant(void) {
  session_participant_config_t config = {.address = "127.0.0.1", .port = 27001};
  session.participant_ctx = session_participant_create(&config);
  cr_assert_not_null(session.participant_ctx);
  cr_assert_eq(session_participant_set_transport(session.participant_ctx, &recovery_transport, ACIP_TRANSPORT_BORROWED),
               ASCIICHAT_OK);
}
Test(nat_refresh, host_liveness_uses_pongs_and_bounded_consecutive_failures) {
  recovery_participant();
  session.state = DISCOVERY_STATE_ACTIVE;
  session.session_type = SESSION_TYPE_DIRECT_TCP;
  session.liveness.ping_interval_ms = 1000;
  session.liveness.timeout_ms = 2000;
  session.liveness.max_failures = 2;
  cr_assert_eq(discovery_session_check_host_alive(&session), ASCIICHAT_OK);
  cr_assert_eq(recovery_packets, 1);
  cr_assert_eq(recovery_type, PACKET_TYPE_PING);
  cr_assert(session.liveness.ping_in_flight);
  discovery_session_host_pong(&session);
  cr_assert_eq(discovery_session_check_host_alive(&session), ASCIICHAT_OK);
  cr_assert_not(session.liveness.ping_in_flight);
  cr_assert_eq(session.liveness.consecutive_failures, 0);
  clock_ms += 1001;
  cr_assert_eq(discovery_session_check_host_alive(&session), ASCIICHAT_OK);
  clock_ms += 2001;
  cr_assert_eq(discovery_session_check_host_alive(&session), ASCIICHAT_OK);
  cr_assert_eq(session.liveness.consecutive_failures, 1);
  cr_assert(session.liveness.ping_in_flight);
  clock_ms += 2001;
  cr_assert_eq(discovery_session_check_host_alive(&session), ERROR_NETWORK);
  cr_assert_eq(session.liveness.consecutive_failures, 2);
}
Test(nat_refresh, failed_ping_sends_and_disconnected_transport_are_not_mistaken_for_a_live_host) {
  recovery_participant();
  session.session_type = SESSION_TYPE_DIRECT_TCP;
  session.liveness.max_failures = 2;
  recovery_send_result = ERROR_NETWORK;
  cr_assert_eq(discovery_session_check_host_alive(&session), ASCIICHAT_OK);
  cr_assert_eq(discovery_session_check_host_alive(&session), ERROR_NETWORK);
  cr_assert_eq(session.liveness.consecutive_failures, 2);
  recovery_connected = false;
  cr_assert_eq(discovery_session_check_host_alive(&session), ERROR_NETWORK);
  session.state = DISCOVERY_STATE_MIGRATING;
  cr_assert_eq(discovery_session_check_host_alive(&session), ERROR_NETWORK);
  session.is_host = true;
  cr_assert_eq(discovery_session_check_host_alive(&session), ASCIICHAT_OK);
  session.is_host = false;
  session.session_type = SESSION_TYPE_WEBRTC;
  cr_assert_eq(discovery_session_check_host_alive(&session), ERROR_NETWORK);
  session.webrtc_transport_ready = true;
  cr_assert_eq(discovery_session_check_host_alive(&session), ASCIICHAT_OK);
}
Test(nat_refresh, host_loss_reports_identity_and_waits_for_authoritative_handoff) {
  session.acds_transport = &recovery_transport;
  session.session_id[0] = 1;
  session.participant_id[0] = 2;
  session.host_id[0] = 3;
  cr_assert_eq(discovery_session_handle_host_disconnect(&session, 7), ASCIICHAT_OK);
  cr_assert_eq(session.state, DISCOVERY_STATE_MIGRATING);
  cr_assert_not(session.handoff_pending);
  cr_assert_eq(recovery_type, PACKET_TYPE_ACIP_HOST_LOST);
  cr_assert_eq(recovery_length, sizeof(acip_host_lost_t));
  acip_host_lost_t lost;
  memcpy(&lost, recovery_payload, sizeof(lost));
  cr_assert_arr_eq(lost.session_id, session.session_id, 16);
  cr_assert_arr_eq(lost.participant_id, session.participant_id, 16);
  cr_assert_arr_eq(lost.last_host_id, session.host_id, 16);
  cr_assert_eq(lost.disconnect_reason, 7);
  cr_assert_eq(lost.disconnect_time_ms, clock_ms);
  recovery_send_result = ERROR_NETWORK;
  cr_assert_eq(discovery_session_handle_host_disconnect(&session, 9), ERROR_NETWORK);
  cr_assert_eq(session.state, DISCOVERY_STATE_FAILED);
  cr_assert_eq(session.error, ERROR_NETWORK);
}
Test(nat_refresh, committed_tcp_role_change_clears_old_liveness_and_requires_a_new_connection) {
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
  cr_assert_eq(discovery_session_apply_handoff(&session), ASCIICHAT_OK);
  cr_assert_null(session.participant_ctx);
  cr_assert_eq(recovery_destroyed, 0);
  cr_assert_not(session.handoff_pending);
  cr_assert_not(session.is_host);
  cr_assert_eq(session.state, DISCOVERY_STATE_CONNECTING_HOST);
  cr_assert_not(session.webrtc_transport_ready);
  cr_assert_not(session.webrtc_connection_initiated);
  cr_assert_eq(session.webrtc_retry_attempt, 0);
  cr_assert_eq(session.webrtc_disconnected_since_ms, 0);
  cr_assert_not(session.liveness.ping_in_flight);
  cr_assert_eq(atomic_load_u64(&session.liveness.last_pong_received_ms), 0);
  session.handoff_pending = true;
  session.host_id[0] = 1;
  cr_assert_eq(discovery_session_apply_handoff(&session), ASCIICHAT_OK);
  cr_assert(session.is_host);
  cr_assert_eq(session.state, DISCOVERY_STATE_STARTING_HOST);
}
Test(nat_refresh, webrtc_credentials_must_be_complete_bounded_and_compatible_with_relay_policy) {
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
    cr_assert_eq(initialize_webrtc_peer_manager(&session), ERROR_INVALID_PARAM, "scenario %u", scenario);
    cr_assert_null(session.peer_manager);
    cr_assert_null(session.stun_servers);
    cr_assert_null(session.turn_servers);
    CLEAR_ERRNO_ALL();
  }
}
Test(nat_refresh, custom_turn_configuration_trims_urls_and_retains_full_credentials_without_network_io) {
  options_t *v = recovery_options();
  v->webrtc_skip_stun = true;
  v->webrtc_disable_turn = false;
  SAFE_STRNCPY(v->turn_username, "local-user", sizeof(v->turn_username));
  SAFE_STRNCPY(v->turn_credential, "local-secret", sizeof(v->turn_credential));
  SAFE_STRNCPY(v->turn_servers, " turn:127.0.0.1:3478 , \tturns:localhost:5349\t ,   ", sizeof(v->turn_servers));
  recovery_publish(v);
  cr_assert_eq(initialize_webrtc_peer_manager(&session), ASCIICHAT_OK);
  cr_assert_not_null(session.peer_manager);
  cr_assert_eq(session.stun_count, 0);
  cr_assert_null(session.stun_servers);
  cr_assert_eq(session.turn_count, 2);
  cr_assert_str_eq(session.turn_servers[0].url, "turn:127.0.0.1:3478");
  cr_assert_str_eq(session.turn_servers[1].url, "turns:localhost:5349");
  cr_assert_str_eq(session.turn_username, "local-user");
  cr_assert_str_eq(session.turn_password, "local-secret");
}
Test(nat_refresh, retry_exhaustion_is_terminal_and_retry_rebuilds_the_peer_manager_after_backoff) {
  options_t *v = recovery_options();
  v->webrtc_skip_stun = true;
  v->webrtc_disable_turn = true;
  v->webrtc_reconnect_attempts = 2;
  recovery_publish(v);
  session.webrtc_retry_attempt = 0;
  session.webrtc_connection_initiated = true;
  cr_assert_eq(discovery_session_retry_webrtc(&session, 500), ASCIICHAT_OK);
  cr_assert_not_null(session.peer_manager);
  cr_assert_eq(session.webrtc_retry_attempt, 1);
  cr_assert_not(session.webrtc_connection_initiated);
  cr_assert_geq(slept_ms, 1000);
  cr_assert_leq(slept_ms, 1999);
  cr_assert_eq(discovery_session_retry_webrtc(&session, 500), ASCIICHAT_OK);
  cr_assert_eq(session.webrtc_retry_attempt, 2);
  cr_assert_eq(discovery_session_retry_webrtc(&session, 500), ERROR_NETWORK_TIMEOUT);
  cr_assert_eq(session.state, DISCOVERY_STATE_FAILED);
  cr_assert_eq(session.error, ERROR_NETWORK_TIMEOUT);
}
Test(nat_refresh, backoff_is_saturated_before_shift_overflow_for_extreme_attempt_counts) {
  const int attempts[] = {-1, 0, 1, 4, 5, 22, 30, INT_MAX};
  const unsigned expected[] = {1000, 1000, 2000, 16000, 30000, 30000, 30000, 30000};
  for (unsigned i = 0; i < ARRAY_SIZE(attempts); ++i) {
    uint32_t delay = calculate_backoff_delay_ms(attempts[i]);
    cr_assert_geq(delay, expected[i]);
    cr_assert_leq(delay, expected[i] + 999);
  }
}

static void identity_fixture(char path[PLATFORM_MAX_PATH_LENGTH], uint8_t public_key[32]) {
  int fd = -1;
  cr_assert_eq(platform_create_temp_file(path, PLATFORM_MAX_PATH_LENGTH, "discovery-identity", &fd), 0);
  cr_assert_eq(platform_close(fd), 0);
  uint8_t secret[64];
  cr_assert_eq(crypto_sign_keypair(public_key, secret), 0);
  cr_assert_eq(acds_identity_save(path, public_key, secret), ASCIICHAT_OK);
  sodium_memzero(secret, sizeof(secret));
}
static unsigned state_calls, error_calls;
static discovery_state_t last_state;
static void state_changed(discovery_state_t state, void *context) {
  cr_assert_eq(context, &session);
  ++state_calls;
  last_state = state;
}
static void discovery_error(asciichat_error_t error, const char *message, void *context) {
  cr_assert_eq(context, &session);
  cr_assert_eq(error, ERROR_NETWORK);
  cr_assert_str_eq(message, "recovery failed");
  ++error_calls;
}
static bool cancel_start(void *context) {
  cr_assert_eq(context, &session);
  return true;
}
Test(nat_refresh, explicit_identity_configuration_is_copied_and_cancelled_start_does_not_connect) {
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
  cr_assert_not_null(created);
  cr_assert_arr_eq(created->identity_pubkey, key, 32);
  cr_assert_eq(created->num_identity_keys, 1);
  cr_assert_str_eq(created->acds_address, config.acds_address);
  cr_assert_str_eq(created->acds_url, config.acds_url);
  cr_assert_eq(created->acds_port, 27002);
  cr_assert_str_eq(created->password, config.password);
  cr_assert_not(created->is_initiator);
  cr_assert_str_eq(discovery_session_get_string(created), config.session_string);
  cr_assert_eq(discovery_session_get_state(created), DISCOVERY_STATE_INIT);
  cr_assert_not(discovery_session_is_active(created));
  cr_assert_not(discovery_session_is_host(created));
  cr_assert_null(discovery_session_get_host(created));
  cr_assert_null(discovery_session_get_participant(created));
  cr_assert_eq(discovery_session_start(created), ASCIICHAT_OK);
  cr_assert_null(created->acds_connection);
  cr_assert_eq(created->state, DISCOVERY_STATE_INIT);
  set_state(created, DISCOVERY_STATE_WAITING_PEER);
  cr_assert_eq(last_state, DISCOVERY_STATE_WAITING_PEER);
  cr_assert_gt(created->waiting_peer_start_ns, 0);
  set_error(created, ERROR_NETWORK, "recovery failed");
  cr_assert_eq(error_calls, 1);
  cr_assert_eq(last_state, DISCOVERY_STATE_FAILED);
  discovery_session_stop(created);
  cr_assert_eq(discovery_session_get_state(created), DISCOVERY_STATE_ENDED);
  cr_assert_eq(state_calls, 3);
  cr_assert_eq(discovery_session_destroy(created), ASCIICHAT_OK);
  cr_assert_eq(platform_unlink(path), 0);
}
Test(nat_refresh, multiple_identity_keys_are_loaded_completely_or_constructor_rejects_them) {
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
  cr_assert_not_null(created);
  cr_assert(created->is_initiator);
  cr_assert_str_eq(created->acds_address, "127.0.0.1");
  cr_assert_eq(created->acds_port, ACIP_DISCOVERY_DEFAULT_PORT);
  cr_assert_eq(created->num_identity_keys, 2);
  cr_assert_arr_eq(created->identity_pubkeys, keys, sizeof(keys));
  cr_assert_arr_eq(created->identity_pubkey, keys[0], 32);
  cr_assert_null(discovery_session_get_string(created));
  cr_assert_eq(discovery_session_destroy(created), ASCIICHAT_OK);
  cr_assert_eq(platform_unlink(paths[1]), 0);
  cr_assert_null(discovery_session_create(&config));
  CLEAR_ERRNO_ALL();
  v = recovery_options();
  v->num_identity_keys = 0;
  SAFE_STRNCPY(v->encrypt_key, paths[1], sizeof(v->encrypt_key));
  recovery_publish(v);
  cr_assert_null(discovery_session_create(&config));
  cr_assert_eq(platform_unlink(paths[0]), 0);
}
Test(nat_refresh, absent_session_is_rejected_without_allocating_or_publishing_transport_state) {
  cr_assert_null(discovery_session_create(NULL));
  cr_assert_eq(discovery_session_destroy(NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(discovery_session_start(NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(discovery_session_get_state(NULL), DISCOVERY_STATE_FAILED);
  cr_assert_not(discovery_session_is_active(NULL));
  cr_assert_not(discovery_session_is_host(NULL));
  cr_assert_null(discovery_session_get_string(NULL));
  cr_assert_null(discovery_session_get_host(NULL));
  cr_assert_null(discovery_session_get_participant(NULL));
  discovery_session_stop(NULL);
  discovery_session_host_pong(NULL);
  set_state(NULL, DISCOVERY_STATE_ACTIVE);
  set_error(NULL, ERROR_NETWORK, NULL);
  cr_assert_eq(initialize_webrtc_peer_manager(NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(discovery_session_handle_host_disconnect(NULL, 0), ERROR_INVALID_PARAM);
  cr_assert_eq(discovery_session_check_host_alive(NULL), ASCIICHAT_OK);
  cr_assert_eq(session.state, DISCOVERY_STATE_INIT);
  cr_assert_null(session.participant_ctx);
  cr_assert_null(session.peer_manager);
}
Test(nat_refresh, late_or_unexpected_webrtc_transport_does_not_change_the_direct_tcp_role) {
  session.session_type = SESSION_TYPE_DIRECT_TCP;
  session.state = DISCOVERY_STATE_ACTIVE;
  uint8_t peer[16] = {1};
  discovery_on_transport_ready(&recovery_transport, peer, NULL);
  CLEAR_ERRNO_ALL();
  discovery_on_transport_ready(&recovery_transport, peer, &session);
  cr_assert_not(session.webrtc_transport_ready);
  cr_assert_null(session.participant_ctx);
  cr_assert_eq(session.state, DISCOVERY_STATE_ACTIVE);
}
Test(nat_refresh, ready_webrtc_channel_can_create_or_replace_a_participant_and_reset_retry_state) {
  session.session_type = SESSION_TYPE_WEBRTC;
  session.webrtc_retry_attempt = 2;
  session.webrtc_disconnected_since_ms = 3;
  SAFE_STRNCPY(session.host_address, "127.0.0.1", sizeof(session.host_address));
  session.host_port = 27001;
  acip_transport_t *owned = SAFE_CALLOC(1, sizeof(*owned), acip_transport_t *);
  owned->methods = &recovery_methods;
  uint8_t peer[16] = {2};
  discovery_on_transport_ready(owned, peer, &session);
  cr_assert(session.webrtc_transport_ready);
  cr_assert_not_null(session.participant_ctx);
  cr_assert_eq(session_participant_get_transport(session.participant_ctx), owned);
  cr_assert_eq(session.webrtc_retry_attempt, 0);
  cr_assert_eq(session.webrtc_disconnected_since_ms, 0);
  acip_transport_t *replacement = SAFE_CALLOC(1, sizeof(*replacement), acip_transport_t *);
  replacement->methods = &recovery_methods;
  discovery_on_transport_ready(replacement, peer, &session);
  cr_assert_eq(session_participant_get_transport(session.participant_ctx), replacement);
  cr_assert_eq(recovery_destroyed, 1);
  cr_assert(session.webrtc_transport_ready);
}
Test(nat_refresh, host_without_listener_rejects_a_ready_channel_and_frees_its_owned_transport) {
  session_host_config_t config = {.max_clients = 1};
  session.host_ctx = session_host_create(&config);
  cr_assert_not_null(session.host_ctx);
  session.session_type = SESSION_TYPE_WEBRTC;
  session.is_host = true;
  uint8_t peer[16] = {2};
  acip_transport_t *owned = SAFE_CALLOC(1, sizeof(*owned), acip_transport_t *);
  owned->methods = &recovery_methods;
  discovery_on_transport_ready(owned, peer, &session);
  cr_assert_eq(recovery_destroyed, 1);
  cr_assert_eq(session_host_get_client_count(session.host_ctx), 0);
}
