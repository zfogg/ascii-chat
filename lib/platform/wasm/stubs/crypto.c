/**
 * @file stubs/crypto.c
 * @brief Stub implementations for crypto-related functions in WASM build
 */

#include <ascii-chat/common.h>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/crypto/keys.h>
#include <ascii-chat/network/packet.h>
#include <ascii-chat/network/acip/transport.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Ed25519 signature stubs (not needed for basic handshake)
asciichat_error_t ed25519_verify_signature(const uint8_t public_key[32], const uint8_t *message, size_t message_len,
                                           const uint8_t signature[64], const char *gpg_key_id) {
  // Server identity verification not supported in WASM (no known_hosts file)
  return ASCIICHAT_OK;
}

asciichat_error_t ed25519_sign_message(const private_key_t *key, const uint8_t *message, size_t message_len,
                                       uint8_t signature[64]) {
  // Client authentication not supported in WASM (no client keys)
  return SET_ERRNO(ERROR_NOT_SUPPORTED, "Ed25519 signing not supported in WASM");
}

// Public key parsing stubs
asciichat_error_t parse_public_keys(const char *input, public_key_t *keys_out, size_t *num_keys, size_t max_keys) {
  // Key file parsing not supported in WASM
  return SET_ERRNO(ERROR_NOT_SUPPORTED, "Public key file parsing not supported in WASM");
}

// Known hosts stubs (no filesystem in WASM)
asciichat_error_t check_known_host(const char *server_ip, uint16_t port, const uint8_t server_key[32]) {
  // Known hosts checking not supported in WASM
  return ASCIICHAT_OK;
}

asciichat_error_t check_known_host_no_identity(const char *server_ip, uint16_t port) {
  // Known hosts checking not supported in WASM
  return ASCIICHAT_OK;
}

bool display_mitm_warning(const char *server_ip, uint16_t port, const uint8_t expected_key[32],
                          const uint8_t received_key[32]) {
  // MITM warnings not displayed in WASM (browser console log instead)
  return false;
}

bool prompt_unknown_host(const char *server_ip, uint16_t port, const uint8_t server_key[32]) {
  // No interactive prompts in WASM - auto-accept
  return true;
}

bool prompt_unknown_host_no_identity(const char *server_ip, uint16_t port) {
  // No interactive prompts in WASM - auto-accept
  return true;
}

asciichat_error_t add_known_host(const char *server_ip, uint16_t port, const uint8_t server_key[32]) {
  // Known hosts file updates not supported in WASM
  return ASCIICHAT_OK;
}

const char *get_known_hosts_path(void) {
  // No filesystem in WASM
  return NULL;
}

// Password prompt stub
asciichat_error_t prompt_password(const char *prompt_text, char *password_out, size_t password_max_len) {
  // Password prompts not supported in WASM
  return SET_ERRNO(ERROR_NOT_SUPPORTED, "Password prompts not supported in WASM");
}
