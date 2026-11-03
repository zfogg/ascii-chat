#include <criterion/criterion.h>
#include "client/connection_internal.h"
#include "client/crypto.h"
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/util/endian.h>
#include <sodium.h>
#include <string.h>

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
  (void)stage;
  return ASCIICHAT_OK;
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
  log_init(NULL, LOG_FATAL, false, false);
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
  cr_assert_arr_eq(key_at_send, peer.crypto_ctx.temp_shared_key, 32);
  cr_assert_arr_eq(local.crypto_ctx.shared_key, peer.crypto_ctx.temp_shared_key, 32);
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
