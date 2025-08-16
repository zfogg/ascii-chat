#include "encryption.h"
#include "aes_hw.h"
#include "options.h"
#include "common.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// Global encryption context
static aes_context_t g_aes_context;
static bool g_encryption_initialized = false;

// Get encryption key from all three sources (priority order: --key, --keyfile, env var)
static int get_encryption_passphrase(char *passphrase_out, size_t max_len) {
  // Priority 1: Command-line --key option
  if (strlen(opt_encrypt_key) > 0) {
    snprintf(passphrase_out, max_len, "%s", opt_encrypt_key);
    log_info("Using encryption key from --key argument");
    return 0;
  }

  // Priority 2: Key file from --keyfile option
  if (strlen(opt_encrypt_keyfile) > 0) {
    FILE *keyfile = fopen(opt_encrypt_keyfile, "r");
    if (!keyfile) {
      log_error("Failed to open key file: %s", opt_encrypt_keyfile);
      return -1;
    }

    if (fgets(passphrase_out, max_len, keyfile) == NULL) {
      log_error("Failed to read key from file: %s", opt_encrypt_keyfile);
      fclose(keyfile);
      return -1;
    }

    fclose(keyfile);

    // Remove trailing newline if present
    size_t len = strlen(passphrase_out);
    if (len > 0 && passphrase_out[len - 1] == '\n') {
      passphrase_out[len - 1] = '\0';
    }

    log_info("Using encryption key from keyfile: %s", opt_encrypt_keyfile);
    return 0;
  }

  // Priority 3: Environment variable ASCII_CHAT_KEY
  const char *env_key = getenv("ASCII_CHAT_KEY");
  if (env_key && strlen(env_key) > 0) {
    snprintf(passphrase_out, max_len, "%s", env_key);
    log_info("Using encryption key from ASCII_CHAT_KEY environment variable");
    return 0;
  }

  log_error("No encryption key provided. Use --key, --keyfile, or ASCII_CHAT_KEY");
  return -1;
}

// Initialize encryption system
int encryption_init(void) {
  if (!opt_encrypt_enabled) {
    return 0; // Encryption disabled
  }

  if (g_encryption_initialized) {
    return 0; // Already initialized
  }

  char passphrase[512];
  if (get_encryption_passphrase(passphrase, sizeof(passphrase)) != 0) {
    return -1;
  }

  if (strlen(passphrase) < 8) {
    log_error("Encryption passphrase too short (minimum 8 characters)");
    memset(passphrase, 0, sizeof(passphrase)); // Clear passphrase from memory
    return -1;
  }

  if (aes_init_context(&g_aes_context, passphrase) != 0) {
    log_error("Failed to initialize AES encryption context");
    memset(passphrase, 0, sizeof(passphrase)); // Clear passphrase from memory
    return -1;
  }

  // Clear passphrase from memory for security
  memset(passphrase, 0, sizeof(passphrase));

  g_encryption_initialized = true;
  log_info("Encryption initialized successfully");

  return 0;
}

// Check if encryption is enabled and initialized
bool encryption_is_enabled(void) {
  return opt_encrypt_enabled && g_encryption_initialized;
}

// Get key verification hash for handshake
uint32_t encryption_get_key_hash(void) {
  if (!encryption_is_enabled()) {
    return 0;
  }

  return aes_key_verification_hash(g_aes_context.key);
}

// Encrypt packet data
int encryption_encrypt_packet(const uint8_t *plaintext, size_t plaintext_len, uint8_t **ciphertext_out,
                              size_t *ciphertext_len_out, uint8_t **iv_out) {
  if (!encryption_is_enabled()) {
    return -1;
  }

  // Allocate memory for ciphertext and IV
  *ciphertext_out = malloc(plaintext_len);
  *iv_out = malloc(AES_IV_SIZE);

  if (!*ciphertext_out || !*iv_out) {
    free(*ciphertext_out);
    free(*iv_out);
    return -1;
  }

  if (aes_encrypt(&g_aes_context, plaintext, plaintext_len, *ciphertext_out, *iv_out) != 0) {
    free(*ciphertext_out);
    free(*iv_out);
    return -1;
  }

  *ciphertext_len_out = plaintext_len;
  return 0;
}

// Decrypt packet data
int encryption_decrypt_packet(const uint8_t *ciphertext, size_t ciphertext_len, const uint8_t *iv,
                              uint8_t **plaintext_out, size_t *plaintext_len_out) {
  if (!encryption_is_enabled()) {
    return -1;
  }

  // Allocate memory for plaintext
  *plaintext_out = malloc(ciphertext_len);
  if (!*plaintext_out) {
    return -1;
  }

  if (aes_decrypt(&g_aes_context, ciphertext, ciphertext_len, *plaintext_out, iv) != 0) {
    free(*plaintext_out);
    return -1;
  }

  *plaintext_len_out = ciphertext_len;
  return 0;
}

// Cleanup encryption system
void encryption_cleanup(void) {
  if (g_encryption_initialized) {
    // Clear encryption context from memory
    memset(&g_aes_context, 0, sizeof(g_aes_context));
    g_encryption_initialized = false;
    log_debug("Encryption context cleaned up");
  }
}