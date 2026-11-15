#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/crypto/keys.h>
#include <ascii-chat/audio/mixer.h>
#include <ascii-chat/network/rate_limit/rate_limit.h>
#include "../../../src/session/h265/server.h"
static bool fail_resource, exit_during_resource;
static asciichat_error_t contract_validate_key(const char *path) {
  return strncmp(path, "bad", 3) == 0 ? ERROR_CRYPTO_KEY : ASCIICHAT_OK;
}
static asciichat_error_t contract_parse_private(const char *path, private_key_t *out) {
  if (strstr(path, "parsefail"))
    return ERROR_CRYPTO_KEY;
  memset(out, 0, sizeof(*out));
  out->type = KEY_TYPE_ED25519;
  memset(out->public_key, 0x42, sizeof(out->public_key));
  memset(out->key.ed25519, 0x24, sizeof(out->key.ed25519));
  return ASCIICHAT_OK;
}
static asciichat_error_t contract_parse_public(const char *path, public_key_t *out, size_t *count, size_t max) {
  if (strcmp(path, "bad-whitelist") == 0)
    return ERROR_CRYPTO_KEY;
  if (!max)
    return ERROR_INVALID_PARAM;
  out[0].type = KEY_TYPE_ED25519;
  memset(out[0].key, 0x42, sizeof(out[0].key));
  *count = 1;
  return ASCIICHAT_OK;
}
static rate_limiter_t *contract_rate_create(void);
static mixer_t *contract_mixer_create(int sources, int rate, int channels);
static h265_server_context_t *contract_h265_create(void);
#include <ascii-chat/network/webrtc/peer_manager.h>
#include <ascii-chat/network/connection.h>
#include <ascii-chat/network/mdns/mdns.h>
#include <ascii-chat/tui/notice.h>
#include "session/server_like.h"
static asciichat_mdns_t *advertisement_context;
static unsigned advertisement_calls, disclosure_prompts;
static asciichat_error_t advertisement_result;
static bool can_prompt, confirm_disclosure;
static char advertisement_txt[2][256];
static uint16_t advertisement_port;
static asciichat_mdns_t *contract_mdns_context(void) {
  return advertisement_context;
}
static asciichat_error_t contract_advertise(asciichat_mdns_t *context, const asciichat_mdns_service_t *service) {
  REQUIRE((context) == (advertisement_context));
  REQUIRE((service->name) != nullptr);
  REQUIRE((service->host) != nullptr);
  REQUIRE(std::strcmp((service->type), ("_ascii-chat._tcp")) == 0);
  REQUIRE((service->txt_count) == (2));
  for (unsigned i = 0; i < 2; ++i)
    SAFE_STRNCPY(advertisement_txt[i], service->txt_records[i], sizeof(advertisement_txt[i]));
  advertisement_port = service->port;
  ++advertisement_calls;
  return advertisement_result;
}
static bool contract_can_prompt(bool user_setting) {
  (void)user_setting;
  return can_prompt;
}
static bool contract_disclosure(ui_prompt_kind_t kind, ui_notice_severity_t severity, const char *text, unsigned timeout) {
  REQUIRE((kind) == (UI_PROMPT_IP_DISCLOSURE));
  REQUIRE((severity) == (UI_NOTICE_DANGER));
  REQUIRE((strstr(text, "PUBLIC IP DISCLOSURE")) != nullptr);
  REQUIRE((timeout) == (30));
  ++disclosure_prompts;
  return confirm_disclosure;
}
static unsigned sdp_calls, ice_calls, adopt_calls, destroyed_channels;
static const void *forwarded_signal;
static asciichat_error_t signal_result, adopt_result;
static char adopted_id[33];
static asciichat_error_t contract_sdp(webrtc_peer_manager_t *manager, const acip_webrtc_sdp_t *message) {
  REQUIRE((manager) != nullptr);
  ++sdp_calls;
  forwarded_signal = message;
  return signal_result;
}
static asciichat_error_t contract_ice(webrtc_peer_manager_t *manager, const acip_webrtc_ice_t *message) {
  (void)manager;
  ++ice_calls;
  forwarded_signal = message;
  return signal_result;
}
static asciichat_error_t contract_adopt(network_server_t *server, acip_transport_t *transport, const char *id, bool authenticated) {
  (void)server;
  REQUIRE((transport) != nullptr);
  REQUIRE(authenticated);
  SAFE_STRNCPY(adopted_id, id, sizeof(adopted_id));
  ++adopt_calls;
  return adopt_result;
}
#define session_server_like_get_mdns_ctx contract_mdns_context
#define asciichat_mdns_advertise contract_advertise
#define terminal_can_prompt_user contract_can_prompt
#define ui_notice_confirm_kind contract_disclosure
#define webrtc_peer_manager_handle_sdp contract_sdp
#define webrtc_peer_manager_handle_ice contract_ice
#define network_server_adopt contract_adopt
#define validate_ssh_key_file contract_validate_key
#define parse_private_key contract_parse_private
#define parse_public_keys contract_parse_public
#define rate_limiter_create_memory contract_rate_create
#define mixer_create_channels contract_mixer_create
#define h265_server_context_create contract_h265_create
#include "../../../src/server/main.c"
#undef session_server_like_get_mdns_ctx
#undef asciichat_mdns_advertise
#undef terminal_can_prompt_user
#undef ui_notice_confirm_kind
#undef webrtc_peer_manager_handle_sdp
#undef webrtc_peer_manager_handle_ice
#undef network_server_adopt
#undef validate_ssh_key_file
#undef parse_private_key
#undef parse_public_keys
#undef rate_limiter_create_memory
#undef mixer_create_channels
#undef h265_server_context_create
static rate_limiter_t *contract_rate_create(void) {
  if (exit_during_resource)
    atomic_store_bool(&g_should_exit, true);
  return fail_resource ? NULL : rate_limiter_create_memory();
}
static mixer_t *contract_mixer_create(int sources, int rate, int channels) {
  if (exit_during_resource)
    atomic_store_bool(&g_should_exit, true);
  return fail_resource ? NULL : mixer_create_channels(sources, rate, channels);
}
static h265_server_context_t *contract_h265_create(void) {
  if (exit_during_resource)
    atomic_store_bool(&g_should_exit, true);
  return fail_resource ? NULL : h265_server_context_create();
}

atomic_t g_should_exit;
bool should_exit(void) {
  return atomic_load_bool(&g_should_exit);
}
void signal_exit(void) {
  atomic_store_bool(&g_should_exit, true);
}
void set_interrupt_callback(void (*callback)(void)) {
  (void)callback;
}

static unsigned contract_sends;
static packet_type_t contract_type;
static size_t contract_length;
static uint8_t contract_payload[16384];
static asciichat_error_t contract_send_result;
static asciichat_error_t contract_send(acip_transport_t *transport, const void *data, size_t length) {
  (void)transport;
  REQUIRE((length) >= (sizeof(packet_header_t)));
  packet_header_t header;
  memcpy(&header, data, sizeof(header));
  contract_type = NET_TO_HOST_U16(header.type);
  contract_length = length - sizeof(header);
  REQUIRE((contract_length) <= (sizeof(contract_payload)));
  memcpy(contract_payload, (const uint8_t *)data + sizeof(header), contract_length);
  ++contract_sends;
  return contract_send_result;
}
static const acip_transport_methods_t contract_methods = {.send = contract_send};
static acip_transport_t contract_transport = {.methods = &contract_methods};
static void contract_setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  REQUIRE((options_state_init()) == (ASCIICHAT_OK));
  contract_send_result = ASCIICHAT_OK;
}
static void contract_teardown(void) {
  g_acds_transport = NULL;
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TEST_CASE("server_main_contract/sdp_relay_preserves_ids_type_and_exact_nonterminated_payload", "[server_main_contract]") {
  uint8_t session[16] = {1}, recipient[16] = {2};
  g_server_participant_id[0] = 3;
  REQUIRE((server_send_sdp(session, recipient, "offer", "v=0", NULL)) == (ERROR_INVALID_STATE));
  CLEAR_ERRNO_ALL();
  g_acds_transport = &contract_transport;
  REQUIRE((server_send_sdp(session, recipient, "offer", "v=0", NULL)) == (ASCIICHAT_OK));
  REQUIRE((contract_type) == (PACKET_TYPE_ACIP_WEBRTC_SDP));
  acip_webrtc_sdp_t header;
  memcpy(&header, contract_payload, sizeof(header));
  REQUIRE(std::memcmp((header.session_id), (session), (16)) == 0);
  REQUIRE(std::memcmp((header.recipient_id), (recipient), (16)) == 0);
  REQUIRE(std::memcmp((header.sender_id), (g_server_participant_id), (16)) == 0);
  REQUIRE((header.sdp_type) == (0));
  REQUIRE((NET_TO_HOST_U16(header.sdp_len)) == (3));
  REQUIRE((contract_length) == (sizeof(header) + 3));
  REQUIRE(std::memcmp((contract_payload + sizeof(header)), ("v=0"), (3)) == 0);
  REQUIRE((server_send_sdp(session, recipient, "answer", "answer", NULL)) == (ASCIICHAT_OK));
  memcpy(&header, contract_payload, sizeof(header));
  REQUIRE((header.sdp_type) == (1));
  unsigned before = contract_sends;
  char oversized[8193];
  memset(oversized, 'x', sizeof(oversized) - 1);
  oversized[sizeof(oversized) - 1] = 0;
  REQUIRE((server_send_sdp(session, recipient, "offer", "", NULL)) == (ERROR_INVALID_PARAM));
  CLEAR_ERRNO_ALL();
  REQUIRE((server_send_sdp(session, recipient, "offer", oversized, NULL)) == (ERROR_INVALID_PARAM));
  CLEAR_ERRNO_ALL();
  REQUIRE((contract_sends) == (before));
  contract_send_result = ERROR_NETWORK;
  REQUIRE((server_send_sdp(session, recipient, "offer", "v=0", NULL)) == (ERROR_NETWORK));
}
TEST_CASE("server_main_contract/ice_relay_encodes_two_strings_and_bounds_combined_payload", "[server_main_contract]") {
  uint8_t session[16] = {4}, recipient[16] = {5};
  g_server_participant_id[0] = 6;
  REQUIRE((server_send_ice(session, recipient, "candidate", "0", NULL)) == (ERROR_INVALID_STATE));
  CLEAR_ERRNO_ALL();
  g_acds_transport = &contract_transport;
  REQUIRE((server_send_ice(session, recipient, "candidate", "0", NULL)) == (ASCIICHAT_OK));
  REQUIRE((contract_type) == (PACKET_TYPE_ACIP_WEBRTC_ICE));
  acip_webrtc_ice_t header;
  memcpy(&header, contract_payload, sizeof(header));
  REQUIRE((NET_TO_HOST_U16(header.candidate_len)) == (9));
  REQUIRE(std::memcmp((header.session_id), (session), (16)) == 0);
  REQUIRE(std::memcmp((header.recipient_id), (recipient), (16)) == 0);
  REQUIRE(std::memcmp((header.sender_id), (g_server_participant_id), (16)) == 0);
  REQUIRE((contract_length) == (sizeof(header) + 12));
  REQUIRE(std::memcmp((contract_payload + sizeof(header)), ("candidate\0"
                             "0\0"), (12)) == 0);
  char oversized[8193];
  memset(oversized, 'x', sizeof(oversized) - 1);
  oversized[sizeof(oversized) - 1] = 0;
  unsigned before = contract_sends;
  REQUIRE((server_send_ice(session, recipient, oversized, "0", NULL)) == (ERROR_INVALID_PARAM));
  CLEAR_ERRNO_ALL();
  REQUIRE((server_send_ice(session, recipient, "x", oversized, NULL)) == (ERROR_INVALID_PARAM));
  CLEAR_ERRNO_ALL();
  REQUIRE((contract_sends) == (before));
  contract_send_result = ERROR_NETWORK;
  REQUIRE((server_send_ice(session, recipient, "candidate", "0", NULL)) == (ERROR_NETWORK));
}

static options_t *server_options(void) {
  options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
  *v = *options_get();
  return v;
}
static void server_options_commit(options_t *v) {
  REQUIRE((options_state_set(v)) == (ASCIICHAT_OK));
  SAFE_FREE(v);
}
TEST_CASE("server_main_contract/identity_selection_skips_invalid_entries_and_commits_the_complete_loaded_set", "[server_main_contract]") {
  options_t *v = server_options();
  v->num_identity_keys = 4;
  v->identity_keys[0][0] = 0;
  SAFE_STRNCPY(v->identity_keys[1], "bad-file", sizeof(v->identity_keys[1]));
  SAFE_STRNCPY(v->identity_keys[2], "good-first", sizeof(v->identity_keys[2]));
  SAFE_STRNCPY(v->identity_keys[3], "gpg:good-second", sizeof(v->identity_keys[3]));
  SAFE_STRNCPY(v->client_keys, "good-whitelist", sizeof(v->client_keys));
  server_options_commit(v);
  REQUIRE((init_server_crypto()) == (0));
  REQUIRE((g_num_server_identity_keys) == (2));
  REQUIRE((g_num_whitelisted_clients) == (1));
  REQUIRE(std::memcmp((&g_server_private_key), (&g_server_identity_keys[0]), (sizeof(g_server_private_key))) == 0);
  REQUIRE(g_server_encryption_enabled);
  REQUIRE((g_client_whitelist[0].type) == (KEY_TYPE_ED25519));
}
TEST_CASE("server_main_contract/identity_failures_leave_encryption_disabled_and_report_key_errors", "[server_main_contract]") {
  options_t *v = server_options();
  v->num_identity_keys = 2;
  SAFE_STRNCPY(v->identity_keys[0], "bad-file", sizeof(v->identity_keys[0]));
  SAFE_STRNCPY(v->identity_keys[1], "github:parsefail", sizeof(v->identity_keys[1]));
  server_options_commit(v);
  REQUIRE((init_server_crypto()) == (-1));
  REQUIRE((g_num_server_identity_keys) == (0));
  REQUIRE(!(g_server_encryption_enabled));
  REQUIRE((GET_ERRNO()) == (ERROR_CRYPTO_KEY));
  CLEAR_ERRNO_ALL();
}
TEST_CASE("server_main_contract/legacy_single_identity_and_whitelist_failures_are_distinct_from_simple_mode", "[server_main_contract]") {
  options_t *v = server_options();
  v->num_identity_keys = 0;
  SAFE_STRNCPY(v->encrypt_key, "bad-file", sizeof(v->encrypt_key));
  server_options_commit(v);
  REQUIRE((init_server_crypto()) == (-1));
  CLEAR_ERRNO_ALL();
  v = server_options();
  SAFE_STRNCPY(v->encrypt_key, "gitlab:parsefail", sizeof(v->encrypt_key));
  server_options_commit(v);
  REQUIRE((init_server_crypto()) == (-1));
  CLEAR_ERRNO_ALL();
  v = server_options();
  SAFE_STRNCPY(v->encrypt_key, "good-file", sizeof(v->encrypt_key));
  SAFE_STRNCPY(v->client_keys, "bad-whitelist", sizeof(v->client_keys));
  server_options_commit(v);
  REQUIRE((init_server_crypto()) == (-1));
  REQUIRE((g_num_server_identity_keys) == (1));
  CLEAR_ERRNO_ALL();
  v = server_options();
  v->client_keys[0] = 0;
  server_options_commit(v);
  REQUIRE((init_server_crypto()) == (0));
  REQUIRE(g_server_encryption_enabled);
}
TEST_CASE("server_main_contract/discovery_request_preserves_security_policy_and_falls_back_when_mapping_expires", "[server_main_contract]") {
  options_t *v = server_options();
  v->webrtc = false;
  v->max_clients = 3;
  SAFE_STRNCPY(v->address, "127.0.0.1", sizeof(v->address));
  server_options_commit(v);
  server_discovery_policy_t policy = {.has_identity = true, .has_password = true, .password = "fixture-secret", .expose_ip = false};
  g_server_encryption_enabled = true;
  memset(g_server_private_key.public_key, 0x42, 32);
  memset(g_server_private_key.key.ed25519, 0x24, 64);
  acds_session_create_params_t params = {0};
  server_prepare_discovery_request(&params, &policy, 27001, NULL, true);
  REQUIRE((params.session_type) == (SESSION_TYPE_WEBRTC));
  REQUIRE((params.max_participants) == (3));
  REQUIRE(params.has_password);
  REQUIRE(std::strcmp((params.password), ("fixture-secret")) == 0);
  REQUIRE(!(params.acds_expose_ip));
  REQUIRE(std::memcmp((params.identity_pubkey), (g_server_private_key.public_key), (32)) == 0);
  REQUIRE(std::memcmp((params.identity_seckey), (g_server_private_key.key.ed25519), (64)) == 0);
  policy.has_identity = policy.has_password = false;
  g_server_encryption_enabled = false;
  v = server_options();
  SAFE_STRNCPY(v->address, "0.0.0.0", sizeof(v->address));
  server_options_commit(v);
  memset(&params, 0, sizeof(params));
  server_prepare_discovery_request(&params, &policy, 27002, NULL, false);
  REQUIRE((params.server_address[0]) == (0));
  REQUIRE((params.server_port) == (27002));
  REQUIRE(!(params.has_password));
  REQUIRE(std::memcmp((params.identity_pubkey), (((uint8_t[32]){0})), (32)) == 0);
}
TEST_CASE("server_main_contract/turn_server_list_requires_credentials_trims_entries_and_bounds_capacity", "[server_main_contract]") {
  turn_server_t servers[MAX_CONFIGURED_TURN_SERVERS] = {0};
  REQUIRE((server_configure_turn(servers, "", "secret")) == (0));
  REQUIRE((server_configure_turn(servers, "user", "")) == (0));
  options_t *v = server_options();
  SAFE_STRNCPY(v->turn_servers, " turn:one , \tturn:two\t ,   ,turn:three,turn:four,turn:ignored", sizeof(v->turn_servers));
  server_options_commit(v);
  REQUIRE((server_configure_turn(servers, "local-user", "local-secret")) == (4));
  REQUIRE(std::strcmp((servers[0].url), ("turn:one")) == 0);
  REQUIRE(std::strcmp((servers[1].url), ("turn:two")) == 0);
  REQUIRE(std::strcmp((servers[3].url), ("turn:four")) == 0);
  for (unsigned i = 0; i < 4; ++i) {
    REQUIRE(std::strcmp((servers[i].username), ("local-user")) == 0);
    REQUIRE(std::strcmp((servers[i].credential), ("local-secret")) == 0);
  }
}
TEST_CASE("server_main_contract/resource_initializers_propagate_failure_and_shutdown_supersedes_startup_errors", "[server_main_contract]") {
  fail_resource = true;
  REQUIRE((server_init_rate_limiter()) == (ERROR_MEMORY));
  CLEAR_ERRNO_ALL();
  REQUIRE((g_rate_limiter) == nullptr);
  REQUIRE((server_init_audio_mixer()) == (ERROR_AUDIO));
  CLEAR_ERRNO_ALL();
  REQUIRE((g_audio_mixer) == nullptr);
  REQUIRE((server_init_h265()) == (ERROR_MEDIA_INIT));
  CLEAR_ERRNO_ALL();
  REQUIRE((g_h265_server) == nullptr);
  exit_during_resource = true;
  REQUIRE((server_init_rate_limiter()) == (ASCIICHAT_OK));
  atomic_store_bool(&g_should_exit, false);
  REQUIRE((server_init_audio_mixer()) == (ASCIICHAT_OK));
  atomic_store_bool(&g_should_exit, false);
  REQUIRE((server_init_h265()) == (ASCIICHAT_OK));
  REQUIRE((server_init_rate_limiter()) == (ASCIICHAT_OK));
  REQUIRE((server_init_audio_mixer()) == (ASCIICHAT_OK));
  REQUIRE((server_init_h265()) == (ASCIICHAT_OK));
  atomic_store_bool(&g_should_exit, false);
  fail_resource = false;
  exit_during_resource = false;
  REQUIRE((server_init_rate_limiter()) == (ASCIICHAT_OK));
  REQUIRE((g_rate_limiter) != nullptr);
  REQUIRE((server_init_audio_mixer()) == (ASCIICHAT_OK));
  REQUIRE((g_audio_mixer) != nullptr);
  REQUIRE((server_init_h265()) == (ASCIICHAT_OK));
  REQUIRE((g_h265_server) != nullptr);
}

TEST_CASE("server_main_contract/discovery_keepalive_replies_only_while_transport_is_available", "[server_main_contract]") {
  on_acds_ping(NULL);
  REQUIRE((contract_sends) == (0));
  g_acds_transport = &contract_transport;
  on_acds_ping(NULL);
  REQUIRE((contract_sends) == (1));
  REQUIRE((contract_type) == (PACKET_TYPE_PONG));
  REQUIRE((contract_length) == (0));
  on_acds_pong(NULL);
  REQUIRE((contract_sends) == (1));
  g_acds_transport = NULL;
  on_acds_ping(NULL);
  REQUIRE((contract_sends) == (1));
}
TEST_CASE("server_main_contract/per_connection_statistics_include_only_ready_clients_and_preserve_dimensions", "[server_main_contract]") {
  acip_transport_t *transport = SAFE_CALLOC(1, sizeof(*transport), acip_transport_t *);
  transport->methods = &contract_methods;
  network_connection_t *connection = NULL;
  REQUIRE((network_connection_adopt(transport, &connection)) == (ASCIICHAT_OK));
  stats_view_t view = {0};
  server_stats_visit_t visit = {.view = &view};
  server_stats_connection(connection, &visit);
  REQUIRE((visit.active) == (0));
  server_connection_state_t state = {0};
  network_connection_set_context(connection, &state);
  server_stats_connection(connection, &visit);
  REQUIRE((visit.active) == (0));
  client_info_t *client = &g_client_manager.clients[0];
  REQUIRE((mutex_init(&client->client_state_mutex, "stats-fixture")) == (ASCIICHAT_OK));
  SAFE_STRNCPY(client->client_id, "stats-client", sizeof(client->client_id));
  client->width = 100;
  client->height = 30;
  atomic_ptr_store(&state.client, client);
  server_stats_connection(connection, &visit);
  REQUIRE((visit.active) == (0));
  atomic_store_bool(&client->event_ready, true);
  server_stats_connection(connection, &visit);
  REQUIRE((visit.active) == (1));
  REQUIRE((view.count) == (0));
  view.page = 1;
  atomic_store_u64(&client->frames_sent_count, 17);
  server_stats_connection(connection, &visit);
  REQUIRE((visit.active) == (2));
  REQUIRE((view.count) == (1));
  REQUIRE(strstr(view.lines[0], "stats-client"));
  REQUIRE(strstr(view.lines[0], "100x30"));
  REQUIRE(strstr(view.lines[0], "17"));
  client->incoming_video_buffer = video_frame_buffer_create("stats-video");
  REQUIRE((client->incoming_video_buffer) != nullptr);
  atomic_store_u64(&client->incoming_video_buffer->total_frames_received, 23);
  server_stats_connection(connection, &visit);
  REQUIRE((view.count) == (2));
  REQUIRE(strstr(view.lines[1], "23"));
  video_frame_buffer_destroy(client->incoming_video_buffer);
  client->incoming_video_buffer = NULL;
  mutex_destroy(&client->client_state_mutex);
  network_connection_set_context(connection, NULL);
  network_connection_destroy(&connection);
  view = (stats_view_t){.page = 1};
  server_stats_provider(&view, NULL);
  REQUIRE((view.count) == (3));
  REQUIRE(strstr(view.lines[2], "Connected clients: 0"));
  ui_status_t status = {0};
  server_status_fn(NULL, &status);
}

TEST_CASE("server_main_contract/incoming_signaling_requires_a_peer_manager_and_complete_payload_before_forwarding", "[server_main_contract]") {
  struct {
    acip_webrtc_sdp_t header;
    char body[4];
  } sdp = {.header = {.sdp_len = HOST_TO_NET_U16(3)}, .body = "v=0"};
  struct {
    acip_webrtc_ice_t header;
    char body[8];
  } ice = {.header = {.candidate_len = HOST_TO_NET_U16(3)}, .body = {'a', 'b', 'c', 0, 'm', 0}};
  on_webrtc_sdp_server(NULL, 0, NULL);
  on_webrtc_ice_server(NULL, 0, NULL);
  on_webrtc_sdp_server(&sdp.header, sizeof(sdp), NULL);
  on_webrtc_ice_server(&ice.header, sizeof(ice), NULL);
  REQUIRE((sdp_calls) == (0));
  REQUIRE((ice_calls) == (0));
  int opaque = 0;
  g_webrtc_peer_manager = (webrtc_peer_manager_t *)&opaque;
  on_webrtc_sdp_server(&sdp.header, sizeof(sdp.header) + 2, NULL);
  on_webrtc_ice_server(&ice.header, sizeof(ice.header) + 2, NULL);
  REQUIRE((sdp_calls) == (0));
  REQUIRE((ice_calls) == (0));
  on_webrtc_sdp_server(&sdp.header, sizeof(sdp), NULL);
  REQUIRE((sdp_calls) == (1));
  REQUIRE((forwarded_signal) == (&sdp));
  sdp.header.sdp_type = 1;
  signal_result = ERROR_NETWORK;
  on_webrtc_sdp_server(&sdp.header, sizeof(sdp), NULL);
  REQUIRE((sdp_calls) == (2));
  on_webrtc_ice_server(&ice.header, sizeof(ice), NULL);
  REQUIRE((ice_calls) == (1));
  REQUIRE((forwarded_signal) == (&ice));
  signal_result = ASCIICHAT_OK;
  on_webrtc_ice_server(&ice.header, sizeof(ice), NULL);
  REQUIRE((ice_calls) == (2));
  g_webrtc_peer_manager = NULL;
}
static void channel_destroy(acip_transport_t *transport) {
  (void)transport;
  ++destroyed_channels;
}
static acip_transport_t *channel(void) {
  static const acip_transport_methods_t methods = {.destroy_impl = channel_destroy};
  acip_transport_t *transport = SAFE_CALLOC(1, sizeof(*transport), acip_transport_t *);
  transport->methods = &methods;
  return transport;
}
TEST_CASE("server_main_contract/ready_channels_transfer_ownership_only_after_authenticated_adoption", "[server_main_contract]") {
  uint8_t peer[16];
  for (unsigned i = 0; i < 16; ++i)
    peer[i] = i;
  server_context_t context = {0};
  on_webrtc_transport_ready(NULL, peer, &context);
  REQUIRE((adopt_calls) == (0));
  on_webrtc_transport_ready(channel(), NULL, &context);
  on_webrtc_transport_ready(channel(), peer, NULL);
  REQUIRE((destroyed_channels) == (2));
  REQUIRE((adopt_calls) == (0));
  adopt_result = ERROR_NETWORK;
  on_webrtc_transport_ready(channel(), peer, &context);
  REQUIRE((destroyed_channels) == (3));
  REQUIRE((adopt_calls) == (1));
  REQUIRE(std::strcmp((adopted_id), ("000102030405060708090a0b0c0d0e0f")) == 0);
  adopt_result = ASCIICHAT_OK;
  acip_transport_t *owned = channel();
  on_webrtc_transport_ready(owned, peer, &context);
  REQUIRE((adopt_calls) == (2));
  REQUIRE((destroyed_channels) == (3));
  acip_transport_destroy(owned);
  REQUIRE((destroyed_channels) == (4));
}

TEST_CASE("server_main_contract/lan_advertisements_preserve_session_identity_port_and_encryption_policy", "[server_main_contract]") {
  advertise_mdns_with_session("river-cloud-tree", 27001);
  REQUIRE((advertisement_calls) == (0));
  unsigned opaque = 0;
  advertisement_context = (asciichat_mdns_t *)&opaque;
  g_server_encryption_enabled = false;
  advertise_mdns_with_session("river-cloud-tree", 27001);
  REQUIRE((advertisement_calls) == (1));
  REQUIRE((advertisement_port) == (27001));
  REQUIRE(std::strcmp((advertisement_txt[0]), ("session_string=river-cloud-tree")) == 0);
  REQUIRE(std::strcmp((advertisement_txt[1]), ("host_pubkey=0000000000000000000000000000000000000000000000000000000000000000")) == 0);
  g_server_encryption_enabled = true;
  for (unsigned i = 0; i < 32; ++i)
    g_server_private_key.public_key[i] = i;
  advertisement_result = ERROR_NETWORK;
  advertise_mdns_with_session("other-session-string", 27002);
  REQUIRE((advertisement_calls) == (2));
  REQUIRE((advertisement_port) == (27002));
  REQUIRE(std::strcmp((advertisement_txt[0]), ("session_string=other-session-string")) == 0);
  REQUIRE(std::strcmp((advertisement_txt[1]), ("host_pubkey=000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f")) == 0);
  REQUIRE(g_server_encryption_enabled);
  advertisement_context = NULL;
}
TEST_CASE("server_main_contract/public_disclosure_requires_confirmation_and_authenticated_discovery_stays_private", "[server_main_contract]") {
  can_prompt = true;
  server_discovery_policy_t policy = {.explicit_expose = true};
  REQUIRE(!(server_allow_discovery(&policy)));
  REQUIRE(!(policy.expose_ip));
  REQUIRE((disclosure_prompts) == (1));
  confirm_disclosure = true;
  REQUIRE(server_allow_discovery(&policy));
  REQUIRE(policy.expose_ip);
  REQUIRE((disclosure_prompts) == (2));
  policy.has_password = true;
  REQUIRE(server_allow_discovery(&policy));
  REQUIRE(!(policy.expose_ip));
  REQUIRE((disclosure_prompts) == (2));
  policy.has_password = false;
  policy.has_identity = true;
  REQUIRE(server_allow_discovery(&policy));
  REQUIRE(!(policy.expose_ip));
  REQUIRE((disclosure_prompts) == (2));
  can_prompt = false;
  policy.has_identity = false;
  REQUIRE(server_allow_discovery(&policy));
  REQUIRE(policy.expose_ip);
  REQUIRE((disclosure_prompts) == (2));
}
