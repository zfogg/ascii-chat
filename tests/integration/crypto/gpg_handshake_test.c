/**
 * @file gpg_handshake_test.c
 * @brief Integration test for GPG authentication in crypto handshake
 *
 * Tests end-to-end GPG key authentication via ACIP transport.
 * Requires TEST_GPG_KEY_ID environment variable to be set.
 */

#include <criterion/criterion.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>

#include <ascii-chat/tests/common.h>
#include <ascii-chat/crypto/handshake/common.h>
#include <ascii-chat/crypto/handshake/client.h>
#include <ascii-chat/crypto/handshake/server.h>
#include <ascii-chat/crypto/keys.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/tests/logging.h>

TestSuite(gpg_handshake);

static const char *get_test_gpg_key_id(void) {
  const char *key_id = getenv("TEST_GPG_KEY_ID");
  if (!key_id || strlen(key_id) != 16)
    return NULL;
  return key_id;
}

typedef struct {
  int server_fd;
  int client_fd;
  acip_transport_t *server_transport;
  acip_transport_t *client_transport;
} test_network_t;

static test_network_t g_network = {0};

static void setup_gpg_test_network(void) {
  memset(&g_network, 0, sizeof(g_network));
  buffer_pool_init_global();

  int sv[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
    cr_assert_fail("Failed to create socket pair: %s", strerror(errno));
    return;
  }
  g_network.server_fd = sv[0];
  g_network.client_fd = sv[1];
  g_network.server_transport = acip_tcp_transport_create("gpg-test-server", sv[0], NULL);
  g_network.client_transport = acip_tcp_transport_create("gpg-test-client", sv[1], NULL);
  setenv("ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK", "1", 1);
}

static void teardown_gpg_test_network(void) {
  if (g_network.server_transport)
    acip_transport_destroy(g_network.server_transport);
  if (g_network.client_transport)
    acip_transport_destroy(g_network.client_transport);
  if (g_network.server_fd > 0)
    close(g_network.server_fd);
  if (g_network.client_fd > 0)
    close(g_network.client_fd);
  memset(&g_network, 0, sizeof(g_network));
}

// =============================================================================
// Protocol Negotiation Helpers
// =============================================================================

static int server_protocol_negotiation(int server_fd, crypto_handshake_context_t *server_ctx) {
  packet_type_t pkt_type;
  void *payload = NULL;
  size_t payload_len = 0;

  if (receive_packet(server_fd, &pkt_type, &payload, &payload_len) != ASCIICHAT_OK ||
      pkt_type != PACKET_TYPE_PROTOCOL_VERSION) {
    if (payload) buffer_pool_free(NULL, payload, payload_len);
    return -1;
  }
  buffer_pool_free(NULL, payload, payload_len);

  protocol_version_packet_t server_version = {0};
  server_version.protocol_version = htons(1);
  server_version.protocol_revision = htons(0);
  server_version.supports_encryption = 1;
  if (send_protocol_version_packet(server_fd, &server_version) != 0) return -1;

  payload = NULL;
  if (receive_packet(server_fd, &pkt_type, &payload, &payload_len) != ASCIICHAT_OK ||
      pkt_type != PACKET_TYPE_CRYPTO_CAPABILITIES) {
    if (payload) buffer_pool_free(NULL, payload, payload_len);
    return -1;
  }
  buffer_pool_free(NULL, payload, payload_len);

  crypto_capabilities_packet_t server_caps = {0};
  server_caps.supported_kex_algorithms = htons(KEX_ALGO_X25519);
  server_caps.supported_auth_algorithms = htons(AUTH_ALGO_ED25519);
  server_caps.supported_cipher_algorithms = htons(CIPHER_ALGO_XSALSA20_POLY1305);
  if (send_crypto_capabilities_packet(server_fd, &server_caps) != 0) return -1;

  crypto_parameters_packet_t server_params = {0};
  server_params.selected_kex = KEX_ALGO_X25519;
  server_params.selected_auth = AUTH_ALGO_ED25519;
  server_params.selected_cipher = CIPHER_ALGO_XSALSA20_POLY1305;
  server_params.verification_enabled = 1;
  server_params.kex_public_key_size = CRYPTO_PUBLIC_KEY_SIZE;
  server_params.auth_public_key_size = ED25519_PUBLIC_KEY_SIZE;
  server_params.signature_size = ED25519_SIGNATURE_SIZE;
  server_params.shared_secret_size = CRYPTO_PUBLIC_KEY_SIZE;
  server_params.nonce_size = CRYPTO_NONCE_SIZE;
  server_params.mac_size = CRYPTO_MAC_SIZE;
  server_params.hmac_size = CRYPTO_HMAC_SIZE;

  if (send_crypto_parameters_packet(server_fd, &server_params) != 0) return -1;
  return crypto_handshake_set_parameters(server_ctx, &server_params);
}

static int client_protocol_negotiation(int client_fd, crypto_handshake_context_t *client_ctx) {
  packet_type_t pkt_type;
  void *payload = NULL;
  size_t payload_len = 0;

  protocol_version_packet_t client_version = {0};
  client_version.protocol_version = htons(1);
  client_version.protocol_revision = htons(0);
  client_version.supports_encryption = 1;
  if (send_protocol_version_packet(client_fd, &client_version) != 0) return -1;

  if (receive_packet(client_fd, &pkt_type, &payload, &payload_len) != ASCIICHAT_OK ||
      pkt_type != PACKET_TYPE_PROTOCOL_VERSION) {
    if (payload) buffer_pool_free(NULL, payload, payload_len);
    return -1;
  }
  buffer_pool_free(NULL, payload, payload_len);

  crypto_capabilities_packet_t client_caps = {0};
  client_caps.supported_kex_algorithms = htons(KEX_ALGO_X25519);
  client_caps.supported_auth_algorithms = htons(AUTH_ALGO_ED25519 | AUTH_ALGO_NONE);
  client_caps.supported_cipher_algorithms = htons(CIPHER_ALGO_XSALSA20_POLY1305);
  if (send_crypto_capabilities_packet(client_fd, &client_caps) != 0) return -1;

  payload = NULL;
  if (receive_packet(client_fd, &pkt_type, &payload, &payload_len) != ASCIICHAT_OK ||
      pkt_type != PACKET_TYPE_CRYPTO_CAPABILITIES) {
    if (payload) buffer_pool_free(NULL, payload, payload_len);
    return -1;
  }
  buffer_pool_free(NULL, payload, payload_len);

  payload = NULL;
  if (receive_packet(client_fd, &pkt_type, &payload, &payload_len) != ASCIICHAT_OK ||
      pkt_type != PACKET_TYPE_CRYPTO_PARAMETERS) {
    if (payload) buffer_pool_free(NULL, payload, payload_len);
    return -1;
  }

  crypto_parameters_packet_t server_params;
  memcpy(&server_params, payload, sizeof(crypto_parameters_packet_t));
  buffer_pool_free(NULL, payload, payload_len);
  return crypto_handshake_set_parameters(client_ctx, &server_params);
}

// =============================================================================
// Client Thread
// =============================================================================

typedef struct {
  int client_fd;
  acip_transport_t *transport;
  crypto_handshake_context_t *ctx;
  int result;
} client_thread_args_t;

static void *client_handshake_thread(void *arg) {
  client_thread_args_t *args = (client_thread_args_t *)arg;
  args->result = -1;

  packet_type_t pkt_type;
  void *payload = NULL;
  size_t payload_len = 0;

  if (client_protocol_negotiation(args->client_fd, args->ctx) != ASCIICHAT_OK)
    return NULL;

  // Receive server's KEY_EXCHANGE_INIT
  if (receive_packet(args->client_fd, &pkt_type, &payload, &payload_len) != ASCIICHAT_OK)
    return NULL;
  if (crypto_handshake_client_key_exchange(args->ctx, args->transport, pkt_type, payload, payload_len) != ASCIICHAT_OK) {
    buffer_pool_free(NULL, payload, payload_len);
    return NULL;
  }
  buffer_pool_free(NULL, payload, payload_len);

  // Receive AUTH_CHALLENGE or HANDSHAKE_COMPLETE
  payload = NULL;
  if (receive_packet(args->client_fd, &pkt_type, &payload, &payload_len) != ASCIICHAT_OK)
    return NULL;
  if (crypto_handshake_client_auth_response(args->ctx, args->transport, pkt_type, payload, payload_len) != ASCIICHAT_OK) {
    buffer_pool_free(NULL, payload, payload_len);
    return NULL;
  }
  buffer_pool_free(NULL, payload, payload_len);

  // If authenticating, receive HANDSHAKE_COMPLETE
  if (args->ctx->state == CRYPTO_HANDSHAKE_AUTHENTICATING) {
    payload = NULL;
    if (receive_packet(args->client_fd, &pkt_type, &payload, &payload_len) != ASCIICHAT_OK)
      return NULL;
    if (crypto_handshake_client_complete(args->ctx, args->transport, pkt_type, payload, payload_len) != ASCIICHAT_OK) {
      buffer_pool_free(NULL, payload, payload_len);
      return NULL;
    }
    buffer_pool_free(NULL, payload, payload_len);
  }

  args->result = 0;
  return NULL;
}

// =============================================================================
// Tests
// =============================================================================

Test(gpg_handshake, complete_gpg_handshake_with_authentication) {
  const char *test_key_id = get_test_gpg_key_id();
  if (!test_key_id)
    cr_skip_test("TEST_GPG_KEY_ID environment variable not set");

  setup_gpg_test_network();

  crypto_handshake_context_t server_ctx, client_ctx;
  cr_assert_eq(crypto_handshake_init("gpg-server", &server_ctx, true), ASCIICHAT_OK);
  cr_assert_eq(crypto_handshake_init("gpg-client", &client_ctx, false), ASCIICHAT_OK);

  SAFE_STRNCPY(client_ctx.server_ip, "127.0.0.1", sizeof(client_ctx.server_ip));
  client_ctx.server_port = 27224;

  // Configure server with GPG key
  char key_input[128];
  snprintf(key_input, sizeof(key_input), "gpg:%s", test_key_id);
  cr_assert_eq(parse_private_key(key_input, &server_ctx.server_private_key), ASCIICHAT_OK);
  cr_assert_eq(parse_private_key(key_input, &client_ctx.client_private_key), ASCIICHAT_OK);

  // Start client thread
  client_thread_args_t client_args = {
      .client_fd = g_network.client_fd,
      .transport = g_network.client_transport,
      .ctx = &client_ctx,
      .result = -1,
  };
  pthread_t client_thread;
  pthread_create(&client_thread, NULL, client_handshake_thread, &client_args);

  // Server: protocol negotiation
  cr_assert_eq(server_protocol_negotiation(g_network.server_fd, &server_ctx), ASCIICHAT_OK);

  // Server: send key exchange init
  cr_assert_eq(crypto_handshake_server_start(&server_ctx, g_network.server_transport), ASCIICHAT_OK);

  // Server: receive client's key exchange response
  packet_type_t pkt_type;
  void *payload = NULL;
  size_t payload_len = 0;
  cr_assert_eq(receive_packet(g_network.server_fd, &pkt_type, &payload, &payload_len), ASCIICHAT_OK);
  cr_assert_eq(crypto_handshake_server_auth_challenge(&server_ctx, g_network.server_transport, pkt_type, payload, payload_len), ASCIICHAT_OK);
  buffer_pool_free(NULL, payload, payload_len);

  if (server_ctx.state == CRYPTO_HANDSHAKE_AUTHENTICATING) {
    payload = NULL;
    cr_assert_eq(receive_packet(g_network.server_fd, &pkt_type, &payload, &payload_len), ASCIICHAT_OK);
    cr_assert_eq(crypto_handshake_server_complete(&server_ctx, g_network.server_transport, pkt_type, payload, payload_len), ASCIICHAT_OK);
    buffer_pool_free(NULL, payload, payload_len);
  }

  pthread_join(client_thread, NULL);
  cr_assert_eq(client_args.result, 0, "Client handshake failed");
  cr_assert_eq(server_ctx.state, CRYPTO_HANDSHAKE_READY);
  cr_assert_eq(client_ctx.state, CRYPTO_HANDSHAKE_READY);

  crypto_handshake_destroy(&server_ctx);
  crypto_handshake_destroy(&client_ctx);
  teardown_gpg_test_network();
}

Test(gpg_handshake, gpg_key_parsing_and_verification) {
  const char *test_key_id = get_test_gpg_key_id();
  if (!test_key_id)
    cr_skip_test("TEST_GPG_KEY_ID environment variable not set");

  char pub_key_input[128];
  snprintf(pub_key_input, sizeof(pub_key_input), "gpg:%s", test_key_id);

  public_key_t public_key;
  memset(&public_key, 0, sizeof(public_key));
  cr_assert_eq(parse_public_key(pub_key_input, &public_key), ASCIICHAT_OK);
  cr_assert_eq(public_key.type, KEY_TYPE_GPG);

  bool all_zeros = true;
  for (int i = 0; i < 32; i++) {
    if (public_key.key[i] != 0) { all_zeros = false; break; }
  }
  cr_assert_eq(all_zeros, false, "Public key should not be all zeros");

  char priv_key_input[128];
  snprintf(priv_key_input, sizeof(priv_key_input), "gpg:%s", test_key_id);

  private_key_t private_key;
  memset(&private_key, 0, sizeof(private_key));
  cr_assert_eq(parse_private_key(priv_key_input, &private_key), ASCIICHAT_OK);
  cr_assert_eq(private_key.type, KEY_TYPE_ED25519);
  cr_assert_eq(private_key.use_gpg_agent, true);
  cr_assert_eq(strlen(private_key.gpg_keygrip), 40);
}
