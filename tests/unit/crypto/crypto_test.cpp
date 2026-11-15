#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <catch2/catch_all.hpp>
#include <catch2/catch_all.hpp>
#include <catch2/catch_all.hpp>
#include <string.h>

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
  REQUIRE((result) == (CRYPTO_OK));
  REQUIRE(ctx1.initialized);
  REQUIRE(ctx1.has_password == false);
  REQUIRE(ctx1.key_exchange_complete == false);
  REQUIRE(crypto_is_ready(&ctx1) == false);
}

TEST_CASE("crypto/init_with_password", "[crypto]") {
  const char *password = "test-password-123";
  crypto_result_t result = crypto_init_with_password(&ctx1, password);

  REQUIRE((result) == (CRYPTO_OK));
  REQUIRE(ctx1.initialized);
  REQUIRE(ctx1.has_password);
  REQUIRE(crypto_is_ready(&ctx1));
}

TEST_CASE("crypto/init_invalid_params", "[crypto]") {
  crypto_result_t result = crypto_init(NULL);
  REQUIRE((result) == (CRYPTO_ERROR_INVALID_PARAMS));

  result = crypto_init_with_password(NULL, "password");
  REQUIRE((result) == (CRYPTO_ERROR_INVALID_PARAMS));

  result = crypto_init_with_password(&ctx1, NULL);
  REQUIRE((result) == (CRYPTO_ERROR_INVALID_PARAMS));

  result = crypto_init_with_password(&ctx1, "");
  REQUIRE((result) == (CRYPTO_ERROR_INVALID_PARAMS));
}

// =============================================================================
// Key Exchange Tests
// =============================================================================

TEST_CASE("crypto/key_exchange_flow", "[crypto]") {
  // Initialize both contexts
  crypto_result_t result = crypto_init(&ctx1);
  REQUIRE((result) == (CRYPTO_OK));

  result = crypto_init(&ctx2);
  REQUIRE((result) == (CRYPTO_OK));

  // Get public keys
  uint8_t pub_key1[CRYPTO_PUBLIC_KEY_SIZE];
  uint8_t pub_key2[CRYPTO_PUBLIC_KEY_SIZE];

  result = crypto_get_public_key(&ctx1, pub_key1);
  REQUIRE((result) == (CRYPTO_OK));

  result = crypto_get_public_key(&ctx2, pub_key2);
  REQUIRE((result) == (CRYPTO_OK));

  // Keys should be different
  REQUIRE((memcmp(pub_key1, pub_key2, CRYPTO_PUBLIC_KEY_SIZE)) != (0));

  // Exchange keys
  result = crypto_set_peer_public_key(&ctx1, pub_key2);
  REQUIRE((result) == (CRYPTO_OK));
  REQUIRE(ctx1.key_exchange_complete);
  REQUIRE(crypto_is_ready(&ctx1));

  result = crypto_set_peer_public_key(&ctx2, pub_key1);
  REQUIRE((result) == (CRYPTO_OK));
  REQUIRE(ctx2.key_exchange_complete);
  REQUIRE(crypto_is_ready(&ctx2));
}

TEST_CASE("crypto/public_key_invalid_params", "[crypto]") {
  uint8_t pub_key[CRYPTO_PUBLIC_KEY_SIZE];

  crypto_result_t result = crypto_get_public_key(NULL, pub_key);
  REQUIRE((result) == (CRYPTO_ERROR_INVALID_PARAMS));

  result = crypto_get_public_key(&ctx1, NULL);
  REQUIRE((result) == (CRYPTO_ERROR_INVALID_PARAMS));

  result = crypto_set_peer_public_key(NULL, pub_key);
  REQUIRE((result) == (CRYPTO_ERROR_INVALID_PARAMS));

  result = crypto_set_peer_public_key(&ctx1, NULL);
  REQUIRE((result) == (CRYPTO_ERROR_INVALID_PARAMS));
}

// =============================================================================
// Password Verification Tests
// =============================================================================

TEST_CASE("crypto/password_verification", "[crypto]") {
  const char *correct_password = "my-secure-password-123";
  const char *wrong_password = "wrong-password-456";

  crypto_result_t result = crypto_init_with_password(&ctx1, correct_password);
  REQUIRE((result) == (CRYPTO_OK));

  // Test correct password
  bool verified = crypto_verify_password(&ctx1, correct_password);
  REQUIRE(verified);

  // Test wrong password
  verified = crypto_verify_password(&ctx1, wrong_password);
  REQUIRE(verified == false);

  // Test empty password
  verified = crypto_verify_password(&ctx1, "");
  REQUIRE(verified == false);
}

// =============================================================================
// Encryption/Decryption Tests
// =============================================================================

// Theory: Encryption roundtrip property - decrypt(encrypt(data)) == data
// Replaces: encrypt_decrypt_password_based and encrypt_decrypt_key_exchange
TEST_CASE("crypto/encryption_roundtrip_property", "[crypto]") {
  size_t data_size = GENERATE(1u, 16u, 64u, 256u, 512u, 1024u, 2048u);
  if (!(data_size > 0 && data_size <= 2048))
    SKIP("assumption not satisfied");

  crypto_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  crypto_result_t result = crypto_init_with_password(&ctx, "test-password-for-theory");
  REQUIRE((result) == (CRYPTO_OK));

  uint8_t *plaintext = SAFE_MALLOC(data_size, uint8_t *);
  for (size_t i = 0; i < data_size; i++) {
    plaintext[i] = (uint8_t)(i % 256);
  }

  uint8_t ciphertext[4096];
  size_t ciphertext_len;
  result = crypto_encrypt(&ctx, plaintext, data_size, ciphertext, sizeof(ciphertext), &ciphertext_len);
  REQUIRE((result) == (CRYPTO_OK));
  REQUIRE((ciphertext_len) > (data_size));

  uint8_t decrypted[4096];
  size_t decrypted_len;
  result = crypto_decrypt(&ctx, ciphertext, ciphertext_len, decrypted, sizeof(decrypted), &decrypted_len);
  REQUIRE((result) == (CRYPTO_OK));

  REQUIRE((decrypted_len) == (data_size));
  REQUIRE((memcmp(decrypted, plaintext, data_size)) == (0));

  SAFE_FREE(plaintext);
  crypto_destroy(&ctx);
}

// Theory: Key exchange encryption roundtrip
TEST_CASE("crypto/key_exchange_roundtrip_property", "[crypto]") {
  size_t data_size = GENERATE(1u, 32u, 128u, 512u, 1024u);
  if (!(data_size > 0 && data_size <= 1024))
    SKIP("assumption not satisfied");

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
  REQUIRE((result) == (CRYPTO_OK));

  uint8_t decrypted[2048];
  size_t decrypted_len;
  result = crypto_decrypt(&ctx2, ciphertext, ciphertext_len, decrypted, sizeof(decrypted), &decrypted_len);
  REQUIRE((result) == (CRYPTO_OK));

  REQUIRE((decrypted_len) == (data_size));
  REQUIRE((memcmp(decrypted, plaintext, data_size)) == (0));

  SAFE_FREE(plaintext);
  crypto_destroy(&ctx1);
  crypto_destroy(&ctx2);
}

TEST_CASE("crypto/encrypt_not_ready", "[crypto]") {
  const char *plaintext = "test";
  uint8_t ciphertext[1024];
  size_t ciphertext_len;

  crypto_result_t result = crypto_init(&ctx1);
  REQUIRE((result) == (CRYPTO_OK));

  // Try to encrypt before key exchange or password
  result = crypto_encrypt(&ctx1, (const uint8_t *)plaintext, strlen(plaintext), ciphertext, sizeof(ciphertext), &ciphertext_len);
  REQUIRE((result) == (CRYPTO_ERROR_KEY_EXCHANGE_INCOMPLETE));
}

TEST_CASE("crypto/encrypt_invalid_params", "[crypto]") {
  crypto_init_with_password(&ctx1, "password");

  const char *plaintext = "test";
  uint8_t ciphertext[1024];
  size_t ciphertext_len;

  // Test NULL parameters
  crypto_result_t result = crypto_encrypt(NULL, (const uint8_t *)plaintext, strlen(plaintext), ciphertext, sizeof(ciphertext), &ciphertext_len);
  REQUIRE((result) == (CRYPTO_ERROR_INVALID_PARAMS));

  result = crypto_encrypt(&ctx1, NULL, strlen(plaintext), ciphertext, sizeof(ciphertext), &ciphertext_len);
  REQUIRE((result) == (CRYPTO_ERROR_INVALID_PARAMS));

  result = crypto_encrypt(&ctx1, (const uint8_t *)plaintext, 0, ciphertext, sizeof(ciphertext), &ciphertext_len);
  REQUIRE((result) == (CRYPTO_ERROR_INVALID_PARAMS));

  result = crypto_encrypt(&ctx1, (const uint8_t *)plaintext, strlen(plaintext), NULL, sizeof(ciphertext), &ciphertext_len);
  REQUIRE((result) == (CRYPTO_ERROR_INVALID_PARAMS));

  result = crypto_encrypt(&ctx1, (const uint8_t *)plaintext, strlen(plaintext), ciphertext, sizeof(ciphertext), NULL);
  REQUIRE((result) == (CRYPTO_ERROR_INVALID_PARAMS));
}

TEST_CASE("crypto/decrypt_invalid_data", "[crypto]") {
  crypto_init_with_password(&ctx1, "password");

  uint8_t invalid_ciphertext[] = {0x01, 0x02, 0x03}; // Too small
  uint8_t plaintext[1024];
  size_t plaintext_len;

  crypto_result_t result = crypto_decrypt(&ctx1, invalid_ciphertext, sizeof(invalid_ciphertext), plaintext, sizeof(plaintext), &plaintext_len);
  REQUIRE((result) == (CRYPTO_ERROR_INVALID_PARAMS));
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
  REQUIRE((result) == (CRYPTO_OK));
  REQUIRE((packet_len) == (sizeof(uint32_t) + CRYPTO_PUBLIC_KEY_SIZE));

  // Process packet
  crypto_init(&ctx2);
  result = crypto_process_public_key_packet(&ctx2, packet, packet_len);
  REQUIRE((result) == (CRYPTO_OK));
  REQUIRE(ctx2.peer_key_received);
  REQUIRE(ctx2.key_exchange_complete);
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
  crypto_result_t result = crypto_create_encrypted_packet(&ctx1, (const uint8_t *)test_data, test_data_len, packet, sizeof(packet), &packet_len);
  REQUIRE((result) == (CRYPTO_OK));

  // Process encrypted packet
  uint8_t decrypted_data[1024];
  size_t decrypted_len;
  result = crypto_process_encrypted_packet(&ctx2, packet, packet_len, decrypted_data, sizeof(decrypted_data), &decrypted_len);
  REQUIRE((result) == (CRYPTO_OK));
  REQUIRE((decrypted_len) == (test_data_len));
  REQUIRE((memcmp(decrypted_data, test_data, test_data_len)) == (0));
}

// =============================================================================
// Utility Function Tests
// =============================================================================

TEST_CASE("crypto/random_bytes", "[crypto]") {
  uint8_t random1[32];
  uint8_t random2[32];

  crypto_result_t result1 = crypto_random_bytes(random1, sizeof(random1));
  crypto_result_t result2 = crypto_random_bytes(random2, sizeof(random2));

  REQUIRE((result1) == (CRYPTO_OK));
  REQUIRE((result2) == (CRYPTO_OK));

  // Very unlikely to be the same (cryptographically secure random)
  REQUIRE((memcmp(random1, random2, sizeof(random1))) != (0));
}

TEST_CASE("crypto/secure_compare", "[crypto]") {
  uint8_t data1[] = {0x01, 0x02, 0x03, 0x04};
  uint8_t data2[] = {0x01, 0x02, 0x03, 0x04};
  uint8_t data3[] = {0x01, 0x02, 0x03, 0x05};

  REQUIRE(crypto_secure_compare(data1, data2, sizeof(data1)));
  REQUIRE(crypto_secure_compare(data1, data3, sizeof(data1)) == false);
  REQUIRE(crypto_secure_compare(NULL, data2, sizeof(data1)) == false);
  REQUIRE(crypto_secure_compare(data1, NULL, sizeof(data1)) == false);
}

TEST_CASE("crypto/result_to_string", "[crypto]") {
  const char *ok_str = crypto_result_to_string(CRYPTO_OK);
  REQUIRE(std::strcmp((ok_str), ("Success")) == 0);

  const char *invalid_str = crypto_result_to_string(CRYPTO_ERROR_INVALID_PARAMS);
  REQUIRE(std::strcmp((invalid_str), ("Invalid parameters")) == 0);

  const char *unknown_str = crypto_result_to_string((crypto_result_t)999);
  REQUIRE(std::strcmp((unknown_str), ("Unknown error")) == 0);
}

TEST_CASE("crypto/get_status", "[crypto]") {
  char status[512];

  // Uninitialized context
  crypto_get_status(&ctx1, status, sizeof(status));
  REQUIRE(std::strcmp((status), ("Not initialized")) == 0);

  // Initialized context with password
  crypto_init_with_password(&ctx1, "test-password");
  crypto_get_status(&ctx1, status, sizeof(status));
  REQUIRE(std::strcmp((status), ("Not initialized")) != 0);
  REQUIRE(strstr(status, "Password: yes") != NULL);
  REQUIRE(strstr(status, "Ready: yes") != NULL);
}

// =============================================================================
// Edge Case and Security Tests
// =============================================================================

// Theory: Nonce uniqueness property - same plaintext encrypted twice produces different ciphertext
TEST_CASE("crypto/nonce_uniqueness_property", "[crypto]") {
  size_t data_size = GENERATE(8u, 64u, 256u);
  if (!(data_size > 0 && data_size <= 256))
    SKIP("assumption not satisfied");

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
  REQUIRE((memcmp(ciphertext1, ciphertext2, min_len)) != (0));

  SAFE_FREE(plaintext);
  crypto_destroy(&ctx);
}

TEST_CASE("crypto/buffer_size_checks", "[crypto]") {
  crypto_init_with_password(&ctx1, "password");

  const char *plaintext = "test message for buffer size testing";
  size_t plaintext_len = strlen(plaintext);

  uint8_t small_buffer[10]; // Too small
  size_t output_len;

  crypto_result_t result = crypto_encrypt(&ctx1, (const uint8_t *)plaintext, plaintext_len, small_buffer, sizeof(small_buffer), &output_len);
  REQUIRE((result) == (CRYPTO_ERROR_BUFFER_TOO_SMALL));
}

TEST_CASE("crypto/cleanup_security", "[crypto]") {
  const char *password = "secret-password-123";

  crypto_init_with_password(&ctx1, password);

  // Verify context has data
  REQUIRE(ctx1.initialized);
  REQUIRE(ctx1.has_password);

  // Cleanup
  crypto_destroy(&ctx1);

  // Verify context is cleared (basic check - real implementation uses sodium_memzero)
  REQUIRE(ctx1.initialized == false);
  REQUIRE(ctx1.has_password == false);
}

TEST_CASE("crypto/nonce_counter_exhaustion", "[crypto]") {
  crypto_init_with_password(&ctx1, "test-password");

  const char *plaintext = "test message for nonce exhaustion";
  size_t plaintext_len = strlen(plaintext);
  uint8_t ciphertext[1024];
  size_t ciphertext_len;

  // Test 1: Normal operation - counter starts at 1 after init
  crypto_result_t result = crypto_encrypt(&ctx1, (const uint8_t *)plaintext, plaintext_len, ciphertext, sizeof(ciphertext), &ciphertext_len);
  REQUIRE((result) == (CRYPTO_OK));
  REQUIRE((ctx1.nonce_counter) == (2));

  // Test 2: Manually set counter to 0 to simulate exhaustion
  ctx1.nonce_counter = 0;

  // This encryption should fail because counter is 0 (exhausted)
  result = crypto_encrypt(&ctx1, (const uint8_t *)plaintext, plaintext_len, ciphertext, sizeof(ciphertext), &ciphertext_len);
  REQUIRE((result) == (CRYPTO_ERROR_NONCE_EXHAUSTED));
  REQUIRE((ctx1.nonce_counter) == (0));

  // Test 3: Verify that counter stays at 0 and continues to fail
  result = crypto_encrypt(&ctx1, (const uint8_t *)plaintext, plaintext_len, ciphertext, sizeof(ciphertext), &ciphertext_len);
  REQUIRE((result) == (CRYPTO_ERROR_NONCE_EXHAUSTED));
  REQUIRE((ctx1.nonce_counter) == (0));

  // Test 4: Verify edge case - UINT64_MAX counter should fail (prevent overflow)
  ctx1.nonce_counter = UINT64_MAX;
  result = crypto_encrypt(&ctx1, (const uint8_t *)plaintext, plaintext_len, ciphertext, sizeof(ciphertext), &ciphertext_len);
  REQUIRE((result) == (CRYPTO_ERROR_NONCE_EXHAUSTED));
  REQUIRE((ctx1.nonce_counter) == (UINT64_MAX));

  // Verify that counter stays at UINT64_MAX and continues to fail
  result = crypto_encrypt(&ctx1, (const uint8_t *)plaintext, plaintext_len, ciphertext, sizeof(ciphertext), &ciphertext_len);
  REQUIRE((result) == (CRYPTO_ERROR_NONCE_EXHAUSTED));
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

static crypto_error_test_case_t crypto_error_cases[] = {{"NULL context", CRYPTO_ERROR_INVALID_PARAMS, true, false, false, false, false},
                                                        {"NULL plaintext", CRYPTO_ERROR_INVALID_PARAMS, false, true, false, false, false},
                                                        {"Zero length", CRYPTO_ERROR_INVALID_PARAMS, false, false, true, false, false},
                                                        {"NULL ciphertext", CRYPTO_ERROR_INVALID_PARAMS, false, false, false, true, false},
                                                        {"NULL length out", CRYPTO_ERROR_INVALID_PARAMS, false, false, false, false, true}};

TEST_CASE("crypto/error_conditions", "[crypto]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(crypto_error_cases));
  auto *tc = &generated_case;
  crypto_context_t ctx;
  crypto_init_with_password(&ctx, "password");

  const char *plaintext = "test";
  uint8_t ciphertext[1024];
  size_t ciphertext_len;

  crypto_result_t result;

  if (tc->test_null_context) {
    result = crypto_encrypt(NULL, (const uint8_t *)plaintext, strlen(plaintext), ciphertext, sizeof(ciphertext), &ciphertext_len);
  } else if (tc->test_null_plaintext) {
    result = crypto_encrypt(&ctx, NULL, strlen(plaintext), ciphertext, sizeof(ciphertext), &ciphertext_len);
  } else if (tc->test_zero_length) {
    result = crypto_encrypt(&ctx, (const uint8_t *)plaintext, 0, ciphertext, sizeof(ciphertext), &ciphertext_len);
  } else if (tc->test_null_ciphertext) {
    result = crypto_encrypt(&ctx, (const uint8_t *)plaintext, strlen(plaintext), NULL, sizeof(ciphertext), &ciphertext_len);
  } else {
    result = crypto_encrypt(&ctx, (const uint8_t *)plaintext, strlen(plaintext), ciphertext, sizeof(ciphertext), NULL);
  }

  REQUIRE((result) == (tc->expected_result));

  crypto_destroy(&ctx);
}

// Test case structure for crypto initialization tests
typedef struct {
  char description[64];
  char password[128];
  bool should_succeed;
  crypto_result_t expected_result;
} crypto_init_test_case_t;

static crypto_init_test_case_t crypto_init_cases[] = {{"Valid password", "test-password-123", true, CRYPTO_OK},
                                                      {"Empty password", "", false, CRYPTO_ERROR_INVALID_PARAMS},
                                                      {"Long password", "very-long-password-that-is-still-valid", true, CRYPTO_OK},
                                                      {"Special chars password", "p@ssw0rd!@#$%", true, CRYPTO_OK}};

TEST_CASE("crypto/init_conditions", "[crypto]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(crypto_init_cases));
  auto *tc = &generated_case;
  crypto_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  crypto_result_t result = crypto_init_with_password(&ctx, tc->password);

  REQUIRE((result) == (tc->expected_result));

  if (tc->should_succeed) {
    REQUIRE(ctx.initialized);
    REQUIRE(ctx.has_password);
    REQUIRE(crypto_is_ready(&ctx));
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

TEST_CASE("crypto/password_verification_comprehensive", "[crypto]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(crypto_verify_cases));
  auto *tc = &generated_case;
  crypto_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  // Initialize with correct password
  crypto_result_t init_result = crypto_init_with_password(&ctx, tc->correct_password);
  REQUIRE((init_result) == (CRYPTO_OK));

  // Test password verification
  bool verified = crypto_verify_password(&ctx, tc->test_password);
  REQUIRE((verified) == (tc->should_verify));

  crypto_destroy(&ctx);
}

// Test case structure for crypto result string tests
typedef struct {
  crypto_result_t result;
  char expected_string[64];
  char description[64];
} crypto_result_string_test_case_t;

static crypto_result_string_test_case_t crypto_result_string_cases[] = {{CRYPTO_OK, "Success", "Success result"},
                                                                        {CRYPTO_ERROR_INVALID_PARAMS, "Invalid parameters", "Invalid params result"},
                                                                        {CRYPTO_ERROR_KEY_EXCHANGE_INCOMPLETE, "Key exchange not complete", "Key exchange incomplete result"},
                                                                        {CRYPTO_ERROR_BUFFER_TOO_SMALL, "Buffer too small", "Buffer too small result"},
                                                                        {CRYPTO_ERROR_NONCE_EXHAUSTED, "Nonce counter exhausted", "Nonce exhausted result"},
                                                                        {(crypto_result_t)999, "Unknown error", "Unknown result"}};

TEST_CASE("crypto/result_strings", "[crypto]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(crypto_result_string_cases));
  auto *tc = &generated_case;
  const char *result_str = crypto_result_to_string(tc->result);
  REQUIRE(std::strcmp((result_str), (tc->expected_string)) == 0);
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

  REQUIRE(crypto_is_ready(&ctx1));
  REQUIRE(crypto_is_ready(&ctx2));

  // Check initial rekey state
  REQUIRE((ctx1.rekey_packet_count) == (0));
  REQUIRE((ctx1.rekey_count) == (0));
  REQUIRE((ctx1.rekey_in_progress) == (false));
  REQUIRE((ctx1.has_temp_key) == (false));
  REQUIRE((ctx1.rekey_failure_count) == (0));

  // Check default thresholds
  // Note: crypto_init() uses test thresholds when TESTING is set
  // The test environment sets these, so we expect test thresholds, not default thresholds
  REQUIRE((ctx1.rekey_packet_threshold) == (REKEY_TEST_PACKET_THRESHOLD));
  REQUIRE((ctx1.rekey_time_threshold) == (REKEY_TEST_TIME_THRESHOLD));
}

TEST_CASE("crypto/rekey_should_trigger_on_packet_count", "[crypto]") {
  // Initialize with test thresholds
  crypto_init(&ctx1);
  ctx1.rekey_packet_threshold = 100; // Low threshold for testing
  ctx1.rekey_time_threshold = 3600;
  ctx1.key_exchange_complete = true;
  ctx1.handshake_complete = true;

  // Should not trigger initially
  REQUIRE((crypto_should_rekey(&ctx1)) == (false));

  // Simulate packet encryption
  ctx1.rekey_packet_count = 99;
  REQUIRE((crypto_should_rekey(&ctx1)) == (false));

  ctx1.rekey_packet_count = 100;
  REQUIRE((crypto_should_rekey(&ctx1)) == (true));

  ctx1.rekey_packet_count = 101;
  REQUIRE((crypto_should_rekey(&ctx1)) == (true));
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
  REQUIRE((crypto_should_rekey(&ctx1)) == (false));

  ctx1.rekey_last_time = now - 10; // 10 seconds ago
  REQUIRE((crypto_should_rekey(&ctx1)) == (true));

  ctx1.rekey_last_time = now - 15; // 15 seconds ago
  REQUIRE((crypto_should_rekey(&ctx1)) == (true));
}

TEST_CASE("crypto/rekey_should_not_trigger_during_rekey", "[crypto]") {
  crypto_init(&ctx1);
  ctx1.rekey_packet_threshold = 100;
  ctx1.rekey_time_threshold = 3600;
  ctx1.key_exchange_complete = true;
  ctx1.handshake_complete = true;
  ctx1.rekey_packet_count = 200; // Above threshold

  // Should trigger normally
  REQUIRE((crypto_should_rekey(&ctx1)) == (true));

  // But not when rekey is in progress
  ctx1.rekey_in_progress = true;
  REQUIRE((crypto_should_rekey(&ctx1)) == (false));
}

TEST_CASE("crypto/rekey_init_basic", "[crypto]") {
  // Set up context
  crypto_init(&ctx1);
  ctx1.key_exchange_complete = true;
  ctx1.handshake_complete = true;
  ctx1.rekey_last_time = time(NULL) - 100; // 100 seconds ago (past min interval)

  crypto_result_t result = crypto_rekey_init(&ctx1);
  REQUIRE((result) == (CRYPTO_OK));
  REQUIRE((ctx1.rekey_in_progress) == (true));
  REQUIRE((ctx1.has_temp_key) == (true));

  // Check that temp keys are non-zero
  uint8_t zero_key[CRYPTO_PUBLIC_KEY_SIZE] = {0};
  REQUIRE((memcmp(ctx1.temp_public_key, zero_key, CRYPTO_PUBLIC_KEY_SIZE)) != (0));
  REQUIRE((memcmp(ctx1.temp_private_key, zero_key, CRYPTO_PRIVATE_KEY_SIZE)) != (0));
}

TEST_CASE("crypto/rekey_init_rate_limiting", "[crypto]") {
  crypto_init(&ctx1);
  ctx1.key_exchange_complete = true;
  ctx1.handshake_complete = true;

  // Set last rekey time to 1 second ago (less than REKEY_MIN_INTERVAL which is 3 seconds)
  ctx1.rekey_last_time = time(NULL) - 1;

  crypto_result_t result = crypto_rekey_init(&ctx1);
  REQUIRE((result) == (CRYPTO_ERROR_REKEY_RATE_LIMITED));
  REQUIRE((ctx1.rekey_in_progress) == (false));
  REQUIRE((ctx1.has_temp_key) == (false));
}

TEST_CASE("crypto/rekey_init_failure_limit", "[crypto]") {
  crypto_init(&ctx1);
  ctx1.key_exchange_complete = true;
  ctx1.handshake_complete = true;
  ctx1.rekey_last_time = time(NULL) - 100;

  // Set failure count to max
  ctx1.rekey_failure_count = REKEY_MAX_FAILURE_COUNT;

  crypto_result_t result = crypto_rekey_init(&ctx1);
  REQUIRE((result) == (CRYPTO_ERROR_REKEY_FAILED));
  REQUIRE((ctx1.rekey_in_progress) == (false));
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
  REQUIRE((result) == (CRYPTO_OK));

  // Responder (ctx2) processes request
  ctx2.rekey_last_time = time(NULL) - 100;
  result = crypto_rekey_init(&ctx2);
  REQUIRE((result) == (CRYPTO_OK));

  result = crypto_rekey_process_request(&ctx2, ctx1.temp_public_key);
  REQUIRE((result) == (CRYPTO_OK));
  REQUIRE((ctx2.has_temp_key) == (true));

  // Check that temp shared key is non-zero
  uint8_t zero_key[CRYPTO_SHARED_KEY_SIZE] = {0};
  REQUIRE((memcmp(ctx2.temp_shared_key, zero_key, CRYPTO_SHARED_KEY_SIZE)) != (0));
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
  REQUIRE((result) == (CRYPTO_OK));

  // Both should have computed the same shared secret
  REQUIRE((memcmp(ctx1.temp_shared_key, ctx2.temp_shared_key, CRYPTO_SHARED_KEY_SIZE)) == (0));
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
  REQUIRE((result) == (CRYPTO_OK));

  // Check state after commit
  REQUIRE((ctx1.rekey_in_progress) == (false));
  REQUIRE((ctx1.has_temp_key) == (false));
  REQUIRE((ctx1.rekey_packet_count) == (0));
  REQUIRE((ctx1.rekey_count) == (old_rekey_count + 1));
  REQUIRE((ctx1.nonce_counter) == (1));
  REQUIRE((ctx1.rekey_failure_count) == (0));

  // Check that shared key was updated
  REQUIRE((memcmp(ctx1.shared_key, temp_shared_key_copy, CRYPTO_SHARED_KEY_SIZE)) == (0));
  REQUIRE((memcmp(ctx1.shared_key, old_shared_key, CRYPTO_SHARED_KEY_SIZE)) != (0));

  // Check that temp keys are wiped
  uint8_t zero_key[CRYPTO_SHARED_KEY_SIZE] = {0};
  REQUIRE((memcmp(ctx1.temp_shared_key, zero_key, CRYPTO_SHARED_KEY_SIZE)) == (0));
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
  REQUIRE((ctx1.rekey_in_progress) == (true));

  // Abort
  crypto_rekey_abort(&ctx1);

  // Check state after abort
  REQUIRE((ctx1.rekey_in_progress) == (false));
  REQUIRE((ctx1.has_temp_key) == (false));
  REQUIRE((ctx1.rekey_failure_count) == (old_failure_count + 1));

  // Check that shared key is unchanged
  REQUIRE((memcmp(ctx1.shared_key, old_shared_key, CRYPTO_SHARED_KEY_SIZE)) == (0));

  // Check that temp keys are wiped
  uint8_t zero_key[CRYPTO_SHARED_KEY_SIZE] = {0};
  REQUIRE((memcmp(ctx1.temp_shared_key, zero_key, CRYPTO_SHARED_KEY_SIZE)) == (0));
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
  crypto_encrypt(&ctx1, (const uint8_t *)test_data, strlen(test_data), ciphertext_before, sizeof(ciphertext_before), &ciphertext_before_len);

  uint8_t decrypted_before[256];
  size_t decrypted_before_len;
  crypto_decrypt(&ctx2, ciphertext_before, ciphertext_before_len, decrypted_before, sizeof(decrypted_before), &decrypted_before_len);
  REQUIRE((memcmp(decrypted_before, test_data, strlen(test_data))) == (0));

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
  REQUIRE((result) == (CRYPTO_OK));

  // Step 2: Responder (ctx2) processes request and generates response
  result = crypto_rekey_init(&ctx2);
  REQUIRE((result) == (CRYPTO_OK));
  result = crypto_rekey_process_request(&ctx2, ctx1.temp_public_key);
  REQUIRE((result) == (CRYPTO_OK));

  // Step 3: Initiator processes response
  result = crypto_rekey_process_response(&ctx1, ctx2.temp_public_key);
  REQUIRE((result) == (CRYPTO_OK));

  // Both should have same temp shared key
  REQUIRE((memcmp(ctx1.temp_shared_key, ctx2.temp_shared_key, CRYPTO_SHARED_KEY_SIZE)) == (0));

  // Step 4: Both commit to new key
  result = crypto_rekey_commit(&ctx1);
  REQUIRE((result) == (CRYPTO_OK));
  result = crypto_rekey_commit(&ctx2);
  REQUIRE((result) == (CRYPTO_OK));

  // Check that shared keys changed
  REQUIRE((memcmp(ctx1.shared_key, old_shared_key1, CRYPTO_SHARED_KEY_SIZE)) != (0));
  REQUIRE((memcmp(ctx2.shared_key, old_shared_key2, CRYPTO_SHARED_KEY_SIZE)) != (0));

  // Check that both have the same new key
  REQUIRE((memcmp(ctx1.shared_key, ctx2.shared_key, CRYPTO_SHARED_KEY_SIZE)) == (0));

  // Test encryption with new key
  const char *test_data_after = "Hello after rekey";
  uint8_t ciphertext_after[256];
  size_t ciphertext_after_len;
  result = crypto_encrypt(&ctx1, (const uint8_t *)test_data_after, strlen(test_data_after), ciphertext_after, sizeof(ciphertext_after), &ciphertext_after_len);
  REQUIRE((result) == (CRYPTO_OK));

  uint8_t decrypted_after[256];
  size_t decrypted_after_len;
  result = crypto_decrypt(&ctx2, ciphertext_after, ciphertext_after_len, decrypted_after, sizeof(decrypted_after), &decrypted_after_len);
  REQUIRE((result) == (CRYPTO_OK));
  REQUIRE((memcmp(decrypted_after, test_data_after, strlen(test_data_after))) == (0));

  // Verify old ciphertext cannot be decrypted with new key
  uint8_t decrypted_old_with_new[256];
  size_t decrypted_old_with_new_len;
  result = crypto_decrypt(&ctx2, ciphertext_before, ciphertext_before_len, decrypted_old_with_new, sizeof(decrypted_old_with_new), &decrypted_old_with_new_len);
  REQUIRE((result) != (CRYPTO_OK));
}

TEST_CASE("crypto/rekey_packet_counter_increments", "[crypto]") {
  // Initialize context
  crypto_init(&ctx1);
  ctx1.key_exchange_complete = true;
  ctx1.handshake_complete = true;

  REQUIRE((ctx1.rekey_packet_count) == (0));

  // Encrypt some data
  const char *data = "test data";
  uint8_t ciphertext[256];
  size_t ciphertext_len;

  for (int i = 0; i < 10; i++) {
    crypto_encrypt(&ctx1, (const uint8_t *)data, strlen(data), ciphertext, sizeof(ciphertext), &ciphertext_len);
  }

  REQUIRE((ctx1.rekey_packet_count) == (10));
}
