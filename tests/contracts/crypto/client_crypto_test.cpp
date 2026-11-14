#include <ascii-chat/tests/catch2_test_helpers.hpp>
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
  ASCIICHAT_REQUIRE_STR_EQ(name, "river-cloud-tree");
  ++startup_lookups;
  memset(result, 0, sizeof(*result));
  result->found = !startup_session_missing;
  for (unsigned i = 0; i < 32; ++i)
    result->host_pubkey[i] = (uint8_t)i;
  return startup_lookup_fails ? ERROR_NETWORK : ASCIICHAT_OK;
}
static asciichat_error_t crypto_discovery_verify(const char *host, const char *path, uint8_t key[32]) {
  ASCIICHAT_REQUIRE_STR_EQ(host, "127.0.0.2");
  ASCIICHAT_REQUIRE_STR_EQ(path, "fixture-discovery-key");
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

// The client adapter borrows its transport from the connection owner.
asciichat_error_t client_connection_checkpoint(const char *stage) {
  ASCIICHAT_REQUIRE_STR_EQ(stage, "crypto");
  return startup_checkpoint_fails ? ERROR_THREAD : ASCIICHAT_OK;
}

static const acip_transport_methods_t methods = {0};
static acip_transport_t transport = {.methods = &methods};

static void setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  ASCIICHAT_REQUIRE_EQ(options_state_init(), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(crypto_init(&local.crypto_ctx), CRYPTO_OK);
  ASCIICHAT_REQUIRE_EQ(crypto_init(&peer.crypto_ctx), CRYPTO_OK);
  ASCIICHAT_REQUIRE_EQ(crypto_set_peer_public_key(&local.crypto_ctx, peer.crypto_ctx.public_key), CRYPTO_OK);
  ASCIICHAT_REQUIRE_EQ(crypto_set_peer_public_key(&peer.crypto_ctx, local.crypto_ctx.public_key), CRYPTO_OK);
  local.state = peer.state = CRYPTO_HANDSHAKE_READY;
  local.crypto_ctx.handshake_complete = peer.crypto_ctx.handshake_complete = true;
  local.crypto_ctx.rekey_last_time = peer.crypto_ctx.rekey_last_time = 0;
  connection.crypto_ctx = &local;
  connection.crypto_mode = ACIP_CRYPTO_ENCRYPT;
  atomic_ptr_store(&connection.transport, &transport);
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
static void startup_security(bool verify_key) {
  options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
  *v = *options_get();
  v->require_client_verify = true;
  SAFE_STRNCPY(v->session_string, "river-cloud-tree", sizeof(v->session_string));
  SAFE_STRNCPY(v->discovery_server, "127.0.0.2", sizeof(v->discovery_server));
  v->discovery_port = 27002;
  if (verify_key)
    SAFE_STRNCPY(v->discovery_service_key, "fixture-discovery-key", sizeof(v->discovery_service_key));
  ASCIICHAT_REQUIRE_EQ(options_state_set(v), ASCIICHAT_OK);
  SAFE_FREE(v);
  ASCIICHAT_REQUIRE_EQ(socket_init(), ASCIICHAT_OK);
  startup_listener = socket_create("crypto-startup-listener", AF_INET, SOCK_STREAM, 0);
  ASCIICHAT_REQUIRE_NE(startup_listener, INVALID_SOCKET_VALUE);
  struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  ASCIICHAT_REQUIRE_EQ(socket_bind(startup_listener, (struct sockaddr *)&address, sizeof(address)), 0);
  ASCIICHAT_REQUIRE_EQ(socket_listen(startup_listener, 1), 0);
  socklen_t length = sizeof(address);
  ASCIICHAT_REQUIRE_EQ(socket_getsockname(startup_listener, (struct sockaddr *)&address, &length), 0);
  network_connect_config_t settings = {.timeout_ms = 1000};
  ASCIICHAT_REQUIRE_EQ(network_connection_open("crypto-startup-peer", "127.0.0.1", NET_TO_HOST_U16(address.sin_port), &settings,
                                       &startup_owner, NULL),
               ASCIICHAT_OK);
  connection.network = startup_owner;
}
TEST_CASE("client_crypto/discovery_identity_is_verified_and_bound_to_the_expected_server_key", "[client_crypto]") {
  startup_security(true);
  ASCIICHAT_REQUIRE_EQ(client_crypto_init(&connection, NULL), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(startup_verifications, 1);
  ASCIICHAT_REQUIRE_EQ(startup_connects, 1);
  ASCIICHAT_REQUIRE_EQ(startup_lookups, 1);
  ASCIICHAT_REQUIRE_EQ(startup_disconnects, 1);
  ASCIICHAT_REQUIRE_STR_EQ(startup_observed.server_address, "127.0.0.2");
  ASCIICHAT_REQUIRE_EQ(startup_observed.server_port, 27002);
  ASCIICHAT_REQUIRE_EQ(startup_observed.timeout_ms, 5000);
  ASCIICHAT_REQUIRE_EQ(connection.crypto_ctx, network_connection_handshake(startup_owner));
  ASCIICHAT_REQUIRE(connection.crypto_ctx->verify_server_key);
  ASCIICHAT_REQUIRE_STR_EQ(connection.crypto_ctx->expected_server_key,
                   "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
}
TEST_CASE("client_crypto/failed_discovery_key_verification_does_not_attempt_an_untrusted_lookup", "[client_crypto]") {
  startup_security(true);
  startup_key_fails = true;
  ASCIICHAT_REQUIRE_EQ(client_crypto_init(&connection, NULL), ERROR_CRYPTO_AUTH);
  ASCIICHAT_REQUIRE_EQ(startup_verifications, 1);
  ASCIICHAT_REQUIRE_EQ(startup_connects, 0);
  ASCIICHAT_REQUIRE_EQ(startup_lookups, 0);
  ASCIICHAT_REQUIRE_NOT(connection.crypto_ctx->verify_server_key);
}
TEST_CASE("client_crypto/discovery_connection_failure_is_fatal_to_required_identity_verification", "[client_crypto]") {
  startup_security(false);
  startup_connect_fails = true;
  ASCIICHAT_REQUIRE_EQ(client_crypto_init(&connection, NULL), ERROR_CRYPTO_AUTH);
  ASCIICHAT_REQUIRE_EQ(startup_connects, 1);
  ASCIICHAT_REQUIRE_EQ(startup_lookups, 0);
  ASCIICHAT_REQUIRE_EQ(startup_disconnects, 0);
}
TEST_CASE("client_crypto/failed_discovery_lookup_does_not_publish_an_expected_key", "[client_crypto]") {
  startup_security(false);
  startup_lookup_fails = true;
  ASCIICHAT_REQUIRE_EQ(client_crypto_init(&connection, NULL), ERROR_CRYPTO_AUTH);
  ASCIICHAT_REQUIRE_EQ(startup_disconnects, 1);
  ASCIICHAT_REQUIRE_NOT(connection.crypto_ctx->verify_server_key);
}
TEST_CASE("client_crypto/missing_discovery_session_does_not_publish_an_expected_key", "[client_crypto]") {
  startup_security(false);
  startup_session_missing = true;
  ASCIICHAT_REQUIRE_EQ(client_crypto_init(&connection, NULL), ERROR_CRYPTO_AUTH);
  ASCIICHAT_REQUIRE_EQ(startup_disconnects, 1);
  ASCIICHAT_REQUIRE_NOT(connection.crypto_ctx->verify_server_key);
}
TEST_CASE("client_crypto/security_preparation_and_checkpoint_failures_do_not_start_identity_lookup", "[client_crypto]") {
  connection.network = NULL;
  ASCIICHAT_REQUIRE_NE(client_crypto_init(&connection, NULL), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(startup_connects, 0);
  startup_security(false);
  startup_checkpoint_fails = true;
  CLEAR_ERRNO_ALL();
  ASCIICHAT_REQUIRE_EQ(client_crypto_init(&connection, NULL), ERROR_THREAD);
  ASCIICHAT_REQUIRE_EQ(startup_connects, 0);
  ASCIICHAT_REQUIRE_EQ(connection.crypto_ctx, network_connection_handshake(startup_owner));
}
