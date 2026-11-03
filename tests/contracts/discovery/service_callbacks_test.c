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
#include "../../../src/discovery-service/server.c"
#undef session_server_like_get_network_server
#undef rate_limiter_create_sqlite
#undef rate_limiter_check
#undef thread_pool_create
#undef thread_pool_spawn
#undef signaling_broadcast
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
  log_init(NULL, LOG_FATAL, false, false);
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
