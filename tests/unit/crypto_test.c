#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>

#include "common.h"
#include "crypto.h"

// Test fixture setup and teardown
static crypto_context_t ctx1, ctx2;

void setup_crypto_context(void) {
    memset(&ctx1, 0, sizeof(ctx1));
    memset(&ctx2, 0, sizeof(ctx2));
    // Set log level to reduce noise during tests
    log_set_level(LOG_FATAL);
}

void teardown_crypto_context(void) {
    crypto_cleanup(&ctx1);
    crypto_cleanup(&ctx2);
    // Restore normal log level
    log_set_level(LOG_DEBUG);
}

TestSuite(crypto, .init = setup_crypto_context, .fini = teardown_crypto_context);

// =============================================================================
// Basic Initialization Tests
// =============================================================================

Test(crypto, init_basic) {
    crypto_result_t result = crypto_init(&ctx1);
    cr_assert_eq(result, CRYPTO_OK, "Basic crypto initialization should succeed");
    cr_assert(ctx1.initialized, "Context should be marked as initialized");
    cr_assert(ctx1.has_password == false, "Should not have password initially");
    cr_assert(ctx1.key_exchange_complete == false, "Key exchange should not be complete initially");
    cr_assert(crypto_is_ready(&ctx1) == false, "Should not be ready without key exchange or password");
}

Test(crypto, init_with_password) {
    const char *password = "test-password-123";
    crypto_result_t result = crypto_init_with_password(&ctx1, password);
    
    cr_assert_eq(result, CRYPTO_OK, "Password-based initialization should succeed");
    cr_assert(ctx1.initialized, "Context should be initialized");
    cr_assert(ctx1.has_password, "Should have password set");
    cr_assert(crypto_is_ready(&ctx1), "Should be ready with password");
}

Test(crypto, init_invalid_params) {
    crypto_result_t result = crypto_init(NULL);
    cr_assert_eq(result, CRYPTO_ERROR_INVALID_PARAMS, "NULL context should fail");
    
    result = crypto_init_with_password(NULL, "password");
    cr_assert_eq(result, CRYPTO_ERROR_INVALID_PARAMS, "NULL context with password should fail");
    
    result = crypto_init_with_password(&ctx1, NULL);
    cr_assert_eq(result, CRYPTO_ERROR_INVALID_PARAMS, "NULL password should fail");
    
    result = crypto_init_with_password(&ctx1, "");
    cr_assert_eq(result, CRYPTO_ERROR_INVALID_PARAMS, "Empty password should fail");
}

// =============================================================================
// Key Exchange Tests
// =============================================================================

Test(crypto, key_exchange_flow) {
    // Initialize both contexts
    crypto_result_t result = crypto_init(&ctx1);
    cr_assert_eq(result, CRYPTO_OK, "First context init should succeed");
    
    result = crypto_init(&ctx2);
    cr_assert_eq(result, CRYPTO_OK, "Second context init should succeed");
    
    // Get public keys
    uint8_t pub_key1[CRYPTO_PUBLIC_KEY_SIZE];
    uint8_t pub_key2[CRYPTO_PUBLIC_KEY_SIZE];
    
    result = crypto_get_public_key(&ctx1, pub_key1);
    cr_assert_eq(result, CRYPTO_OK, "Getting public key 1 should succeed");
    
    result = crypto_get_public_key(&ctx2, pub_key2);
    cr_assert_eq(result, CRYPTO_OK, "Getting public key 2 should succeed");
    
    // Keys should be different
    cr_assert_neq(memcmp(pub_key1, pub_key2, CRYPTO_PUBLIC_KEY_SIZE), 0, 
                  "Public keys should be different");
    
    // Exchange keys
    result = crypto_set_peer_public_key(&ctx1, pub_key2);
    cr_assert_eq(result, CRYPTO_OK, "Setting peer key 1 should succeed");
    cr_assert(ctx1.key_exchange_complete, "Key exchange should be complete for ctx1");
    cr_assert(crypto_is_ready(&ctx1), "ctx1 should be ready after key exchange");
    
    result = crypto_set_peer_public_key(&ctx2, pub_key1);
    cr_assert_eq(result, CRYPTO_OK, "Setting peer key 2 should succeed");
    cr_assert(ctx2.key_exchange_complete, "Key exchange should be complete for ctx2");
    cr_assert(crypto_is_ready(&ctx2), "ctx2 should be ready after key exchange");
}

Test(crypto, public_key_invalid_params) {
    uint8_t pub_key[CRYPTO_PUBLIC_KEY_SIZE];
    
    crypto_result_t result = crypto_get_public_key(NULL, pub_key);
    cr_assert_eq(result, CRYPTO_ERROR_INVALID_PARAMS, "NULL context should fail");
    
    result = crypto_get_public_key(&ctx1, NULL);
    cr_assert_eq(result, CRYPTO_ERROR_INVALID_PARAMS, "NULL output buffer should fail");
    
    result = crypto_set_peer_public_key(NULL, pub_key);
    cr_assert_eq(result, CRYPTO_ERROR_INVALID_PARAMS, "NULL context should fail");
    
    result = crypto_set_peer_public_key(&ctx1, NULL);
    cr_assert_eq(result, CRYPTO_ERROR_INVALID_PARAMS, "NULL peer key should fail");
}

// =============================================================================
// Password Verification Tests
// =============================================================================

Test(crypto, password_verification) {
    const char *correct_password = "my-secure-password-123";
    const char *wrong_password = "wrong-password-456";
    
    crypto_result_t result = crypto_init_with_password(&ctx1, correct_password);
    cr_assert_eq(result, CRYPTO_OK, "Init with password should succeed");
    
    // Test correct password
    bool verified = crypto_verify_password(&ctx1, correct_password);
    cr_assert(verified, "Correct password should verify");
    
    // Test wrong password
    verified = crypto_verify_password(&ctx1, wrong_password);
    cr_assert(verified == false, "Wrong password should not verify");
    
    // Test empty password
    verified = crypto_verify_password(&ctx1, "");
    cr_assert(verified == false, "Empty password should not verify");
}

// =============================================================================
// Encryption/Decryption Tests
// =============================================================================

Test(crypto, encrypt_decrypt_password_based) {
    const char *password = "test-encryption-password";
    const char *plaintext = "Hello, Criterion! This is a test message for crypto testing.";
    size_t plaintext_len = strlen(plaintext);
    
    crypto_result_t result = crypto_init_with_password(&ctx1, password);
    cr_assert_eq(result, CRYPTO_OK, "Password init should succeed");
    
    // Encrypt
    uint8_t ciphertext[1024];
    size_t ciphertext_len;
    result = crypto_encrypt(&ctx1, (const uint8_t*)plaintext, plaintext_len,
                           ciphertext, sizeof(ciphertext), &ciphertext_len);
    cr_assert_eq(result, CRYPTO_OK, "Encryption should succeed");
    cr_assert_gt(ciphertext_len, plaintext_len, "Ciphertext should be larger (includes nonce + MAC)");
    
    // Decrypt
    uint8_t decrypted[1024];
    size_t decrypted_len;
    result = crypto_decrypt(&ctx1, ciphertext, ciphertext_len,
                           decrypted, sizeof(decrypted), &decrypted_len);
    cr_assert_eq(result, CRYPTO_OK, "Decryption should succeed");
    cr_assert_eq(decrypted_len, plaintext_len, "Decrypted length should match plaintext");
    cr_assert_eq(memcmp(decrypted, plaintext, plaintext_len), 0, "Decrypted text should match plaintext");
}

Test(crypto, encrypt_decrypt_key_exchange) {
    const char *plaintext = "Key exchange encryption test message";
    size_t plaintext_len = strlen(plaintext);
    
    // Set up key exchange
    crypto_init(&ctx1);
    crypto_init(&ctx2);
    
    uint8_t pub_key1[CRYPTO_PUBLIC_KEY_SIZE];
    uint8_t pub_key2[CRYPTO_PUBLIC_KEY_SIZE];
    
    crypto_get_public_key(&ctx1, pub_key1);
    crypto_get_public_key(&ctx2, pub_key2);
    
    crypto_set_peer_public_key(&ctx1, pub_key2);
    crypto_set_peer_public_key(&ctx2, pub_key1);
    
    // Encrypt with ctx1
    uint8_t ciphertext[1024];
    size_t ciphertext_len;
    crypto_result_t result = crypto_encrypt(&ctx1, (const uint8_t*)plaintext, plaintext_len,
                                           ciphertext, sizeof(ciphertext), &ciphertext_len);
    cr_assert_eq(result, CRYPTO_OK, "Encryption should succeed");
    
    // Decrypt with ctx2
    uint8_t decrypted[1024];
    size_t decrypted_len;
    result = crypto_decrypt(&ctx2, ciphertext, ciphertext_len,
                           decrypted, sizeof(decrypted), &decrypted_len);
    cr_assert_eq(result, CRYPTO_OK, "Decryption should succeed");
    cr_assert_eq(decrypted_len, plaintext_len, "Decrypted length should match");
    cr_assert_eq(memcmp(decrypted, plaintext, plaintext_len), 0, "Decrypted should match plaintext");
}

Test(crypto, encrypt_not_ready) {
    const char *plaintext = "test";
    uint8_t ciphertext[1024];
    size_t ciphertext_len;
    
    crypto_result_t result = crypto_init(&ctx1);
    cr_assert_eq(result, CRYPTO_OK, "Init should succeed");
    
    // Try to encrypt before key exchange or password
    result = crypto_encrypt(&ctx1, (const uint8_t*)plaintext, strlen(plaintext),
                           ciphertext, sizeof(ciphertext), &ciphertext_len);
    cr_assert_eq(result, CRYPTO_ERROR_KEY_EXCHANGE_INCOMPLETE, 
                 "Encryption should fail when not ready");
}

Test(crypto, encrypt_invalid_params) {
    crypto_init_with_password(&ctx1, "password");
    
    const char *plaintext = "test";
    uint8_t ciphertext[1024];
    size_t ciphertext_len;
    
    // Test NULL parameters
    crypto_result_t result = crypto_encrypt(NULL, (const uint8_t*)plaintext, strlen(plaintext),
                                           ciphertext, sizeof(ciphertext), &ciphertext_len);
    cr_assert_eq(result, CRYPTO_ERROR_INVALID_PARAMS, "NULL context should fail");
    
    result = crypto_encrypt(&ctx1, NULL, strlen(plaintext),
                           ciphertext, sizeof(ciphertext), &ciphertext_len);
    cr_assert_eq(result, CRYPTO_ERROR_INVALID_PARAMS, "NULL plaintext should fail");
    
    result = crypto_encrypt(&ctx1, (const uint8_t*)plaintext, 0,
                           ciphertext, sizeof(ciphertext), &ciphertext_len);
    cr_assert_eq(result, CRYPTO_ERROR_INVALID_PARAMS, "Zero length should fail");
    
    result = crypto_encrypt(&ctx1, (const uint8_t*)plaintext, strlen(plaintext),
                           NULL, sizeof(ciphertext), &ciphertext_len);
    cr_assert_eq(result, CRYPTO_ERROR_INVALID_PARAMS, "NULL ciphertext should fail");
    
    result = crypto_encrypt(&ctx1, (const uint8_t*)plaintext, strlen(plaintext),
                           ciphertext, sizeof(ciphertext), NULL);
    cr_assert_eq(result, CRYPTO_ERROR_INVALID_PARAMS, "NULL length out should fail");
}

Test(crypto, decrypt_invalid_data) {
    crypto_init_with_password(&ctx1, "password");
    
    uint8_t invalid_ciphertext[] = {0x01, 0x02, 0x03}; // Too small
    uint8_t plaintext[1024];
    size_t plaintext_len;
    
    crypto_result_t result = crypto_decrypt(&ctx1, invalid_ciphertext, sizeof(invalid_ciphertext),
                                           plaintext, sizeof(plaintext), &plaintext_len);
    cr_assert_eq(result, CRYPTO_ERROR_INVALID_PARAMS, "Too small ciphertext should fail");
}

// =============================================================================
// Network Packet Tests
// =============================================================================

Test(crypto, public_key_packet) {
    crypto_init(&ctx1);
    
    uint8_t packet[1024];
    size_t packet_len;
    
    // Create packet
    crypto_result_t result = crypto_create_public_key_packet(&ctx1, packet, sizeof(packet), &packet_len);
    cr_assert_eq(result, CRYPTO_OK, "Creating public key packet should succeed");
    cr_assert_eq(packet_len, sizeof(uint32_t) + CRYPTO_PUBLIC_KEY_SIZE, "Packet size should be correct");
    
    // Process packet
    crypto_init(&ctx2);
    result = crypto_process_public_key_packet(&ctx2, packet, packet_len);
    cr_assert_eq(result, CRYPTO_OK, "Processing public key packet should succeed");
    cr_assert(ctx2.peer_key_received, "Peer key should be received");
    cr_assert(ctx2.key_exchange_complete, "Key exchange should be complete");
}

Test(crypto, encrypted_data_packet) {
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
    crypto_result_t result = crypto_create_encrypted_packet(&ctx1, (const uint8_t*)test_data, test_data_len,
                                                           packet, sizeof(packet), &packet_len);
    cr_assert_eq(result, CRYPTO_OK, "Creating encrypted packet should succeed");
    
    // Process encrypted packet
    uint8_t decrypted_data[1024];
    size_t decrypted_len;
    result = crypto_process_encrypted_packet(&ctx2, packet, packet_len,
                                            decrypted_data, sizeof(decrypted_data), &decrypted_len);
    cr_assert_eq(result, CRYPTO_OK, "Processing encrypted packet should succeed");
    cr_assert_eq(decrypted_len, test_data_len, "Decrypted length should match");
    cr_assert_eq(memcmp(decrypted_data, test_data, test_data_len), 0, "Decrypted data should match");
}

// =============================================================================
// Utility Function Tests
// =============================================================================

Test(crypto, random_bytes) {
    uint8_t random1[32];
    uint8_t random2[32];
    
    crypto_result_t result1 = crypto_random_bytes(random1, sizeof(random1));
    crypto_result_t result2 = crypto_random_bytes(random2, sizeof(random2));
    
    cr_assert_eq(result1, CRYPTO_OK, "First random bytes should succeed");
    cr_assert_eq(result2, CRYPTO_OK, "Second random bytes should succeed");
    
    // Very unlikely to be the same (cryptographically secure random)
    cr_assert_neq(memcmp(random1, random2, sizeof(random1)), 0, 
                  "Random bytes should be different");
}

Test(crypto, secure_compare) {
    uint8_t data1[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t data2[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t data3[] = {0x01, 0x02, 0x03, 0x05};
    
    cr_assert(crypto_secure_compare(data1, data2, sizeof(data1)), "Identical data should match");
    cr_assert(crypto_secure_compare(data1, data3, sizeof(data1)) == false, "Different data should not match");
    cr_assert(crypto_secure_compare(NULL, data2, sizeof(data1)) == false, "NULL data should not match");
    cr_assert(crypto_secure_compare(data1, NULL, sizeof(data1)) == false, "NULL data should not match");
}

Test(crypto, result_to_string) {
    const char *ok_str = crypto_result_to_string(CRYPTO_OK);
    cr_assert_str_eq(ok_str, "Success", "CRYPTO_OK should return 'Success'");
    
    const char *invalid_str = crypto_result_to_string(CRYPTO_ERROR_INVALID_PARAMS);
    cr_assert_str_eq(invalid_str, "Invalid parameters", "Should return correct error message");
    
    const char *unknown_str = crypto_result_to_string((crypto_result_t)999);
    cr_assert_str_eq(unknown_str, "Unknown error", "Unknown code should return 'Unknown error'");
}

Test(crypto, get_status) {
    char status[512];
    
    // Uninitialized context
    crypto_get_status(&ctx1, status, sizeof(status));
    cr_assert_str_eq(status, "Not initialized", "Uninitialized context should report not initialized");
    
    // Initialized context with password
    crypto_init_with_password(&ctx1, "test-password");
    crypto_get_status(&ctx1, status, sizeof(status));
    cr_assert_str_neq(status, "Not initialized", "Initialized context should not say not initialized");
    cr_assert(strstr(status, "Password: yes") != NULL, "Should show password is set");
    cr_assert(strstr(status, "Ready: yes") != NULL, "Should show ready status");
}

// =============================================================================
// Edge Case and Security Tests
// =============================================================================

Test(crypto, nonce_uniqueness) {
    crypto_init_with_password(&ctx1, "password");
    
    const char *plaintext = "test message";
    size_t plaintext_len = strlen(plaintext);
    
    uint8_t ciphertext1[1024], ciphertext2[1024];
    size_t ciphertext1_len, ciphertext2_len;
    
    // Encrypt the same message twice
    crypto_encrypt(&ctx1, (const uint8_t*)plaintext, plaintext_len,
                  ciphertext1, sizeof(ciphertext1), &ciphertext1_len);
    crypto_encrypt(&ctx1, (const uint8_t*)plaintext, plaintext_len,
                  ciphertext2, sizeof(ciphertext2), &ciphertext2_len);
    
    // Ciphertexts should be different (different nonces)
    cr_assert_neq(memcmp(ciphertext1, ciphertext2, 
                        ciphertext1_len < ciphertext2_len ? ciphertext1_len : ciphertext2_len), 0,
                  "Same plaintext should produce different ciphertexts (different nonces)");
}

Test(crypto, buffer_size_checks) {
    crypto_init_with_password(&ctx1, "password");
    
    const char *plaintext = "test message for buffer size testing";
    size_t plaintext_len = strlen(plaintext);
    
    uint8_t small_buffer[10]; // Too small
    size_t output_len;
    
    crypto_result_t result = crypto_encrypt(&ctx1, (const uint8_t*)plaintext, plaintext_len,
                                           small_buffer, sizeof(small_buffer), &output_len);
    cr_assert_eq(result, CRYPTO_ERROR_BUFFER_TOO_SMALL, "Small buffer should fail");
}

Test(crypto, cleanup_security) {
    const char *password = "secret-password-123";
    
    crypto_init_with_password(&ctx1, password);
    
    // Verify context has data
    cr_assert(ctx1.initialized, "Context should be initialized");
    cr_assert(ctx1.has_password, "Context should have password");
    
    // Cleanup
    crypto_cleanup(&ctx1);
    
    // Verify context is cleared (basic check - real implementation uses sodium_memzero)
    cr_assert(ctx1.initialized == false, "Context should not be initialized after cleanup");
    cr_assert(ctx1.has_password == false, "Context should not have password after cleanup");
}