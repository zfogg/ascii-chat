/**
 * @file crypto_handshake_test.c
 * @brief Unit tests for lib/crypto/handshake.c
 */

#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <string.h>

#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/crypto/handshake/common.h>
#include <ascii-chat/crypto/handshake/client.h>
#include <ascii-chat/crypto/handshake/server.h>
#include <ascii-chat/crypto/keys.h>
// =============================================================================
// Handshake Initialization Tests
// =============================================================================

TEST_CASE("crypto_handshake/init_server", "[crypto_handshake]") {
  crypto_handshake_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  asciichat_error_t result = crypto_handshake_init("test_server", &ctx, true);
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Server handshake init should succeed");
  ASCIICHAT_REQUIRE(ctx.is_server, "Context should be marked as server");
  ASCIICHAT_REQUIRE_EQ(ctx.state, CRYPTO_HANDSHAKE_INIT, "Initial state should be INIT");

  crypto_handshake_destroy(&ctx);
}

TEST_CASE("crypto_handshake/init_client", "[crypto_handshake]") {
  crypto_handshake_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  asciichat_error_t result = crypto_handshake_init("test_client", &ctx, false);
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Client handshake init should succeed");
  ASCIICHAT_REQUIRE_NOT(ctx.is_server, "Context should be marked as client");
  ASCIICHAT_REQUIRE_EQ(ctx.state, CRYPTO_HANDSHAKE_INIT, "Initial state should be INIT");

  crypto_handshake_destroy(&ctx);
}

TEST_CASE("crypto_handshake/init_null_context", "[crypto_handshake]") {
  asciichat_error_t result = crypto_handshake_init("test", NULL, true);
  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK, "Init with NULL context should fail");
}

TEST_CASE("crypto_handshake/init_null_name", "[crypto_handshake]") {
  crypto_handshake_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  asciichat_error_t result = crypto_handshake_init(NULL, &ctx, true);
  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK, "Init with NULL name should fail");
}

// =============================================================================
// Context State Tests
// =============================================================================

TEST_CASE("crypto_handshake/context_state_transitions", "[crypto_handshake]") {
  crypto_handshake_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  asciichat_error_t result = crypto_handshake_init("test", &ctx, true);
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK);

  // Initial state should be INIT
  ASCIICHAT_REQUIRE_EQ(ctx.state, CRYPTO_HANDSHAKE_INIT, "State should be INIT after init");

  // Test that we can destroy without issues
  crypto_handshake_destroy(&ctx);
}

TEST_CASE("crypto_handshake/destroy_null_context", "[crypto_handshake]") {
  // Destroying NULL should be safe
  crypto_handshake_destroy(NULL);
  ASCIICHAT_REQUIRE(true, "Destroying NULL context should not crash");
}

TEST_CASE("crypto_handshake/destroy_twice", "[crypto_handshake]") {
  crypto_handshake_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  asciichat_error_t result = crypto_handshake_init("test", &ctx, true);
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK);

  // Destroy once
  crypto_handshake_destroy(&ctx);

  // Destroying again should be safe (idempotent)
  crypto_handshake_destroy(&ctx);
  ASCIICHAT_REQUIRE(true, "Double destroy should be safe");
}

// =============================================================================
// Client vs Server Tests
// =============================================================================

TEST_CASE("crypto_handshake/server_vs_client_flags", "[crypto_handshake]") {
  crypto_handshake_context_t server_ctx;
  crypto_handshake_context_t client_ctx;

  memset(&server_ctx, 0, sizeof(server_ctx));
  memset(&client_ctx, 0, sizeof(client_ctx));

  crypto_handshake_init("server", &server_ctx, true);
  crypto_handshake_init("client", &client_ctx, false);

  ASCIICHAT_REQUIRE(server_ctx.is_server, "Server context should have is_server=true");
  ASCIICHAT_REQUIRE_NOT(client_ctx.is_server, "Client context should have is_server=false");

  crypto_handshake_destroy(&server_ctx);
  crypto_handshake_destroy(&client_ctx);
}

// =============================================================================
// Key Generation Tests
// =============================================================================

TEST_CASE("crypto_handshake/ephemeral_key_generation", "[crypto_handshake]") {
  crypto_handshake_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  asciichat_error_t result = crypto_handshake_init("test", &ctx, true);
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK);

  // Ephemeral keys should be generated during init
  // (Key material exists in the context, but we can't directly inspect it)
  // Just verify the context is in a valid state
  ASCIICHAT_REQUIRE_EQ(ctx.state, CRYPTO_HANDSHAKE_INIT);

  crypto_handshake_destroy(&ctx);
}

// =============================================================================
// Name Handling Tests
// =============================================================================

TEST_CASE("crypto_handshake/various_names", "[crypto_handshake]") {
  const char *names[] = {
      "simple", "with-dashes", "with_underscores", "123numbers", "UPPERCASE", "CamelCase",
  };

  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    crypto_handshake_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    asciichat_error_t result = crypto_handshake_init(names[i], &ctx, true);
    ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Should accept name: %s", names[i]);

    crypto_handshake_destroy(&ctx);
  }
}
