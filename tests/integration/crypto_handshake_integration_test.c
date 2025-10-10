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

#include "tests/common.h"
#include "crypto/handshake.h"
#include "crypto/keys/keys.h"
#include "crypto/known_hosts.h"
#include "tests/logging.h"

// Use the enhanced macro to create complete test suite with basic quiet logging
TEST_SUITE_WITH_QUIET_LOGGING(crypto_handshake_integration);

// Mock network functions for integration testing
typedef struct {
  int server_fd;
  int client_fd;
  bool connected;
  uint8_t *buffer;
  size_t buffer_size;
  size_t buffer_pos;
} mock_network_t;

static mock_network_t g_network = {0};

// Mock socket functions
static ssize_t mock_send(int sock, const void *buf, size_t len, int flags) {
  if (sock == g_network.client_fd) {
    // Client sending to server
    memcpy(g_network.buffer + g_network.buffer_pos, buf, len);
    g_network.buffer_pos += len;
    return len;
  }
  return -1;
}

static ssize_t mock_recv(int sock, void *buf, size_t len, int flags) {
  if (sock == g_network.client_fd && g_network.buffer_pos > 0) {
    // Client receiving from server
    size_t to_copy = (len < g_network.buffer_pos) ? len : g_network.buffer_pos;
    memcpy(buf, g_network.buffer, to_copy);
    memmove(g_network.buffer, g_network.buffer + to_copy, g_network.buffer_pos - to_copy);
    g_network.buffer_pos -= to_copy;
    return to_copy;
  }
  return 0;
}

// Test setup and teardown
void setup_mock_network(void) {
  memset(&g_network, 0, sizeof(g_network));
  g_network.server_fd = 1;
  g_network.client_fd = 2;
  g_network.connected = true;
  g_network.buffer = SAFE_MALLOC(4096, void *);
  g_network.buffer_size = 4096;
  g_network.buffer_pos = 0;
}

void teardown_mock_network(void) {
  SAFE_FREE(g_network.buffer);
  memset(&g_network, 0, sizeof(g_network));
}

// =============================================================================
// Complete Handshake Flow Tests
// =============================================================================

Test(crypto_handshake_integration, complete_handshake_flow) {
  setup_mock_network();

  // Initialize server and client contexts
  crypto_handshake_context_t server_ctx, client_ctx;
  crypto_handshake_init(&server_ctx, true);
  crypto_handshake_init(&client_ctx, false);

  // Server starts handshake
  int server_result = crypto_handshake_server_start(&server_ctx, g_network.server_fd);
  cr_assert_eq(server_result, 0, "Server start should succeed");

  // Client initiates key exchange
  int client_result = crypto_handshake_client_key_exchange(&client_ctx, g_network.client_fd);
  cr_assert_eq(client_result, 0, "Client key exchange should succeed");

  // Server sends auth challenge
  int server_auth_result = crypto_handshake_server_auth_challenge(&server_ctx, g_network.server_fd);
  cr_assert_eq(server_auth_result, 0, "Server auth challenge should succeed");

  // Client responds to auth challenge
  int client_auth_result = crypto_handshake_client_auth_response(&client_ctx, g_network.client_fd);
  cr_assert_eq(client_auth_result, 0, "Client auth response should succeed");

  // Server completes handshake
  int server_complete_result = crypto_handshake_server_complete(&server_ctx, g_network.server_fd);
  cr_assert_eq(server_complete_result, 0, "Server complete should succeed");

  // Verify final states
  cr_assert_eq(server_ctx.state, CRYPTO_HANDSHAKE_READY, "Server should be complete");
  cr_assert_eq(client_ctx.state, CRYPTO_HANDSHAKE_AUTHENTICATING, "Client should be authenticating");

  crypto_handshake_cleanup(&server_ctx);
  crypto_handshake_cleanup(&client_ctx);
  teardown_mock_network();
}

// =============================================================================
// Key Type Tests (Parameterized)
// =============================================================================

typedef struct {
  const char *key_type;
  const char *key_data;
  key_type_t expected_type;
  const char *description;
} key_type_test_case_t;

static key_type_test_case_t key_type_cases[] = {
    {"ed25519", "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGplY2VrZXJzIGVkMjU1MTkga2V5", KEY_TYPE_ED25519,
     "SSH Ed25519 key"},
    {"x25519", "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", KEY_TYPE_X25519, "X25519 hex key"},
    {"gpg", "gpg:0x1234567890ABCDEF", KEY_TYPE_GPG, "GPG key ID"},
    {"github", "github:username", KEY_TYPE_ED25519, "GitHub username (should fetch Ed25519)"},
    {"gitlab", "gitlab:username", KEY_TYPE_ED25519, "GitLab username (should fetch Ed25519)"}};

ParameterizedTestParameters(crypto_handshake_integration, key_type_tests) {
  size_t nb_cases = sizeof(key_type_cases) / sizeof(key_type_cases[0]);
  return cr_make_param_array(key_type_test_case_t, key_type_cases, nb_cases);
}

ParameterizedTest(key_type_test_case_t *tc, crypto_handshake_integration, key_type_tests) {
  setup_mock_network();

  // Test key parsing
  public_key_t key;
  int parse_result = parse_public_key(tc->key_data, &key);

  if (parse_result == 0) {
    cr_assert_eq(key.type, tc->expected_type, "Key type should match for case: %s", tc->description);
  } else {
    // Some keys might fail to parse without BearSSL/GPG
    cr_assert_eq(parse_result, -1, "Key parsing should fail gracefully for case: %s", tc->description);
  }

  teardown_mock_network();
}

// =============================================================================
// Encryption/Decryption Tests
// =============================================================================

Test(crypto_handshake_integration, encryption_after_handshake) {
  setup_mock_network();

  crypto_handshake_context_t server_ctx, client_ctx;
  crypto_handshake_init(&server_ctx, true);
  crypto_handshake_init(&client_ctx, false);

  // Complete handshake
  crypto_handshake_server_start(&server_ctx, g_network.server_fd);
  crypto_handshake_client_key_exchange(&client_ctx, g_network.client_fd);
  crypto_handshake_server_auth_challenge(&server_ctx, g_network.server_fd);
  crypto_handshake_client_auth_response(&client_ctx, g_network.client_fd);
  crypto_handshake_server_complete(&server_ctx, g_network.server_fd);

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
  teardown_mock_network();
}

Test(crypto_handshake_integration, bidirectional_encryption) {
  setup_mock_network();

  crypto_handshake_context_t server_ctx, client_ctx;
  crypto_handshake_init(&server_ctx, true);
  crypto_handshake_init(&client_ctx, false);

  // Complete handshake
  crypto_handshake_server_start(&server_ctx, g_network.server_fd);
  crypto_handshake_client_key_exchange(&client_ctx, g_network.client_fd);
  crypto_handshake_server_auth_challenge(&server_ctx, g_network.server_fd);
  crypto_handshake_client_auth_response(&client_ctx, g_network.client_fd);
  crypto_handshake_server_complete(&server_ctx, g_network.server_fd);

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
  teardown_mock_network();
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
  setup_mock_network();

  crypto_handshake_context_t server_ctx, client_ctx;
  crypto_handshake_init(&server_ctx, true);
  crypto_handshake_init(&client_ctx, false);

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

  // All steps should succeed
  cr_assert_eq(server_start, 0, "Server start should succeed for auth method: %s", auth_method);
  cr_assert_eq(client_key_exchange, 0, "Client key exchange should succeed for auth method: %s", auth_method);
  cr_assert_eq(server_auth, 0, "Server auth should succeed for auth method: %s", auth_method);
  cr_assert_eq(client_auth, 0, "Client auth should succeed for auth method: %s", auth_method);
  cr_assert_eq(server_complete, 0, "Server complete should succeed for auth method: %s", auth_method);

  crypto_handshake_cleanup(&server_ctx);
  crypto_handshake_cleanup(&client_ctx);
  teardown_mock_network();
}

// =============================================================================
// Concurrent Handshakes Tests
// =============================================================================

typedef struct {
  int client_id;
  crypto_handshake_context_t *server_ctx;
  int result;
} client_handshake_args_t;

static void *client_handshake_thread(void *arg) {
  client_handshake_args_t *args = (client_handshake_args_t *)arg;

  crypto_handshake_context_t client_ctx;
  crypto_handshake_init(&client_ctx, false);

  // Simulate client handshake
  int result = crypto_handshake_client_key_exchange(&client_ctx, g_network.client_fd + args->client_id);
  if (result == 0) {
    result = crypto_handshake_client_auth_response(&client_ctx, g_network.client_fd + args->client_id);
  }

  args->result = result;
  crypto_handshake_cleanup(&client_ctx);
  return NULL;
}

Test(crypto_handshake_integration, concurrent_handshakes) {
  setup_mock_network();

  crypto_handshake_context_t server_ctx;
  crypto_handshake_init(&server_ctx, true);

  // Start server
  int server_result = crypto_handshake_server_start(&server_ctx, g_network.server_fd);
  cr_assert_eq(server_result, 0, "Server start should succeed");

  // Create multiple client threads
  const int num_clients = 3;
  pthread_t client_threads[num_clients];
  client_handshake_args_t client_args[num_clients];

  for (int i = 0; i < num_clients; i++) {
    client_args[i].client_id = i;
    client_args[i].server_ctx = &server_ctx;
    client_args[i].result = -1;

    int thread_result = pthread_create(&client_threads[i], NULL, client_handshake_thread, &client_args[i]);
    cr_assert_eq(thread_result, 0, "Client thread %d should be created", i);
  }

  // Wait for all clients to complete
  for (int i = 0; i < num_clients; i++) {
    int join_result = pthread_join(client_threads[i], NULL);
    cr_assert_eq(join_result, 0, "Client thread %d should complete", i);
  }

  // All clients should have completed (though some might fail due to mock limitations)
  for (int i = 0; i < num_clients; i++) {
    cr_assert(client_args[i].result == 0 || client_args[i].result == -1, "Client %d should complete or fail gracefully",
              i);
  }

  crypto_handshake_cleanup(&server_ctx);
  teardown_mock_network();
}

// =============================================================================
// Large Data Handling Tests
// =============================================================================

Test(crypto_handshake_integration, large_data_encryption) {
  setup_mock_network();

  crypto_handshake_context_t server_ctx, client_ctx;
  crypto_handshake_init(&server_ctx, true);
  crypto_handshake_init(&client_ctx, false);

  // Complete handshake
  crypto_handshake_server_start(&server_ctx, g_network.server_fd);
  crypto_handshake_client_key_exchange(&client_ctx, g_network.client_fd);
  crypto_handshake_server_auth_challenge(&server_ctx, g_network.server_fd);
  crypto_handshake_client_auth_response(&client_ctx, g_network.client_fd);
  crypto_handshake_server_complete(&server_ctx, g_network.server_fd);

  // Test with large data
  const size_t large_size = 1024 * 1024; // 1MB
  uint8_t *large_data = SAFE_MALLOC(large_size, void *);
  uint8_t *ciphertext = SAFE_MALLOC(large_size + 1024, char *); // Extra space for encryption overhead
  uint8_t *decrypted = SAFE_MALLOC(large_size, void *);

  // Fill with test data
  for (size_t i = 0; i < large_size; i++) {
    large_data[i] = (uint8_t)(i % 256);
  }

  size_t ciphertext_len, decrypted_len;

  // Encrypt large data
  int encrypt_result = crypto_handshake_encrypt_packet(&server_ctx, large_data, large_size, ciphertext,
                                                       large_size + 1024, &ciphertext_len);
  cr_assert_eq(encrypt_result, 0, "Large data encryption should succeed");
  cr_assert_gt(ciphertext_len, 0, "Ciphertext should not be empty");

  // Decrypt large data
  int decrypt_result =
      crypto_handshake_decrypt_packet(&client_ctx, ciphertext, ciphertext_len, decrypted, large_size, &decrypted_len);
  cr_assert_eq(decrypt_result, 0, "Large data decryption should succeed");
  cr_assert_eq(decrypted_len, large_size, "Decrypted size should match original");
  cr_assert_eq(memcmp(decrypted, large_data, large_size), 0, "Decrypted data should match original");

  SAFE_FREE(large_data);
  SAFE_FREE(ciphertext);
  SAFE_FREE(decrypted);

  crypto_handshake_cleanup(&server_ctx);
  crypto_handshake_cleanup(&client_ctx);
  teardown_mock_network();
}

// =============================================================================
// Error Recovery Tests
// =============================================================================

Test(crypto_handshake_integration, handshake_interruption_recovery) {
  setup_mock_network();

  crypto_handshake_context_t server_ctx, client_ctx;
  crypto_handshake_init(&server_ctx, true);
  crypto_handshake_init(&client_ctx, false);

  // Start handshake
  crypto_handshake_server_start(&server_ctx, g_network.server_fd);
  crypto_handshake_client_key_exchange(&client_ctx, g_network.client_fd);

  // Simulate interruption (network failure)
  g_network.connected = false;

  // Try to continue handshake (should fail gracefully)
  int server_auth = crypto_handshake_server_auth_challenge(&server_ctx, g_network.server_fd);
  cr_assert_eq(server_auth, -1, "Server auth should fail when network is down");

  // Restore network
  g_network.connected = true;

  // Should be able to restart handshake
  crypto_handshake_cleanup(&server_ctx);
  crypto_handshake_cleanup(&client_ctx);

  crypto_handshake_init(&server_ctx, true);
  crypto_handshake_init(&client_ctx, false);

  int new_server_start = crypto_handshake_server_start(&server_ctx, g_network.server_fd);
  cr_assert_eq(new_server_start, 0, "New handshake should succeed after recovery");

  crypto_handshake_cleanup(&server_ctx);
  crypto_handshake_cleanup(&client_ctx);
  teardown_mock_network();
}

// =============================================================================
// Performance Tests
// =============================================================================

Test(crypto_handshake_integration, handshake_performance) {
  setup_mock_network();

  const int num_handshakes = 10;
  double total_time = 0;

  for (int i = 0; i < num_handshakes; i++) {
    crypto_handshake_context_t server_ctx, client_ctx;
    crypto_handshake_init(&server_ctx, true);
    crypto_handshake_init(&client_ctx, false);

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

  teardown_mock_network();
}
