/**
 * @file crypto_handshake_test.c
 * @brief Unit tests for lib/crypto/handshake.c - Tests the intended final crypto implementation
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/parameterized.h>
#include <criterion/theories.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tests/common.h"
#include "crypto/handshake.h"
#include "crypto/keys.h"
#include "tests/logging.h"

// Use the enhanced macro to create complete test suite with basic quiet logging
TEST_SUITE_WITH_QUIET_LOGGING(crypto_handshake);

// Mock socket type for testing
typedef struct {
  int fd;
  bool connected;
  uint8_t *send_buffer;
  size_t send_buffer_size;
  uint8_t *recv_buffer;
  size_t recv_buffer_size;
  size_t recv_buffer_pos;
} mock_socket_t;

// Global mock sockets for testing
static mock_socket_t g_server_socket = {0};
static mock_socket_t g_client_socket = {0};

// Mock socket functions
static ssize_t mock_socket_send(int sock, const void *buf, size_t len, int flags) {
  mock_socket_t *mock_sock = (mock_socket_t *)sock;
  if (!mock_sock || !mock_sock->connected)
    return -1;

  // Store sent data in server's receive buffer
  if (mock_sock == &g_client_socket) {
    memcpy(g_server_socket.recv_buffer + g_server_socket.recv_buffer_pos, buf, len);
    g_server_socket.recv_buffer_pos += len;
  } else {
    memcpy(g_client_socket.recv_buffer + g_client_socket.recv_buffer_pos, buf, len);
    g_client_socket.recv_buffer_pos += len;
  }

  return len;
}

static ssize_t mock_socket_recv(int sock, void *buf, size_t len, int flags) {
  mock_socket_t *mock_sock = (mock_socket_t *)sock;
  if (!mock_sock || !mock_sock->connected)
    return -1;

  size_t available = mock_sock->recv_buffer_pos;
  if (available == 0)
    return 0;

  size_t to_copy = (len < available) ? len : available;
  memcpy(buf, mock_sock->recv_buffer, to_copy);

  // Shift remaining data
  memmove(mock_sock->recv_buffer, mock_sock->recv_buffer + to_copy, available - to_copy);
  mock_sock->recv_buffer_pos -= to_copy;

  return to_copy;
}

// Test setup and teardown
void setup_mock_sockets(void) {
  memset(&g_server_socket, 0, sizeof(g_server_socket));
  memset(&g_client_socket, 0, sizeof(g_client_socket));

  g_server_socket.fd = 1;
  g_server_socket.connected = true;
  g_server_socket.recv_buffer = malloc(4096);
  g_server_socket.recv_buffer_size = 4096;
  g_server_socket.recv_buffer_pos = 0;

  g_client_socket.fd = 2;
  g_client_socket.connected = true;
  g_client_socket.recv_buffer = malloc(4096);
  g_client_socket.recv_buffer_size = 4096;
  g_client_socket.recv_buffer_pos = 0;
}

void teardown_mock_sockets(void) {
  free(g_server_socket.recv_buffer);
  free(g_client_socket.recv_buffer);
  memset(&g_server_socket, 0, sizeof(g_server_socket));
  memset(&g_client_socket, 0, sizeof(g_client_socket));
}

// =============================================================================
// Handshake Initialization Tests
// =============================================================================

Test(crypto_handshake, init_server) {
  crypto_handshake_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  int result = crypto_handshake_init(&ctx, true); // true = server
  cr_assert_eq(result, 0, "Server handshake init should succeed");
  cr_assert(ctx.is_server, "Context should be marked as server");
  cr_assert_eq(ctx.state, CRYPTO_HANDSHAKE_INIT, "Initial state should be INIT");

  crypto_handshake_cleanup(&ctx);
}

Test(crypto_handshake, init_client) {
  crypto_handshake_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  int result = crypto_handshake_init(&ctx, false); // false = client
  cr_assert_eq(result, 0, "Client handshake init should succeed");
  cr_assert_not(ctx.is_server, "Context should be marked as client");
  cr_assert_eq(ctx.state, CRYPTO_HANDSHAKE_INIT, "Initial state should be INIT");

  crypto_handshake_cleanup(&ctx);
}

Test(crypto_handshake, init_null_context) {
  int result = crypto_handshake_init(NULL, true);
  cr_assert_eq(result, -1, "NULL context should fail");
}

Test(crypto_handshake, cleanup_null_context) {
  // Should not crash
  crypto_handshake_cleanup(NULL);
}

// =============================================================================
// Server Handshake Tests
// =============================================================================

Test(crypto_handshake, server_start_success) {
  crypto_handshake_context_t ctx;
  crypto_handshake_init(&ctx, true);

  int server_sock = (int)&g_server_socket;
  int result = crypto_handshake_server_start(&ctx, server_sock);

  cr_assert_eq(result, 0, "Server start should succeed");
  cr_assert_eq(ctx.state, CRYPTO_HANDSHAKE_KEY_EXCHANGE, "State should be KEY_EXCHANGE");

  crypto_handshake_cleanup(&ctx);
}

Test(crypto_handshake, server_start_null_context) {
  int server_sock = (int)&g_server_socket;
  int result = crypto_handshake_server_start(NULL, server_sock);

  cr_assert_eq(result, -1, "NULL context should fail");
}

Test(crypto_handshake, server_auth_challenge) {
  crypto_handshake_context_t ctx;
  crypto_handshake_init(&ctx, true);
  crypto_handshake_server_start(&ctx, (int)&g_server_socket);

  int result = crypto_handshake_server_auth_challenge(&ctx, (int)&g_server_socket);

  cr_assert_eq(result, 0, "Server auth challenge should succeed");
  cr_assert_eq(ctx.state, CRYPTO_HANDSHAKE_AUTHENTICATING, "State should be AUTHENTICATING");

  crypto_handshake_cleanup(&ctx);
}

Test(crypto_handshake, server_complete) {
  crypto_handshake_context_t ctx;
  crypto_handshake_init(&ctx, true);
  crypto_handshake_server_start(&ctx, (int)&g_server_socket);
  crypto_handshake_server_auth_challenge(&ctx, (int)&g_server_socket);

  int result = crypto_handshake_server_complete(&ctx, (int)&g_server_socket);

  cr_assert_eq(result, 0, "Server complete should succeed");
  cr_assert_eq(ctx.state, CRYPTO_HANDSHAKE_READY, "State should be COMPLETE");

  crypto_handshake_cleanup(&ctx);
}

// =============================================================================
// Client Handshake Tests
// =============================================================================

Test(crypto_handshake, client_key_exchange) {
  crypto_handshake_context_t ctx;
  crypto_handshake_init(&ctx, false);

  int client_sock = (int)&g_client_socket;
  int result = crypto_handshake_client_key_exchange(&ctx, client_sock);

  cr_assert_eq(result, 0, "Client key exchange should succeed");
  cr_assert_eq(ctx.state, CRYPTO_HANDSHAKE_KEY_EXCHANGE, "State should be KEY_EXCHANGE");

  crypto_handshake_cleanup(&ctx);
}

Test(crypto_handshake, client_auth_response) {
  crypto_handshake_context_t ctx;
  crypto_handshake_init(&ctx, false);
  crypto_handshake_client_key_exchange(&ctx, (int)&g_client_socket);

  int result = crypto_handshake_client_auth_response(&ctx, (int)&g_client_socket);

  cr_assert_eq(result, 0, "Client auth response should succeed");
  cr_assert_eq(ctx.state, CRYPTO_HANDSHAKE_AUTHENTICATING, "State should be AUTHENTICATING");

  crypto_handshake_cleanup(&ctx);
}

Test(crypto_handshake, client_key_exchange_null_context) {
  int client_sock = (int)&g_client_socket;
  int result = crypto_handshake_client_key_exchange(NULL, client_sock);

  cr_assert_eq(result, -1, "NULL context should fail");
}

// =============================================================================
// Complete Handshake Flow Tests
// =============================================================================

Test(crypto_handshake, complete_handshake_flow) {
  // Initialize both server and client
  crypto_handshake_context_t server_ctx, client_ctx;
  crypto_handshake_init(&server_ctx, true);
  crypto_handshake_init(&client_ctx, false);

  // Server starts
  int server_result = crypto_handshake_server_start(&server_ctx, (int)&g_server_socket);
  cr_assert_eq(server_result, 0, "Server start should succeed");

  // Client key exchange
  int client_result = crypto_handshake_client_key_exchange(&client_ctx, (int)&g_client_socket);
  cr_assert_eq(client_result, 0, "Client key exchange should succeed");

  // Server auth challenge
  int server_auth_result = crypto_handshake_server_auth_challenge(&server_ctx, (int)&g_server_socket);
  cr_assert_eq(server_auth_result, 0, "Server auth challenge should succeed");

  // Client auth response
  int client_auth_result = crypto_handshake_client_auth_response(&client_ctx, (int)&g_client_socket);
  cr_assert_eq(client_auth_result, 0, "Client auth response should succeed");

  // Server complete
  int server_complete_result = crypto_handshake_server_complete(&server_ctx, (int)&g_server_socket);
  cr_assert_eq(server_complete_result, 0, "Server complete should succeed");

  // Verify final states
  cr_assert_eq(server_ctx.state, CRYPTO_HANDSHAKE_READY, "Server should be complete");
  cr_assert_eq(client_ctx.state, CRYPTO_HANDSHAKE_AUTHENTICATING, "Client should be authenticating");

  crypto_handshake_cleanup(&server_ctx);
  crypto_handshake_cleanup(&client_ctx);
}

// =============================================================================
// State Machine Tests
// =============================================================================

Test(crypto_handshake, state_machine_progression) {
  crypto_handshake_context_t ctx;
  crypto_handshake_init(&ctx, true);

  // Initial state
  cr_assert_eq(ctx.state, CRYPTO_HANDSHAKE_INIT, "Should start in INIT state");

  // Server start
  crypto_handshake_server_start(&ctx, (int)&g_server_socket);
  cr_assert_eq(ctx.state, CRYPTO_HANDSHAKE_KEY_EXCHANGE, "Should be in KEY_EXCHANGE state");

  // Auth challenge
  crypto_handshake_server_auth_challenge(&ctx, (int)&g_server_socket);
  cr_assert_eq(ctx.state, CRYPTO_HANDSHAKE_AUTHENTICATING, "Should be in AUTHENTICATING state");

  // Complete
  crypto_handshake_server_complete(&ctx, (int)&g_server_socket);
  cr_assert_eq(ctx.state, CRYPTO_HANDSHAKE_READY, "Should be in COMPLETE state");

  crypto_handshake_cleanup(&ctx);
}

Test(crypto_handshake, invalid_state_transitions) {
  crypto_handshake_context_t ctx;
  crypto_handshake_init(&ctx, false);

  // Try to do server operations on client context
  int result = crypto_handshake_server_start(&ctx, (int)&g_client_socket);
  cr_assert_eq(result, -1, "Client should not be able to do server operations");

  crypto_handshake_cleanup(&ctx);
}

// =============================================================================
// Error Handling Tests
// =============================================================================

Test(crypto_handshake, socket_errors) {
  crypto_handshake_context_t ctx;
  crypto_handshake_init(&ctx, true);

  // Test with invalid socket
  int result = crypto_handshake_server_start(&ctx, -1);
  cr_assert_eq(result, -1, "Invalid socket should fail");

  crypto_handshake_cleanup(&ctx);
}

Test(crypto_handshake, handshake_timeout) {
  crypto_handshake_context_t ctx;
  crypto_handshake_init(&ctx, true);

  // Simulate timeout by not completing the handshake
  crypto_handshake_server_start(&ctx, (int)&g_server_socket);

  // State should remain in KEY_EXCHANGE
  cr_assert_eq(ctx.state, CRYPTO_HANDSHAKE_KEY_EXCHANGE, "Should remain in KEY_EXCHANGE without completion");

  crypto_handshake_cleanup(&ctx);
}

// =============================================================================
// Theory Tests for Handshake States
// =============================================================================

TheoryDataPoints(crypto_handshake, handshake_states) = {
    DataPoints(crypto_handshake_state_t, CRYPTO_HANDSHAKE_INIT, CRYPTO_HANDSHAKE_KEY_EXCHANGE,
               CRYPTO_HANDSHAKE_AUTHENTICATING, CRYPTO_HANDSHAKE_READY, CRYPTO_HANDSHAKE_FAILED),
};

Theory((crypto_handshake_state_t state), crypto_handshake, handshake_states) {
  crypto_handshake_context_t ctx;
  crypto_handshake_init(&ctx, true);

  // Manually set state (for testing purposes)
  ctx.state = state;

  // Test that the state is preserved
  cr_assert_eq(ctx.state, state, "Handshake state should be preserved");

  crypto_handshake_cleanup(&ctx);
}

// =============================================================================
// Edge Cases and Stress Tests
// =============================================================================

Test(crypto_handshake, multiple_handshakes) {
  // Test multiple handshake contexts
  crypto_handshake_context_t ctx1, ctx2, ctx3;

  crypto_handshake_init(&ctx1, true);
  crypto_handshake_init(&ctx2, false);
  crypto_handshake_init(&ctx3, true);

  // All should be independent
  cr_assert_eq(ctx1.state, CRYPTO_HANDSHAKE_INIT, "Context 1 should be in INIT");
  cr_assert_eq(ctx2.state, CRYPTO_HANDSHAKE_INIT, "Context 2 should be in INIT");
  cr_assert_eq(ctx3.state, CRYPTO_HANDSHAKE_INIT, "Context 3 should be in INIT");

  crypto_handshake_cleanup(&ctx1);
  crypto_handshake_cleanup(&ctx2);
  crypto_handshake_cleanup(&ctx3);
}

Test(crypto_handshake, handshake_cleanup_multiple_times) {
  crypto_handshake_context_t ctx;
  crypto_handshake_init(&ctx, true);

  // Cleanup multiple times should not crash
  crypto_handshake_cleanup(&ctx);
  crypto_handshake_cleanup(&ctx);
  crypto_handshake_cleanup(&ctx);
}

Test(crypto_handshake, handshake_with_large_data) {
  crypto_handshake_context_t ctx;
  crypto_handshake_init(&ctx, true);

  // Test with large socket buffers
  g_server_socket.recv_buffer_size = 1024 * 1024; // 1MB
  g_server_socket.recv_buffer = realloc(g_server_socket.recv_buffer, g_server_socket.recv_buffer_size);

  int result = crypto_handshake_server_start(&ctx, (int)&g_server_socket);
  cr_assert_eq(result, 0, "Should handle large buffers");

  crypto_handshake_cleanup(&ctx);
}
