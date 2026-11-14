#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include "../../../src/common/session/server_like.h"
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/util/time.h>
static unsigned network_token, mdns_token;
static unsigned fail_stage, created, started, ran, stopped, destroyed, initialized, cleaned, interrupted, mapped,
    unmapped, renewed, joined, advertised, status_draws;
static bool cancel_initialization, fail_worker, mapping_matches = true, mapping_active = true, fail_mapping,
                                                fail_advertisement, fail_mdns;
static network_server_config_t observed_network;
static nat_upnp_context_t mapping_storage[2];
static asciichat_error_t owner_create(const network_server_config_t *config, network_server_t **out) {
  ++created;
  observed_network = *config;
  if (fail_stage == 1)
    return ERROR_NETWORK;
  *out = (network_server_t *)&network_token;
  return ASCIICHAT_OK;
}
static asciichat_error_t owner_start(network_server_t *server) {
  ASCIICHAT_REQUIRE_NOT_NULL(server);
  ++started;
  return fail_stage == 2 ? ERROR_NETWORK_BIND : ASCIICHAT_OK;
}
static asciichat_error_t owner_run(network_server_t *server) {
  ASCIICHAT_REQUIRE_NOT_NULL(server);
  ++ran;
  return fail_stage == 3 ? ERROR_NETWORK : ASCIICHAT_OK;
}
static asciichat_error_t owner_stop(network_server_t *server) {
  (void)server;
  ++stopped;
  return ASCIICHAT_OK;
}
static void owner_destroy(network_server_t **server) {
  if (*server)
    ++destroyed;
  *server = NULL;
}
static bool owner_websocket(network_server_t *server) {
  (void)server;
  return observed_network.websocket_enabled;
}
static asciichat_error_t owner_map(uint16_t port, const char *description, nat_upnp_context_t **out) {
  (void)port;
  ASCIICHAT_REQUIRE_NOT_NULL(description);
  if (fail_mapping)
    return ERROR_NETWORK;
  *out = &mapping_storage[mapped % 2];
  ++mapped;
  return ASCIICHAT_OK;
}
static void owner_unmap(nat_upnp_context_t **mapping) {
  if (*mapping)
    ++unmapped;
  *mapping = NULL;
}
static bool owner_mapping_match(const nat_upnp_context_t *mapping, const char *address) {
  (void)mapping;
  (void)address;
  return mapping_matches;
}
static bool owner_mapping_active(const nat_upnp_context_t *mapping) {
  (void)mapping;
  return mapping_active;
}
static asciichat_error_t owner_mapping_address(const nat_upnp_context_t *mapping, char *out, size_t size) {
  (void)mapping;
  SAFE_STRNCPY(out, "203.0.113.5:27001", size);
  return ASCIICHAT_OK;
}
static asciichat_error_t owner_refresh(nat_upnp_context_t *mapping) {
  ASCIICHAT_REQUIRE_NOT_NULL(mapping);
  ++renewed;
  return ERROR_NETWORK;
}
static asciichat_mdns_t *owner_mdns_init(void) {
  return fail_mdns ? NULL : (asciichat_mdns_t *)&mdns_token;
}
static void owner_mdns_destroy(asciichat_mdns_t *context) {
  ASCIICHAT_REQUIRE_NOT_NULL(context);
}
static asciichat_error_t owner_advertise(asciichat_mdns_t *context, const asciichat_mdns_service_t *service) {
  ASCIICHAT_REQUIRE_NOT_NULL(context);
  ASCIICHAT_REQUIRE_EQ(service->port, 27001);
  ++advertised;
  return fail_advertisement ? ERROR_NETWORK : ASCIICHAT_OK;
}
static int owner_thread(platform_thread_t *thread, const char *name, void *(*fn)(void *), void *data) {
  (void)thread;
  (void)name;
  (void)fn;
  (void)data;
  return fail_worker ? -1 : 0;
}
static int owner_join(platform_thread_t *thread, void **result) {
  (void)thread;
  (void)result;
  ++joined;
  return 0;
}
static int owner_join_timeout(platform_thread_t *thread, void **result, uint64_t timeout) {
  (void)timeout;
  return owner_join(thread, result);
}
static void owner_sleep(uint64_t ns);
static void owner_status(const ui_status_t *status) {
  ASCIICHAT_REQUIRE_NOT_NULL(status);
  ++status_draws;
}
#define network_server_create owner_create
#define network_server_start owner_start
#define network_server_run owner_run
#define network_server_stop owner_stop
#define network_server_destroy owner_destroy
#define network_server_websocket_enabled owner_websocket
#define nat_upnp_open owner_map
#define nat_upnp_close owner_unmap
#define nat_upnp_matches_bind_address owner_mapping_match
#define nat_upnp_is_active owner_mapping_active
#define nat_upnp_get_address owner_mapping_address
#define nat_upnp_refresh owner_refresh
#define asciichat_mdns_init owner_mdns_init
#define asciichat_mdns_destroy owner_mdns_destroy
#define asciichat_mdns_advertise owner_advertise
#define platform_thread_create owner_thread
#define thread_join owner_join
#define thread_join_timeout owner_join_timeout
#define time_sleep_ns owner_sleep
#define ui_status_display_interactive owner_status
#include "../../../src/common/session/server_like.c"
#undef network_server_create
#undef network_server_start
#undef network_server_run
#undef network_server_stop
#undef network_server_destroy
#undef network_server_websocket_enabled
#undef nat_upnp_open
#undef nat_upnp_close
#undef nat_upnp_matches_bind_address
#undef nat_upnp_is_active
#undef nat_upnp_get_address
#undef nat_upnp_refresh
#undef asciichat_mdns_init
#undef asciichat_mdns_destroy
#undef asciichat_mdns_advertise
#undef platform_thread_create
#undef thread_join
#undef thread_join_timeout
#undef time_sleep_ns
#undef ui_status_display_interactive
static void owner_sleep(uint64_t ns) {
  ASCIICHAT_REQUIRE_EQ(ns, 100 * NS_PER_MS_INT);
  atomic_store_bool(&g_upnp_stop, true);
}
static asciichat_error_t mode_init(void *data) {
  ASCIICHAT_REQUIRE_EQ(data, &initialized);
  ++initialized;
  if (cancel_initialization)
    session_server_like_request_shutdown();
  return fail_stage == 4 ? ERROR_CONFIG : ASCIICHAT_OK;
}
static void mode_cleanup(void *data) {
  ASCIICHAT_REQUIRE_EQ(data, &cleaned);
  ++cleaned;
  ASCIICHAT_REQUIRE_EQ(mapped, unmapped);
}
static void mode_interrupt(int sig) {
  ASCIICHAT_REQUIRE_EQ(sig, SIGINT);
  ++interrupted;
}
static asciichat_error_t mode_packet(network_connection_t *connection, packet_type_t type, const void *payload,
                                     size_t length, void *data) {
  (void)connection;
  (void)type;
  (void)payload;
  (void)length;
  (void)data;
  return ASCIICHAT_OK;
}
static void mode_status(void *data, ui_status_t *status) {
  ASCIICHAT_REQUIRE_EQ(data, &status_draws);
  memset(status, 0, sizeof(*status));
  atomic_store_bool(&g_status_should_exit, true);
}
static session_server_like_config_t config(void) {
  return (session_server_like_config_t){.init_fn = mode_init,
                                        .init_user_data = &initialized,
                                        .cleanup_fn = mode_cleanup,
                                        .cleanup_user_data = &cleaned,
                                        .interrupt_fn = mode_interrupt,
                                        .connections = {.packet = mode_packet}};
}
static void setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  ASCIICHAT_REQUIRE_EQ(options_state_init(), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(options_set_int("port", 27001), ASCIICHAT_OK);
}
static void teardown(void) {
  ASCIICHAT_REQUIRE_NULL(g_network_server);
  ASCIICHAT_REQUIRE_NULL(g_upnp_ctx);
  ASCIICHAT_REQUIRE_NULL(g_ws_upnp_ctx);
  ASCIICHAT_REQUIRE_NULL(g_mdns_ctx);
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TEST_CASE("server_lifecycle/missing_callbacks_and_conflicting_keepawake_reject_start_before_allocating_listener", "[server_lifecycle]") {
  session_server_like_config_t c = {0};
  ASCIICHAT_REQUIRE_EQ(session_server_like_run(NULL), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(session_server_like_run(&c), ERROR_INVALID_PARAM);
  c.init_fn = mode_init;
  ASCIICHAT_REQUIRE_EQ(session_server_like_run(&c), ERROR_INVALID_PARAM);
  c.interrupt_fn = mode_interrupt;
  ASCIICHAT_REQUIRE_EQ(session_server_like_run(&c), ERROR_INVALID_PARAM);
  c = config();
  options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
  *v = *options_get();
  v->enable_keepawake = v->disable_keepawake = true;
  ASCIICHAT_REQUIRE_EQ(options_state_set(v), ASCIICHAT_OK);
  SAFE_FREE(v);
  ASCIICHAT_REQUIRE_EQ(session_server_like_run(&c), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(created, 0);
  ASCIICHAT_REQUIRE_EQ(cleaned, 0);
}
TEST_CASE("server_lifecycle/listener_creation_failure_runs_mode_cleanup_without_starting_mode_or_event_loop", "[server_lifecycle]") {
  session_server_like_config_t c = config();
  fail_stage = 1;
  ASCIICHAT_REQUIRE_EQ(session_server_like_run(&c), ERROR_NETWORK);
  ASCIICHAT_REQUIRE_EQ(created, 1);
  ASCIICHAT_REQUIRE_EQ(initialized, 0);
  ASCIICHAT_REQUIRE_EQ(started, 0);
  ASCIICHAT_REQUIRE_EQ(ran, 0);
  ASCIICHAT_REQUIRE_EQ(cleaned, 1);
  ASCIICHAT_REQUIRE_EQ(destroyed, 0);
  ASCIICHAT_REQUIRE_EQ(stopped, 1);
}
TEST_CASE("server_lifecycle/mode_failure_and_requested_shutdown_never_start_accepting_connections", "[server_lifecycle]") {
  session_server_like_config_t c = config();
  fail_stage = 4;
  ASCIICHAT_REQUIRE_EQ(session_server_like_run(&c), ERROR_CONFIG);
  ASCIICHAT_REQUIRE_EQ(started, 0);
  ASCIICHAT_REQUIRE_EQ(cleaned, 1);
  ASCIICHAT_REQUIRE_EQ(destroyed, 1);
  fail_stage = 0;
  cancel_initialization = true;
  ASCIICHAT_REQUIRE_EQ(session_server_like_run(&c), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(started, 0);
  ASCIICHAT_REQUIRE_EQ(interrupted, 1);
  ASCIICHAT_REQUIRE(session_server_like_shutdown_requested());
  ASCIICHAT_REQUIRE_EQ(cleaned, 2);
  ASCIICHAT_REQUIRE_EQ(destroyed, 2);
}
TEST_CASE("server_lifecycle/event_loop_start_and_run_failures_return_the_failure_after_owned_resources_are_released", "[server_lifecycle]") {
  session_server_like_config_t c = config();
  fail_stage = 2;
  ASCIICHAT_REQUIRE_EQ(session_server_like_run(&c), ERROR_NETWORK_BIND);
  ASCIICHAT_REQUIRE_EQ(started, 1);
  ASCIICHAT_REQUIRE_EQ(ran, 0);
  ASCIICHAT_REQUIRE_EQ(destroyed, 1);
  fail_stage = 3;
  ASCIICHAT_REQUIRE_EQ(session_server_like_run(&c), ERROR_NETWORK);
  ASCIICHAT_REQUIRE_EQ(ran, 1);
  ASCIICHAT_REQUIRE_EQ(destroyed, 2);
  ASCIICHAT_REQUIRE_EQ(cleaned, 2);
}
TEST_CASE("server_lifecycle/listener_options_preserve_ipv4_ipv6_websocket_and_packet_limits", "[server_lifecycle]") {
  session_server_like_config_t c = config();
  c.websocket_enabled = true;
  c.max_payload = 1234;
  c.partial_timeout_ms = 99;
  c.idle_timeout_ms = 333;
  for (unsigned i = 0; i < 3; ++i) {
    options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
    *v = *options_get();
    v->address[0] = v->address6[0] = 0;
    if (i == 0)
      SAFE_STRNCPY(v->address, "127.0.0.1", sizeof(v->address));
    if (i == 1)
      SAFE_STRNCPY(v->address6, "::1", sizeof(v->address6));
    ASCIICHAT_REQUIRE_EQ(options_state_set(v), ASCIICHAT_OK);
    SAFE_FREE(v);
    ASCIICHAT_REQUIRE_EQ(session_server_like_run(&c), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_EQ(observed_network.bind_ipv4, i != 1);
    ASCIICHAT_REQUIRE_EQ(observed_network.bind_ipv6, i != 0);
    ASCIICHAT_REQUIRE_EQ(observed_network.max_payload, 1234);
    ASCIICHAT_REQUIRE_EQ(observed_network.partial_timeout_ms, 99);
    ASCIICHAT_REQUIRE_EQ(observed_network.idle_timeout_ms, 333);
    ASCIICHAT_REQUIRE(observed_network.websocket_enabled);
  }
}
TEST_CASE("server_lifecycle/mapping_and_advertisement_failures_are_nonfatal_and_remove_mappings_before_mode_cleanup", "[server_lifecycle]") {
  session_server_like_config_t c = config();
  c.upnp.enabled = true;
  c.upnp.description = "coverage service";
  c.mdns.enabled = true;
  c.mdns.service_name = "coverage";
  c.mdns.service_type = "_coverage._tcp";
  options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
  *v = *options_get();
  v->enable_upnp = true;
  SAFE_STRNCPY(v->address, "0.0.0.0", sizeof(v->address));
  ASCIICHAT_REQUIRE_EQ(options_state_set(v), ASCIICHAT_OK);
  SAFE_FREE(v);
  ASCIICHAT_REQUIRE_EQ(session_server_like_run(&c), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(mapped, 1);
  ASCIICHAT_REQUIRE_EQ(unmapped, 1);
  ASCIICHAT_REQUIRE_EQ(joined, 1);
  ASCIICHAT_REQUIRE_EQ(advertised, 1);
  mapping_matches = false;
  fail_advertisement = true;
  ASCIICHAT_REQUIRE_EQ(session_server_like_run(&c), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(mapped, 2);
  ASCIICHAT_REQUIRE_EQ(unmapped, 2);
  ASCIICHAT_REQUIRE_EQ(joined, 1);
  fail_mapping = true;
  fail_mdns = true;
  ASCIICHAT_REQUIRE_EQ(session_server_like_run(&c), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(mapped, 2);
  ASCIICHAT_REQUIRE_EQ(cleaned, 3);
  ASCIICHAT_REQUIRE_EQ(session_server_like_mdns_advertise("no-context", "_coverage._tcp", 27001, NULL, 0), ERROR_INVALID_STATE);
}
TEST_CASE("server_lifecycle/failed_renewal_worker_removes_both_tcp_and_websocket_mappings", "[server_lifecycle]") {
  session_server_like_config_t c = config();
  c.upnp.enabled = true;
  c.upnp.description = "coverage";
  c.websocket_enabled = true;
  options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
  *v = *options_get();
  v->enable_upnp = true;
  SAFE_STRNCPY(v->address, "0.0.0.0", sizeof(v->address));
  ASCIICHAT_REQUIRE_EQ(options_state_set(v), ASCIICHAT_OK);
  SAFE_FREE(v);
  fail_worker = true;
  ASCIICHAT_REQUIRE_EQ(session_server_like_run(&c), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(mapped, 2);
  ASCIICHAT_REQUIRE_EQ(unmapped, 2);
  ASCIICHAT_REQUIRE_EQ(joined, 0);
  ASCIICHAT_REQUIRE_EQ(cleaned, 1);
}
TEST_CASE("server_lifecycle/renewal_attempts_each_expired_mapping_and_handles_failed_refresh_before_stop", "[server_lifecycle]") {
  g_upnp_ctx = &mapping_storage[0];
  g_ws_upnp_ctx = &mapping_storage[1];
  atomic_store_bool(&g_upnp_ready, true);
  atomic_store_bool(&g_upnp_stop, false);
  upnp_renewal_thread(NULL);
  ASCIICHAT_REQUIRE_EQ(renewed, 2);
  g_upnp_ctx = g_ws_upnp_ctx = NULL;
}
TEST_CASE("server_lifecycle/status_worker_lifetime_tracks_start_success_and_failure", "[server_lifecycle]") {
  session_server_like_config_t c = config();
  c.status_fn = mode_status;
  c.status_user_data = &status_draws;
  options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
  *v = *options_get();
  v->status_screen = true;
  v->status_screen_explicitly_set = true;
  ASCIICHAT_REQUIRE_EQ(options_state_set(v), ASCIICHAT_OK);
  SAFE_FREE(v);
  ASCIICHAT_REQUIRE_EQ(session_server_like_run(&c), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(joined, 1);
  ASCIICHAT_REQUIRE_NULL(g_status_fn);
  ASCIICHAT_REQUIRE_NULL(g_status_user_data);
  fail_worker = true;
  ASCIICHAT_REQUIRE_EQ(session_server_like_run(&c), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(joined, 1);
  ASCIICHAT_REQUIRE_NULL(g_status_fn);
  ASCIICHAT_REQUIRE_NULL(g_status_user_data);
  g_status_fn = mode_status;
  g_status_user_data = &status_draws;
  atomic_store_bool(&g_status_should_exit, false);
  status_screen_thread_func(NULL);
  ASCIICHAT_REQUIRE_EQ(status_draws, 1);
  g_status_fn = NULL;
  g_status_user_data = NULL;
}
