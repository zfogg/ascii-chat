#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>

#include "crypto.h"
#include "tests/common.h"

void setup_crypto_network_quiet_logging(void);
void restore_crypto_network_logging(void);

TestSuite(crypto_network_integration, .init = setup_crypto_network_quiet_logging,
          .fini = restore_crypto_network_logging);

void setup_crypto_network_quiet_logging(void) {
  // Set log level to FATAL to suppress all INFO/DEBUG messages
  log_set_level(LOG_FATAL);
}

void restore_crypto_network_logging(void) {
  log_set_level(LOG_DEBUG);
}

// =============================================================================
// End-to-End Crypto Integration Tests
// =============================================================================

Test(crypto_network_integration, full_handshake_simulation) {
  crypto_context_t client_ctx, server_ctx;

  // Initialize both contexts (simulating client and server startup)
  crypto_result_t result = crypto_init(&client_ctx);
  cr_assert_eq(result, CRYPTO_OK, "Client crypto initialization should succeed");

  result = crypto_init(&server_ctx);
  cr_assert_eq(result, CRYPTO_OK, "Server crypto initialization should succeed");

  // Simulate network handshake
  uint8_t client_packet[1024];
  uint8_t server_packet[1024];
  size_t client_packet_len, server_packet_len;

  // Step 1: Client sends public key to server
  result = crypto_create_public_key_packet(&client_ctx, client_packet, sizeof(client_packet), &client_packet_len);
  cr_assert_eq(result, CRYPTO_OK, "Client public key packet creation should succeed");

  // Step 2: Server receives and processes client's public key
  result = crypto_process_public_key_packet(&server_ctx, client_packet, client_packet_len);
  cr_assert_eq(result, CRYPTO_OK, "Server should process client public key successfully");
  cr_assert(server_ctx.key_exchange_complete, "Server key exchange should be complete");

  // Step 3: Server sends its public key to client
  result = crypto_create_public_key_packet(&server_ctx, server_packet, sizeof(server_packet), &server_packet_len);
  cr_assert_eq(result, CRYPTO_OK, "Server public key packet creation should succeed");

  // Step 4: Client receives and processes server's public key
  result = crypto_process_public_key_packet(&client_ctx, server_packet, server_packet_len);
  cr_assert_eq(result, CRYPTO_OK, "Client should process server public key successfully");
  cr_assert(client_ctx.key_exchange_complete, "Client key exchange should be complete");

  // Both parties should now be ready for encrypted communication
  cr_assert(crypto_is_ready(&client_ctx), "Client should be ready for encrypted communication");
  cr_assert(crypto_is_ready(&server_ctx), "Server should be ready for encrypted communication");

  // Cleanup
  crypto_cleanup(&client_ctx);
  crypto_cleanup(&server_ctx);
}

Test(crypto_network_integration, bidirectional_encrypted_communication) {
  crypto_context_t alice_ctx, bob_ctx;

  // Set up key exchange (Alice and Bob)
  crypto_init(&alice_ctx);
  crypto_init(&bob_ctx);

  uint8_t alice_pubkey[CRYPTO_PUBLIC_KEY_SIZE];
  uint8_t bob_pubkey[CRYPTO_PUBLIC_KEY_SIZE];

  crypto_get_public_key(&alice_ctx, alice_pubkey);
  crypto_get_public_key(&bob_ctx, bob_pubkey);

  crypto_set_peer_public_key(&alice_ctx, bob_pubkey);
  crypto_set_peer_public_key(&bob_ctx, alice_pubkey);

  // Alice sends message to Bob
  const char *alice_message = "Hello Bob, this is Alice! How are you doing today?";
  uint8_t alice_encrypted_packet[1024];
  size_t alice_packet_len;

  crypto_result_t result =
      crypto_create_encrypted_packet(&alice_ctx, (const uint8_t *)alice_message, strlen(alice_message),
                                     alice_encrypted_packet, sizeof(alice_encrypted_packet), &alice_packet_len);
  cr_assert_eq(result, CRYPTO_OK, "Alice should encrypt message successfully");

  // Bob receives and decrypts Alice's message
  uint8_t bob_decrypted[1024];
  size_t bob_decrypted_len;

  result = crypto_process_encrypted_packet(&bob_ctx, alice_encrypted_packet, alice_packet_len, bob_decrypted,
                                           sizeof(bob_decrypted), &bob_decrypted_len);
  cr_assert_eq(result, CRYPTO_OK, "Bob should decrypt Alice's message successfully");
  cr_assert_eq(bob_decrypted_len, strlen(alice_message), "Decrypted length should match");
  cr_assert_eq(memcmp(bob_decrypted, alice_message, bob_decrypted_len), 0,
               "Bob should receive Alice's message correctly");

  // Bob sends reply to Alice
  const char *bob_message = "Hi Alice! I'm doing great, thanks for asking. How about you?";
  uint8_t bob_encrypted_packet[1024];
  size_t bob_packet_len;

  result = crypto_create_encrypted_packet(&bob_ctx, (const uint8_t *)bob_message, strlen(bob_message),
                                          bob_encrypted_packet, sizeof(bob_encrypted_packet), &bob_packet_len);
  cr_assert_eq(result, CRYPTO_OK, "Bob should encrypt reply successfully");

  // Alice receives and decrypts Bob's reply
  uint8_t alice_decrypted[1024];
  size_t alice_decrypted_len;

  result = crypto_process_encrypted_packet(&alice_ctx, bob_encrypted_packet, bob_packet_len, alice_decrypted,
                                           sizeof(alice_decrypted), &alice_decrypted_len);
  cr_assert_eq(result, CRYPTO_OK, "Alice should decrypt Bob's reply successfully");
  cr_assert_eq(alice_decrypted_len, strlen(bob_message), "Decrypted length should match");
  cr_assert_eq(memcmp(alice_decrypted, bob_message, alice_decrypted_len), 0,
               "Alice should receive Bob's reply correctly");

  crypto_cleanup(&alice_ctx);
  crypto_cleanup(&bob_ctx);
}

Test(crypto_network_integration, password_vs_key_exchange_priority) {
  crypto_context_t ctx;

  // Initialize with password first
  const char *password = "shared-secret-password";
  crypto_result_t result = crypto_init_with_password(&ctx, password);
  cr_assert_eq(result, CRYPTO_OK, "Password initialization should succeed");
  cr_assert(crypto_is_ready(&ctx), "Should be ready with password");

  // Now set up key exchange as well
  crypto_context_t peer_ctx;
  crypto_init(&peer_ctx);

  uint8_t ctx_pubkey[CRYPTO_PUBLIC_KEY_SIZE];
  uint8_t peer_pubkey[CRYPTO_PUBLIC_KEY_SIZE];

  crypto_get_public_key(&ctx, ctx_pubkey);
  crypto_get_public_key(&peer_ctx, peer_pubkey);

  crypto_set_peer_public_key(&ctx, peer_pubkey);
  crypto_set_peer_public_key(&peer_ctx, ctx_pubkey);

  // Both password and key exchange are now available
  cr_assert(ctx.has_password, "Should still have password");
  cr_assert(ctx.key_exchange_complete, "Should have completed key exchange");
  cr_assert(crypto_is_ready(&ctx), "Should be ready");

  // Test that encryption uses shared key (higher priority than password)
  const char *test_message = "Testing priority: shared key should be used";
  uint8_t ciphertext[1024];
  size_t ciphertext_len;

  result = crypto_encrypt(&ctx, (const uint8_t *)test_message, strlen(test_message), ciphertext, sizeof(ciphertext),
                          &ciphertext_len);
  cr_assert_eq(result, CRYPTO_OK, "Encryption should succeed");

  // Peer should be able to decrypt (proving shared key was used)
  uint8_t decrypted[1024];
  size_t decrypted_len;

  result = crypto_decrypt(&peer_ctx, ciphertext, ciphertext_len, decrypted, sizeof(decrypted), &decrypted_len);
  cr_assert_eq(result, CRYPTO_OK, "Peer decryption should succeed");
  cr_assert_eq(memcmp(decrypted, test_message, strlen(test_message)), 0,
               "Message should decrypt correctly with shared key");

  crypto_cleanup(&ctx);
  crypto_cleanup(&peer_ctx);
}

Test(crypto_network_integration, multiple_messages_same_session) {
  crypto_context_t client_ctx, server_ctx;

  // Set up session
  crypto_init(&client_ctx);
  crypto_init(&server_ctx);

  uint8_t client_pubkey[CRYPTO_PUBLIC_KEY_SIZE];
  uint8_t server_pubkey[CRYPTO_PUBLIC_KEY_SIZE];

  crypto_get_public_key(&client_ctx, client_pubkey);
  crypto_get_public_key(&server_ctx, server_pubkey);
  crypto_set_peer_public_key(&client_ctx, server_pubkey);
  crypto_set_peer_public_key(&server_ctx, client_pubkey);

  // Send multiple messages in sequence
  const char *messages[] = {"Message 1: Connection established", "Message 2: Sending video data",
                            "Message 3: Audio stream active", "Message 4: Client count update",
                            "Message 5: Session closing"};

  for (int i = 0; i < 5; i++) {
    // Client to server
    uint8_t encrypted_packet[1024];
    size_t packet_len;
    crypto_result_t result =
        crypto_create_encrypted_packet(&client_ctx, (const uint8_t *)messages[i], strlen(messages[i]), encrypted_packet,
                                       sizeof(encrypted_packet), &packet_len);
    cr_assert_eq(result, CRYPTO_OK, "Message %d encryption should succeed", i + 1);

    uint8_t decrypted[1024];
    size_t decrypted_len;
    result = crypto_process_encrypted_packet(&server_ctx, encrypted_packet, packet_len, decrypted, sizeof(decrypted),
                                             &decrypted_len);
    cr_assert_eq(result, CRYPTO_OK, "Message %d decryption should succeed", i + 1);
    cr_assert_eq(memcmp(decrypted, messages[i], strlen(messages[i])), 0, "Message %d content should match", i + 1);

    // Server acknowledgment back to client
    char ack_message[256];
    snprintf(ack_message, sizeof(ack_message), "ACK: Received message %d", i + 1);

    result = crypto_create_encrypted_packet(&server_ctx, (const uint8_t *)ack_message, strlen(ack_message),
                                            encrypted_packet, sizeof(encrypted_packet), &packet_len);
    cr_assert_eq(result, CRYPTO_OK, "ACK %d encryption should succeed", i + 1);

    result = crypto_process_encrypted_packet(&client_ctx, encrypted_packet, packet_len, decrypted, sizeof(decrypted),
                                             &decrypted_len);
    cr_assert_eq(result, CRYPTO_OK, "ACK %d decryption should succeed", i + 1);
    cr_assert_eq(memcmp(decrypted, ack_message, strlen(ack_message)), 0, "ACK %d content should match", i + 1);
  }

  crypto_cleanup(&client_ctx);
  crypto_cleanup(&server_ctx);
}

Test(crypto_network_integration, large_message_handling) {
  crypto_context_t ctx1, ctx2;

  // Set up key exchange
  crypto_init(&ctx1);
  crypto_init(&ctx2);

  uint8_t pubkey1[CRYPTO_PUBLIC_KEY_SIZE];
  uint8_t pubkey2[CRYPTO_PUBLIC_KEY_SIZE];

  crypto_get_public_key(&ctx1, pubkey1);
  crypto_get_public_key(&ctx2, pubkey2);
  crypto_set_peer_public_key(&ctx1, pubkey2);
  crypto_set_peer_public_key(&ctx2, pubkey1);

  // Create large message (simulating video frame data)
  size_t large_message_size = 64 * 1024; // 64KB
  uint8_t *large_message;
  SAFE_MALLOC(large_message, large_message_size, uint8_t *);

  // Fill with pattern
  for (size_t i = 0; i < large_message_size; i++) {
    large_message[i] = (uint8_t)(i % 256);
  }

  // Encrypt large message
  uint8_t *encrypted_packet;
  SAFE_MALLOC(encrypted_packet, large_message_size + 1024, uint8_t *); // Extra space for crypto overhead
  size_t packet_len;

  crypto_result_t result = crypto_create_encrypted_packet(&ctx1, large_message, large_message_size, encrypted_packet,
                                                          large_message_size + 1024, &packet_len);
  cr_assert_eq(result, CRYPTO_OK, "Large message encryption should succeed");
  cr_assert_gt(packet_len, large_message_size, "Encrypted packet should be larger than plaintext");

  // Decrypt large message
  uint8_t *decrypted_message;
  SAFE_MALLOC(decrypted_message, large_message_size, uint8_t *);
  size_t decrypted_len;

  result = crypto_process_encrypted_packet(&ctx2, encrypted_packet, packet_len, decrypted_message, large_message_size,
                                           &decrypted_len);
  cr_assert_eq(result, CRYPTO_OK, "Large message decryption should succeed");
  cr_assert_eq(decrypted_len, large_message_size, "Decrypted size should match original");
  cr_assert_eq(memcmp(decrypted_message, large_message, large_message_size), 0,
               "Decrypted content should match original");

  free(large_message);
  free(encrypted_packet);
  free(decrypted_message);
  crypto_cleanup(&ctx1);
  crypto_cleanup(&ctx2);
}

Test(crypto_network_integration, error_handling_integration) {
  crypto_context_t ctx1, ctx2;

  crypto_init(&ctx1);
  crypto_init(&ctx2);

  // Test various error conditions in integrated context

  // 1. Try to encrypt before key exchange
  const char *test_msg = "test";
  uint8_t packet[1024];
  size_t packet_len;

  crypto_result_t result = crypto_create_encrypted_packet(&ctx1, (const uint8_t *)test_msg, strlen(test_msg), packet,
                                                          sizeof(packet), &packet_len);
  cr_assert_eq(result, CRYPTO_ERROR_KEY_EXCHANGE_INCOMPLETE, "Should fail without key exchange");

  // 2. Complete key exchange
  uint8_t pubkey1[CRYPTO_PUBLIC_KEY_SIZE];
  uint8_t pubkey2[CRYPTO_PUBLIC_KEY_SIZE];

  crypto_get_public_key(&ctx1, pubkey1);
  crypto_get_public_key(&ctx2, pubkey2);
  crypto_set_peer_public_key(&ctx1, pubkey2);
  crypto_set_peer_public_key(&ctx2, pubkey1);

  // 3. Now encryption should work
  result = crypto_create_encrypted_packet(&ctx1, (const uint8_t *)test_msg, strlen(test_msg), packet, sizeof(packet),
                                          &packet_len);
  cr_assert_eq(result, CRYPTO_OK, "Should succeed after key exchange");

  // 4. Test malformed packet decryption
  uint8_t malformed_packet[] = {0x01, 0x02, 0x03, 0x04}; // Too small
  uint8_t decrypted[1024];
  size_t decrypted_len;

  result = crypto_process_encrypted_packet(&ctx2, malformed_packet, sizeof(malformed_packet), decrypted,
                                           sizeof(decrypted), &decrypted_len);
  cr_assert_neq(result, CRYPTO_OK, "Malformed packet should fail to decrypt");

  crypto_cleanup(&ctx1);
  crypto_cleanup(&ctx2);
}

Test(crypto_network_integration, session_cleanup_and_restart) {
  crypto_context_t ctx1, ctx2;

  // First session
  crypto_init(&ctx1);
  crypto_init(&ctx2);

  uint8_t pubkey1[CRYPTO_PUBLIC_KEY_SIZE];
  uint8_t pubkey2[CRYPTO_PUBLIC_KEY_SIZE];

  crypto_get_public_key(&ctx1, pubkey1);
  crypto_get_public_key(&ctx2, pubkey2);
  crypto_set_peer_public_key(&ctx1, pubkey2);
  crypto_set_peer_public_key(&ctx2, pubkey1);

  // Test communication
  const char *msg1 = "First session message";
  uint8_t packet[1024];
  size_t packet_len;

  crypto_result_t result =
      crypto_create_encrypted_packet(&ctx1, (const uint8_t *)msg1, strlen(msg1), packet, sizeof(packet), &packet_len);
  cr_assert_eq(result, CRYPTO_OK, "First session encryption should work");

  // Clean up first session
  crypto_cleanup(&ctx1);
  crypto_cleanup(&ctx2);

  // Start new session (simulating reconnection)
  crypto_init(&ctx1);
  crypto_init(&ctx2);

  // New key exchange
  crypto_get_public_key(&ctx1, pubkey1);
  crypto_get_public_key(&ctx2, pubkey2);
  crypto_set_peer_public_key(&ctx1, pubkey2);
  crypto_set_peer_public_key(&ctx2, pubkey1);

  // Test communication with new session
  const char *msg2 = "Second session message";
  result =
      crypto_create_encrypted_packet(&ctx1, (const uint8_t *)msg2, strlen(msg2), packet, sizeof(packet), &packet_len);
  cr_assert_eq(result, CRYPTO_OK, "Second session encryption should work");

  uint8_t decrypted[1024];
  size_t decrypted_len;
  result = crypto_process_encrypted_packet(&ctx2, packet, packet_len, decrypted, sizeof(decrypted), &decrypted_len);
  cr_assert_eq(result, CRYPTO_OK, "Second session decryption should work");
  cr_assert_eq(memcmp(decrypted, msg2, strlen(msg2)), 0, "Second session message should match");

  crypto_cleanup(&ctx1);
  crypto_cleanup(&ctx2);
}
