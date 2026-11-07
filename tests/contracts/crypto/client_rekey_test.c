#include <criterion/criterion.h>
#include "client/connection_internal.h"
#include "client/crypto.h"
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/util/endian.h>
#include <sodium.h>
#include <string.h>

#include <ascii-chat/network/acip/acds_client.h>
#include <ascii-chat/crypto/discovery_keys.h>
static bool startup_connect_fails, startup_lookup_fails, startup_session_missing, startup_key_fails,
    startup_checkpoint_fails;
static unsigned startup_connects, startup_disconnects, startup_lookups, startup_verifications;
static acds_client_config_t startup_observed;
static asciichat_error_t crypto_discovery_connect(acds_client_t *client, const acds_client_config_t *config) {
  (void)client;
  ++startup_connects;
  startup_observed = *config;
  return startup_connect_fails ? ERROR_NETWORK : ASCIICHAT_OK;
}
static void crypto_discovery_disconnect(acds_client_t *client) {
  (void)client;
  ++startup_disconnects;
}
static asciichat_error_t crypto_discovery_lookup(acds_client_t *client, const char *name,
                                                 acds_session_lookup_result_t *result) {
  (void)client;
  cr_assert_str_eq(name, "river-cloud-tree");
  ++startup_lookups;
  memset(result, 0, sizeof(*result));
  result->found = !startup_session_missing;
  for (unsigned i = 0; i < 32; ++i)
    result->host_pubkey[i] = (uint8_t)i;
  return startup_lookup_fails ? ERROR_NETWORK : ASCIICHAT_OK;
}
static asciichat_error_t crypto_discovery_verify(const char *host, const char *path, uint8_t key[32]) {
  cr_assert_str_eq(host, "127.0.0.2");
  cr_assert_str_eq(path, "fixture-discovery-key");
  ++startup_verifications;
  memset(key, 0x42, 32);
  return startup_key_fails ? ERROR_CRYPTO_AUTH : ASCIICHAT_OK;
}
#define acds_client_connect crypto_discovery_connect
#define acds_client_disconnect crypto_discovery_disconnect
#define acds_session_lookup crypto_discovery_lookup
#define discovery_keys_verify crypto_discovery_verify
#include "../../../src/client/crypto.c"
#undef acds_client_connect
#undef acds_client_disconnect
#undef acds_session_lookup
#undef discovery_keys_verify
static network_connection_t *startup_owner;
static socket_t startup_listener = INVALID_SOCKET_VALUE;

static client_connection_t connection;
static crypto_handshake_context_t local, peer;
static asciichat_error_t send_result;
static packet_type_t sent_type;
static uint8_t sent_payload[128], key_at_send[32];
static size_t sent_length;

// The client adapter borrows its transport from the connection owner.
acip_transport_t *server_connection_get_transport(client_connection_t *owner) {
  return owner->transport;
}
asciichat_error_t client_connection_checkpoint(const char *stage) {
  cr_assert_str_eq(stage, "crypto");
  return startup_checkpoint_fails ? ERROR_THREAD : ASCIICHAT_OK;
}

static asciichat_error_t capture(acip_transport_t *transport, const void *data, size_t length) {
  (void)transport;
  cr_assert_geq(length, sizeof(packet_header_t));
  packet_header_t header;
  memcpy(&header, data, sizeof(header));
  sent_type = NET_TO_HOST_U16(header.type);
  sent_length = length - sizeof(header);
  cr_assert_leq(sent_length, sizeof(sent_payload));
  memcpy(sent_payload, (const uint8_t *)data + sizeof(header), sent_length);
  memcpy(key_at_send, local.crypto_ctx.shared_key, sizeof(key_at_send));
  return send_result;
}
static const acip_transport_methods_t methods = {.send = capture};
static acip_transport_t transport = {.methods = &methods};

static void setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  cr_assert_eq(crypto_init(&local.crypto_ctx), CRYPTO_OK);
  cr_assert_eq(crypto_init(&peer.crypto_ctx), CRYPTO_OK);
  cr_assert_eq(crypto_set_peer_public_key(&local.crypto_ctx, peer.crypto_ctx.public_key), CRYPTO_OK);
  cr_assert_eq(crypto_set_peer_public_key(&peer.crypto_ctx, local.crypto_ctx.public_key), CRYPTO_OK);
  local.state = peer.state = CRYPTO_HANDSHAKE_READY;
  local.crypto_ctx.handshake_complete = peer.crypto_ctx.handshake_complete = true;
  local.crypto_ctx.rekey_last_time = peer.crypto_ctx.rekey_last_time = 0;
  connection.crypto_ctx = &local;
  connection.crypto_mode = ACIP_CRYPTO_ENCRYPT;
  connection.transport = &transport;
  send_result = ASCIICHAT_OK;
}
static void teardown(void) {
  network_connection_destroy(&startup_owner);
  if (startup_listener != INVALID_SOCKET_VALUE)
    socket_close(startup_listener);
  crypto_handshake_destroy(&local);
  crypto_handshake_destroy(&peer);
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TestSuite(client_rekey, .init = setup, .fini = teardown);

static void expect_not_ready(void) {
  uint8_t key[32] = {0};
  cr_assert_not(crypto_client_should_rekey(&connection));
  cr_assert_eq(crypto_client_initiate_rekey(&connection), ERROR_INVALID_STATE);
  cr_assert_eq(crypto_client_process_rekey_request(&connection, key, sizeof(key)), ERROR_INVALID_STATE);
  cr_assert_eq(crypto_client_process_rekey_response(&connection, key, sizeof(key)), ERROR_INVALID_STATE);
  cr_assert_eq(crypto_client_send_rekey_response(&connection), ERROR_INVALID_STATE);
  cr_assert_eq(crypto_client_send_rekey_complete(&connection), ERROR_INVALID_STATE);
}

Test(client_rekey, uninitialized_disabled_and_incomplete_connections_cannot_rotate_keys) {
  connection.crypto_ctx = NULL;
  expect_not_ready();
  connection.crypto_ctx = &local;
  connection.crypto_mode = ACIP_CRYPTO_NONE;
  expect_not_ready();
  connection.crypto_mode = ACIP_CRYPTO_ENCRYPT;
  local.state = CRYPTO_HANDSHAKE_INIT;
  expect_not_ready();
  local.state = CRYPTO_HANDSHAKE_READY;
  local.crypto_ctx.encrypt_data = false;
  cr_assert_not(crypto_client_should_rekey(&connection));
}

Test(client_rekey, initiator_transmits_ephemeral_key_and_commits_only_after_confirmation_send) {
  uint8_t old[32];
  memcpy(old, local.crypto_ctx.shared_key, sizeof(old));
  cr_assert_eq(crypto_client_initiate_rekey(&connection), ASCIICHAT_OK);
  cr_assert_eq(sent_type, PACKET_TYPE_CRYPTO_REKEY_REQUEST);
  cr_assert_eq(sent_length, 32);
  cr_assert_arr_eq(sent_payload, local.crypto_ctx.temp_public_key, 32);
  cr_assert_arr_eq(local.crypto_ctx.shared_key, old, 32);
  cr_assert_eq(crypto_handshake_process_rekey_request(&peer, sent_payload, sent_length), ASCIICHAT_OK);
  cr_assert_eq(crypto_client_process_rekey_response(&connection, peer.crypto_ctx.temp_public_key, 32), ASCIICHAT_OK);
  cr_assert_arr_eq(local.crypto_ctx.temp_shared_key, peer.crypto_ctx.temp_shared_key, 32);
  cr_assert_eq(crypto_client_send_rekey_complete(&connection), ASCIICHAT_OK);
  cr_assert_eq(sent_type, PACKET_TYPE_CRYPTO_REKEY_COMPLETE);
  cr_assert_arr_eq(key_at_send, old, 32);
  cr_assert_eq(crypto_handshake_process_rekey_complete(&peer, sent_payload, sent_length), ASCIICHAT_OK);
  cr_assert_arr_eq(local.crypto_ctx.shared_key, peer.crypto_ctx.shared_key, 32);
  cr_assert_neq(memcmp(local.crypto_ctx.shared_key, old, 32), 0);
  cr_assert_not(local.crypto_ctx.rekey_in_progress);
  cr_assert_eq(local.crypto_ctx.rekey_count, 1);
}

Test(client_rekey, packet_threshold_triggers_rekey_only_for_an_encrypted_ready_connection) {
  local.crypto_ctx.rekey_last_time = time(NULL);
  local.crypto_ctx.rekey_packet_threshold = 3;
  local.crypto_ctx.rekey_packet_count = 2;
  cr_assert_not(crypto_client_should_rekey(&connection));
  local.crypto_ctx.rekey_packet_count = 3;
  cr_assert(crypto_client_should_rekey(&connection));
  local.crypto_ctx.rekey_in_progress = true;
  cr_assert_not(crypto_client_should_rekey(&connection));
  local.crypto_ctx.rekey_in_progress = false;
  connection.crypto_mode = ACIP_CRYPTO_NONE;
  cr_assert_not(crypto_client_should_rekey(&connection));
}

Test(client_rekey, responder_rejects_truncated_request_then_sends_its_new_public_key) {
  cr_assert_eq(crypto_rekey_init(&peer.crypto_ctx), CRYPTO_OK);
  uint8_t old[32];
  memcpy(old, local.crypto_ctx.shared_key, sizeof(old));
  cr_assert_eq(crypto_client_process_rekey_request(&connection, peer.crypto_ctx.temp_public_key, 31),
               ERROR_INVALID_PARAM);
  cr_assert_not(local.crypto_ctx.rekey_in_progress);
  cr_assert_eq(crypto_client_process_rekey_request(&connection, peer.crypto_ctx.temp_public_key, 32), ERROR_CRYPTO);
  local.crypto_ctx.rekey_last_request_time -= REKEY_MIN_REQUEST_INTERVAL / NS_PER_SEC_INT;
  cr_assert_eq(crypto_client_process_rekey_request(&connection, peer.crypto_ctx.temp_public_key, 32), ASCIICHAT_OK);
  cr_assert_eq(crypto_client_send_rekey_response(&connection), ASCIICHAT_OK);
  cr_assert_eq(sent_type, PACKET_TYPE_CRYPTO_REKEY_RESPONSE);
  cr_assert_eq(sent_length, 32);
  cr_assert_arr_eq(sent_payload, local.crypto_ctx.temp_public_key, 32);
  cr_assert_eq(crypto_handshake_process_rekey_response(&peer, sent_payload, sent_length), ASCIICHAT_OK);
  cr_assert_arr_eq(local.crypto_ctx.temp_shared_key, peer.crypto_ctx.temp_shared_key, 32);
  cr_assert_arr_eq(local.crypto_ctx.shared_key, old, 32);
}

Test(client_rekey, send_failures_abort_temporary_keys_and_preserve_the_active_key) {
  uint8_t old[32];
  memcpy(old, local.crypto_ctx.shared_key, sizeof(old));
  send_result = ERROR_NETWORK;
  cr_assert_eq(crypto_client_initiate_rekey(&connection), ERROR_NETWORK);
  cr_assert_not(local.crypto_ctx.rekey_in_progress);
  cr_assert(sodium_is_zero(local.crypto_ctx.temp_private_key, 32));
  cr_assert_arr_eq(local.crypto_ctx.shared_key, old, 32);
  cr_assert_eq(crypto_rekey_init(&peer.crypto_ctx), CRYPTO_OK);
  cr_assert_eq(crypto_client_process_rekey_request(&connection, peer.crypto_ctx.temp_public_key, 32), ASCIICHAT_OK);
  cr_assert_eq(crypto_client_send_rekey_response(&connection), ERROR_NETWORK);
  cr_assert_not(local.crypto_ctx.rekey_in_progress);
  cr_assert_arr_eq(local.crypto_ctx.shared_key, old, 32);
  crypto_rekey_abort(&peer.crypto_ctx);
  send_result = ASCIICHAT_OK;
  cr_assert_eq(crypto_client_initiate_rekey(&connection), ASCIICHAT_OK);
  cr_assert_eq(crypto_handshake_process_rekey_request(&peer, local.crypto_ctx.temp_public_key, 32), ASCIICHAT_OK);
  cr_assert_eq(crypto_client_process_rekey_response(&connection, peer.crypto_ctx.temp_public_key, 32), ASCIICHAT_OK);
  send_result = ERROR_NETWORK;
  cr_assert_eq(crypto_client_send_rekey_complete(&connection), ERROR_NETWORK);
  cr_assert_not(local.crypto_ctx.rekey_in_progress);
  cr_assert_arr_eq(local.crypto_ctx.shared_key, old, 32);
  cr_assert(sodium_is_zero(local.crypto_ctx.temp_private_key, 32));
}

Test(client_rekey, missing_transport_or_unsolicited_response_does_not_change_active_key) {
  uint8_t old[32], invalid[32] = {0};
  memcpy(old, local.crypto_ctx.shared_key, sizeof(old));
  connection.transport = NULL;
  cr_assert_eq(crypto_client_initiate_rekey(&connection), ERROR_INVALID_STATE);
  cr_assert_eq(crypto_client_send_rekey_response(&connection), ERROR_INVALID_STATE);
  cr_assert_eq(crypto_client_send_rekey_complete(&connection), ERROR_INVALID_STATE);
  connection.transport = &transport;
  cr_assert_eq(crypto_client_process_rekey_response(&connection, invalid, sizeof(invalid)), ERROR_INVALID_STATE);
  cr_assert_eq(crypto_client_send_rekey_response(&connection), ERROR_INVALID_STATE);
  cr_assert_eq(crypto_client_send_rekey_complete(&connection), ERROR_INVALID_STATE);
  cr_assert_eq(crypto_client_initiate_rekey(&connection), ASCIICHAT_OK);
  cr_assert_eq(crypto_client_process_rekey_response(&connection, invalid, 31), ERROR_INVALID_PARAM);
  cr_assert_eq(crypto_client_process_rekey_response(&connection, invalid, sizeof(invalid)), ERROR_CRYPTO);
  cr_assert_arr_eq(local.crypto_ctx.shared_key, old, sizeof(old));
}

static void startup_security(bool verify_key) {
  options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
  *v = *options_get();
  v->require_client_verify = true;
  SAFE_STRNCPY(v->session_string, "river-cloud-tree", sizeof(v->session_string));
  SAFE_STRNCPY(v->discovery_server, "127.0.0.2", sizeof(v->discovery_server));
  v->discovery_port = 27002;
  if (verify_key)
    SAFE_STRNCPY(v->discovery_service_key, "fixture-discovery-key", sizeof(v->discovery_service_key));
  cr_assert_eq(options_state_set(v), ASCIICHAT_OK);
  SAFE_FREE(v);
  cr_assert_eq(socket_init(), ASCIICHAT_OK);
  startup_listener = socket_create("crypto-startup-listener", AF_INET, SOCK_STREAM, 0);
  cr_assert_neq(startup_listener, INVALID_SOCKET_VALUE);
  struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  cr_assert_eq(socket_bind(startup_listener, (struct sockaddr *)&address, sizeof(address)), 0);
  cr_assert_eq(socket_listen(startup_listener, 1), 0);
  socklen_t length = sizeof(address);
  cr_assert_eq(socket_getsockname(startup_listener, (struct sockaddr *)&address, &length), 0);
  network_connect_config_t settings = {.timeout_ms = 1000};
  cr_assert_eq(network_connection_open("crypto-startup-peer", "127.0.0.1", NET_TO_HOST_U16(address.sin_port), &settings,
                                       &startup_owner, NULL),
               ASCIICHAT_OK);
  connection.network = startup_owner;
}
Test(client_rekey, discovery_identity_is_verified_and_bound_to_the_expected_server_key) {
  startup_security(true);
  cr_assert_eq(client_crypto_init(&connection, NULL), ASCIICHAT_OK);
  cr_assert_eq(startup_verifications, 1);
  cr_assert_eq(startup_connects, 1);
  cr_assert_eq(startup_lookups, 1);
  cr_assert_eq(startup_disconnects, 1);
  cr_assert_str_eq(startup_observed.server_address, "127.0.0.2");
  cr_assert_eq(startup_observed.server_port, 27002);
  cr_assert_eq(startup_observed.timeout_ms, 5000);
  cr_assert_eq(connection.crypto_ctx, network_connection_handshake(startup_owner));
  cr_assert(connection.crypto_ctx->verify_server_key);
  cr_assert_str_eq(connection.crypto_ctx->expected_server_key,
                   "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
}
Test(client_rekey, failed_discovery_key_verification_does_not_attempt_an_untrusted_lookup) {
  startup_security(true);
  startup_key_fails = true;
  cr_assert_eq(client_crypto_init(&connection, NULL), ERROR_CRYPTO_AUTH);
  cr_assert_eq(startup_verifications, 1);
  cr_assert_eq(startup_connects, 0);
  cr_assert_eq(startup_lookups, 0);
  cr_assert_not(connection.crypto_ctx->verify_server_key);
}
Test(client_rekey, discovery_connection_failure_is_fatal_to_required_identity_verification) {
  startup_security(false);
  startup_connect_fails = true;
  cr_assert_eq(client_crypto_init(&connection, NULL), ERROR_CRYPTO_AUTH);
  cr_assert_eq(startup_connects, 1);
  cr_assert_eq(startup_lookups, 0);
  cr_assert_eq(startup_disconnects, 0);
}
Test(client_rekey, failed_discovery_lookup_does_not_publish_an_expected_key) {
  startup_security(false);
  startup_lookup_fails = true;
  cr_assert_eq(client_crypto_init(&connection, NULL), ERROR_CRYPTO_AUTH);
  cr_assert_eq(startup_disconnects, 1);
  cr_assert_not(connection.crypto_ctx->verify_server_key);
}
Test(client_rekey, missing_discovery_session_does_not_publish_an_expected_key) {
  startup_security(false);
  startup_session_missing = true;
  cr_assert_eq(client_crypto_init(&connection, NULL), ERROR_CRYPTO_AUTH);
  cr_assert_eq(startup_disconnects, 1);
  cr_assert_not(connection.crypto_ctx->verify_server_key);
}
Test(client_rekey, security_preparation_and_checkpoint_failures_do_not_start_identity_lookup) {
  connection.network = NULL;
  cr_assert_neq(client_crypto_init(&connection, NULL), ASCIICHAT_OK);
  cr_assert_eq(startup_connects, 0);
  startup_security(false);
  startup_checkpoint_fails = true;
  CLEAR_ERRNO_ALL();
  cr_assert_eq(client_crypto_init(&connection, NULL), ERROR_THREAD);
  cr_assert_eq(startup_connects, 0);
  cr_assert_eq(connection.crypto_ctx, network_connection_handshake(startup_owner));
}
