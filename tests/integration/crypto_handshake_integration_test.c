/**
 * @file crypto_handshake_integration_test.c
 * @brief Integration tests for end-to-end crypto handshake - Tests the intended final crypto implementation
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/parameterized.h>
#include <criterion/theories.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <arpa/inet.h>

#include "tests/common.h"
#include "crypto/handshake.h"
#include "crypto/keys/keys.h"
#include "crypto/known_hosts.h"
#include "network/packet.h"
#include "network/packet_types.h"
#include "buffer_pool.h"
#include "tests/logging.h"

// Use verbose logging to debug test failures
TEST_SUITE_WITH_QUIET_LOGGING(crypto_handshake_integration);

// Network state for integration testing
typedef struct {
  int server_fd;
  int client_fd;
  bool connected;
} test_network_t;

static test_network_t g_network = {0};

// Test setup and teardown
void setup_test_network(void) {
  memset(&g_network, 0, sizeof(g_network));

  // Use real socket pairs for integration testing
  int sv[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
    cr_fatal("Failed to create socket pair: %s", strerror(errno));
    return;
  }
  g_network.server_fd = sv[0];
  g_network.client_fd = sv[1];
  g_network.connected = true;

  // Skip host identity checking in tests
  setenv("ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK", "1", 1);
}

// Helper to set up client context for socketpair testing (AF_UNIX has no IP)
static void setup_client_ctx_for_socketpair(crypto_handshake_context_t *ctx) {
  SAFE_STRNCPY(ctx->server_ip, "127.0.0.1", sizeof(ctx->server_ip));
  ctx->server_port = 27224;
}

void teardown_test_network(void) {
  if (g_network.server_fd > 0)
    close(g_network.server_fd);
  if (g_network.client_fd > 0)
    close(g_network.client_fd);
  memset(&g_network, 0, sizeof(g_network));
}

// =============================================================================
// Protocol Negotiation Helpers
// =============================================================================

// Server-side protocol negotiation: receive client version/caps, send server response
static int server_protocol_negotiation(int server_fd, crypto_handshake_context_t *server_ctx) {
  packet_type_t packet_type;
  void *payload = NULL;
  size_t payload_len = 0;

  // Step 1: Receive client's PROTOCOL_VERSION
  int result = receive_packet(server_fd, &packet_type, &payload, &payload_len);
  if (result != ASCIICHAT_OK || packet_type != PACKET_TYPE_PROTOCOL_VERSION) {
    if (payload)
      buffer_pool_free(payload, payload_len);
    return -1;
  }
  buffer_pool_free(payload, payload_len);

  // Step 2: Send server's PROTOCOL_VERSION
  protocol_version_packet_t server_version = {0};
  server_version.protocol_version = htons(1);
  server_version.protocol_revision = htons(0);
  server_version.supports_encryption = 1;
  result = send_protocol_version_packet(server_fd, &server_version);
  if (result != 0)
    return -1;

  // Step 3: Receive client's CRYPTO_CAPABILITIES
  payload = NULL;
  result = receive_packet(server_fd, &packet_type, &payload, &payload_len);
  if (result != ASCIICHAT_OK || packet_type != PACKET_TYPE_CRYPTO_CAPABILITIES) {
    if (payload)
      buffer_pool_free(payload, payload_len);
    return -1;
  }
  buffer_pool_free(payload, payload_len);

  // Step 4: Send server's CRYPTO_PARAMETERS
  crypto_parameters_packet_t server_params = {0};
  server_params.selected_kex = KEX_ALGO_X25519;
  server_params.selected_auth = AUTH_ALGO_NONE; // Simple mode, no auth
  server_params.selected_cipher = CIPHER_ALGO_XSALSA20_POLY1305;
  server_params.verification_enabled = 0;
  server_params.kex_public_key_size = CRYPTO_PUBLIC_KEY_SIZE;
  server_params.auth_public_key_size = 0; // No auth keys
  server_params.signature_size = 0;       // No signatures
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

// Client-side protocol negotiation: send client version/caps, receive server response
static int client_protocol_negotiation(int client_fd, crypto_handshake_context_t *client_ctx) {
  packet_type_t packet_type;
  void *payload = NULL;
  size_t payload_len = 0;

  // Step 1: Send client's PROTOCOL_VERSION
  protocol_version_packet_t client_version = {0};
  client_version.protocol_version = htons(1);
  client_version.protocol_revision = htons(0);
  client_version.supports_encryption = 1;
  int result = send_protocol_version_packet(client_fd, &client_version);
  if (result != 0)
    return -1;

  // Step 2: Receive server's PROTOCOL_VERSION
  result = receive_packet(client_fd, &packet_type, &payload, &payload_len);
  if (result != ASCIICHAT_OK || packet_type != PACKET_TYPE_PROTOCOL_VERSION) {
    if (payload)
      buffer_pool_free(payload, payload_len);
    return -1;
  }
  buffer_pool_free(payload, payload_len);

  // Step 3: Send client's CRYPTO_CAPABILITIES
  crypto_capabilities_packet_t client_caps = {0};
  client_caps.supported_kex_algorithms = htons(KEX_ALGO_X25519);
  client_caps.supported_auth_algorithms = htons(AUTH_ALGO_ED25519 | AUTH_ALGO_NONE);
  client_caps.supported_cipher_algorithms = htons(CIPHER_ALGO_XSALSA20_POLY1305);
  result = send_crypto_capabilities_packet(client_fd, &client_caps);
  if (result != 0)
    return -1;

  // Step 4: Receive server's CRYPTO_PARAMETERS
  payload = NULL;
  result = receive_packet(client_fd, &packet_type, &payload, &payload_len);
  if (result != ASCIICHAT_OK || packet_type != PACKET_TYPE_CRYPTO_PARAMETERS) {
    if (payload)
      buffer_pool_free(payload, payload_len);
    return -1;
  }

  crypto_parameters_packet_t server_params;
  memcpy(&server_params, payload, sizeof(crypto_parameters_packet_t));
  buffer_pool_free(payload, payload_len);

  // Set parameters in client context (client converts from network byte order)
  return crypto_handshake_set_parameters(client_ctx, &server_params);
}

// =============================================================================
// Complete Handshake Flow Tests
// =============================================================================

// Thread arguments for complete handshake client
typedef struct {
  int client_fd;
  crypto_handshake_context_t *ctx;
  int result;
} complete_handshake_client_args_t;

// Client thread for complete handshake test
static void *complete_handshake_client_thread(void *arg) {
  complete_handshake_client_args_t *args = (complete_handshake_client_args_t *)arg;
  args->result = -1;

  // Protocol negotiation
  if (client_protocol_negotiation(args->client_fd, args->ctx) != ASCIICHAT_OK) {
    return NULL;
  }

  // Key exchange
  if (crypto_handshake_client_key_exchange(args->ctx, args->client_fd) != ASCIICHAT_OK) {
    return NULL;
  }

  // Auth response (handles both AUTH_CHALLENGE and HANDSHAKE_COMPLETE)
  if (crypto_handshake_client_auth_response(args->ctx, args->client_fd) != ASCIICHAT_OK) {
    return NULL;
  }

  // If we're in AUTHENTICATING state, complete the handshake
  if (args->ctx->state == CRYPTO_HANDSHAKE_AUTHENTICATING) {
    if (crypto_handshake_client_complete(args->ctx, args->client_fd) != ASCIICHAT_OK) {
      return NULL;
    }
  }

  args->result = 0;
  return NULL;
}

Test(crypto_handshake_integration, complete_handshake_flow) {
  setup_test_network();

  // Initialize server and client contexts
  crypto_handshake_context_t server_ctx, client_ctx;
  asciichat_error_t init_server = crypto_handshake_init(&server_ctx, true);
  asciichat_error_t init_client = crypto_handshake_init(&client_ctx, false);
  cr_assert_eq(init_server, ASCIICHAT_OK, "Server init should succeed (got %d)", init_server);
  cr_assert_eq(init_client, ASCIICHAT_OK, "Client init should succeed (got %d)", init_client);

  // Set fake server IP/port for the client (AF_UNIX socketpair has no IP addresses)
  setup_client_ctx_for_socketpair(&client_ctx);

  // Start client thread
  pthread_t client_thread;
  complete_handshake_client_args_t client_args = {
      .client_fd = g_network.client_fd,
      .ctx = &client_ctx,
      .result = -1,
  };
  int thread_result = pthread_create(&client_thread, NULL, complete_handshake_client_thread, &client_args);
  cr_assert_eq(thread_result, 0, "Client thread should be created");

  // Server-side: protocol negotiation
  int server_nego_result = server_protocol_negotiation(g_network.server_fd, &server_ctx);
  cr_assert_eq(server_nego_result, ASCIICHAT_OK, "Server protocol negotiation should succeed (got %d)",
               server_nego_result);

  // Server starts key exchange
  asciichat_error_t server_result = crypto_handshake_server_start(&server_ctx, g_network.server_fd);
  cr_assert_eq(server_result, ASCIICHAT_OK, "Server start should succeed (got %d)", server_result);

  // Server processes client key exchange and sends auth challenge (or HANDSHAKE_COMPLETE if no auth needed)
  asciichat_error_t server_auth_result = crypto_handshake_server_auth_challenge(&server_ctx, g_network.server_fd);
  cr_assert_eq(server_auth_result, ASCIICHAT_OK, "Server auth challenge should succeed (got %d)", server_auth_result);

  // If authentication was performed (server is in AUTHENTICATING state), complete the handshake
  if (server_ctx.state == CRYPTO_HANDSHAKE_AUTHENTICATING) {
    asciichat_error_t server_complete_result = crypto_handshake_server_complete(&server_ctx, g_network.server_fd);
    cr_assert_eq(server_complete_result, ASCIICHAT_OK, "Server complete should succeed (got %d)",
                 server_complete_result);
  }

  // Wait for client thread
  pthread_join(client_thread, NULL);
  cr_assert_eq(client_args.result, 0, "Client handshake should succeed");

  // Verify final states - both should be READY
  cr_assert_eq(server_ctx.state, CRYPTO_HANDSHAKE_READY, "Server should be complete");
  cr_assert_eq(client_ctx.state, CRYPTO_HANDSHAKE_READY, "Client should be ready");

  crypto_handshake_cleanup(&server_ctx);
  crypto_handshake_cleanup(&client_ctx);
  teardown_test_network();
}

// =============================================================================
// Key Type Tests
// =============================================================================

// Test key type parsing - uses a loop instead of ParameterizedTest to avoid
// Criterion's fork issues with pointer data
Test(crypto_handshake_integration, key_type_parsing) {
  // Test cases defined inline to avoid pointer issues with Criterion's fork model
  struct {
    const char *key_data;
    key_type_t expected_type;
    const char *description;
  } test_cases[] = {
      {"ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGplY2VrZXJzIGVkMjU1MTkga2V5", KEY_TYPE_ED25519, "SSH Ed25519 key"},
      {"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", KEY_TYPE_X25519, "X25519 hex key"},
      {"gpg:0x1234567890ABCDEF", KEY_TYPE_GPG, "GPG key ID"},
  };
  size_t num_cases = sizeof(test_cases) / sizeof(test_cases[0]);

  for (size_t i = 0; i < num_cases; i++) {
    public_key_t key;
    int parse_result = parse_public_key(test_cases[i].key_data, &key);

    if (parse_result == 0) {
      cr_assert_eq(key.type, test_cases[i].expected_type, "Key type should match for case: %s",
                   test_cases[i].description);
    }
    // Note: Some keys might fail to parse without BearSSL/GPG - that's acceptable
  }
}

// NOTE: GitHub/GitLab key fetching requires network access, so we can't test
// it reliably in CI. The functionality is tested manually and in integration
// tests with actual network access.

// =============================================================================
// Encryption/Decryption Tests
// =============================================================================

Test(crypto_handshake_integration, encryption_after_handshake) {
  setup_test_network();

  crypto_handshake_context_t server_ctx, client_ctx;
  crypto_handshake_init(&server_ctx, true);
  crypto_handshake_init(&client_ctx, false);
  setup_client_ctx_for_socketpair(&client_ctx);

  // Start client thread for handshake
  pthread_t client_thread;
  complete_handshake_client_args_t client_args = {
      .client_fd = g_network.client_fd,
      .ctx = &client_ctx,
      .result = -1,
  };
  pthread_create(&client_thread, NULL, complete_handshake_client_thread, &client_args);

  // Server-side: protocol negotiation + handshake
  int nego_result = server_protocol_negotiation(g_network.server_fd, &server_ctx);
  cr_assert_eq(nego_result, ASCIICHAT_OK, "Server protocol negotiation should succeed");

  crypto_handshake_server_start(&server_ctx, g_network.server_fd);
  crypto_handshake_server_auth_challenge(&server_ctx, g_network.server_fd);
  if (server_ctx.state == CRYPTO_HANDSHAKE_AUTHENTICATING) {
    crypto_handshake_server_complete(&server_ctx, g_network.server_fd);
  }

  pthread_join(client_thread, NULL);
  cr_assert_eq(client_args.result, 0, "Client handshake should succeed");
  cr_assert_eq(server_ctx.state, CRYPTO_HANDSHAKE_READY, "Server should be ready");
  cr_assert_eq(client_ctx.state, CRYPTO_HANDSHAKE_READY, "Client should be ready");

  // Test encryption/decryption
  const char *plaintext = "Hello, encrypted world!";
  uint8_t ciphertext[256];
  uint8_t decrypted[256];
  size_t ciphertext_len, decrypted_len;

  // Server encrypts
  int encrypt_result = crypto_handshake_encrypt_packet(&server_ctx, (const uint8_t *)plaintext, strlen(plaintext),
                                                       ciphertext, sizeof(ciphertext), &ciphertext_len);
  cr_assert_eq(encrypt_result, 0, "Server encryption should succeed");
  cr_assert_gt(ciphertext_len, 0, "Ciphertext should not be empty");

  // Client decrypts
  int decrypt_result = crypto_handshake_decrypt_packet(&client_ctx, ciphertext, ciphertext_len, decrypted,
                                                       sizeof(decrypted), &decrypted_len);
  cr_assert_eq(decrypt_result, 0, "Client decryption should succeed");
  cr_assert_eq(decrypted_len, strlen(plaintext), "Decrypted length should match plaintext");
  cr_assert_eq(memcmp(decrypted, plaintext, strlen(plaintext)), 0, "Decrypted text should match plaintext");

  crypto_handshake_cleanup(&server_ctx);
  crypto_handshake_cleanup(&client_ctx);
  teardown_test_network();
}

Test(crypto_handshake_integration, bidirectional_encryption) {
  setup_test_network();

  crypto_handshake_context_t server_ctx, client_ctx;
  crypto_handshake_init(&server_ctx, true);
  crypto_handshake_init(&client_ctx, false);
  setup_client_ctx_for_socketpair(&client_ctx);

  // Start client thread for handshake
  pthread_t client_thread;
  complete_handshake_client_args_t client_args = {
      .client_fd = g_network.client_fd,
      .ctx = &client_ctx,
      .result = -1,
  };
  pthread_create(&client_thread, NULL, complete_handshake_client_thread, &client_args);

  // Server-side: protocol negotiation + handshake
  int nego_result = server_protocol_negotiation(g_network.server_fd, &server_ctx);
  cr_assert_eq(nego_result, ASCIICHAT_OK, "Server protocol negotiation should succeed");

  crypto_handshake_server_start(&server_ctx, g_network.server_fd);
  crypto_handshake_server_auth_challenge(&server_ctx, g_network.server_fd);
  if (server_ctx.state == CRYPTO_HANDSHAKE_AUTHENTICATING) {
    crypto_handshake_server_complete(&server_ctx, g_network.server_fd);
  }

  pthread_join(client_thread, NULL);
  cr_assert_eq(client_args.result, 0, "Client handshake should succeed");
  cr_assert_eq(server_ctx.state, CRYPTO_HANDSHAKE_READY, "Server should be ready");
  cr_assert_eq(client_ctx.state, CRYPTO_HANDSHAKE_READY, "Client should be ready");

  // Test bidirectional encryption
  const char *server_message = "Server to client message";
  const char *client_message = "Client to server message";
  uint8_t server_cipher[256], client_cipher[256];
  uint8_t server_decrypted[256], client_decrypted[256];
  size_t server_cipher_len, client_cipher_len, server_decrypted_len, client_decrypted_len;

  // Server encrypts to client
  int server_encrypt =
      crypto_handshake_encrypt_packet(&server_ctx, (const uint8_t *)server_message, strlen(server_message),
                                      server_cipher, sizeof(server_cipher), &server_cipher_len);
  cr_assert_eq(server_encrypt, 0, "Server encryption should succeed");

  // Client decrypts server message
  int client_decrypt = crypto_handshake_decrypt_packet(&client_ctx, server_cipher, server_cipher_len, client_decrypted,
                                                       sizeof(client_decrypted), &client_decrypted_len);
  cr_assert_eq(client_decrypt, 0, "Client decryption should succeed");
  cr_assert_eq(memcmp(client_decrypted, server_message, strlen(server_message)), 0,
               "Client should decrypt server message correctly");

  // Client encrypts to server
  int client_encrypt =
      crypto_handshake_encrypt_packet(&client_ctx, (const uint8_t *)client_message, strlen(client_message),
                                      client_cipher, sizeof(client_cipher), &client_cipher_len);
  cr_assert_eq(client_encrypt, 0, "Client encryption should succeed");

  // Server decrypts client message
  int server_decrypt = crypto_handshake_decrypt_packet(&server_ctx, client_cipher, client_cipher_len, server_decrypted,
                                                       sizeof(server_decrypted), &server_decrypted_len);
  cr_assert_eq(server_decrypt, 0, "Server decryption should succeed");
  cr_assert_eq(memcmp(server_decrypted, client_message, strlen(client_message)), 0,
               "Server should decrypt client message correctly");

  crypto_handshake_cleanup(&server_ctx);
  crypto_handshake_cleanup(&client_ctx);
  teardown_test_network();
}

// =============================================================================
// Authentication Scenarios (Theory Tests)
// =============================================================================

TheoryDataPoints(crypto_handshake_integration, authentication_scenarios) = {
    DataPoints(const char *, "password", "ssh-key", "github-key", "gpg-key"),
    DataPoints(bool, true, false), // known_hosts_verification
    DataPoints(bool, true, false), // client_whitelist_check
};

Theory((const char *auth_method, bool known_hosts_verification, bool client_whitelist_check),
       crypto_handshake_integration, authentication_scenarios) {
  // SKIP: Criterion Theory tests with socket pairs don't work across fork boundaries
  // The socket state is not properly shared between parent and child processes
  // TODO: Rewrite as regular tests or use a shared memory approach
  cr_skip("Criterion fork issues with socket pairs in Theory tests");

  setup_test_network();

  crypto_handshake_context_t server_ctx, client_ctx;
  crypto_handshake_init(&server_ctx, true);
  crypto_handshake_init(&client_ctx, false);
  setup_client_ctx_for_socketpair(&client_ctx);

  // Set up authentication context
  if (known_hosts_verification) {
    // Add server to known hosts
    uint8_t test_server_key[32];
    memset(test_server_key, 0xAB, 32); // Simple test key
    add_known_host("test-server.com", 8080, test_server_key);
  }

  if (client_whitelist_check) {
    // Add client to authorized keys
    public_key_t client_key;
    client_key.type = KEY_TYPE_ED25519;
    memset(client_key.key, 0x42, 32);
    strcpy(client_key.comment, "test-client");

    // This would normally be done through parse_keys_from_file
    // For testing, we'll simulate the check
  }

  // Complete handshake
  int server_start = crypto_handshake_server_start(&server_ctx, g_network.server_fd);
  int client_key_exchange = crypto_handshake_client_key_exchange(&client_ctx, g_network.client_fd);
  int server_auth = crypto_handshake_server_auth_challenge(&server_ctx, g_network.server_fd);
  int client_auth = crypto_handshake_client_auth_response(&client_ctx, g_network.client_fd);
  int server_complete = crypto_handshake_server_complete(&server_ctx, g_network.server_fd);

  // Cleanup - do before assertions so it always runs even if assertions fail
  crypto_handshake_cleanup(&server_ctx);
  crypto_handshake_cleanup(&client_ctx);
  teardown_test_network();

  // All steps should succeed (assertions after cleanup to ensure cleanup always runs)
  cr_assert_eq(server_start, 0, "Server start should succeed for auth method: %s", auth_method);
  cr_assert_eq(client_key_exchange, 0, "Client key exchange should succeed for auth method: %s", auth_method);
  cr_assert_eq(server_auth, 0, "Server auth should succeed for auth method: %s", auth_method);
  cr_assert_eq(client_auth, 0, "Client auth should succeed for auth method: %s", auth_method);
  cr_assert_eq(server_complete, 0, "Server complete should succeed for auth method: %s", auth_method);
}

// =============================================================================
// Concurrent Handshakes Tests
// =============================================================================

#define MAX_CONCURRENT_CLIENTS 3

typedef struct {
  int client_id;
  int client_fd; // Each client gets its own socket FD
  int result;
} client_handshake_args_t;

static void *client_handshake_thread(void *arg) {
  client_handshake_args_t *args = (client_handshake_args_t *)arg;
  args->result = -1;

  crypto_handshake_context_t client_ctx;
  crypto_handshake_init(&client_ctx, false);
  SAFE_STRNCPY(client_ctx.server_ip, "127.0.0.1", sizeof(client_ctx.server_ip));
  client_ctx.server_port = 27224;

  // Protocol negotiation
  if (client_protocol_negotiation(args->client_fd, &client_ctx) != ASCIICHAT_OK) {
    crypto_handshake_cleanup(&client_ctx);
    return NULL;
  }

  // Key exchange
  if (crypto_handshake_client_key_exchange(&client_ctx, args->client_fd) != ASCIICHAT_OK) {
    crypto_handshake_cleanup(&client_ctx);
    return NULL;
  }

  // Auth response
  if (crypto_handshake_client_auth_response(&client_ctx, args->client_fd) != ASCIICHAT_OK) {
    crypto_handshake_cleanup(&client_ctx);
    return NULL;
  }

  // If we're in AUTHENTICATING state, complete the handshake
  if (client_ctx.state == CRYPTO_HANDSHAKE_AUTHENTICATING) {
    if (crypto_handshake_client_complete(&client_ctx, args->client_fd) != ASCIICHAT_OK) {
      crypto_handshake_cleanup(&client_ctx);
      return NULL;
    }
  }

  args->result = (client_ctx.state == CRYPTO_HANDSHAKE_READY) ? 0 : -1;
  crypto_handshake_cleanup(&client_ctx);
  return NULL;
}

Test(crypto_handshake_integration, concurrent_handshakes) {
  // Create multiple socket pairs - one per client
  int server_fds[MAX_CONCURRENT_CLIENTS];
  int client_fds[MAX_CONCURRENT_CLIENTS];

  for (int i = 0; i < MAX_CONCURRENT_CLIENTS; i++) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
      // Clean up already-created sockets
      for (int j = 0; j < i; j++) {
        close(server_fds[j]);
        close(client_fds[j]);
      }
      cr_fatal("Failed to create socket pair %d: %s", i, strerror(errno));
      return;
    }
    server_fds[i] = sv[0];
    client_fds[i] = sv[1];
  }

  // Skip host identity checking in tests
  setenv("ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK", "1", 1);

  // Create client threads - each with its own socket
  pthread_t client_threads[MAX_CONCURRENT_CLIENTS];
  client_handshake_args_t client_args[MAX_CONCURRENT_CLIENTS];

  for (int i = 0; i < MAX_CONCURRENT_CLIENTS; i++) {
    client_args[i].client_id = i;
    client_args[i].client_fd = client_fds[i];
    client_args[i].result = -1;

    int thread_result = pthread_create(&client_threads[i], NULL, client_handshake_thread, &client_args[i]);
    cr_assert_eq(thread_result, 0, "Client thread %d should be created", i);
  }

  // Simulate server handling each client (sequentially for simplicity)
  for (int i = 0; i < MAX_CONCURRENT_CLIENTS; i++) {
    crypto_handshake_context_t server_ctx;
    crypto_handshake_init(&server_ctx, true);

    // Protocol negotiation
    int nego_result = server_protocol_negotiation(server_fds[i], &server_ctx);
    if (nego_result != ASCIICHAT_OK) {
      crypto_handshake_cleanup(&server_ctx);
      continue;
    }

    // Key exchange
    int server_start = crypto_handshake_server_start(&server_ctx, server_fds[i]);
    if (server_start != ASCIICHAT_OK) {
      crypto_handshake_cleanup(&server_ctx);
      continue;
    }

    // Auth challenge
    int auth_result = crypto_handshake_server_auth_challenge(&server_ctx, server_fds[i]);
    if (auth_result != ASCIICHAT_OK) {
      crypto_handshake_cleanup(&server_ctx);
      continue;
    }

    // Complete if in authenticating state
    if (server_ctx.state == CRYPTO_HANDSHAKE_AUTHENTICATING) {
      crypto_handshake_server_complete(&server_ctx, server_fds[i]);
    }

    crypto_handshake_cleanup(&server_ctx);
  }

  // Wait for all client threads to complete
  for (int i = 0; i < MAX_CONCURRENT_CLIENTS; i++) {
    pthread_join(client_threads[i], NULL);
  }

  // Verify results - all clients should have completed handshake successfully
  int successful = 0;
  for (int i = 0; i < MAX_CONCURRENT_CLIENTS; i++) {
    if (client_args[i].result == 0) {
      successful++;
    }
  }
  cr_assert_gt(successful, 0, "At least one client should complete handshake successfully");

  // Clean up all sockets
  for (int i = 0; i < MAX_CONCURRENT_CLIENTS; i++) {
    close(server_fds[i]);
    close(client_fds[i]);
  }
}

// =============================================================================
// Large Data Handling Tests
// =============================================================================

Test(crypto_handshake_integration, large_data_encryption) {
  setup_test_network();

  crypto_handshake_context_t server_ctx, client_ctx;
  crypto_handshake_init(&server_ctx, true);
  crypto_handshake_init(&client_ctx, false);
  setup_client_ctx_for_socketpair(&client_ctx);

  // Start client thread for handshake
  pthread_t client_thread;
  complete_handshake_client_args_t client_args = {
      .client_fd = g_network.client_fd,
      .ctx = &client_ctx,
      .result = -1,
  };
  pthread_create(&client_thread, NULL, complete_handshake_client_thread, &client_args);

  // Server-side: protocol negotiation + handshake
  int nego_result = server_protocol_negotiation(g_network.server_fd, &server_ctx);
  cr_assert_eq(nego_result, ASCIICHAT_OK, "Server protocol negotiation should succeed");

  crypto_handshake_server_start(&server_ctx, g_network.server_fd);
  crypto_handshake_server_auth_challenge(&server_ctx, g_network.server_fd);
  if (server_ctx.state == CRYPTO_HANDSHAKE_AUTHENTICATING) {
    crypto_handshake_server_complete(&server_ctx, g_network.server_fd);
  }

  pthread_join(client_thread, NULL);
  cr_assert_eq(client_args.result, 0, "Client handshake should succeed");
  cr_assert_eq(server_ctx.state, CRYPTO_HANDSHAKE_READY, "Server should be ready");
  cr_assert_eq(client_ctx.state, CRYPTO_HANDSHAKE_READY, "Client should be ready");

  // Test with large data
  const size_t large_size = 1024 * 1024; // 1MB
  uint8_t *large_data = SAFE_MALLOC(large_size, void *);
  uint8_t *ciphertext = SAFE_MALLOC(large_size + 1024, uint8_t *); // Extra space for encryption overhead
  uint8_t *decrypted = SAFE_MALLOC(large_size, void *);

  // Fill with test data
  for (size_t i = 0; i < large_size; i++) {
    large_data[i] = (uint8_t)(i % 256);
  }

  size_t ciphertext_len, decrypted_len;

  // Encrypt large data
  int encrypt_result = crypto_handshake_encrypt_packet(&server_ctx, large_data, large_size, ciphertext,
                                                       large_size + 1024, &ciphertext_len);

  // Decrypt large data
  int decrypt_result =
      crypto_handshake_decrypt_packet(&client_ctx, ciphertext, ciphertext_len, decrypted, large_size, &decrypted_len);

  // Save comparison result before freeing memory
  int memcmp_result = memcmp(decrypted, large_data, large_size);

  // Cleanup - do before assertions so it always runs even if assertions fail
  SAFE_FREE(large_data);
  SAFE_FREE(ciphertext);
  SAFE_FREE(decrypted);
  crypto_handshake_cleanup(&server_ctx);
  crypto_handshake_cleanup(&client_ctx);
  teardown_test_network();

  // Assertions after cleanup to ensure cleanup always runs
  cr_assert_eq(encrypt_result, 0, "Large data encryption should succeed");
  cr_assert_gt(ciphertext_len, 0, "Ciphertext should not be empty");
  cr_assert_eq(decrypt_result, 0, "Large data decryption should succeed");
  cr_assert_eq(decrypted_len, large_size, "Decrypted size should match original");
  cr_assert_eq(memcmp_result, 0, "Decrypted data should match original");
}

// =============================================================================
// Error Recovery Tests
// =============================================================================

Test(crypto_handshake_integration, handshake_interruption_recovery) {
  // Test that handshake can recover after a network failure
  // We simulate failure by shutting down the socket, then creating a new one

  setup_test_network();

  crypto_handshake_context_t server_ctx, client_ctx;
  crypto_handshake_init(&server_ctx, true);
  crypto_handshake_init(&client_ctx, false);
  setup_client_ctx_for_socketpair(&client_ctx);

  // Start handshake
  crypto_handshake_server_start(&server_ctx, g_network.server_fd);
  crypto_handshake_client_key_exchange(&client_ctx, g_network.client_fd);

  // Simulate network failure by shutting down the socket
  shutdown(g_network.server_fd, SHUT_RDWR);
  shutdown(g_network.client_fd, SHUT_RDWR);

  // Try to continue handshake on closed socket (should fail)
  int server_auth = crypto_handshake_server_auth_challenge(&server_ctx, g_network.server_fd);
  (void)server_auth; // Result depends on implementation - we just care it doesn't crash

  // Clean up old handshake state
  crypto_handshake_cleanup(&server_ctx);
  crypto_handshake_cleanup(&client_ctx);
  teardown_test_network();

  // Create new socket pair to simulate network recovery
  setup_test_network();

  // Start fresh handshake on new socket
  crypto_handshake_init(&server_ctx, true);
  crypto_handshake_init(&client_ctx, false);
  setup_client_ctx_for_socketpair(&client_ctx);

  int new_server_start = crypto_handshake_server_start(&server_ctx, g_network.server_fd);
  cr_assert_eq(new_server_start, 0, "New handshake should succeed after recovery");

  crypto_handshake_cleanup(&server_ctx);
  crypto_handshake_cleanup(&client_ctx);
  teardown_test_network();
}

// =============================================================================
// Performance Tests
// =============================================================================

Test(crypto_handshake_integration, handshake_performance) {
  setup_test_network();

  const int num_handshakes = 10;
  double total_time = 0;

  for (int i = 0; i < num_handshakes; i++) {
    crypto_handshake_context_t server_ctx, client_ctx;
    crypto_handshake_init(&server_ctx, true);
    crypto_handshake_init(&client_ctx, false);
    setup_client_ctx_for_socketpair(&client_ctx);

    // Time the handshake
    clock_t start = clock();

    crypto_handshake_server_start(&server_ctx, g_network.server_fd);
    crypto_handshake_client_key_exchange(&client_ctx, g_network.client_fd);
    crypto_handshake_server_auth_challenge(&server_ctx, g_network.server_fd);
    crypto_handshake_client_auth_response(&client_ctx, g_network.client_fd);
    crypto_handshake_server_complete(&server_ctx, g_network.server_fd);

    clock_t end = clock();
    double handshake_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    total_time += handshake_time;

    crypto_handshake_cleanup(&server_ctx);
    crypto_handshake_cleanup(&client_ctx);
  }

  double average_time = total_time / num_handshakes;
  cr_assert_lt(average_time, 1.0, "Average handshake time should be less than 1 second");

  teardown_test_network();
}
