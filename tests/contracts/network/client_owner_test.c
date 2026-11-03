#include <criterion/criterion.h>
#include "client/connection_internal.h"
#include "client/server.h"
#include "client/protocol_adapter.h"
#include "client/capture.h"
#include "client/audio.h"
#include "client/display.h"
#include "session/client_like.h"
#include <ascii-chat/tui/mdns.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/audio/analysis.h>
#include <ascii-chat/util/time.h>

static bool exiting, missing_render_callback, active, lost, cancel_on_destroy;
static unsigned starts, destroys, polls, resets, selections, queries, frees, sleeps, capture_calls, audio_calls;
static unsigned destroy_failures;
static int capture_failure, audio_failure, selection_result;
static asciichat_error_t start_result, create_result, remote_result, runtime_result;
static uint64_t clock_ns = 10 * NS_PER_SEC_INT;
static client_connection_t owned_connection;
static ui_mdns_server_t selected_server;
static bool render_exit(void *context) {
  (void)context;
  return exiting;
}
static bool (*owner_render_callback(void))(void *) {
  return missing_render_callback ? NULL : render_exit;
}
static uint64_t owner_clock(void) {
  return clock_ns;
}
static int owner_capture(void) {
  ++capture_calls;
  return capture_failure;
}
static int owner_audio(void) {
  ++audio_calls;
  return audio_failure;
}
static void owner_sleep(uint32_t delay) {
  (void)delay;
  ++sleeps;
}
static void owner_display(session_display_ctx_t *display) {
  (void)display;
}
static void owner_reset(void) {
  ++resets;
}
static void owner_poll(client_connection_t *connection) {
  (void)connection;
  ++polls;
  lost = true;
}
static bool owner_active(client_connection_t *connection) {
  (void)connection;
  return active;
}
static bool owner_lost(client_connection_t *connection) {
  (void)connection;
  return lost;
}
static asciichat_error_t owner_remote(client_connection_t *connection) {
  (void)connection;
  return remote_result;
}
static asciichat_error_t owner_start(client_connection_t *connection, const char *address, uint16_t port,
                                     char *password, size_t capacity) {
  (void)password;
  (void)capacity;
  cr_assert_eq(connection, &owned_connection);
  cr_assert_str_eq(address, "127.0.0.2");
  cr_assert_eq(port, 27002);
  ++starts;
  return start_result;
}
static asciichat_error_t owner_create(client_connection_t **out) {
  if (create_result == ASCIICHAT_OK)
    *out = &owned_connection;
  return create_result;
}
static asciichat_error_t owner_destroy(client_connection_t **connection) {
  cr_assert_eq(*connection, &owned_connection);
  ++destroys;
  if (destroys <= destroy_failures)
    return ERROR_THREAD;
  *connection = NULL;
  if (cancel_on_destroy)
    exiting = true;
  return ASCIICHAT_OK;
}
static asciichat_error_t owner_runtime(acip_websocket_runtime_t **out) {
  (void)out;
  return runtime_result;
}
static ui_mdns_server_t *owner_query(const ui_mdns_config_t *config, int *count) {
  cr_assert_eq(config->max_servers, 20);
  cr_assert(config->quiet);
  ++queries;
  *count = 1;
  return &selected_server;
}
static int owner_select(const ui_mdns_server_t *servers, int count, uint64_t deadline, ui_mdns_server_t *out) {
  cr_assert_eq(servers, &selected_server);
  cr_assert_eq(count, 1);
  cr_assert_gt(deadline, 0);
  ++selections;
  *out = selected_server;
  return selection_result;
}
static void owner_free(ui_mdns_server_t *servers) {
  cr_assert_eq(servers, &selected_server);
  ++frees;
}
static asciichat_error_t owner_settings(acip_transport_t *transport, const options_t *values, uint32_t request,
                                        uint32_t mask) {
  (void)transport;
  (void)request;
  (void)mask;
  cr_assert_eq(values->width, 80);
  cr_assert_eq(values->height, 24);
  return ASCIICHAT_OK;
}
#define capture_init owner_capture
#define audio_client_init owner_audio
#define display_set_context owner_display
#define display_full_reset owner_reset
#define display_poll_keyboard owner_poll
#define client_connection_create owner_create
#define client_connection_start owner_start
#define client_connection_destroy owner_destroy
#define server_connection_is_active owner_active
#define client_protocol_connection_lost owner_lost
#define server_connection_get_remote_error owner_remote
#define session_client_like_get_render_should_exit owner_render_callback
#define time_get_ns owner_clock
#define platform_sleep_ms owner_sleep
#define ui_mdns_query owner_query
#define ui_mdns_select_live owner_select
#define ui_mdns_free_results owner_free
#define acip_websocket_runtime_create owner_runtime
#define acip_settings_send owner_settings
#include "../../../src/client/main.c"
#undef capture_init
#undef audio_client_init
#undef display_set_context
#undef display_full_reset
#undef display_poll_keyboard
#undef client_connection_create
#undef client_connection_start
#undef client_connection_destroy
#undef server_connection_is_active
#undef client_protocol_connection_lost
#undef server_connection_get_remote_error
#undef session_client_like_get_render_should_exit
#undef time_get_ns
#undef platform_sleep_ms
#undef ui_mdns_query
#undef ui_mdns_select_live
#undef ui_mdns_free_results
#undef acip_websocket_runtime_create
#undef acip_settings_send

bool should_exit(void) {
  return exiting;
}
void signal_exit(void) {
  exiting = true;
}
void set_interrupt_callback(void (*callback)(void)) {
  cr_assert_null(callback);
}

static void setup(void) {
  log_init(NULL, LOG_ERROR, false, false);
  log_set_terminal_output(false);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  cr_assert_eq(options_set_int("width", 80), ASCIICHAT_OK);
  cr_assert_eq(options_set_int("height", 24), ASCIICHAT_OK);
  cr_assert_eq(options_set_bool("audio_enabled", false), ASCIICHAT_OK);
  g_client_session.discovered_address = "127.0.0.2";
  g_client_session.discovered_port = 27002;
  SAFE_STRNCPY(selected_server.name, "fixture-server", sizeof(selected_server.name));
  SAFE_STRNCPY(selected_server.ipv4, "127.0.0.2", sizeof(selected_server.ipv4));
  selected_server.port = 27002;
}
static void teardown(void) {
  splash_intro_done();
  splash_wait_for_animation();
  ui_controller_shutdown();
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TestSuite(client_owner, .init = setup, .fini = teardown, .timeout = 15);

Test(client_owner, lan_selection_commits_the_chosen_endpoint_and_releases_query_results) {
  cr_assert_eq(options_set_bool("lan_discovery", true), ASCIICHAT_OK);
  cr_assert_eq(client_select_destination(NULL), ASCIICHAT_OK);
  cr_assert_eq(queries, 1);
  cr_assert_eq(selections, 1);
  cr_assert_eq(frees, 1);
  cr_assert_str_eq(GET_OPTION(address), "127.0.0.2");
  cr_assert_eq(GET_OPTION(port), 27002);
  cr_assert_str_eq(g_client_session.discovered_address, "127.0.0.2");
  cr_assert_eq(g_client_session.discovered_port, 27002);
}
Test(client_owner, cancelled_lan_selection_preserves_configured_endpoint_and_cleans_up) {
  cr_assert_eq(options_set_bool("lan_discovery", true), ASCIICHAT_OK);
  selection_result = -1;
  cr_assert_eq(client_prepare_destination(NULL), ERROR_NETWORK);
  cr_assert_eq(frees, 1);
  cr_assert_eq(capture_calls, 0);
  cr_assert_str_eq(GET_OPTION(address), OPT_ADDRESS_DEFAULT);
  cr_assert_eq(GET_OPTION(port), OPT_PORT_INT_DEFAULT);
}
Test(client_owner, explicit_destination_and_disabled_discovery_do_not_open_the_picker) {
  cr_assert_eq(client_select_destination(NULL), ASCIICHAT_OK);
  options_t *values = SAFE_MALLOC(sizeof(*values), options_t *);
  *values = *options_get();
  values->lan_discovery = true;
  values->address_explicit = true;
  cr_assert_eq(options_state_set(values), ASCIICHAT_OK);
  cr_assert_eq(client_select_destination(NULL), ASCIICHAT_OK);
  values->address_explicit = false;
  values->port = 27003;
  cr_assert_eq(options_state_set(values), ASCIICHAT_OK);
  cr_assert_eq(client_select_destination(NULL), ASCIICHAT_OK);
  cr_assert_eq(queries, 0);
  SAFE_FREE(values);
}
Test(client_owner, device_failure_is_returned_before_connection_attempts_and_audio_failure_is_optional) {
  capture_failure = ERROR_MEDIA_INIT;
  cr_assert_eq(client_initialize_devices(), ERROR_MEDIA_INIT);
  cr_assert_eq(capture_calls, 1);
  cr_assert_eq(audio_calls, 0);
  capture_failure = 0;
  audio_failure = ERROR_AUDIO;
  cr_assert_eq(options_set_bool("audio_enabled", true), ASCIICHAT_OK);
  cr_assert_eq(client_initialize_devices(), ASCIICHAT_OK);
  cr_assert_eq(audio_calls, 1);
  cr_assert_eq(starts, 0);
}
Test(client_owner, missing_render_callback_and_failed_dial_are_cleaned_up_by_the_owner) {
  missing_render_callback = true;
  cr_assert_eq(client_run(NULL, NULL, NULL), ERROR_INVALID_STATE);
  cr_assert_eq(starts, 0);
  cr_assert_eq(destroys, 1);
  missing_render_callback = false;
  start_result = ERROR_CRYPTO_AUTH;
  cr_assert_eq(client_run(NULL, NULL, NULL), ERROR_CRYPTO_AUTH);
  cr_assert_eq(starts, 1);
  cr_assert_eq(destroys, 2);
}
Test(client_owner, failed_worker_join_is_retried_before_borrowed_resources_can_be_released) {
  start_result = ERROR_NETWORK;
  destroy_failures = 2;
  cancel_on_destroy = true;
  cr_assert_eq(client_run(NULL, NULL, NULL), ASCIICHAT_OK);
  cr_assert_eq(starts, 1);
  cr_assert_eq(destroys, 3);
  cr_assert_eq(sleeps, 2);
}
Test(client_owner, expired_snapshot_on_disconnect_succeeds_and_does_not_request_reconnection) {
  cr_assert_eq(options_set_bool("snapshot_mode", true), ASCIICHAT_OK);
  cr_assert_eq(options_set_double("snapshot_delay", 0), ASCIICHAT_OK);
  atomic_store_u64(&owned_connection.snapshot_first_frame_time_ns, clock_ns - NS_PER_SEC_INT);
  cr_assert_eq(client_run(NULL, NULL, NULL), ASCIICHAT_OK);
  cr_assert(exiting);
  cr_assert_eq(destroys, 1);
}
Test(client_owner, live_connection_publishes_dimensions_and_retains_a_peer_rejection) {
  active = true;
  remote_result = ERROR_CRYPTO_VERIFICATION;
  cr_assert_eq(client_run(NULL, NULL, NULL), ERROR_CRYPTO_VERIFICATION);
  cr_assert_eq(polls, 1);
  cr_assert_eq(resets, 1);
  cr_assert_eq(owned_connection.advertised_dimensions.cols, 80);
  cr_assert_eq(owned_connection.advertised_dimensions.rows, 24);
  cr_assert(g_client_session.has_ever_connected);
  cr_assert_eq(destroys, 1);
}
Test(client_owner, runtime_and_connection_creation_failures_do_not_attempt_unowned_cleanup) {
  g_client_session.discovered_address = "wss://fixture.invalid";
  runtime_result = ERROR_MEMORY;
  cr_assert_eq(client_run(NULL, NULL, NULL), ERROR_MEMORY);
  g_client_session.discovered_address = "127.0.0.2";
  create_result = ERROR_MEMORY;
  cr_assert_eq(client_run(NULL, NULL, NULL), ERROR_MEMORY);
  cr_assert_eq(starts, 0);
  cr_assert_eq(destroys, 0);
}
Test(client_owner, retry_policy_distinguishes_fatal_rejections_from_transient_disconnects) {
  const asciichat_error_t fatal[] = {ERROR_CRYPTO_AUTH,      ERROR_CRYPTO_KEY,       ERROR_CRYPTO_VERIFICATION,
                                     ERROR_CONFIG,           ERROR_INVALID_PASSWORD, ERROR_INVALID_SIGNATURE,
                                     ERROR_NETWORK_PROTOCOL, ERROR_INTERNAL};
  for (size_t i = 0; i < ARRAY_SIZE(fatal); ++i)
    cr_assert_not(client_should_reconnect(fatal[i], 1, NULL));
  cr_assert(client_should_reconnect(ERROR_NETWORK, 1, NULL));
  cr_assert_eq(options_set_bool("snapshot_mode", true), ASCIICHAT_OK);
  cr_assert_not(client_should_reconnect(ERROR_NETWORK, 1, NULL));
}
