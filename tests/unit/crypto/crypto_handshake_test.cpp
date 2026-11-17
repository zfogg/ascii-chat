#include <catch2/catch_test_macros.hpp>
/**
 * @file crypto_handshake_test.c
 * @brief Unit tests for lib/crypto/handshake.c
 */

#include <string.h>

#include <ascii-chat/tests/logging.h>
#include <ascii-chat/crypto/handshake/common.h>
#include <ascii-chat/crypto/handshake/client.h>
#include <ascii-chat/crypto/handshake/server.h>
#include <ascii-chat/crypto/keys.h>
// =============================================================================
// Handshake Initialization Tests
// =============================================================================

TEST_CASE("crypto_handshake/init_server", "[crypto_handshake] [unit]") {
  crypto_handshake_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  asciichat_error_t result = crypto_handshake_init("test_server", &ctx, true);
  REQUIRE((result) == (ASCIICHAT_OK));
  REQUIRE(ctx.is_server);
  REQUIRE((ctx.state) == (CRYPTO_HANDSHAKE_INIT));

  crypto_handshake_destroy(&ctx);
}

TEST_CASE("crypto_handshake/init_client", "[crypto_handshake] [unit]") {
  crypto_handshake_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  asciichat_error_t result = crypto_handshake_init("test_client", &ctx, false);
  REQUIRE((result) == (ASCIICHAT_OK));
  REQUIRE(!(ctx.is_server));
  REQUIRE((ctx.state) == (CRYPTO_HANDSHAKE_INIT));

  crypto_handshake_destroy(&ctx);
}

TEST_CASE("crypto_handshake/init_null_context", "[crypto_handshake] [unit]") {
  asciichat_error_t result = crypto_handshake_init("test", NULL, true);
  REQUIRE((result) != (ASCIICHAT_OK));
}

TEST_CASE("crypto_handshake/init_null_name", "[crypto_handshake] [unit]") {
  crypto_handshake_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  asciichat_error_t result = crypto_handshake_init(NULL, &ctx, true);
  REQUIRE((result) != (ASCIICHAT_OK));
}

// =============================================================================
// Context State Tests
// =============================================================================

TEST_CASE("crypto_handshake/context_state_transitions", "[crypto_handshake] [unit]") {
  crypto_handshake_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  asciichat_error_t result = crypto_handshake_init("test", &ctx, true);
  REQUIRE((result) == (ASCIICHAT_OK));

  // Initial state should be INIT
  REQUIRE((ctx.state) == (CRYPTO_HANDSHAKE_INIT));

  // Test that we can destroy without issues
  crypto_handshake_destroy(&ctx);
}

TEST_CASE("crypto_handshake/destroy_null_context", "[crypto_handshake] [unit]") {
  // Destroying NULL should be safe
  crypto_handshake_destroy(NULL);
  REQUIRE(true);
}

TEST_CASE("crypto_handshake/destroy_twice", "[crypto_handshake] [unit]") {
  crypto_handshake_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  asciichat_error_t result = crypto_handshake_init("test", &ctx, true);
  REQUIRE((result) == (ASCIICHAT_OK));

  // Destroy once
  crypto_handshake_destroy(&ctx);

  // Destroying again should be safe (idempotent)
  crypto_handshake_destroy(&ctx);
  REQUIRE(true);
}

// =============================================================================
// Client vs Server Tests
// =============================================================================

TEST_CASE("crypto_handshake/server_vs_client_flags", "[crypto_handshake] [unit]") {
  crypto_handshake_context_t server_ctx;
  crypto_handshake_context_t client_ctx;

  memset(&server_ctx, 0, sizeof(server_ctx));
  memset(&client_ctx, 0, sizeof(client_ctx));

  crypto_handshake_init("server", &server_ctx, true);
  crypto_handshake_init("client", &client_ctx, false);

  REQUIRE(server_ctx.is_server);
  REQUIRE(!(client_ctx.is_server));

  crypto_handshake_destroy(&server_ctx);
  crypto_handshake_destroy(&client_ctx);
}

// =============================================================================
// Key Generation Tests
// =============================================================================

TEST_CASE("crypto_handshake/ephemeral_key_generation", "[crypto_handshake] [unit]") {
  crypto_handshake_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  asciichat_error_t result = crypto_handshake_init("test", &ctx, true);
  REQUIRE((result) == (ASCIICHAT_OK));

  // Ephemeral keys should be generated during init
  // (Key material exists in the context, but we can't directly inspect it)
  // Just verify the context is in a valid state
  REQUIRE((ctx.state) == (CRYPTO_HANDSHAKE_INIT));

  crypto_handshake_destroy(&ctx);
}

// =============================================================================
// Name Handling Tests
// =============================================================================

TEST_CASE("crypto_handshake/various_names", "[crypto_handshake] [unit]") {
  const char *names[] = {
      "simple", "with-dashes", "with_underscores", "123numbers", "UPPERCASE", "CamelCase",
  };

  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    crypto_handshake_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    asciichat_error_t result = crypto_handshake_init(names[i], &ctx, true);
    REQUIRE((result) == (ASCIICHAT_OK));

    crypto_handshake_destroy(&ctx);
  }
}
