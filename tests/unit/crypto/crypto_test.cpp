#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <string.h>

#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/crypto/crypto.h>

// Test fixture setup and teardown
static crypto_context_t ctx1, ctx2;

void setup_crypto_context(void) {
  memset(&ctx1, 0, sizeof(ctx1));
  memset(&ctx2, 0, sizeof(ctx2));
  // Set log level to reduce noise during tests
  log_set_level(LOG_FATAL);
}

void teardown_crypto_context(void) {
  crypto_destroy(&ctx1);
  crypto_destroy(&ctx2);
  // Restore normal log level
  log_set_level(LOG_DEBUG);
}
// =============================================================================
// Basic Initialization Tests
// =============================================================================
// Use verbose logging with debug level enabled and stdout/stderr not disabled

TEST_CASE("crypto/init_basic", "[crypto]") {
  crypto_result_t result = crypto_init(&ctx1);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Basic crypto initialization should succeed");
  ASCIICHAT_REQUIRE(ctx1.initialized, "Context should be marked as initialized");
  ASCIICHAT_REQUIRE(ctx1.has_password == false, "Should not have password initially");
  ASCIICHAT_REQUIRE(ctx1.key_exchange_complete == false, "Key exchange should not be complete initially");
  ASCIICHAT_REQUIRE(crypto_is_ready(&ctx1) == false, "Should not be ready without key exchange or password");
}

TEST_CASE("crypto/init_with_password", "[crypto]") {
  const char *password = "test-password-123";
  crypto_result_t result = crypto_init_with_password(&ctx1, password);

  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Password-based initialization should succeed");
  ASCIICHAT_REQUIRE(ctx1.initialized, "Context should be initialized");
  ASCIICHAT_REQUIRE(ctx1.has_password, "Should have password set");
  ASCIICHAT_REQUIRE(crypto_is_ready(&ctx1), "Should be ready with password");
}

TEST_CASE("crypto/init_invalid_params", "[crypto]") {
  crypto_result_t result = crypto_init(NULL);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_ERROR_INVALID_PARAMS, "NULL context should fail");

  result = crypto_init_with_password(NULL, "password");
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_ERROR_INVALID_PARAMS, "NULL context with password should fail");

  result = crypto_init_with_password(&ctx1, NULL);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_ERROR_INVALID_PARAMS, "NULL password should fail");

  result = crypto_init_with_password(&ctx1, "");
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_ERROR_INVALID_PARAMS, "Empty password should fail");
}

// =============================================================================
// Key Exchange Tests
// =============================================================================

TEST_CASE("crypto/key_exchange_flow", "[crypto]") {
  // Initialize both contexts
  crypto_result_t result = crypto_init(&ctx1);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "First context init should succeed");

  result = crypto_init(&ctx2);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Second context init should succeed");

  // Get public keys
  uint8_t pub_key1[CRYPTO_PUBLIC_KEY_SIZE];
  uint8_t pub_key2[CRYPTO_PUBLIC_KEY_SIZE];

  result = crypto_get_public_key(&ctx1, pub_key1);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Getting public key 1 should succeed");

  result = crypto_get_public_key(&ctx2, pub_key2);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Getting public key 2 should succeed");

  // Keys should be different
  ASCIICHAT_REQUIRE_NE(memcmp(pub_key1, pub_key2, CRYPTO_PUBLIC_KEY_SIZE), 0, "Public keys should be different");

  // Exchange keys
  result = crypto_set_peer_public_key(&ctx1, pub_key2);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Setting peer key 1 should succeed");
  ASCIICHAT_REQUIRE(ctx1.key_exchange_complete, "Key exchange should be complete for ctx1");
  ASCIICHAT_REQUIRE(crypto_is_ready(&ctx1), "ctx1 should be ready after key exchange");

  result = crypto_set_peer_public_key(&ctx2, pub_key1);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Setting peer key 2 should succeed");
  ASCIICHAT_REQUIRE(ctx2.key_exchange_complete, "Key exchange should be complete for ctx2");
  ASCIICHAT_REQUIRE(crypto_is_ready(&ctx2), "ctx2 should be ready after key exchange");
}

TEST_CASE("crypto/public_key_invalid_params", "[crypto]") {
  uint8_t pub_key[CRYPTO_PUBLIC_KEY_SIZE];

  crypto_result_t result = crypto_get_public_key(NULL, pub_key);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_ERROR_INVALID_PARAMS, "NULL context should fail");

  result = crypto_get_public_key(&ctx1, NULL);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_ERROR_INVALID_PARAMS, "NULL output buffer should fail");

  result = crypto_set_peer_public_key(NULL, pub_key);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_ERROR_INVALID_PARAMS, "NULL context should fail");

  result = crypto_set_peer_public_key(&ctx1, NULL);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_ERROR_INVALID_PARAMS, "NULL peer key should fail");
}

// =============================================================================
// Password Verification Tests
// =============================================================================

TEST_CASE("crypto/password_verification", "[crypto]") {
  const char *correct_password = "my-secure-password-123";
  const char *wrong_password = "wrong-password-456";

  crypto_result_t result = crypto_init_with_password(&ctx1, correct_password);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Init with password should succeed");

  // Test correct password
  bool verified = crypto_verify_password(&ctx1, correct_password);
  ASCIICHAT_REQUIRE(verified, "Correct password should verify");

  // Test wrong password
  verified = crypto_verify_password(&ctx1, wrong_password);
  ASCIICHAT_REQUIRE(verified == false, "Wrong password should not verify");

  // Test empty password
  verified = crypto_verify_password(&ctx1, "");
  ASCIICHAT_REQUIRE(verified == false, "Empty password should not verify");
}

// =============================================================================
// Encryption/Decryption Tests
// =============================================================================

// Theory: Encryption roundtrip property - decrypt(encrypt(data)) == data
// Replaces: encrypt_decrypt_password_based and encrypt_decrypt_key_exchange
TEST_CASE("crypto/encryption_roundtrip_property", "[crypto]") {
  size_t data_size = GENERATE(1u, 16u, 64u, 256u, 512u, 1024u, 2048u);
  ASCIICHAT_ASSUME(data_size > 0 && data_size <= 2048);

  crypto_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  crypto_result_t result = crypto_init_with_password(&ctx, "test-password-for-theory");
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Context init should succeed for size %zu", data_size);

  uint8_t *plaintext = SAFE_MALLOC(data_size, uint8_t *);
  for (size_t i = 0; i < data_size; i++) {
    plaintext[i] = (uint8_t)(i % 256);
  }

  uint8_t ciphertext[4096];
  size_t ciphertext_len;
  result = crypto_encrypt(&ctx, plaintext, data_size, ciphertext, sizeof(ciphertext), &ciphertext_len);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Encryption should succeed for size %zu", data_size);
  ASCIICHAT_REQUIRE_GT(ciphertext_len, data_size, "Ciphertext should be larger than plaintext for size %zu", data_size);

  uint8_t decrypted[4096];
  size_t decrypted_len;
  result = crypto_decrypt(&ctx, ciphertext, ciphertext_len, decrypted, sizeof(decrypted), &decrypted_len);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Decryption should succeed for size %zu", data_size);

  ASCIICHAT_REQUIRE_EQ(decrypted_len, data_size, "Decrypted length must match plaintext length for size %zu", data_size);
  ASCIICHAT_REQUIRE_EQ(memcmp(decrypted, plaintext, data_size), 0,
               "Decrypted data must match plaintext for size %zu (roundtrip property)", data_size);

  SAFE_FREE(plaintext);
  crypto_destroy(&ctx);
}

// Theory: Key exchange encryption roundtrip
TEST_CASE("crypto/key_exchange_roundtrip_property", "[crypto]") {
  size_t data_size = GENERATE(1u, 32u, 128u, 512u, 1024u);
  ASCIICHAT_ASSUME(data_size > 0 && data_size <= 1024);

  crypto_context_t ctx1, ctx2;
  memset(&ctx1, 0, sizeof(ctx1));
  memset(&ctx2, 0, sizeof(ctx2));

  crypto_init(&ctx1);
  crypto_init(&ctx2);

  uint8_t pub_key1[CRYPTO_PUBLIC_KEY_SIZE];
  uint8_t pub_key2[CRYPTO_PUBLIC_KEY_SIZE];

  crypto_get_public_key(&ctx1, pub_key1);
  crypto_get_public_key(&ctx2, pub_key2);
  crypto_set_peer_public_key(&ctx1, pub_key2);
  crypto_set_peer_public_key(&ctx2, pub_key1);

  uint8_t *plaintext = SAFE_MALLOC(data_size, uint8_t *);
  for (size_t i = 0; i < data_size; i++) {
    plaintext[i] = (uint8_t)((i * 7) % 256);
  }

  uint8_t ciphertext[2048];
  size_t ciphertext_len;
  crypto_result_t result = crypto_encrypt(&ctx1, plaintext, data_size, ciphertext, sizeof(ciphertext), &ciphertext_len);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Encryption should succeed for key exchange size %zu", data_size);

  uint8_t decrypted[2048];
  size_t decrypted_len;
  result = crypto_decrypt(&ctx2, ciphertext, ciphertext_len, decrypted, sizeof(decrypted), &decrypted_len);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Decryption should succeed for key exchange size %zu", data_size);

  ASCIICHAT_REQUIRE_EQ(decrypted_len, data_size, "Decrypted length must match for key exchange size %zu", data_size);
  ASCIICHAT_REQUIRE_EQ(memcmp(decrypted, plaintext, data_size), 0, "Key exchange roundtrip must preserve data for size %zu",
               data_size);

  SAFE_FREE(plaintext);
  crypto_destroy(&ctx1);
  crypto_destroy(&ctx2);
}

TEST_CASE("crypto/encrypt_not_ready", "[crypto]") {
  const char *plaintext = "test";
  uint8_t ciphertext[1024];
  size_t ciphertext_len;

  crypto_result_t result = crypto_init(&ctx1);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Init should succeed");

  // Try to encrypt before key exchange or password
  result = crypto_encrypt(&ctx1, (const uint8_t *)plaintext, strlen(plaintext), ciphertext, sizeof(ciphertext),
                          &ciphertext_len);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_ERROR_KEY_EXCHANGE_INCOMPLETE, "Encryption should fail when not ready");
}

TEST_CASE("crypto/encrypt_invalid_params", "[crypto]") {
  crypto_init_with_password(&ctx1, "password");

  const char *plaintext = "test";
  uint8_t ciphertext[1024];
  size_t ciphertext_len;

  // Test NULL parameters
  crypto_result_t result = crypto_encrypt(NULL, (const uint8_t *)plaintext, strlen(plaintext), ciphertext,
                                          sizeof(ciphertext), &ciphertext_len);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_ERROR_INVALID_PARAMS, "NULL context should fail");

  result = crypto_encrypt(&ctx1, NULL, strlen(plaintext), ciphertext, sizeof(ciphertext), &ciphertext_len);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_ERROR_INVALID_PARAMS, "NULL plaintext should fail");

  result = crypto_encrypt(&ctx1, (const uint8_t *)plaintext, 0, ciphertext, sizeof(ciphertext), &ciphertext_len);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_ERROR_INVALID_PARAMS, "Zero length should fail");

  result =
      crypto_encrypt(&ctx1, (const uint8_t *)plaintext, strlen(plaintext), NULL, sizeof(ciphertext), &ciphertext_len);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_ERROR_INVALID_PARAMS, "NULL ciphertext should fail");

  result = crypto_encrypt(&ctx1, (const uint8_t *)plaintext, strlen(plaintext), ciphertext, sizeof(ciphertext), NULL);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_ERROR_INVALID_PARAMS, "NULL length out should fail");
}

TEST_CASE("crypto/decrypt_invalid_data", "[crypto]") {
  crypto_init_with_password(&ctx1, "password");

  uint8_t invalid_ciphertext[] = {0x01, 0x02, 0x03}; // Too small
  uint8_t plaintext[1024];
  size_t plaintext_len;

  crypto_result_t result = crypto_decrypt(&ctx1, invalid_ciphertext, sizeof(invalid_ciphertext), plaintext,
                                          sizeof(plaintext), &plaintext_len);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_ERROR_INVALID_PARAMS, "Too small ciphertext should fail");
}

// =============================================================================
// Network Packet Tests
// =============================================================================

TEST_CASE("crypto/public_key_packet", "[crypto]") {
  crypto_init(&ctx1);

  uint8_t packet[1024];
  size_t packet_len;

  // Create packet
  crypto_result_t result = crypto_create_public_key_packet(&ctx1, packet, sizeof(packet), &packet_len);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Creating public key packet should succeed");
  ASCIICHAT_REQUIRE_EQ(packet_len, sizeof(uint32_t) + CRYPTO_PUBLIC_KEY_SIZE, "Packet size should be correct");

  // Process packet
  crypto_init(&ctx2);
  result = crypto_process_public_key_packet(&ctx2, packet, packet_len);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Processing public key packet should succeed");
  ASCIICHAT_REQUIRE(ctx2.peer_key_received, "Peer key should be received");
  ASCIICHAT_REQUIRE(ctx2.key_exchange_complete, "Key exchange should be complete");
}

TEST_CASE("crypto/encrypted_data_packet", "[crypto]") {
  // Set up key exchange
  crypto_init(&ctx1);
  crypto_init(&ctx2);

  uint8_t pub_key1[CRYPTO_PUBLIC_KEY_SIZE];
  uint8_t pub_key2[CRYPTO_PUBLIC_KEY_SIZE];

  crypto_get_public_key(&ctx1, pub_key1);
  crypto_get_public_key(&ctx2, pub_key2);
  crypto_set_peer_public_key(&ctx1, pub_key2);
  crypto_set_peer_public_key(&ctx2, pub_key1);

  // Test data
  const char *test_data = "Network packet test data";
  size_t test_data_len = strlen(test_data);

  // Create encrypted packet
  uint8_t packet[1024];
  size_t packet_len;
  crypto_result_t result = crypto_create_encrypted_packet(&ctx1, (const uint8_t *)test_data, test_data_len, packet,
                                                          sizeof(packet), &packet_len);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Creating encrypted packet should succeed");

  // Process encrypted packet
  uint8_t decrypted_data[1024];
  size_t decrypted_len;
  result = crypto_process_encrypted_packet(&ctx2, packet, packet_len, decrypted_data, sizeof(decrypted_data),
                                           &decrypted_len);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Processing encrypted packet should succeed");
  ASCIICHAT_REQUIRE_EQ(decrypted_len, test_data_len, "Decrypted length should match");
  ASCIICHAT_REQUIRE_EQ(memcmp(decrypted_data, test_data, test_data_len), 0, "Decrypted data should match");
}

// =============================================================================
// Utility Function Tests
// =============================================================================

TEST_CASE("crypto/random_bytes", "[crypto]") {
  uint8_t random1[32];
  uint8_t random2[32];

  crypto_result_t result1 = crypto_random_bytes(random1, sizeof(random1));
  crypto_result_t result2 = crypto_random_bytes(random2, sizeof(random2));

  ASCIICHAT_REQUIRE_EQ(result1, CRYPTO_OK, "First random bytes should succeed");
  ASCIICHAT_REQUIRE_EQ(result2, CRYPTO_OK, "Second random bytes should succeed");

  // Very unlikely to be the same (cryptographically secure random)
  ASCIICHAT_REQUIRE_NE(memcmp(random1, random2, sizeof(random1)), 0, "Random bytes should be different");
}

TEST_CASE("crypto/secure_compare", "[crypto]") {
  uint8_t data1[] = {0x01, 0x02, 0x03, 0x04};
  uint8_t data2[] = {0x01, 0x02, 0x03, 0x04};
  uint8_t data3[] = {0x01, 0x02, 0x03, 0x05};

  ASCIICHAT_REQUIRE(crypto_secure_compare(data1, data2, sizeof(data1)), "Identical data should match");
  ASCIICHAT_REQUIRE(crypto_secure_compare(data1, data3, sizeof(data1)) == false, "Different data should not match");
  ASCIICHAT_REQUIRE(crypto_secure_compare(NULL, data2, sizeof(data1)) == false, "NULL data should not match");
  ASCIICHAT_REQUIRE(crypto_secure_compare(data1, NULL, sizeof(data1)) == false, "NULL data should not match");
}

TEST_CASE("crypto/result_to_string", "[crypto]") {
  const char *ok_str = crypto_result_to_string(CRYPTO_OK);
  ASCIICHAT_REQUIRE_STR_EQ(ok_str, "Success", "CRYPTO_OK should return 'Success'");

  const char *invalid_str = crypto_result_to_string(CRYPTO_ERROR_INVALID_PARAMS);
  ASCIICHAT_REQUIRE_STR_EQ(invalid_str, "Invalid parameters", "Should return correct error message");

  const char *unknown_str = crypto_result_to_string((crypto_result_t)999);
  ASCIICHAT_REQUIRE_STR_EQ(unknown_str, "Unknown error", "Unknown code should return 'Unknown error'");
}

TEST_CASE("crypto/get_status", "[crypto]") {
  char status[512];

  // Uninitialized context
  crypto_get_status(&ctx1, status, sizeof(status));
  ASCIICHAT_REQUIRE_STR_EQ(status, "Not initialized", "Uninitialized context should report not initialized");

  // Initialized context with password
  crypto_init_with_password(&ctx1, "test-password");
  crypto_get_status(&ctx1, status, sizeof(status));
  ASCIICHAT_REQUIRE_STR_NE(status, "Not initialized", "Initialized context should not say not initialized");
  ASCIICHAT_REQUIRE(strstr(status, "Password: yes") != NULL, "Should show password is set");
  ASCIICHAT_REQUIRE(strstr(status, "Ready: yes") != NULL, "Should show ready status");
}

// =============================================================================
// Edge Case and Security Tests
// =============================================================================

// Theory: Nonce uniqueness property - same plaintext encrypted twice produces different ciphertext
TEST_CASE("crypto/nonce_uniqueness_property", "[crypto]") {
  size_t data_size = GENERATE(8u, 64u, 256u);
  ASCIICHAT_ASSUME(data_size > 0 && data_size <= 256);

  crypto_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  crypto_init_with_password(&ctx, "nonce-test-password");

  uint8_t *plaintext = SAFE_MALLOC(data_size, uint8_t *);
  for (size_t i = 0; i < data_size; i++) {
    plaintext[i] = 0xAA;
  }

  uint8_t ciphertext1[1024], ciphertext2[1024];
  size_t ciphertext1_len, ciphertext2_len;

  crypto_encrypt(&ctx, plaintext, data_size, ciphertext1, sizeof(ciphertext1), &ciphertext1_len);
  crypto_encrypt(&ctx, plaintext, data_size, ciphertext2, sizeof(ciphertext2), &ciphertext2_len);

  size_t min_len = ciphertext1_len < ciphertext2_len ? ciphertext1_len : ciphertext2_len;
  ASCIICHAT_REQUIRE_NE(memcmp(ciphertext1, ciphertext2, min_len), 0,
                "Nonce uniqueness property: same plaintext size %zu must produce different ciphertext", data_size);

  SAFE_FREE(plaintext);
  crypto_destroy(&ctx);
}

TEST_CASE("crypto/buffer_size_checks", "[crypto]") {
  crypto_init_with_password(&ctx1, "password");

  const char *plaintext = "test message for buffer size testing";
  size_t plaintext_len = strlen(plaintext);

  uint8_t small_buffer[10]; // Too small
  size_t output_len;

  crypto_result_t result =
      crypto_encrypt(&ctx1, (const uint8_t *)plaintext, plaintext_len, small_buffer, sizeof(small_buffer), &output_len);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_ERROR_BUFFER_TOO_SMALL, "Small buffer should fail");
}

TEST_CASE("crypto/cleanup_security", "[crypto]") {
  const char *password = "secret-password-123";

  crypto_init_with_password(&ctx1, password);

  // Verify context has data
  ASCIICHAT_REQUIRE(ctx1.initialized, "Context should be initialized");
  ASCIICHAT_REQUIRE(ctx1.has_password, "Context should have password");

  // Cleanup
  crypto_destroy(&ctx1);

  // Verify context is cleared (basic check - real implementation uses sodium_memzero)
  ASCIICHAT_REQUIRE(ctx1.initialized == false, "Context should not be initialized after cleanup");
  ASCIICHAT_REQUIRE(ctx1.has_password == false, "Context should not have password after cleanup");
}

TEST_CASE("crypto/nonce_counter_exhaustion", "[crypto]") {
  crypto_init_with_password(&ctx1, "test-password");

  const char *plaintext = "test message for nonce exhaustion";
  size_t plaintext_len = strlen(plaintext);
  uint8_t ciphertext[1024];
  size_t ciphertext_len;

  // Test 1: Normal operation - counter starts at 1 after init
  crypto_result_t result =
      crypto_encrypt(&ctx1, (const uint8_t *)plaintext, plaintext_len, ciphertext, sizeof(ciphertext), &ciphertext_len);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Normal encryption should succeed");
  ASCIICHAT_REQUIRE_EQ(ctx1.nonce_counter, 2, "Counter should increment from 1 to 2");

  // Test 2: Manually set counter to 0 to simulate exhaustion
  ctx1.nonce_counter = 0;

  // This encryption should fail because counter is 0 (exhausted)
  result =
      crypto_encrypt(&ctx1, (const uint8_t *)plaintext, plaintext_len, ciphertext, sizeof(ciphertext), &ciphertext_len);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_ERROR_NONCE_EXHAUSTED, "Encryption should fail when nonce counter is 0 (exhausted)");
  ASCIICHAT_REQUIRE_EQ(ctx1.nonce_counter, 0, "Counter should remain 0 after failed encryption");

  // Test 3: Verify that counter stays at 0 and continues to fail
  result =
      crypto_encrypt(&ctx1, (const uint8_t *)plaintext, plaintext_len, ciphertext, sizeof(ciphertext), &ciphertext_len);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_ERROR_NONCE_EXHAUSTED,
               "Subsequent encryptions should continue to fail with exhausted counter");
  ASCIICHAT_REQUIRE_EQ(ctx1.nonce_counter, 0, "Counter should remain at 0 (exhausted state)");

  // Test 4: Verify edge case - UINT64_MAX counter should fail (prevent overflow)
  ctx1.nonce_counter = UINT64_MAX;
  result =
      crypto_encrypt(&ctx1, (const uint8_t *)plaintext, plaintext_len, ciphertext, sizeof(ciphertext), &ciphertext_len);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_ERROR_NONCE_EXHAUSTED, "Encryption at UINT64_MAX should fail to prevent overflow");
  ASCIICHAT_REQUIRE_EQ(ctx1.nonce_counter, UINT64_MAX, "Counter should remain at UINT64_MAX (exhausted state)");

  // Verify that counter stays at UINT64_MAX and continues to fail
  result =
      crypto_encrypt(&ctx1, (const uint8_t *)plaintext, plaintext_len, ciphertext, sizeof(ciphertext), &ciphertext_len);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_ERROR_NONCE_EXHAUSTED, "Subsequent encryptions should continue to fail at UINT64_MAX");
}

// =============================================================================
// Parameterized Tests for Crypto Error Conditions
// =============================================================================

// Test case structure for crypto error conditions
typedef struct {
  const char *description;
  crypto_result_t expected_result;
  bool test_null_context;
  bool test_null_plaintext;
  bool test_zero_length;
  bool test_null_ciphertext;
  bool test_null_length_out;
} crypto_error_test_case_t;

static crypto_error_test_case_t crypto_error_cases[] = {
    {"NULL context", CRYPTO_ERROR_INVALID_PARAMS, true, false, false, false, false},
    {"NULL plaintext", CRYPTO_ERROR_INVALID_PARAMS, false, true, false, false, false},
    {"Zero length", CRYPTO_ERROR_INVALID_PARAMS, false, false, true, false, false},
    {"NULL ciphertext", CRYPTO_ERROR_INVALID_PARAMS, false, false, false, true, false},
    {"NULL length out", CRYPTO_ERROR_INVALID_PARAMS, false, false, false, false, true}};



TEST_CASE("crypto/error_conditions", "[crypto]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(crypto_error_cases)); auto *tc = &generated_case;
  crypto_context_t ctx;
  crypto_init_with_password(&ctx, "password");

  const char *plaintext = "test";
  uint8_t ciphertext[1024];
  size_t ciphertext_len;

  crypto_result_t result;

  if (tc->test_null_context) {
    result = crypto_encrypt(NULL, (const uint8_t *)plaintext, strlen(plaintext), ciphertext, sizeof(ciphertext),
                            &ciphertext_len);
  } else if (tc->test_null_plaintext) {
    result = crypto_encrypt(&ctx, NULL, strlen(plaintext), ciphertext, sizeof(ciphertext), &ciphertext_len);
  } else if (tc->test_zero_length) {
    result = crypto_encrypt(&ctx, (const uint8_t *)plaintext, 0, ciphertext, sizeof(ciphertext), &ciphertext_len);
  } else if (tc->test_null_ciphertext) {
    result =
        crypto_encrypt(&ctx, (const uint8_t *)plaintext, strlen(plaintext), NULL, sizeof(ciphertext), &ciphertext_len);
  } else {
    result = crypto_encrypt(&ctx, (const uint8_t *)plaintext, strlen(plaintext), ciphertext, sizeof(ciphertext), NULL);
  }

  ASCIICHAT_REQUIRE_EQ(result, tc->expected_result, "Test case: %s", tc->description);

  crypto_destroy(&ctx);
}

// Test case structure for crypto initialization tests
typedef struct {
  char description[64];
  char password[128];
  bool should_succeed;
  crypto_result_t expected_result;
} crypto_init_test_case_t;

static crypto_init_test_case_t crypto_init_cases[] = {
    {"Valid password", "test-password-123", true, CRYPTO_OK},
    {"Empty password", "", false, CRYPTO_ERROR_INVALID_PARAMS},
    {"Long password", "very-long-password-that-is-still-valid", true, CRYPTO_OK},
    {"Special chars password", "p@ssw0rd!@#$%", true, CRYPTO_OK}};



TEST_CASE("crypto/init_conditions", "[crypto]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(crypto_init_cases)); auto *tc = &generated_case;
  crypto_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  crypto_result_t result = crypto_init_with_password(&ctx, tc->password);

  ASCIICHAT_REQUIRE_EQ(result, tc->expected_result, "Init result should match for %s", tc->description);

  if (tc->should_succeed) {
    ASCIICHAT_REQUIRE(ctx.initialized, "Context should be initialized for %s", tc->description);
    ASCIICHAT_REQUIRE(ctx.has_password, "Context should have password for %s", tc->description);
    ASCIICHAT_REQUIRE(crypto_is_ready(&ctx), "Context should be ready for %s", tc->description);
  }

  crypto_destroy(&ctx);
}

// Test case structure for crypto password verification tests
typedef struct {
  bool should_verify;
  char correct_password[128];
  char test_password[128];
  char description[64];
} crypto_verify_test_case_t;

static crypto_verify_test_case_t crypto_verify_cases[] = {{true, "my-password", "my-password", "Correct password"},
                                                          {false, "my-password", "wrong-password", "Wrong password"},
                                                          {false, "my-password", "", "Empty test password"},
                                                          {false, "MyPassword", "mypassword", "Case sensitive"},
                                                          {false, "password", " password ", "Extra spaces"}};



TEST_CASE("crypto/password_verification_comprehensive", "[crypto]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(crypto_verify_cases)); auto *tc = &generated_case;
  crypto_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  // Initialize with correct password
  crypto_result_t init_result = crypto_init_with_password(&ctx, tc->correct_password);
  ASCIICHAT_REQUIRE_EQ(init_result, CRYPTO_OK, "Init should succeed for %s", tc->description);

  // Test password verification
  bool verified = crypto_verify_password(&ctx, tc->test_password);
  ASCIICHAT_REQUIRE_EQ(verified, tc->should_verify, "Password verification should match for %s", tc->description);

  crypto_destroy(&ctx);
}

// Test case structure for crypto result string tests
typedef struct {
  crypto_result_t result;
  char expected_string[64];
  char description[64];
} crypto_result_string_test_case_t;

static crypto_result_string_test_case_t crypto_result_string_cases[] = {
    {CRYPTO_OK, "Success", "Success result"},
    {CRYPTO_ERROR_INVALID_PARAMS, "Invalid parameters", "Invalid params result"},
    {CRYPTO_ERROR_KEY_EXCHANGE_INCOMPLETE, "Key exchange not complete", "Key exchange incomplete result"},
    {CRYPTO_ERROR_BUFFER_TOO_SMALL, "Buffer too small", "Buffer too small result"},
    {CRYPTO_ERROR_NONCE_EXHAUSTED, "Nonce counter exhausted", "Nonce exhausted result"},
    {(crypto_result_t)999, "Unknown error", "Unknown result"}};



TEST_CASE("crypto/result_strings", "[crypto]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(crypto_result_string_cases)); auto *tc = &generated_case;
  const char *result_str = crypto_result_to_string(tc->result);
  ASCIICHAT_REQUIRE_STR_EQ(result_str, tc->expected_string, "Result string should match for %s", tc->description);
}

// =============================================================================
// Session Rekeying Tests
// =============================================================================

TEST_CASE("crypto/rekey_initialization", "[crypto]") {
  // Initialize context with key exchange
  crypto_init(&ctx1);
  crypto_init(&ctx2);

  uint8_t pub_key1[CRYPTO_PUBLIC_KEY_SIZE];
  uint8_t pub_key2[CRYPTO_PUBLIC_KEY_SIZE];

  crypto_get_public_key(&ctx1, pub_key1);
  crypto_get_public_key(&ctx2, pub_key2);

  crypto_set_peer_public_key(&ctx1, pub_key2);
  crypto_set_peer_public_key(&ctx2, pub_key1);

  ASCIICHAT_REQUIRE(crypto_is_ready(&ctx1), "ctx1 should be ready");
  ASCIICHAT_REQUIRE(crypto_is_ready(&ctx2), "ctx2 should be ready");

  // Check initial rekey state
  ASCIICHAT_REQUIRE_EQ(ctx1.rekey_packet_count, 0, "Initial packet count should be 0");
  ASCIICHAT_REQUIRE_EQ(ctx1.rekey_count, 0, "Initial rekey count should be 0");
  ASCIICHAT_REQUIRE_EQ(ctx1.rekey_in_progress, false, "No rekey should be in progress");
  ASCIICHAT_REQUIRE_EQ(ctx1.has_temp_key, false, "Should not have temp key");
  ASCIICHAT_REQUIRE_EQ(ctx1.rekey_failure_count, 0, "Initial failure count should be 0");

  // Check default thresholds
  // Note: crypto_init() uses test thresholds when TESTING is set
  // The test environment sets these, so we expect test thresholds, not default thresholds
  ASCIICHAT_REQUIRE_EQ(ctx1.rekey_packet_threshold, REKEY_TEST_PACKET_THRESHOLD, "Test packet threshold should be 1000");
  ASCIICHAT_REQUIRE_EQ(ctx1.rekey_time_threshold, REKEY_TEST_TIME_THRESHOLD, "Test time threshold should be 30s");
}

TEST_CASE("crypto/rekey_should_trigger_on_packet_count", "[crypto]") {
  // Initialize with test thresholds
  crypto_init(&ctx1);
  ctx1.rekey_packet_threshold = 100; // Low threshold for testing
  ctx1.rekey_time_threshold = 3600;
  ctx1.key_exchange_complete = true;
  ctx1.handshake_complete = true;

  // Should not trigger initially
  ASCIICHAT_REQUIRE_EQ(crypto_should_rekey(&ctx1), false, "Should not rekey initially");

  // Simulate packet encryption
  ctx1.rekey_packet_count = 99;
  ASCIICHAT_REQUIRE_EQ(crypto_should_rekey(&ctx1), false, "Should not rekey before threshold");

  ctx1.rekey_packet_count = 100;
  ASCIICHAT_REQUIRE_EQ(crypto_should_rekey(&ctx1), true, "Should rekey at threshold");

  ctx1.rekey_packet_count = 101;
  ASCIICHAT_REQUIRE_EQ(crypto_should_rekey(&ctx1), true, "Should rekey after threshold");
}

TEST_CASE("crypto/rekey_should_trigger_on_time", "[crypto]") {
  // Initialize with test thresholds
  crypto_init(&ctx1);
  ctx1.rekey_packet_threshold = 1000000;
  ctx1.rekey_time_threshold = 10; // 10 seconds for testing
  ctx1.key_exchange_complete = true;
  ctx1.handshake_complete = true;

  time_t now = time(NULL);
  ctx1.rekey_last_time = now - 5; // 5 seconds ago
  ASCIICHAT_REQUIRE_EQ(crypto_should_rekey(&ctx1), false, "Should not rekey before time threshold");

  ctx1.rekey_last_time = now - 10; // 10 seconds ago
  ASCIICHAT_REQUIRE_EQ(crypto_should_rekey(&ctx1), true, "Should rekey at time threshold");

  ctx1.rekey_last_time = now - 15; // 15 seconds ago
  ASCIICHAT_REQUIRE_EQ(crypto_should_rekey(&ctx1), true, "Should rekey after time threshold");
}

TEST_CASE("crypto/rekey_should_not_trigger_during_rekey", "[crypto]") {
  crypto_init(&ctx1);
  ctx1.rekey_packet_threshold = 100;
  ctx1.rekey_time_threshold = 3600;
  ctx1.key_exchange_complete = true;
  ctx1.handshake_complete = true;
  ctx1.rekey_packet_count = 200; // Above threshold

  // Should trigger normally
  ASCIICHAT_REQUIRE_EQ(crypto_should_rekey(&ctx1), true, "Should trigger with high packet count");

  // But not when rekey is in progress
  ctx1.rekey_in_progress = true;
  ASCIICHAT_REQUIRE_EQ(crypto_should_rekey(&ctx1), false, "Should not trigger during rekey");
}

TEST_CASE("crypto/rekey_init_basic", "[crypto]") {
  // Set up context
  crypto_init(&ctx1);
  ctx1.key_exchange_complete = true;
  ctx1.handshake_complete = true;
  ctx1.rekey_last_time = time(NULL) - 100; // 100 seconds ago (past min interval)

  crypto_result_t result = crypto_rekey_init(&ctx1);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Rekey init should succeed");
  ASCIICHAT_REQUIRE_EQ(ctx1.rekey_in_progress, true, "Rekey should be in progress");
  ASCIICHAT_REQUIRE_EQ(ctx1.has_temp_key, true, "Should have temp key");

  // Check that temp keys are non-zero
  uint8_t zero_key[CRYPTO_PUBLIC_KEY_SIZE] = {0};
  ASCIICHAT_REQUIRE_NE(memcmp(ctx1.temp_public_key, zero_key, CRYPTO_PUBLIC_KEY_SIZE), 0, "Temp public key should be set");
  ASCIICHAT_REQUIRE_NE(memcmp(ctx1.temp_private_key, zero_key, CRYPTO_PRIVATE_KEY_SIZE), 0, "Temp private key should be set");
}

TEST_CASE("crypto/rekey_init_rate_limiting", "[crypto]") {
  crypto_init(&ctx1);
  ctx1.key_exchange_complete = true;
  ctx1.handshake_complete = true;

  // Set last rekey time to 1 second ago (less than REKEY_MIN_INTERVAL which is 3 seconds)
  ctx1.rekey_last_time = time(NULL) - 1;

  crypto_result_t result = crypto_rekey_init(&ctx1);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_ERROR_REKEY_RATE_LIMITED, "Should be rate limited");
  ASCIICHAT_REQUIRE_EQ(ctx1.rekey_in_progress, false, "Rekey should not start");
  ASCIICHAT_REQUIRE_EQ(ctx1.has_temp_key, false, "Should not have temp key");
}

TEST_CASE("crypto/rekey_init_failure_limit", "[crypto]") {
  crypto_init(&ctx1);
  ctx1.key_exchange_complete = true;
  ctx1.handshake_complete = true;
  ctx1.rekey_last_time = time(NULL) - 100;

  // Set failure count to max
  ctx1.rekey_failure_count = REKEY_MAX_FAILURE_COUNT;

  crypto_result_t result = crypto_rekey_init(&ctx1);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_ERROR_REKEY_FAILED, "Should fail after max failures");
  ASCIICHAT_REQUIRE_EQ(ctx1.rekey_in_progress, false, "Rekey should not start");
}

TEST_CASE("crypto/rekey_process_request", "[crypto]") {
  // Initialize both contexts
  crypto_init(&ctx1);
  crypto_init(&ctx2);

  // Complete initial key exchange
  uint8_t pub_key1[CRYPTO_PUBLIC_KEY_SIZE];
  uint8_t pub_key2[CRYPTO_PUBLIC_KEY_SIZE];
  crypto_get_public_key(&ctx1, pub_key1);
  crypto_get_public_key(&ctx2, pub_key2);
  crypto_set_peer_public_key(&ctx1, pub_key2);
  crypto_set_peer_public_key(&ctx2, pub_key1);

  // Initiator (ctx1) starts rekey
  ctx1.rekey_last_time = time(NULL) - 100;
  crypto_result_t result = crypto_rekey_init(&ctx1);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Initiator rekey init should succeed");

  // Responder (ctx2) processes request
  ctx2.rekey_last_time = time(NULL) - 100;
  result = crypto_rekey_init(&ctx2);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Responder rekey init should succeed");

  result = crypto_rekey_process_request(&ctx2, ctx1.temp_public_key);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Process request should succeed");
  ASCIICHAT_REQUIRE_EQ(ctx2.has_temp_key, true, "Responder should have temp shared key");

  // Check that temp shared key is non-zero
  uint8_t zero_key[CRYPTO_SHARED_KEY_SIZE] = {0};
  ASCIICHAT_REQUIRE_NE(memcmp(ctx2.temp_shared_key, zero_key, CRYPTO_SHARED_KEY_SIZE), 0, "Temp shared key should be set");
}

TEST_CASE("crypto/rekey_process_response", "[crypto]") {
  // Initialize both contexts with key exchange
  crypto_init(&ctx1);
  crypto_init(&ctx2);

  uint8_t pub_key1[CRYPTO_PUBLIC_KEY_SIZE];
  uint8_t pub_key2[CRYPTO_PUBLIC_KEY_SIZE];
  crypto_get_public_key(&ctx1, pub_key1);
  crypto_get_public_key(&ctx2, pub_key2);
  crypto_set_peer_public_key(&ctx1, pub_key2);
  crypto_set_peer_public_key(&ctx2, pub_key1);

  // Both sides init rekey
  ctx1.rekey_last_time = time(NULL) - 100;
  ctx2.rekey_last_time = time(NULL) - 100;
  crypto_rekey_init(&ctx1);
  crypto_rekey_init(&ctx2);

  // Responder processes request
  crypto_rekey_process_request(&ctx2, ctx1.temp_public_key);

  // Initiator processes response
  crypto_result_t result = crypto_rekey_process_response(&ctx1, ctx2.temp_public_key);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Process response should succeed");

  // Both should have computed the same shared secret
  ASCIICHAT_REQUIRE_EQ(memcmp(ctx1.temp_shared_key, ctx2.temp_shared_key, CRYPTO_SHARED_KEY_SIZE), 0,
               "Both sides should compute same shared secret");
}

TEST_CASE("crypto/rekey_commit", "[crypto]") {
  // Set up context with temp keys
  crypto_init(&ctx1);
  ctx1.key_exchange_complete = true;
  ctx1.handshake_complete = true;
  ctx1.rekey_last_time = time(NULL) - 100;

  // Save old shared key
  uint8_t old_shared_key[CRYPTO_SHARED_KEY_SIZE];
  memcpy(old_shared_key, ctx1.shared_key, CRYPTO_SHARED_KEY_SIZE);

  // Initialize rekey
  crypto_rekey_init(&ctx1);

  // Set up temp shared key (simulate completed exchange)
  randombytes_buf(ctx1.temp_shared_key, CRYPTO_SHARED_KEY_SIZE);
  uint8_t temp_shared_key_copy[CRYPTO_SHARED_KEY_SIZE];
  memcpy(temp_shared_key_copy, ctx1.temp_shared_key, CRYPTO_SHARED_KEY_SIZE);

  uint64_t old_rekey_count = ctx1.rekey_count;

  // Commit
  crypto_result_t result = crypto_rekey_commit(&ctx1);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Commit should succeed");

  // Check state after commit
  ASCIICHAT_REQUIRE_EQ(ctx1.rekey_in_progress, false, "Rekey should no longer be in progress");
  ASCIICHAT_REQUIRE_EQ(ctx1.has_temp_key, false, "Should not have temp key after commit");
  ASCIICHAT_REQUIRE_EQ(ctx1.rekey_packet_count, 0, "Packet count should reset");
  ASCIICHAT_REQUIRE_EQ(ctx1.rekey_count, old_rekey_count + 1, "Rekey count should increment");
  ASCIICHAT_REQUIRE_EQ(ctx1.nonce_counter, 1, "Nonce counter should reset to 1");
  ASCIICHAT_REQUIRE_EQ(ctx1.rekey_failure_count, 0, "Failure count should reset");

  // Check that shared key was updated
  ASCIICHAT_REQUIRE_EQ(memcmp(ctx1.shared_key, temp_shared_key_copy, CRYPTO_SHARED_KEY_SIZE), 0,
               "Shared key should be updated to temp key");
  ASCIICHAT_REQUIRE_NE(memcmp(ctx1.shared_key, old_shared_key, CRYPTO_SHARED_KEY_SIZE), 0,
                "Shared key should differ from old key");

  // Check that temp keys are wiped
  uint8_t zero_key[CRYPTO_SHARED_KEY_SIZE] = {0};
  ASCIICHAT_REQUIRE_EQ(memcmp(ctx1.temp_shared_key, zero_key, CRYPTO_SHARED_KEY_SIZE), 0, "Temp shared key should be wiped");
}

TEST_CASE("crypto/rekey_abort", "[crypto]") {
  // Set up context with rekey in progress
  crypto_init(&ctx1);
  ctx1.key_exchange_complete = true;
  ctx1.handshake_complete = true;
  ctx1.rekey_last_time = time(NULL) - 100;

  // Save old shared key
  uint8_t old_shared_key[CRYPTO_SHARED_KEY_SIZE];
  memcpy(old_shared_key, ctx1.shared_key, CRYPTO_SHARED_KEY_SIZE);

  uint8_t old_failure_count = ctx1.rekey_failure_count;

  // Initialize rekey
  crypto_rekey_init(&ctx1);
  ASCIICHAT_REQUIRE_EQ(ctx1.rekey_in_progress, true, "Rekey should be in progress");

  // Abort
  crypto_rekey_abort(&ctx1);

  // Check state after abort
  ASCIICHAT_REQUIRE_EQ(ctx1.rekey_in_progress, false, "Rekey should no longer be in progress");
  ASCIICHAT_REQUIRE_EQ(ctx1.has_temp_key, false, "Should not have temp key");
  ASCIICHAT_REQUIRE_EQ(ctx1.rekey_failure_count, old_failure_count + 1, "Failure count should increment");

  // Check that shared key is unchanged
  ASCIICHAT_REQUIRE_EQ(memcmp(ctx1.shared_key, old_shared_key, CRYPTO_SHARED_KEY_SIZE), 0, "Shared key should be unchanged");

  // Check that temp keys are wiped
  uint8_t zero_key[CRYPTO_SHARED_KEY_SIZE] = {0};
  ASCIICHAT_REQUIRE_EQ(memcmp(ctx1.temp_shared_key, zero_key, CRYPTO_SHARED_KEY_SIZE), 0, "Temp shared key should be wiped");
}

TEST_CASE("crypto/rekey_full_exchange", "[crypto]") {
  // Initialize both contexts
  crypto_init(&ctx1);
  crypto_init(&ctx2);

  // Complete initial key exchange
  uint8_t pub_key1[CRYPTO_PUBLIC_KEY_SIZE];
  uint8_t pub_key2[CRYPTO_PUBLIC_KEY_SIZE];
  crypto_get_public_key(&ctx1, pub_key1);
  crypto_get_public_key(&ctx2, pub_key2);
  crypto_set_peer_public_key(&ctx1, pub_key2);
  crypto_set_peer_public_key(&ctx2, pub_key1);

  // Encrypt test data with original key
  const char *test_data = "Hello before rekey";
  uint8_t ciphertext_before[256];
  size_t ciphertext_before_len;
  crypto_encrypt(&ctx1, (const uint8_t *)test_data, strlen(test_data), ciphertext_before, sizeof(ciphertext_before),
                 &ciphertext_before_len);

  uint8_t decrypted_before[256];
  size_t decrypted_before_len;
  crypto_decrypt(&ctx2, ciphertext_before, ciphertext_before_len, decrypted_before, sizeof(decrypted_before),
                 &decrypted_before_len);
  ASCIICHAT_REQUIRE_EQ(memcmp(decrypted_before, test_data, strlen(test_data)), 0, "Original encryption should work");

  // Save original shared keys
  uint8_t old_shared_key1[CRYPTO_SHARED_KEY_SIZE];
  uint8_t old_shared_key2[CRYPTO_SHARED_KEY_SIZE];
  memcpy(old_shared_key1, ctx1.shared_key, CRYPTO_SHARED_KEY_SIZE);
  memcpy(old_shared_key2, ctx2.shared_key, CRYPTO_SHARED_KEY_SIZE);

  // Perform full rekey exchange
  ctx1.rekey_last_time = time(NULL) - 100;
  ctx2.rekey_last_time = time(NULL) - 100;

  // Step 1: Initiator (ctx1) sends REKEY_REQUEST
  crypto_result_t result = crypto_rekey_init(&ctx1);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Initiator init should succeed");

  // Step 2: Responder (ctx2) processes request and generates response
  result = crypto_rekey_init(&ctx2);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Responder init should succeed");
  result = crypto_rekey_process_request(&ctx2, ctx1.temp_public_key);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Process request should succeed");

  // Step 3: Initiator processes response
  result = crypto_rekey_process_response(&ctx1, ctx2.temp_public_key);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Process response should succeed");

  // Both should have same temp shared key
  ASCIICHAT_REQUIRE_EQ(memcmp(ctx1.temp_shared_key, ctx2.temp_shared_key, CRYPTO_SHARED_KEY_SIZE), 0,
               "Temp shared keys should match");

  // Step 4: Both commit to new key
  result = crypto_rekey_commit(&ctx1);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Initiator commit should succeed");
  result = crypto_rekey_commit(&ctx2);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Responder commit should succeed");

  // Check that shared keys changed
  ASCIICHAT_REQUIRE_NE(memcmp(ctx1.shared_key, old_shared_key1, CRYPTO_SHARED_KEY_SIZE), 0, "ctx1 shared key should change");
  ASCIICHAT_REQUIRE_NE(memcmp(ctx2.shared_key, old_shared_key2, CRYPTO_SHARED_KEY_SIZE), 0, "ctx2 shared key should change");

  // Check that both have the same new key
  ASCIICHAT_REQUIRE_EQ(memcmp(ctx1.shared_key, ctx2.shared_key, CRYPTO_SHARED_KEY_SIZE), 0, "New shared keys should match");

  // Test encryption with new key
  const char *test_data_after = "Hello after rekey";
  uint8_t ciphertext_after[256];
  size_t ciphertext_after_len;
  result = crypto_encrypt(&ctx1, (const uint8_t *)test_data_after, strlen(test_data_after), ciphertext_after,
                          sizeof(ciphertext_after), &ciphertext_after_len);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Encryption with new key should work");

  uint8_t decrypted_after[256];
  size_t decrypted_after_len;
  result = crypto_decrypt(&ctx2, ciphertext_after, ciphertext_after_len, decrypted_after, sizeof(decrypted_after),
                          &decrypted_after_len);
  ASCIICHAT_REQUIRE_EQ(result, CRYPTO_OK, "Decryption with new key should work");
  ASCIICHAT_REQUIRE_EQ(memcmp(decrypted_after, test_data_after, strlen(test_data_after)), 0,
               "Decrypted data should match with new key");

  // Verify old ciphertext cannot be decrypted with new key
  uint8_t decrypted_old_with_new[256];
  size_t decrypted_old_with_new_len;
  result = crypto_decrypt(&ctx2, ciphertext_before, ciphertext_before_len, decrypted_old_with_new,
                          sizeof(decrypted_old_with_new), &decrypted_old_with_new_len);
  ASCIICHAT_REQUIRE_NE(result, CRYPTO_OK, "Old ciphertext should not decrypt with new key");
}

TEST_CASE("crypto/rekey_packet_counter_increments", "[crypto]") {
  // Initialize context
  crypto_init(&ctx1);
  ctx1.key_exchange_complete = true;
  ctx1.handshake_complete = true;

  ASCIICHAT_REQUIRE_EQ(ctx1.rekey_packet_count, 0, "Initial packet count should be 0");

  // Encrypt some data
  const char *data = "test data";
  uint8_t ciphertext[256];
  size_t ciphertext_len;

  for (int i = 0; i < 10; i++) {
    crypto_encrypt(&ctx1, (const uint8_t *)data, strlen(data), ciphertext, sizeof(ciphertext), &ciphertext_len);
  }

  ASCIICHAT_REQUIRE_EQ(ctx1.rekey_packet_count, 10, "Packet count should increment on each encryption");
}
