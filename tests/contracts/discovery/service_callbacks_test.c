#include <criterion/criterion.h>
#include "../../../src/discovery-service/server.h"
#include "../../../src/discovery-service/signaling.h"
#include "session/server_like.h"
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/discovery/database.h>
#include <ascii-chat/network/rate_limit/rate_limit.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <sodium.h>

static network_server_t *service_network;
static bool fail_rate_create, fail_pool_create;
static unsigned rate_mode, broadcasts;
static asciichat_error_t broadcast_result;
static acds_server_t *cleanup_owner;
static unsigned cleanup_sleeps, cleanup_prunes, cleanup_sessions;
static bool fail_prune;
static void service_cleanup_sleep(unsigned ms) {
  if (!cleanup_owner) {
    platform_sleep_ms(ms);
    return;
  }
  cr_assert_eq(ms, 100);
  ++cleanup_sleeps;
}
static asciichat_error_t service_prune(rate_limiter_t *limiter, uint32_t age) {
  ++cleanup_prunes;
  cr_assert_eq(age, 3600);
  return fail_prune ? ERROR_INVALID_STATE : rate_limiter_prune(limiter, age);
}
static void service_expire(sqlite3 *db) {
  ++cleanup_sessions;
  database_session_cleanup_expired(db);
  if (cleanup_owner)
    atomic_store_bool(&cleanup_owner->shutdown, true);
}
static network_server_t *service_network_get(void) {
  return service_network;
}
static rate_limiter_t *service_rate_create(const char *path) {
  return fail_rate_create ? NULL : rate_limiter_create_sqlite(path);
}
static thread_pool_t *service_pool_create(const char *name) {
  return fail_pool_create ? NULL : thread_pool_create(name);
}
static asciichat_error_t service_spawn(thread_pool_t *pool, void *(*fn)(void *), void *arg, int order,
                                       const char *name) {
  (void)pool;
  (void)fn;
  (void)arg;
  (void)order;
  (void)name;
  return ERROR_THREAD;
}
static asciichat_error_t service_rate_check(rate_limiter_t *rate, const char *ip, rate_event_type_t event,
                                            const rate_limit_config_t *config, bool *allowed) {
  if (rate_mode) {
    *allowed = false;
    return rate_mode == 1 ? ASCIICHAT_OK : ERROR_INVALID_STATE;
  }
  return rate_limiter_check(rate, ip, event, config, allowed);
}
static asciichat_error_t service_broadcast(sqlite3 *db, network_server_t *network, const uint8_t id[16],
                                           packet_type_t type, const void *packet, size_t length,
                                           const uint8_t *exclude) {
  (void)db;
  (void)network;
  (void)id;
  (void)type;
  (void)packet;
  (void)length;
  (void)exclude;
  ++broadcasts;
  return broadcast_result;
}

// Opaque connection access is the boundary; callbacks retain real negotiation and database behavior.
typedef struct {
  void *context;
  acip_transport_t transport;
  crypto_handshake_context_t handshake;
  network_connection_state_t state;
  bool secure;
  unsigned ready_calls;
} service_connection_t;
static service_connection_t *service_connection(network_connection_t *c) {
  return (service_connection_t *)c;
}
static void *service_context(network_connection_t *c) {
  return service_connection(c)->context;
}
static void service_set_context(network_connection_t *c, void *context) {
  service_connection(c)->context = context;
}
static acip_transport_t *service_transport(network_connection_t *c) {
  return &service_connection(c)->transport;
}
static crypto_handshake_context_t *service_handshake(network_connection_t *c) {
  return &service_connection(c)->handshake;
}
static bool service_secure(network_connection_t *c) {
  return service_connection(c)->secure;
}
static network_connection_state_t service_state(network_connection_t *c) {
  return service_connection(c)->state;
}
static const char *service_peer(network_connection_t *c) {
  (void)c;
  return "127.0.0.8";
}
static void service_ready(network_connection_t *c) {
  ++service_connection(c)->ready_calls;
  service_connection(c)->state = NETWORK_READY;
}
#define network_connection_context service_context
#define network_connection_set_context service_set_context
#define network_connection_transport service_transport
#define network_connection_handshake service_handshake
#define network_connection_secure service_secure
#define network_connection_state service_state
#define network_connection_peer service_peer
#define network_connection_ready service_ready

#define session_server_like_get_network_server service_network_get
#define rate_limiter_create_sqlite service_rate_create
#define rate_limiter_check service_rate_check
#define thread_pool_create service_pool_create
#define thread_pool_spawn service_spawn
#define signaling_broadcast service_broadcast
#define platform_sleep_ms service_cleanup_sleep
#define rate_limiter_prune service_prune
#define database_session_cleanup_expired service_expire
#include "../../../src/discovery-service/server.c"
#undef session_server_like_get_network_server
#undef rate_limiter_create_sqlite
#undef rate_limiter_check
#undef thread_pool_create
#undef thread_pool_spawn
#undef signaling_broadcast
#undef platform_sleep_ms
#undef rate_limiter_prune
#undef database_session_cleanup_expired
#undef network_connection_context
#undef network_connection_set_context
#undef network_connection_transport
#undef network_connection_handshake
#undef network_connection_secure
#undef network_connection_state
#undef network_connection_peer
#undef network_connection_ready

static acds_server_t authority;
static bool authority_ready;
static packet_type_t response_type;
static uint8_t response[16384];
static size_t response_size;
static acip_session_created_t created;
static acip_session_joined_t joined;
static unsigned created_responses, joined_responses;
static asciichat_error_t service_send(acip_transport_t *transport, const void *data, size_t length) {
  (void)transport;
  packet_header_t header;
  cr_assert_geq(length, sizeof(header));
  memcpy(&header, data, sizeof(header));
  response_type = NET_TO_HOST_U16(header.type);
  response_size = length - sizeof(header);
  cr_assert_leq(response_size, sizeof(response));
  memcpy(response, (const uint8_t *)data + sizeof(header), response_size);
  if (response_type == PACKET_TYPE_ACIP_SESSION_CREATED) {
    cr_assert_geq(response_size, sizeof(created));
    memcpy(&created, response, sizeof(created));
    ++created_responses;
  }
  if (response_type == PACKET_TYPE_ACIP_SESSION_JOINED) {
    cr_assert_geq(response_size, sizeof(joined));
    memcpy(&joined, response, sizeof(joined));
    ++joined_responses;
  }
  return ASCIICHAT_OK;
}
static const acip_transport_methods_t service_methods = {.send = service_send};
static acds_client_data_t creator_data, joiner_data;
static acip_transport_t creator_transport = {.methods = &service_methods, .user_data = &creator_data};
static acip_transport_t joiner_transport = {.methods = &service_methods, .user_data = &joiner_data};
static acds_config_t service_config(void) {
  acds_config_t config = {0};
  SAFE_STRNCPY(config.database_path, ":memory:", sizeof(config.database_path));
  return config;
}
static void setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  errno_suppress(true);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
}
static void teardown(void) {
  if (authority_ready)
    acds_server_shutdown(&authority);
  network_server_destroy(&service_network);
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TestSuite(discovery_service_callbacks, .init = setup, .fini = teardown, .timeout = 20);
static void make_network(void) {
  network_server_config_t config = {.port = 27001,
                                    .ipv4_address = "127.0.0.1",
                                    .bind_ipv4 = true,
                                    .max_connections = 8,
                                    .callbacks = {.packet = acds_connection_packet},
                                    .user_data = &authority};
  cr_assert_eq(network_server_create(&config, &service_network), ASCIICHAT_OK);
}
static void initialize(void) {
  make_network();
  acds_config_t config = service_config();
  cr_assert_eq(acds_server_init(&authority, &config), ASCIICHAT_OK);
  authority_ready = true;
}
static acip_session_create_t create_request(void) {
  acip_session_create_t req = {.timestamp = time_ns_to_ms(time_get_realtime_ns()),
                               .capabilities = 3,
                               .max_participants = 3,
                               .session_type = SESSION_TYPE_WEBRTC};
  return req;
}
static void create_session_fixture(void) {
  acip_session_create_t req = create_request();
  acds_on_session_create(&req, &creator_transport, "127.0.0.1", &authority);
  cr_assert_eq(created_responses, 1);
  cr_assert(creator_data.joined_session);
  cr_assert(creator_data.session_established);
}
Test(discovery_service_callbacks, initialization_failures_clear_ownership_and_allow_idempotent_shutdown) {
  acds_config_t config = service_config();
  cr_assert_eq(acds_server_init(&authority, &config), ERROR_INVALID_STATE);
  cr_assert_null(authority.db);
  cr_assert_null(authority.rate_limiter);
  cr_assert_null(authority.worker_pool);
  acds_server_shutdown(&authority);
  acds_server_shutdown(&authority);
}
Test(discovery_service_callbacks, database_and_rate_factory_failures_do_not_publish_partial_services) {
  acds_config_t config = service_config();
  SAFE_STRNCPY(config.database_path, "/missing/ascii-coverage/database.sqlite", sizeof(config.database_path));
  cr_assert_neq(acds_server_init(&authority, &config), ASCIICHAT_OK);
  cr_assert_null(authority.db);
  CLEAR_ERRNO_ALL();
  config = service_config();
  fail_rate_create = true;
  cr_assert_eq(acds_server_init(&authority, &config), ERROR_MEMORY);
  cr_assert_null(authority.db);
  cr_assert_null(authority.rate_limiter);
  acds_server_shutdown(&authority);
}
Test(discovery_service_callbacks, worker_pool_failure_cleans_database_and_rate_limiter) {
  make_network();
  fail_pool_create = true;
  acds_config_t config = service_config();
  cr_assert_eq(acds_server_init(&authority, &config), ERROR_MEMORY);
  cr_assert_null(authority.db);
  cr_assert_null(authority.rate_limiter);
  cr_assert_null(authority.worker_pool);
  acds_server_shutdown(&authority);
}
Test(discovery_service_callbacks, session_create_lookup_join_and_leave_commit_matching_connection_membership) {
  initialize();
  create_session_fixture();
  acip_session_lookup_t lookup = {.session_string_len = created.session_string_len};
  memcpy(lookup.session_string, created.session_string, created.session_string_len);
  acds_on_session_lookup(&lookup, &joiner_transport, "127.0.0.2", &authority);
  cr_assert_eq(response_type, PACKET_TYPE_ACIP_SESSION_INFO);
  acip_session_info_t info;
  memcpy(&info, response, sizeof(info));
  cr_assert(info.found);
  acip_session_join_t request = {.session_string_len = created.session_string_len,
                                 .timestamp = time_ns_to_ms(time_get_realtime_ns())};
  memcpy(request.session_string, created.session_string, created.session_string_len);
  uint8_t secret[64];
  cr_assert_eq(crypto_sign_keypair(request.identity_pubkey, secret), 0);
  cr_assert_eq(acds_sign_session_join(secret, request.timestamp, request.session_string, request.signature),
               ASCIICHAT_OK);
  sodium_memzero(secret, sizeof(secret));
  acds_on_session_join(&request, &joiner_transport, "127.0.0.2", &authority);
  cr_assert_eq(joined_responses, 1);
  cr_assert(joined.success);
  cr_assert(joiner_data.joined_session);
  cr_assert_arr_eq(joiner_data.session_id, created.session_id, 16);
  cr_assert_gt(broadcasts, 0);
  session_entry_t *entry = database_session_find_by_id(authority.db, created.session_id);
  cr_assert_not_null(entry);
  cr_assert_eq(entry->current_participants, 1);
  session_entry_destroy(entry);
  acip_session_leave_t leave = {0};
  memcpy(leave.session_id, joiner_data.session_id, 16);
  memcpy(leave.participant_id, joiner_data.participant_id, 16);
  leave.participant_id[0] ^= 1;
  acds_on_session_leave(&leave, &joiner_transport, "127.0.0.2", &authority);
  cr_assert(joiner_data.joined_session);
  leave.participant_id[0] ^= 1;
  broadcast_result = ERROR_NETWORK;
  acds_on_session_leave(&leave, &joiner_transport, "127.0.0.2", &authority);
  cr_assert_not(joiner_data.joined_session);
  entry = database_session_find_by_id(authority.db, created.session_id);
  cr_assert_null(entry);
}
Test(discovery_service_callbacks, invalid_creation_and_address_claims_do_not_publish_a_session) {
  initialize();
  acip_session_create_t request = create_request();
  creator_transport.user_data = NULL;
  acds_on_session_create(&request, &creator_transport, "127.0.0.1", &authority);
  cr_assert_eq(response_type, PACKET_TYPE_ERROR_MESSAGE);
  creator_transport.user_data = &creator_data;
  request.total_keys = MAX_SESSION_KEYS + 1;
  acds_on_session_create(&request, &creator_transport, "127.0.0.1", &authority);
  cr_assert_not(creator_data.joined_session);
  cr_assert_eq(created_responses, 0);
  request = create_request();
  request.session_type = SESSION_TYPE_DIRECT_TCP;
  SAFE_STRNCPY(request.server_address, "127.0.0.99", sizeof(request.server_address));
  acds_on_session_create(&request, &creator_transport, "127.0.0.1", &authority);
  cr_assert_not(creator_data.joined_session);
  cr_assert_eq(created_responses, 0);
  cr_assert_eq(creator_data.create_keys.count, 0);
}
Test(discovery_service_callbacks, rate_limit_rejection_and_backend_failure_clear_pending_creation_proofs) {
  initialize();
  acip_session_create_t create = create_request();
  acip_session_join_t join = {0};
  acip_session_lookup_t lookup = {0};
  for (rate_mode = 1; rate_mode <= 2; ++rate_mode) {
    creator_data.create_keys.count = 1;
    acds_on_session_create(&create, &creator_transport, "127.0.0.1", &authority);
    cr_assert_eq(response_type, PACKET_TYPE_ERROR_MESSAGE);
    cr_assert_eq(creator_data.create_keys.count, 0);
    cr_assert_not(creator_data.joined_session);
    acds_on_session_lookup(&lookup, &creator_transport, "127.0.0.1", &authority);
    cr_assert_eq(response_type, PACKET_TYPE_ERROR_MESSAGE);
    acds_on_session_join(&join, &creator_transport, "127.0.0.1", &authority);
    cr_assert_eq(response_type, PACKET_TYPE_ERROR_MESSAGE);
  }
}
Test(discovery_service_callbacks, unknown_session_join_returns_a_rejection_without_membership_mutation) {
  initialize();
  acip_session_join_t request = {.session_string_len = 13, .timestamp = time_ns_to_ms(time_get_realtime_ns())};
  memcpy(request.session_string, "missing-words", 13);
  acds_on_session_join(&request, &joiner_transport, "127.0.0.2", &authority);
  cr_assert_eq(joined_responses, 1);
  cr_assert_not(joined.success);
  cr_assert_not(joiner_data.joined_session);
  acip_session_leave_t leave = {0};
  acds_on_session_leave(&leave, &joiner_transport, "127.0.0.2", &authority);
  cr_assert_eq(response_type, PACKET_TYPE_ERROR_MESSAGE);
  cr_assert_eq(acds_remove_client_from_session(&authority, NULL), ASCIICHAT_OK);
  cr_assert_eq(acds_remove_client_from_session(NULL, &joiner_data), ASCIICHAT_OK);
}

Test(discovery_service_callbacks, connection_open_installs_identity_and_closing_releases_membership) {
  initialize();
  authority.config.require_client_identity = true;
  service_connection_t c = {.transport = {.methods = &service_methods}, .secure = true, .state = NETWORK_READY};
  cr_assert_eq(crypto_sign_keypair(authority.identity_public, authority.identity_secret), 0);
  network_connection_t *connection = (network_connection_t *)&c;
  cr_assert_eq(acds_connection_open(connection, &authority), ASCIICHAT_OK);
  acds_client_data_t *client = c.context;
  cr_assert_not_null(client);
  cr_assert_eq(c.transport.user_data, client);
  cr_assert(client->negotiation.initialized);
  cr_assert(client->negotiation.secure);
  cr_assert(client->negotiation.require_auth);
  cr_assert(c.handshake.require_client_auth);
  cr_assert(c.handshake.allow_unlisted_client_identity);
  cr_assert_arr_eq(c.handshake.server_public_key.key, authority.identity_public, 32);
  cr_assert_arr_eq(c.handshake.server_private_key.key.ed25519, authority.identity_secret, 64);
  authority.config.require_client_identity = false;
  create_session_fixture();
  acip_session_join_t join = {.session_string_len = created.session_string_len,
                              .timestamp = time_ns_to_ms(time_get_realtime_ns())};
  memcpy(join.session_string, created.session_string, created.session_string_len);
  acds_on_session_join(&join, &c.transport, "127.0.0.8", &authority);
  cr_assert(joined.success);
  cr_assert(client->joined_session);
  cr_assert_eq(acds_connection_closing(connection, &authority), ASCIICHAT_OK);
  cr_assert_not(client->joined_session);
  session_entry_t *entry = database_session_find_by_id(authority.db, created.session_id);
  cr_assert_null(entry);
  crypto_handshake_destroy(&c.handshake);
  acds_connection_closed(connection, &authority);
  cr_assert_null(c.context);
  acds_connection_closed(connection, &authority);
  cr_assert_eq(acds_connection_closing(connection, &authority), ASCIICHAT_OK);
}
Test(discovery_service_callbacks, partial_creation_allows_keepalive_but_blocks_lookup_and_counts_wire_bytes) {
  initialize();
  service_connection_t c = {.transport = {.methods = &service_methods}, .state = NETWORK_READY};
  network_connection_t *connection = (network_connection_t *)&c;
  cr_assert_eq(acds_connection_open(connection, &authority), ASCIICHAT_OK);
  acds_client_data_t *client = c.context;
  client->create_keys.count = 1;
  acip_session_lookup_t lookup = {0};
  cr_assert_eq(acds_connection_packet(connection, PACKET_TYPE_ACIP_SESSION_LOOKUP, &lookup, sizeof(lookup), &authority),
               ASCIICHAT_OK);
  cr_assert_eq(response_type, PACKET_TYPE_ERROR_MESSAGE);
  cr_assert_eq(client->create_keys.count, 1);
  cr_assert_eq(atomic_load_u64(&authority.stats.packets_received), 1);
  cr_assert_eq(atomic_load_u64(&authority.stats.bytes_received), sizeof(lookup) + sizeof(packet_header_t));
  cr_assert_eq(acds_connection_packet(connection, PACKET_TYPE_PING, NULL, 0, &authority), ASCIICHAT_OK);
  cr_assert_eq(client->create_keys.count, 1);
  cr_assert_eq(acds_connection_packet(connection, PACKET_TYPE_PONG, NULL, 0, &authority), ASCIICHAT_OK);
  cr_assert_eq(atomic_load_u64(&authority.stats.packets_received), 3);
  client->create_keys.count = 0;
  cr_assert_eq(
      acds_connection_packet(connection, PACKET_TYPE_ACIP_SESSION_LOOKUP, &lookup, sizeof(lookup) - 1, &authority),
      ERROR_INVALID_PARAM);
  crypto_handshake_destroy(&c.handshake);
  acds_connection_closed(connection, &authority);
}

Test(discovery_service_callbacks, sent_byte_statistics_include_the_serialized_packet_header) {
  initialize();
  create_session_fixture();
  cr_assert_eq(atomic_load_u64(&authority.stats.packets_sent), 1);
  cr_assert_eq(atomic_load_u64(&authority.stats.bytes_sent), sizeof(packet_header_t) + sizeof(acip_session_created_t));
}

// Application startup configuration is tested independently of listener ownership.
static acds_config_t startup_config;
static unsigned startup_calls, shutdown_queries;
static unsigned cancel_on_query;
static asciichat_error_t startup_result;
static asciichat_error_t startup_server_init(acds_server_t *server, const acds_config_t *config) {
  ++startup_calls;
  startup_config = *config;
  memset(server, 0, sizeof(*server));
  return startup_result;
}
static bool startup_cancelled(void) {
  return cancel_on_query && ++shutdown_queries >= cancel_on_query;
}
static void (*startup_interrupt)(void);
static void startup_set_interrupt(void (*callback)(void)) {
  startup_interrupt = callback;
}
static session_server_like_config_t startup_run_config;
static asciichat_error_t startup_run(const session_server_like_config_t *config) {
  startup_run_config = *config;
  cr_assert_not_null(startup_interrupt);
  return ERROR_NETWORK;
}
#define acds_server_init startup_server_init
#define session_server_like_shutdown_requested startup_cancelled
#define session_server_like_run startup_run
#define set_interrupt_callback startup_set_interrupt
#include "../../../src/discovery-service/main.c"
#undef acds_server_init
#undef session_server_like_shutdown_requested
#undef session_server_like_run
#undef set_interrupt_callback
static char startup_path[PLATFORM_MAX_PATH_LENGTH];
static uint8_t startup_public[32];
static void startup_identity(void) {
  int fd = -1;
  cr_assert_eq(platform_create_temp_file(startup_path, sizeof(startup_path), "authority-config", &fd), 0);
  cr_assert_eq(platform_close(fd), 0);
  uint8_t secret[64];
  cr_assert_eq(crypto_sign_keypair(startup_public, secret), 0);
  cr_assert_eq(acds_identity_save(startup_path, startup_public, secret), ASCIICHAT_OK);
  sodium_memzero(secret, sizeof(secret));
  options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
  *v = *options_get();
  SAFE_STRNCPY(v->encrypt_key, startup_path, sizeof(v->encrypt_key));
  v->port = 27002;
  cr_assert_eq(options_state_set(v), ASCIICHAT_OK);
  SAFE_FREE(v);
}
Test(discovery_service_callbacks, startup_options_trim_and_bound_servers_and_install_identity_after_initialization) {
  startup_identity();
  options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
  *v = *options_get();
  v->require_server_identity = true;
  v->require_client_identity = true;
  SAFE_STRNCPY(v->address, "127.0.0.4", sizeof(v->address));
  SAFE_STRNCPY(v->address6, "::1", sizeof(v->address6));
  SAFE_STRNCPY(v->discovery_database_path, ":memory:", sizeof(v->discovery_database_path));
  SAFE_STRNCPY(v->log_file, "authority-test.log", sizeof(v->log_file));
  SAFE_STRNCPY(v->stun_servers, "  stun:one  , ,stun:two,stun:three,stun:four,stun:ignored", sizeof(v->stun_servers));
  SAFE_STRNCPY(v->turn_servers, " turn:one ,	turn:two	,turn:three,turn:four,turn:ignored",
               sizeof(v->turn_servers));
  SAFE_STRNCPY(v->turn_username, "fixture-user", sizeof(v->turn_username));
  SAFE_STRNCPY(v->turn_credential, "fixture-password", sizeof(v->turn_credential));
  SAFE_STRNCPY(v->turn_secret, "fixture-hmac", sizeof(v->turn_secret));
  cr_assert_eq(options_state_set(v), ASCIICHAT_OK);
  SAFE_FREE(v);
  cr_assert_eq(acds_init_fn(NULL), ASCIICHAT_OK);
  cr_assert_eq(startup_calls, 1);
  cr_assert_eq(startup_config.port, 27002);
  cr_assert_str_eq(startup_config.address, "127.0.0.4");
  cr_assert_str_eq(startup_config.address6, "::1");
  cr_assert_str_eq(startup_config.log_file, "authority-test.log");
  cr_assert_str_eq(startup_config.database_path, ":memory:");
  cr_assert(startup_config.require_server_identity);
  cr_assert(startup_config.require_client_identity);
  cr_assert_eq(startup_config.stun_count, 4);
  cr_assert_str_eq(startup_config.stun_servers[0].host, "stun:one");
  cr_assert_str_eq(startup_config.stun_servers[3].host, "stun:four");
  cr_assert_eq(startup_config.turn_count, 4);
  cr_assert_str_eq(startup_config.turn_servers[1].url, "turn:two");
  cr_assert_str_eq(startup_config.turn_servers[0].username, "fixture-user");
  cr_assert_str_eq(startup_config.turn_servers[0].credential, "fixture-password");
  cr_assert_str_eq(startup_config.turn_secret, "fixture-hmac");
  cr_assert_arr_eq(g_server.identity_public, startup_public, 32);
  cr_assert_not(sodium_is_zero(g_server.identity_secret, 64));
  stats_runtime_set_provider(NULL, NULL);
  cr_assert_eq(platform_unlink(startup_path), 0);
}
Test(discovery_service_callbacks, startup_rejects_bad_port_or_identity_and_honors_cancellation_before_and_after_init) {
  startup_identity();
  options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
  *v = *options_get();
  v->port = 0;
  cr_assert_eq(options_state_set(v), ASCIICHAT_OK);
  SAFE_FREE(v);
  cr_assert_eq(acds_init_fn(NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(startup_calls, 0);
  cr_assert_eq(options_set_int("port", 27002), ASCIICHAT_OK);
  cancel_on_query = 1;
  cr_assert_eq(acds_init_fn(NULL), ASCIICHAT_OK);
  cr_assert_eq(startup_calls, 0);
  cancel_on_query = 2;
  shutdown_queries = 0;
  cr_assert_eq(acds_init_fn(NULL), ASCIICHAT_OK);
  cr_assert_eq(startup_calls, 1);
  cr_assert_arr_eq(g_server.identity_public, startup_public, 32);
  cancel_on_query = 0;
  startup_result = ERROR_CONFIG;
  cr_assert_eq(acds_init_fn(NULL), ERROR_CONFIG);
  cr_assert_eq(startup_calls, 2);
  cr_assert(sodium_is_zero(g_server.identity_secret, 64));
  FILE *f = platform_fopen("invalid-authority-key", startup_path, "wb");
  cr_assert_not_null(f);
  cr_assert_eq(fwrite("invalid", 1, 7, f), 7);
  cr_assert_eq(fclose(f), 0);
  cr_assert_neq(acds_init_fn(NULL), ASCIICHAT_OK);
  cr_assert_eq(startup_calls, 2);
  cr_assert_eq(platform_unlink(startup_path), 0);
}
Test(discovery_service_callbacks, startup_discards_overlong_ice_urls_and_credentials_instead_of_truncating_them) {
  startup_identity();
  options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
  *v = *options_get();
  memset(v->stun_servers, 's', 80);
  strcpy(v->stun_servers + 80, ", ,stun:valid");
  memset(v->turn_servers, 't', 80);
  strcpy(v->turn_servers + 80, ", ,turn:valid");
  memset(v->turn_username, 'u', sizeof(startup_config.turn_servers[0].username));
  v->turn_username[sizeof(startup_config.turn_servers[0].username)] = 0;
  memset(v->turn_credential, 'p', sizeof(startup_config.turn_servers[0].credential));
  v->turn_credential[sizeof(startup_config.turn_servers[0].credential)] = 0;
  v->turn_secret[0] = 0;
  v->address[0] = v->address6[0] = v->log_file[0] = 0;
  cr_assert_eq(options_state_set(v), ASCIICHAT_OK);
  SAFE_FREE(v);
  cr_assert_eq(acds_init_fn(NULL), ASCIICHAT_OK);
  cr_assert_eq(startup_config.stun_count, 1);
  cr_assert_eq(startup_config.turn_count, 1);
  cr_assert_str_eq(startup_config.stun_servers[0].host, "stun:valid");
  cr_assert_str_eq(startup_config.turn_servers[0].url, "turn:valid");
  cr_assert_eq(startup_config.turn_servers[0].username_len, 0);
  cr_assert_eq(startup_config.turn_servers[0].credential_len, 0);
  cr_assert_eq(startup_config.turn_secret[0], 0);
  cr_assert_str_eq(startup_config.address, "127.0.0.1");
  cr_assert_str_eq(startup_config.address6, "::1");
  cr_assert_str_eq(startup_config.log_file, "acds.log");
  stats_runtime_set_provider(NULL, NULL);
  cr_assert_eq(platform_unlink(startup_path), 0);
}
Test(discovery_service_callbacks, entry_point_wires_lifecycle_callbacks_and_restores_interrupt_handler) {
  cr_assert_eq(acds_main(), ERROR_NETWORK);
  cr_assert_null(startup_interrupt);
  cr_assert_not_null(startup_run_config.init_fn);
  cr_assert_not_null(startup_run_config.cleanup_fn);
  cr_assert_not_null(startup_run_config.interrupt_fn);
  cr_assert_not_null(startup_run_config.connections.packet);
  cr_assert_eq(startup_run_config.connection_user_data, &g_server);
  cr_assert(startup_run_config.websocket_enabled);
  cr_assert_eq(startup_run_config.partial_timeout_ms, 5000);
  cr_assert_eq(startup_run_config.idle_timeout_ms, 60000);
}
Test(discovery_service_callbacks, database_statistics_report_rows_totals_and_unavailability) {
  initialize();
  create_session_fixture();
  stats_view_t view = {0};
  acds_stats_provider(&view, &authority);
  cr_assert_eq(view.count, 1);
  cr_assert(strstr(view.lines[0], "Active sessions: 1"));
  view = (stats_view_t){.page = 1};
  acds_stats_provider(&view, &authority);
  cr_assert_geq(view.count, 4);
  cr_assert(strstr(view.lines[1], "PARTICIPANTS"));
  cr_assert(strstr(view.lines[2], created.session_string));
  acds_server_t absent = {0};
  view = (stats_view_t){0};
  acds_stats_provider(&view, &absent);
  cr_assert_eq(view.count, 1);
  cr_assert(strstr(view.lines[0], "unavailable"));
}

static void run_periodic_cleanup(bool prune_failure) {
  initialize();
  create_session_fixture();
  cr_assert_eq(sqlite3_exec(authority.db, "UPDATE sessions SET last_activity_at=1", NULL, NULL, NULL), SQLITE_OK);
  cleanup_owner = &authority;
  fail_prune = prune_failure;
  cr_assert_null(cleanup_thread_func(NULL));
  cr_assert_null(cleanup_thread_func(&authority));
  cr_assert_eq(cleanup_sleeps, 3000);
  cr_assert_eq(cleanup_prunes, 1);
  cr_assert_eq(cleanup_sessions, 1);
  cr_assert(atomic_load_bool(&authority.shutdown));
  cr_assert_null(database_session_find_by_id(authority.db, created.session_id));
  cleanup_owner = NULL;
}
Test(discovery_service_callbacks, periodic_cleanup_expires_inactive_sessions_after_the_complete_poll_interval) {
  run_periodic_cleanup(false);
}
Test(discovery_service_callbacks, rate_cleanup_failure_does_not_prevent_expiring_inactive_sessions) {
  run_periodic_cleanup(true);
}
Test(discovery_service_callbacks, host_loss_requires_matching_transport_membership_before_mutating_handoff_state) {
  initialize();
  create_session_fixture();
  consensus_handoff_t *state = acds_handoff_find(&authority, created.session_id, true);
  cr_assert_not_null(state);
  acip_host_lost_t lost = {0};
  memcpy(lost.session_id, created.session_id, sizeof(lost.session_id));
  memcpy(lost.participant_id, creator_data.participant_id, sizeof(lost.participant_id));
  memcpy(lost.last_host_id, state->host, sizeof(lost.last_host_id));
  lost.participant_id[0] ^= 1;
  acds_on_host_lost(&lost, &creator_transport, "127.0.0.1", &authority);
  cr_assert_eq(response_type, PACKET_TYPE_ERROR_MESSAGE);
  cr_assert_not(state->host_lost);
  lost.participant_id[0] ^= 1;
  lost.session_id[0] ^= 1;
  acds_on_host_lost(&lost, &creator_transport, "127.0.0.1", &authority);
  cr_assert_not(state->host_lost);
  lost.session_id[0] ^= 1;
  creator_data.joined_session = false;
  acds_on_host_lost(&lost, &creator_transport, "127.0.0.1", &authority);
  cr_assert_not(state->host_lost);
  creator_data.joined_session = true;
  acds_on_host_lost(&lost, &creator_transport, "127.0.0.1", &authority);
  cr_assert(state->host_lost);
  cr_assert_eq(creator_data.joined_session, true);
}
Test(discovery_service_callbacks, malformed_handoff_and_signaling_requests_reply_without_changing_membership) {
  initialize();
  create_session_fixture();
  uint8_t bytes[CONSENSUS_HANDOFF_HEADER] = {0};
  consensus_handoff_message_t handoff = {.kind = HANDOFF_READY};
  memcpy(handoff.session, created.session_id, sizeof(handoff.session));
  memcpy(handoff.participant, creator_data.participant_id, sizeof(handoff.participant));
  cr_assert_neq(acds_on_handoff(bytes, 1, &creator_transport, &authority), ASCIICHAT_OK);
  handoff.participant[0] ^= 1;
  consensus_handoff_encode(&handoff, bytes);
  cr_assert_eq(acds_on_handoff(bytes, sizeof(bytes), &creator_transport, &authority), ERROR_NETWORK_PROTOCOL);
  acip_webrtc_sdp_t sdp = {0};
  acds_on_webrtc_sdp(&sdp, sizeof(sdp), &creator_transport, "127.0.0.1", &authority);
  cr_assert_eq(response_type, PACKET_TYPE_ERROR_MESSAGE);
  acip_webrtc_ice_t ice = {0};
  acds_on_webrtc_ice(&ice, sizeof(ice), &creator_transport, "127.0.0.1", &authority);
  cr_assert_eq(response_type, PACKET_TYPE_ERROR_MESSAGE);
  cr_assert(creator_data.joined_session);
  cr_assert_arr_eq(creator_data.session_id, created.session_id, sizeof(created.session_id));
  acds_server_shutdown(NULL);
  cr_assert_eq(acds_server_init(NULL, NULL), ERROR_INVALID_PARAM);
}
