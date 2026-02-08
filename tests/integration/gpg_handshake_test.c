/**
 * @file gpg_handshake_test.c
 * @brief Integration test for GPG authentication in crypto handshake
 *
 * Tests end-to-end GPG key authentication:
 * - Server authenticates with GPG key
 * - Client authenticates with GPG key
 * - Both sides verify signatures via GPG
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
#include <ascii-chat/network/packet.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/tests/logging.h>

// Use verbose logging for debugging
TEST_SUITE_WITH_DEBUG_LOGGING(gpg_handshake);

// Test GPG key ID - obtained from environment variable set by setup script
// If not set, tests will be skipped
static const char *get_test_gpg_key_id(void) {
  const char *key_id = getenv("TEST_GPG_KEY_ID");
  if (!key_id || strlen(key_id) != 16) {
    return NULL;
  }
  return key_id;
}

// Network state for testing
typedef struct {
  int server_fd;
  int client_fd;
  bool connected;
} test_network_t;

static test_network_t g_network = {0};

// Setup and teardown
void setup_gpg_test_network(void) {
  memset(&g_network, 0, sizeof(g_network));

  // Initialize global buffer pool for packet allocations
  buffer_pool_init_global();

  // Create socket pair for testing
  int sv[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
    fprintf(stderr, "Failed to create socket pair: %s\n", strerror(errno));
    return;
  }
  g_network.server_fd = sv[0];
  g_network.client_fd = sv[1];
  g_network.connected = true;

  // Skip host identity checking in tests
  setenv("ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK", "1", 1);
}

void teardown_gpg_test_network(void) {
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
  packet_type_t packet_type;
  void *payload = NULL;
  size_t payload_len = 0;

  // Receive client's PROTOCOL_VERSION
  int result = receive_packet(server_fd, &packet_type, &payload, &payload_len);
  if (result != ASCIICHAT_OK || packet_type != PACKET_TYPE_PROTOCOL_VERSION) {
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
  result = send_protocol_version_packet(server_fd, &server_version);
  if (result != 0)
    return -1;

  // Receive client's CRYPTO_CAPABILITIES
  payload = NULL;
  result = receive_packet(server_fd, &packet_type, &payload, &payload_len);
  if (result != ASCIICHAT_OK || packet_type != PACKET_TYPE_CRYPTO_CAPABILITIES) {
    if (payload)
      buffer_pool_free(NULL, payload, payload_len);
    return -1;
  }
  buffer_pool_free(NULL, payload, payload_len);

  // Send server's CRYPTO_CAPABILITIES
  crypto_capabilities_packet_t server_caps = {0};
  server_caps.supported_kex_algorithms = htons(KEX_ALGO_X25519);
  server_caps.supported_auth_algorithms = htons(AUTH_ALGO_ED25519);
  server_caps.supported_cipher_algorithms = htons(CIPHER_ALGO_XSALSA20_POLY1305);
  result = send_crypto_capabilities_packet(server_fd, &server_caps);
  if (result != 0)
    return -1;

  // Send server's CRYPTO_PARAMETERS with authentication enabled
  crypto_parameters_packet_t server_params = {0};
  server_params.selected_kex = KEX_ALGO_X25519;
  server_params.selected_auth = AUTH_ALGO_ED25519; // GPG key authentication
  server_params.selected_cipher = CIPHER_ALGO_XSALSA20_POLY1305;
  server_params.verification_enabled = 1;
  server_params.kex_public_key_size = CRYPTO_PUBLIC_KEY_SIZE;
  server_params.auth_public_key_size = ED25519_PUBLIC_KEY_SIZE; // Ed25519 identity key
  server_params.signature_size = ED25519_SIGNATURE_SIZE;        // Ed25519 signature
  server_params.shared_secret_size = CRYPTO_PUBLIC_KEY_SIZE;
  server_params.nonce_size = CRYPTO_NONCE_SIZE;
  server_params.mac_size = CRYPTO_MAC_SIZE;
  server_params.hmac_size = CRYPTO_HMAC_SIZE;

  result = send_crypto_parameters_packet(server_fd, &server_params);
  if (result != 0)
    return -1;

  // Set parameters in server context (server uses host byte order)
  return crypto_handshake_set_parameters(server_ctx, &server_params);
}

static int client_protocol_negotiation(int client_fd, crypto_handshake_context_t *client_ctx) {
  // Send client's PROTOCOL_VERSION
  protocol_version_packet_t client_version = {0};
  client_version.protocol_version = htons(1);
  client_version.protocol_revision = htons(0);
  client_version.supports_encryption = 1;
  int result = send_protocol_version_packet(client_fd, &client_version);
  if (result != 0)
    return -1;

  // Receive server's PROTOCOL_VERSION
  packet_type_t packet_type;
  void *payload = NULL;
  size_t payload_len = 0;
  result = receive_packet(client_fd, &packet_type, &payload, &payload_len);
  if (result != ASCIICHAT_OK || packet_type != PACKET_TYPE_PROTOCOL_VERSION) {
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
  result = send_crypto_capabilities_packet(client_fd, &client_caps);
  if (result != 0)
    return -1;

  // Receive server's CRYPTO_CAPABILITIES
  payload = NULL;
  result = receive_packet(client_fd, &packet_type, &payload, &payload_len);
  if (result != ASCIICHAT_OK || packet_type != PACKET_TYPE_CRYPTO_CAPABILITIES) {
    if (payload)
      buffer_pool_free(NULL, payload, payload_len);
    return -1;
  }
  buffer_pool_free(NULL, payload, payload_len);

  // Receive server's CRYPTO_PARAMETERS
  payload = NULL;
  result = receive_packet(client_fd, &packet_type, &payload, &payload_len);
  if (result != ASCIICHAT_OK || packet_type != PACKET_TYPE_CRYPTO_PARAMETERS) {
    if (payload)
      buffer_pool_free(NULL, payload, payload_len);
    return -1;
  }

  crypto_parameters_packet_t server_params;
  memcpy(&server_params, payload, sizeof(crypto_parameters_packet_t));
  buffer_pool_free(NULL, payload, payload_len);

  // Set parameters in client context (client converts from network byte order)
  return crypto_handshake_set_parameters(client_ctx, &server_params);
}

// =============================================================================
// Client Thread
// =============================================================================

typedef struct {
  int client_fd;
  crypto_handshake_context_t *ctx;
  int result;
} client_thread_args_t;

static void *client_handshake_thread(void *arg) {
  client_thread_args_t *args = (client_thread_args_t *)arg;
  args->result = -1;

  // Protocol negotiation
  fprintf(stderr, "[TEST] Client: Starting protocol negotiation\n");
  if (client_protocol_negotiation(args->client_fd, args->ctx) != ASCIICHAT_OK) {
    fprintf(stderr, "[TEST] Client: Protocol negotiation FAILED\n");
    return NULL;
  }
  fprintf(stderr, "[TEST] Client: Protocol negotiation OK\n");

  // Key exchange
  fprintf(stderr, "[TEST] Client: Starting key exchange\n");
  if (crypto_handshake_client_key_exchange_socket(args->ctx, args->client_fd) != ASCIICHAT_OK) {
    fprintf(stderr, "[TEST] Client: Key exchange FAILED\n");
    return NULL;
  }
  fprintf(stderr, "[TEST] Client: Key exchange OK\n");

  // Respond to auth challenge
  fprintf(stderr, "[TEST] Client: Starting auth response\n");
  if (crypto_handshake_client_auth_response_socket(args->ctx, args->client_fd) != ASCIICHAT_OK) {
    fprintf(stderr, "[TEST] Client: Auth response FAILED\n");
    return NULL;
  }
  fprintf(stderr, "[TEST] Client: Auth response OK\n");

  // Wait for handshake complete confirmation
  fprintf(stderr, "[TEST] Client: Waiting for handshake complete\n");
  if (crypto_handshake_client_complete_socket(args->ctx, args->client_fd) != ASCIICHAT_OK) {
    fprintf(stderr, "[TEST] Client: Handshake complete FAILED\n");
    return NULL;
  }
  fprintf(stderr, "[TEST] Client: Handshake complete confirmation received\n");

  args->result = 0;
  fprintf(stderr, "[TEST] Client: Handshake complete!\n");
  return NULL;
}

// =============================================================================
// GPG Authentication Test
// =============================================================================

Test(gpg_handshake, complete_gpg_handshake_with_authentication) {
  const char *test_key_id = get_test_gpg_key_id();
  if (!test_key_id) {
    cr_skip_test("TEST_GPG_KEY_ID environment variable not set");
  }

  setup_gpg_test_network();

  // Initialize contexts
  crypto_handshake_context_t server_ctx, client_ctx;
  asciichat_error_t init_server = crypto_handshake_init(&server_ctx, true);
  asciichat_error_t init_client = crypto_handshake_init(&client_ctx, false);
  cr_assert_eq(init_server, ASCIICHAT_OK, "Server init failed: %d", init_server);
  cr_assert_eq(init_client, ASCIICHAT_OK, "Client init failed: %d", init_client);

  // Set fake server IP/port for socketpair
  SAFE_STRNCPY(client_ctx.server_ip, "127.0.0.1", sizeof(client_ctx.server_ip));
  client_ctx.server_port = 27224;

  // Configure server with GPG key
  char server_key_input[128];
  snprintf(server_key_input, sizeof(server_key_input), "gpg:%s", test_key_id);

  asciichat_error_t server_key_result = parse_private_key(server_key_input, &server_ctx.server_private_key);
  cr_assert_eq(server_key_result, ASCIICHAT_OK, "Failed to parse server GPG key: %d", server_key_result);
  cr_assert_eq(server_ctx.server_private_key.type, KEY_TYPE_ED25519, "Server key should be Ed25519 type (GPG-derived)");
  cr_assert_eq(server_ctx.server_private_key.use_gpg_agent, true, "Server should use GPG agent");

  // Configure client with GPG key
  char client_key_input[128];
  snprintf(client_key_input, sizeof(client_key_input), "gpg:%s", test_key_id);

  asciichat_error_t client_key_result = parse_private_key(client_key_input, &client_ctx.client_private_key);
  cr_assert_eq(client_key_result, ASCIICHAT_OK, "Failed to parse client GPG key: %d", client_key_result);
  cr_assert_eq(client_ctx.client_private_key.type, KEY_TYPE_ED25519, "Client key should be Ed25519 type (GPG-derived)");
  cr_assert_eq(client_ctx.client_private_key.use_gpg_agent, true, "Client should use GPG agent");

  // Start client thread
  pthread_t client_thread;
  client_thread_args_t client_args = {
      .client_fd = g_network.client_fd,
      .ctx = &client_ctx,
      .result = -1,
  };
  int thread_result = pthread_create(&client_thread, NULL, client_handshake_thread, &client_args);
  cr_assert_eq(thread_result, 0, "Failed to create client thread");

  // Server-side: protocol negotiation
  int server_nego_result = server_protocol_negotiation(g_network.server_fd, &server_ctx);
  cr_assert_eq(server_nego_result, ASCIICHAT_OK, "Server protocol negotiation failed: %d", server_nego_result);

  // Server starts key exchange
  asciichat_error_t server_start = crypto_handshake_server_start_socket(&server_ctx, g_network.server_fd);
  cr_assert_eq(server_start, ASCIICHAT_OK, "Server start failed: %d", server_start);

  // Server sends auth challenge
  asciichat_error_t server_auth = crypto_handshake_server_auth_challenge_socket(&server_ctx, g_network.server_fd);
  cr_assert_eq(server_auth, ASCIICHAT_OK, "Server auth challenge failed: %d", server_auth);

  // Server completes handshake
  if (server_ctx.state == CRYPTO_HANDSHAKE_AUTHENTICATING) {
    asciichat_error_t server_complete = crypto_handshake_server_complete_socket(&server_ctx, g_network.server_fd);
    cr_assert_eq(server_complete, ASCIICHAT_OK, "Server complete failed: %d", server_complete);
  }

  // Wait for client thread
  pthread_join(client_thread, NULL);
  cr_assert_eq(client_args.result, 0, "Client handshake failed");

  // Verify final states
  cr_assert_eq(server_ctx.state, CRYPTO_HANDSHAKE_READY, "Server should be READY, got state %d", server_ctx.state);
  cr_assert_eq(client_ctx.state, CRYPTO_HANDSHAKE_READY, "Client should be READY, got state %d", client_ctx.state);

  // Cleanup
  crypto_handshake_destroy(&server_ctx);
  crypto_handshake_destroy(&client_ctx);
  teardown_gpg_test_network();
}

// =============================================================================
// GPG Key Verification Test
// =============================================================================

Test(gpg_handshake, gpg_key_parsing_and_verification) {
  const char *test_key_id = get_test_gpg_key_id();
  if (!test_key_id) {
    cr_skip_test("TEST_GPG_KEY_ID environment variable not set");
  }

  // Parse GPG public key
  char pub_key_input[128];
  snprintf(pub_key_input, sizeof(pub_key_input), "gpg:%s", test_key_id);

  public_key_t public_key;
  memset(&public_key, 0, sizeof(public_key));
  asciichat_error_t pub_result = parse_public_key(pub_key_input, &public_key);
  cr_assert_eq(pub_result, ASCIICHAT_OK, "Failed to parse GPG public key: %d", pub_result);
  cr_assert_eq(public_key.type, KEY_TYPE_GPG, "Public key should be GPG type");

  // Verify we got a valid 32-byte public key (not all zeros)
  bool all_zeros = true;
  for (int i = 0; i < 32; i++) {
    if (public_key.key[i] != 0) {
      all_zeros = false;
      break;
    }
  }
  cr_assert_eq(all_zeros, false, "Public key should not be all zeros");

  // Parse GPG private key
  char priv_key_input[128];
  snprintf(priv_key_input, sizeof(priv_key_input), "gpg:%s", test_key_id);

  private_key_t private_key;
  memset(&private_key, 0, sizeof(private_key));
  asciichat_error_t priv_result = parse_private_key(priv_key_input, &private_key);
  cr_assert_eq(priv_result, ASCIICHAT_OK, "Failed to parse GPG private key: %d", priv_result);
  cr_assert_eq(private_key.type, KEY_TYPE_ED25519, "Private key should be Ed25519 type (GPG-derived)");
  cr_assert_eq(private_key.use_gpg_agent, true, "Should use GPG agent");
  cr_assert_eq(strlen(private_key.gpg_keygrip), 40, "Keygrip should be 40 characters, got %zu",
               strlen(private_key.gpg_keygrip));
}
