/**
 * @file test_gpg_authentication.c
 * @brief Integration tests for GPG authentication
 */

#include <criterion/criterion.h>
#include <criterion/logging.h>
#include <string.h>
#include <stdlib.h>
#include "../../lib/crypto/gpg.h"
#include "../../lib/crypto/keys/keys.h"
#include "../../lib/crypto/keys/gpg_keys.h"

// Test GPG key ID (user must have this key in their keyring)
#define TEST_GPG_KEY_ID "7FE90A79F2E80ED3"

Test(gpg_authentication, sign_message_with_gpg_key) {
    const char *message = "Authentication challenge nonce 123456";
    uint8_t signature[512];
    size_t signature_len = 0;

    int result = gpg_sign_with_key(TEST_GPG_KEY_ID, 
                                   (const uint8_t *)message, strlen(message),
                                   signature, &signature_len);

    cr_assert_eq(result, 0, "gpg_sign_with_key should succeed");
    cr_assert_gt(signature_len, 0, "Signature length should be > 0");
    cr_assert_lt(signature_len, 512, "Signature length should be < 512");
    
    // Ed25519 OpenPGP signatures are typically ~119 bytes
    cr_log_info("Signature created: %zu bytes", signature_len);
}

Test(gpg_authentication, verify_valid_signature) {
    const char *message = "Authentication challenge nonce 123456";
    uint8_t signature[512];
    size_t signature_len = 0;

    // First create a signature
    int sign_result = gpg_sign_with_key(TEST_GPG_KEY_ID,
                                        (const uint8_t *)message, strlen(message),
                                        signature, &signature_len);
    cr_assert_eq(sign_result, 0, "Signing should succeed");

    // Now verify it
    int verify_result = gpg_verify_signature_with_binary(signature, signature_len,
                                                         (const uint8_t *)message, strlen(message),
                                                         TEST_GPG_KEY_ID);

    cr_assert_eq(verify_result, 0, "Verification should succeed for valid signature");
}

Test(gpg_authentication, reject_tampered_message) {
    const char *message = "Authentication challenge nonce 123456";
    const char *tampered = "TAMPERED challenge nonce 123456";
    uint8_t signature[512];
    size_t signature_len = 0;

    // Create signature for original message
    int sign_result = gpg_sign_with_key(TEST_GPG_KEY_ID,
                                        (const uint8_t *)message, strlen(message),
                                        signature, &signature_len);
    cr_assert_eq(sign_result, 0, "Signing should succeed");

    // Try to verify with tampered message
    int verify_result = gpg_verify_signature_with_binary(signature, signature_len,
                                                         (const uint8_t *)tampered, strlen(tampered),
                                                         TEST_GPG_KEY_ID);

    cr_assert_neq(verify_result, 0, "Verification should fail for tampered message");
}

Test(gpg_authentication, parse_gpg_public_key) {
    char key_input[64];
    snprintf(key_input, sizeof(key_input), "gpg:%s", TEST_GPG_KEY_ID);

    public_key_t public_key;
    memset(&public_key, 0, sizeof(public_key));

    asciichat_error_t result = parse_public_key(key_input, &public_key);

    cr_assert_eq(result, ASCIICHAT_OK, "parse_public_key should succeed for gpg:KEYID format");
    cr_assert_eq(public_key.type, KEY_TYPE_GPG, "Key type should be KEY_TYPE_GPG");
    
    // Verify we got a valid 32-byte public key
    bool all_zeros = true;
    for (int i = 0; i < 32; i++) {
        if (public_key.key[i] != 0) {
            all_zeros = false;
            break;
        }
    }
    cr_assert_eq(all_zeros, false, "Public key should not be all zeros");
}

Test(gpg_authentication, parse_gpg_private_key) {
    char key_input[64];
    snprintf(key_input, sizeof(key_input), "gpg:%s", TEST_GPG_KEY_ID);

    private_key_t private_key;
    memset(&private_key, 0, sizeof(private_key));

    asciichat_error_t result = parse_private_key(key_input, &private_key);

    cr_assert_eq(result, ASCIICHAT_OK, "parse_private_key should succeed for gpg:KEYID format");
    cr_assert_eq(private_key.use_gpg_agent, true, "use_gpg_agent flag should be set");
    cr_assert_eq(strlen(private_key.gpg_keygrip), 40, "Keygrip should be 40 characters");
    
    cr_log_info("Keygrip: %s", private_key.gpg_keygrip);
}

Test(gpg_authentication, get_public_key_from_keyring) {
    uint8_t public_key[32];
    char keygrip[64];

    int result = gpg_get_public_key(TEST_GPG_KEY_ID, public_key, keygrip);

    cr_assert_eq(result, 0, "gpg_get_public_key should succeed");
    cr_assert_eq(strlen(keygrip), 40, "Keygrip should be 40 characters");
    
    // Verify we got a valid public key (not all zeros)
    bool all_zeros = true;
    for (int i = 0; i < 32; i++) {
        if (public_key[i] != 0) {
            all_zeros = false;
            break;
        }
    }
    cr_assert_eq(all_zeros, false, "Public key should not be all zeros");
    
    cr_log_info("Public key (first 16 bytes): ");
    for (int i = 0; i < 16; i++) {
        cr_log_info("%02x", public_key[i]);
    }
}
