/**
 * @file crypto_handshake_test.c
 * @brief Unit tests for lib/crypto/handshake.c - Tests the intended final crypto implementation
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/parameterized.h>
#include <criterion/theories.h>
#include <string.h>

#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/crypto/handshake/common.h>
#include <ascii-chat/crypto/handshake/client.h>
#include <ascii-chat/crypto/handshake/server.h>
#include <ascii-chat/crypto/keys.h>

// Use verbose logging with debug level enabled and stdout/stderr not disabled
TestSuite(crypto_handshake);

// =============================================================================
// Handshake Initialization Tests
// =============================================================================

Test(crypto_handshake, init_server) {
  crypto_handshake_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  asciichat_error_t result = crypto_handshake_init("test_server", &ctx, true); // true = server
  cr_assert_eq(result, ASCIICHAT_OK, "Server handshake init should succeed");
  cr_assert(ctx.is_server, "Context should be marked as server");
  cr_assert_eq(ctx.state, CRYPTO_HANDSHAKE_INIT, "Initial state should be INIT");

  crypto_handshake_destroy(&ctx);
}

Test(crypto_handshake, init_client) {
  crypto_handshake_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  asciichat_error_t result = crypto_handshake_init("test_client", &ctx, false); // false = client
  cr_assert_eq(result, ASCIICHAT_OK, "Client handshake init should succeed");
  cr_assert_not(ctx.is_server, "Context should be marked as client");
  cr_assert_eq(ctx.state, CRYPTO_HANDSHAKE_INIT, "Initial state should be INIT");

  crypto_handshake_destroy(&ctx);
}

Test(crypto_handshake, init_null_context) {
  asciichat_error_t result = crypto_handshake_init("test", NULL, true);
  cr_assert_neq(result, ASCIICHAT_OK, "NULL context should fail");
  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should return ERROR_INVALID_PARAM");
}

Test(crypto_handshake, init_null_name) {
  crypto_handshake_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  asciichat_error_t result = crypto_handshake_init(NULL, &ctx, true);
  cr_assert_neq(result, ASCIICHAT_OK, "NULL name should fail");
  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should return ERROR_INVALID_PARAM");
}

Test(crypto_handshake, cleanup_null_context) {
  // Should not crash
  crypto_handshake_destroy(NULL);
}

// =============================================================================
// State Machine Tests
// =============================================================================

Test(crypto_handshake, multiple_handshakes) {
  // Test multiple handshake contexts
  crypto_handshake_context_t ctx1, ctx2, ctx3;

  crypto_handshake_init("test1", &ctx1, true);
  crypto_handshake_init("test2", &ctx2, false);
  crypto_handshake_init("test3", &ctx3, true);

  // All should be independent
  cr_assert_eq(ctx1.state, CRYPTO_HANDSHAKE_INIT, "Context 1 should be in INIT");
  cr_assert_eq(ctx2.state, CRYPTO_HANDSHAKE_INIT, "Context 2 should be in INIT");
  cr_assert_eq(ctx3.state, CRYPTO_HANDSHAKE_INIT, "Context 3 should be in INIT");
  cr_assert(ctx1.is_server, "Context 1 should be server");
  cr_assert_not(ctx2.is_server, "Context 2 should be client");
  cr_assert(ctx3.is_server, "Context 3 should be server");

  crypto_handshake_destroy(&ctx1);
  crypto_handshake_destroy(&ctx2);
  crypto_handshake_destroy(&ctx3);
}

Test(crypto_handshake, handshake_cleanup_multiple_times) {
  crypto_handshake_context_t ctx;
  crypto_handshake_init("test", &ctx, true);

  // Cleanup multiple times should not crash
  crypto_handshake_destroy(&ctx);
  crypto_handshake_destroy(&ctx);
  crypto_handshake_destroy(&ctx);
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
  crypto_handshake_init("test", &ctx, true);

  // Manually set state (for testing purposes)
  ctx.state = state;

  // Test that the state is preserved
  cr_assert_eq(ctx.state, state, "Handshake state should be preserved");

  crypto_handshake_destroy(&ctx);
}

// =============================================================================
// Password Authentication Tests
// =============================================================================

Test(crypto_handshake, init_with_password) {
  crypto_handshake_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  asciichat_error_t result = crypto_handshake_init_with_password("test_pwd", &ctx, true, "secret123");
  cr_assert_eq(result, ASCIICHAT_OK, "Init with password should succeed");
  cr_assert(ctx.has_password, "Context should have password flag set");
  cr_assert_eq(ctx.state, CRYPTO_HANDSHAKE_INIT, "Initial state should be INIT");

  crypto_handshake_destroy(&ctx);
}

Test(crypto_handshake, init_with_password_null_context) {
  asciichat_error_t result = crypto_handshake_init_with_password("test", NULL, true, "secret");
  cr_assert_neq(result, ASCIICHAT_OK, "NULL context should fail");
  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should return ERROR_INVALID_PARAM");
}

Test(crypto_handshake, init_with_password_null_password) {
  crypto_handshake_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  asciichat_error_t result = crypto_handshake_init_with_password("test", &ctx, true, NULL);
  cr_assert_neq(result, ASCIICHAT_OK, "NULL password should fail");
  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should return ERROR_INVALID_PARAM");
}

Test(crypto_handshake, init_with_empty_password) {
  crypto_handshake_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  asciichat_error_t result = crypto_handshake_init_with_password("test", &ctx, true, "");
  cr_assert_neq(result, ASCIICHAT_OK, "Empty password should fail");
}
