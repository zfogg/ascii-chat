/**
 * @file crypto_handshake_test.c
 * @brief Unit tests for lib/crypto/handshake.c
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>

#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/crypto/handshake/common.h>
#include <ascii-chat/crypto/handshake/client.h>
#include <ascii-chat/crypto/handshake/server.h>
#include <ascii-chat/crypto/keys.h>

TestSuite(crypto_handshake);

// =============================================================================
// Handshake Initialization Tests
// =============================================================================

Test(crypto_handshake, init_server) {
  crypto_handshake_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  asciichat_error_t result = crypto_handshake_init("test_server", &ctx, true);
  cr_assert_eq(result, ASCIICHAT_OK, "Server handshake init should succeed");
  cr_assert(ctx.is_server, "Context should be marked as server");
  cr_assert_eq(ctx.state, CRYPTO_HANDSHAKE_INIT, "Initial state should be INIT");

  crypto_handshake_destroy(&ctx);
}

Test(crypto_handshake, init_client) {
  crypto_handshake_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  asciichat_error_t result = crypto_handshake_init("test_client", &ctx, false);
  cr_assert_eq(result, ASCIICHAT_OK, "Client handshake init should succeed");
  cr_assert_not(ctx.is_server, "Context should be marked as client");
  cr_assert_eq(ctx.state, CRYPTO_HANDSHAKE_INIT, "Initial state should be INIT");

  crypto_handshake_destroy(&ctx);
}

Test(crypto_handshake, init_null_context) {
  asciichat_error_t result = crypto_handshake_init("test", NULL, true);
  cr_assert_neq(result, ASCIICHAT_OK, "Init with NULL context should fail");
}

Test(crypto_handshake, init_null_name) {
  crypto_handshake_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  asciichat_error_t result = crypto_handshake_init(NULL, &ctx, true);
  cr_assert_neq(result, ASCIICHAT_OK, "Init with NULL name should fail");
}

// =============================================================================
// Context State Tests
// =============================================================================

Test(crypto_handshake, context_state_transitions) {
  crypto_handshake_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  asciichat_error_t result = crypto_handshake_init("test", &ctx, true);
  cr_assert_eq(result, ASCIICHAT_OK);

  // Initial state should be INIT
  cr_assert_eq(ctx.state, CRYPTO_HANDSHAKE_INIT, "State should be INIT after init");

  // Test that we can destroy without issues
  crypto_handshake_destroy(&ctx);
}

Test(crypto_handshake, destroy_null_context) {
  // Destroying NULL should be safe
  crypto_handshake_destroy(NULL);
  cr_assert(true, "Destroying NULL context should not crash");
}

Test(crypto_handshake, destroy_twice) {
  crypto_handshake_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  asciichat_error_t result = crypto_handshake_init("test", &ctx, true);
  cr_assert_eq(result, ASCIICHAT_OK);

  // Destroy once
  crypto_handshake_destroy(&ctx);

  // Destroying again should be safe (idempotent)
  crypto_handshake_destroy(&ctx);
  cr_assert(true, "Double destroy should be safe");
}

// =============================================================================
// Client vs Server Tests
// =============================================================================

Test(crypto_handshake, server_vs_client_flags) {
  crypto_handshake_context_t server_ctx;
  crypto_handshake_context_t client_ctx;

  memset(&server_ctx, 0, sizeof(server_ctx));
  memset(&client_ctx, 0, sizeof(client_ctx));

  crypto_handshake_init("server", &server_ctx, true);
  crypto_handshake_init("client", &client_ctx, false);

  cr_assert(server_ctx.is_server, "Server context should have is_server=true");
  cr_assert_not(client_ctx.is_server, "Client context should have is_server=false");

  crypto_handshake_destroy(&server_ctx);
  crypto_handshake_destroy(&client_ctx);
}

// =============================================================================
// Key Generation Tests
// =============================================================================

Test(crypto_handshake, ephemeral_key_generation) {
  crypto_handshake_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  asciichat_error_t result = crypto_handshake_init("test", &ctx, true);
  cr_assert_eq(result, ASCIICHAT_OK);

  // Ephemeral keys should be generated during init
  // (Key material exists in the context, but we can't directly inspect it)
  // Just verify the context is in a valid state
  cr_assert_eq(ctx.state, CRYPTO_HANDSHAKE_INIT);

  crypto_handshake_destroy(&ctx);
}

// =============================================================================
// Name Handling Tests
// =============================================================================

Test(crypto_handshake, various_names) {
  const char *names[] = {
      "simple",
      "with-dashes",
      "with_underscores",
      "123numbers",
      "UPPERCASE",
      "CamelCase",
  };

  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    crypto_handshake_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    asciichat_error_t result = crypto_handshake_init(names[i], &ctx, true);
    cr_assert_eq(result, ASCIICHAT_OK, "Should accept name: %s", names[i]);

    crypto_handshake_destroy(&ctx);
  }
}
