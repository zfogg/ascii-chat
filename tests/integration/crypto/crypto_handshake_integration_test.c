/**
 * @file crypto_handshake_integration_test.c
 * @brief Integration tests for end-to-end crypto handshake via ACIP transport
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
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

TestSuite(crypto_handshake_integration);

// Network state for integration testing
typedef struct {
  int server_fd;
  int client_fd;
  acip_transport_t *server_transport;
  acip_transport_t *client_transport;
} test_network_t;

static test_network_t g_network = {0};

static void setup_test_network(void) {
  memset(&g_network, 0, sizeof(g_network));

  int sv[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
    cr_fatal("Failed to create socket pair: %s", strerror(errno));
    return;
  }
  g_network.server_fd = sv[0];
  g_network.client_fd = sv[1];

  g_network.server_transport = acip_tcp_transport_create("test-server", sv[0], NULL);
  g_network.client_transport = acip_tcp_transport_create("test-client", sv[1], NULL);

  cr_assert_not_null(g_network.server_transport, "Server transport should be created");
  cr_assert_not_null(g_network.client_transport, "Client transport should be created");

  setenv("ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK", "1", 1);
}

static void setup_client_ctx_for_socketpair(crypto_handshake_context_t *ctx) {
  SAFE_STRNCPY(ctx->server_ip, "127.0.0.1", sizeof(ctx->server_ip));
  ctx->server_port = 27224;
}

static void teardown_test_network(void) {
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

// Receive a packet via raw socket (transport layer handles framing)
static asciichat_error_t recv_packet(int fd, packet_type_t *type, void **payload, size_t *len) {
  return receive_packet(fd, type, payload, len);
}

// =============================================================================
// Protocol Negotiation Helpers
// =============================================================================

static int server_protocol_negotiation(int server_fd, crypto_handshake_context_t *server_ctx) {
  packet_type_t pkt_type;
  void *payload = NULL;
  size_t payload_len = 0;

  // Receive client's PROTOCOL_VERSION
  if (recv_packet(server_fd, &pkt_type, &payload, &payload_len) != ASCIICHAT_OK ||
      pkt_type != PACKET_TYPE_PROTOCOL_VERSION) {
    if (payload)
      buffer_pool_free(NULL, payload, payload_len);
    return -1;
  }
  buffer_pool_free(NULL, payload, payload_len);

  // Send server's PROTOCOL_VERSION
  protocol_version_packet_t server_version = {0};
  server_version.protocol_version = htons(1);
  server_version.protocol_revision = htons(0);
  server_version.supports_encryption = 1;
  if (send_protocol_version_packet(server_fd, &server_version) != 0)
    return -1;

  // Receive client's CRYPTO_CAPABILITIES
  payload = NULL;
  if (recv_packet(server_fd, &pkt_type, &payload, &payload_len) != ASCIICHAT_OK ||
      pkt_type != PACKET_TYPE_CRYPTO_CAPABILITIES) {
    if (payload)
      buffer_pool_free(NULL, payload, payload_len);
    return -1;
  }
  buffer_pool_free(NULL, payload, payload_len);

  // Send server's CRYPTO_PARAMETERS
  crypto_parameters_packet_t server_params = {0};
  server_params.selected_kex = KEX_ALGO_X25519;
  server_params.selected_auth = AUTH_ALGO_NONE;
  server_params.selected_cipher = CIPHER_ALGO_XSALSA20_POLY1305;
  server_params.verification_enabled = 0;
  server_params.kex_public_key_size = CRYPTO_PUBLIC_KEY_SIZE;
  server_params.auth_public_key_size = 0;
  server_params.signature_size = 0;
  server_params.shared_secret_size = CRYPTO_PUBLIC_KEY_SIZE;
  server_params.nonce_size = CRYPTO_NONCE_SIZE;
  server_params.mac_size = CRYPTO_MAC_SIZE;
  server_params.hmac_size = CRYPTO_HMAC_SIZE;

  if (send_crypto_parameters_packet(server_fd, &server_params) != 0)
    return -1;

  return crypto_handshake_set_parameters(server_ctx, &server_params);
}

static int client_protocol_negotiation(int client_fd, crypto_handshake_context_t *client_ctx) {
  packet_type_t pkt_type;
  void *payload = NULL;
  size_t payload_len = 0;

  // Send client's PROTOCOL_VERSION
  protocol_version_packet_t client_version = {0};
  client_version.protocol_version = htons(1);
  client_version.protocol_revision = htons(0);
  client_version.supports_encryption = 1;
  if (send_protocol_version_packet(client_fd, &client_version) != 0)
    return -1;

  // Receive server's PROTOCOL_VERSION
  if (recv_packet(client_fd, &pkt_type, &payload, &payload_len) != ASCIICHAT_OK ||
      pkt_type != PACKET_TYPE_PROTOCOL_VERSION) {
    if (payload)
      buffer_pool_free(NULL, payload, payload_len);
    return -1;
  }
  buffer_pool_free(NULL, payload, payload_len);

  // Send client's CRYPTO_CAPABILITIES
  crypto_capabilities_packet_t client_caps = {0};
  client_caps.supported_kex_algorithms = htons(KEX_ALGO_X25519);
  client_caps.supported_auth_algorithms = htons(AUTH_ALGO_ED25519 | AUTH_ALGO_NONE);
  client_caps.supported_cipher_algorithms = htons(CIPHER_ALGO_XSALSA20_POLY1305);
  if (send_crypto_capabilities_packet(client_fd, &client_caps) != 0)
    return -1;

  // Receive server's CRYPTO_PARAMETERS
  payload = NULL;
  if (recv_packet(client_fd, &pkt_type, &payload, &payload_len) != ASCIICHAT_OK ||
      pkt_type != PACKET_TYPE_CRYPTO_PARAMETERS) {
    if (payload)
      buffer_pool_free(NULL, payload, payload_len);
    return -1;
  }

  crypto_parameters_packet_t server_params;
  memcpy(&server_params, payload, sizeof(crypto_parameters_packet_t));
  buffer_pool_free(NULL, payload, payload_len);

  return crypto_handshake_set_parameters(client_ctx, &server_params);
}

// =============================================================================
// Client thread for handshake
// =============================================================================

typedef struct {
  int client_fd;
  acip_transport_t *transport;
  crypto_handshake_context_t *ctx;
  int result;
} handshake_client_args_t;

static void *handshake_client_thread(void *arg) {
  handshake_client_args_t *args = (handshake_client_args_t *)arg;
  args->result = -1;

  packet_type_t pkt_type;
  void *payload = NULL;
  size_t payload_len = 0;

  // Protocol negotiation (still uses raw packets)
  if (client_protocol_negotiation(args->client_fd, args->ctx) != ASCIICHAT_OK)
    return NULL;

  // Receive server's KEY_EXCHANGE_INIT
  if (recv_packet(args->client_fd, &pkt_type, &payload, &payload_len) != ASCIICHAT_OK)
    return NULL;

  // Client key exchange
  if (crypto_handshake_client_key_exchange(args->ctx, args->transport, pkt_type, payload, payload_len) !=
      ASCIICHAT_OK) {
    buffer_pool_free(NULL, payload, payload_len);
    return NULL;
  }
  buffer_pool_free(NULL, payload, payload_len);

  // Receive server's AUTH_CHALLENGE or HANDSHAKE_COMPLETE
  payload = NULL;
  if (recv_packet(args->client_fd, &pkt_type, &payload, &payload_len) != ASCIICHAT_OK)
    return NULL;

  if (crypto_handshake_client_auth_response(args->ctx, args->transport, pkt_type, payload, payload_len) !=
      ASCIICHAT_OK) {
    buffer_pool_free(NULL, payload, payload_len);
    return NULL;
  }
  buffer_pool_free(NULL, payload, payload_len);

  // If authenticating, receive HANDSHAKE_COMPLETE
  if (args->ctx->state == CRYPTO_HANDSHAKE_AUTHENTICATING) {
    payload = NULL;
    if (recv_packet(args->client_fd, &pkt_type, &payload, &payload_len) != ASCIICHAT_OK)
      return NULL;
    if (crypto_handshake_client_complete(args->ctx, args->transport, pkt_type, payload, payload_len) !=
        ASCIICHAT_OK) {
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

Test(crypto_handshake_integration, complete_handshake_flow) {
  setup_test_network();

  crypto_handshake_context_t server_ctx, client_ctx;
  cr_assert_eq(crypto_handshake_init("test-server", &server_ctx, true), ASCIICHAT_OK);
  cr_assert_eq(crypto_handshake_init("test-client", &client_ctx, false), ASCIICHAT_OK);
  setup_client_ctx_for_socketpair(&client_ctx);

  // Start client thread
  handshake_client_args_t client_args = {
      .client_fd = g_network.client_fd,
      .transport = g_network.client_transport,
      .ctx = &client_ctx,
      .result = -1,
  };
  pthread_t client_thread;
  pthread_create(&client_thread, NULL, handshake_client_thread, &client_args);

  // Server: protocol negotiation
  cr_assert_eq(server_protocol_negotiation(g_network.server_fd, &server_ctx), ASCIICHAT_OK);

  // Server: send key exchange init
  cr_assert_eq(crypto_handshake_server_start(&server_ctx, g_network.server_transport), ASCIICHAT_OK);

  // Server: receive client's key exchange response
  packet_type_t pkt_type;
  void *payload = NULL;
  size_t payload_len = 0;
  cr_assert_eq(recv_packet(g_network.server_fd, &pkt_type, &payload, &payload_len), ASCIICHAT_OK);

  // Server: process client key and send auth challenge (or complete)
  asciichat_error_t auth_result =
      crypto_handshake_server_auth_challenge(&server_ctx, g_network.server_transport, pkt_type, payload, payload_len);
  cr_assert_eq(auth_result, ASCIICHAT_OK);
  buffer_pool_free(NULL, payload, payload_len);

  // If authenticating, receive client auth response and complete
  if (server_ctx.state == CRYPTO_HANDSHAKE_AUTHENTICATING) {
    payload = NULL;
    cr_assert_eq(recv_packet(g_network.server_fd, &pkt_type, &payload, &payload_len), ASCIICHAT_OK);
    cr_assert_eq(
        crypto_handshake_server_complete(&server_ctx, g_network.server_transport, pkt_type, payload, payload_len),
        ASCIICHAT_OK);
    buffer_pool_free(NULL, payload, payload_len);
  }

  pthread_join(client_thread, NULL);
  cr_assert_eq(client_args.result, 0, "Client handshake should succeed");
  cr_assert_eq(server_ctx.state, CRYPTO_HANDSHAKE_READY);
  cr_assert_eq(client_ctx.state, CRYPTO_HANDSHAKE_READY);

  crypto_handshake_destroy(&server_ctx);
  crypto_handshake_destroy(&client_ctx);
  teardown_test_network();
}

Test(crypto_handshake_integration, key_type_parsing) {
  struct {
    const char *key_data;
    key_type_t expected_type;
    const char *description;
  } test_cases[] = {
      {"ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGplY2VrZXJzIGVkMjU1MTkga2V5", KEY_TYPE_ED25519, "SSH Ed25519 key"},
      {"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", KEY_TYPE_X25519, "X25519 hex key"},
      {"gpg:0x1234567890ABCDEF", KEY_TYPE_GPG, "GPG key ID"},
  };

  for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); i++) {
    public_key_t key;
    int parse_result = parse_public_key(test_cases[i].key_data, &key);
    if (parse_result == 0) {
      cr_assert_eq(key.type, test_cases[i].expected_type, "Key type should match for: %s",
                   test_cases[i].description);
    }
  }
}

Test(crypto_handshake_integration, encryption_after_handshake) {
  setup_test_network();

  crypto_handshake_context_t server_ctx, client_ctx;
  crypto_handshake_init("test-server", &server_ctx, true);
  crypto_handshake_init("test-client", &client_ctx, false);
  setup_client_ctx_for_socketpair(&client_ctx);

  // Run handshake via client thread
  handshake_client_args_t client_args = {
      .client_fd = g_network.client_fd,
      .transport = g_network.client_transport,
      .ctx = &client_ctx,
      .result = -1,
  };
  pthread_t client_thread;
  pthread_create(&client_thread, NULL, handshake_client_thread, &client_args);

  // Server side
  server_protocol_negotiation(g_network.server_fd, &server_ctx);
  crypto_handshake_server_start(&server_ctx, g_network.server_transport);

  packet_type_t pkt_type;
  void *payload = NULL;
  size_t payload_len = 0;
  recv_packet(g_network.server_fd, &pkt_type, &payload, &payload_len);
  crypto_handshake_server_auth_challenge(&server_ctx, g_network.server_transport, pkt_type, payload, payload_len);
  buffer_pool_free(NULL, payload, payload_len);

  if (server_ctx.state == CRYPTO_HANDSHAKE_AUTHENTICATING) {
    payload = NULL;
    recv_packet(g_network.server_fd, &pkt_type, &payload, &payload_len);
    crypto_handshake_server_complete(&server_ctx, g_network.server_transport, pkt_type, payload, payload_len);
    buffer_pool_free(NULL, payload, payload_len);
  }

  pthread_join(client_thread, NULL);
  cr_assert_eq(client_args.result, 0);
  cr_assert_eq(server_ctx.state, CRYPTO_HANDSHAKE_READY);
  cr_assert_eq(client_ctx.state, CRYPTO_HANDSHAKE_READY);

  // Test encryption/decryption
  const char *plaintext = "Hello, encrypted world!";
  uint8_t ciphertext[256];
  uint8_t decrypted[256];
  size_t ciphertext_len, decrypted_len;

  cr_assert_eq(crypto_handshake_encrypt_packet(&server_ctx, (const uint8_t *)plaintext, strlen(plaintext), ciphertext,
                                                sizeof(ciphertext), &ciphertext_len),
               0);
  cr_assert_gt(ciphertext_len, 0);

  cr_assert_eq(crypto_handshake_decrypt_packet(&client_ctx, ciphertext, ciphertext_len, decrypted, sizeof(decrypted),
                                                &decrypted_len),
               0);
  cr_assert_eq(decrypted_len, strlen(plaintext));
  cr_assert_eq(memcmp(decrypted, plaintext, strlen(plaintext)), 0);

  crypto_handshake_destroy(&server_ctx);
  crypto_handshake_destroy(&client_ctx);
  teardown_test_network();
}
