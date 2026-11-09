/** Browser-facing C interface implemented in client.c. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <ascii-chat/asciichat_errno.h>

typedef struct browser_transport browser_transport_t;

#ifdef __cplusplus
extern "C" {
#endif

int client_init(browser_transport_t *transport);
int client_set_server_address(browser_transport_t *transport, const char *server_host, int server_port);
const uint8_t *client_public_key_bytes(browser_transport_t *transport);
asciichat_error_t client_handshake_receive(browser_transport_t *transport, uint32_t type, const void *payload,
                                           size_t length);
asciichat_error_t client_acds_handshake_receive(browser_transport_t *transport, uint32_t type, const void *payload,
                                                size_t length);
int client_encrypt_packet(browser_transport_t *transport, const uint8_t *plaintext, size_t plaintext_len, uint8_t *ciphertext, size_t ciphertext_size,
                          size_t *out_len);
int client_decrypt_packet(browser_transport_t *transport, const uint8_t *ciphertext, size_t ciphertext_len, uint8_t *plaintext, size_t plaintext_size,
                          size_t *out_len);
asciichat_error_t client_settings_receive(browser_transport_t *transport, const void *payload, size_t length);
char *client_settings_values(browser_transport_t *transport);
asciichat_error_t client_validate_frame(browser_transport_t *transport, const char *text, size_t length, uint32_t width, uint32_t height,
                                        uint32_t checksum, uint32_t flags, const void *metadata,
                                        size_t metadata_length);
asciichat_error_t client_decode_frame_data(const void *encoded, size_t encoded_length, void *output,
                                           size_t output_length);
int client_parse_ssh_private_key(const char *key_path, uint8_t *secret_key_out);
int client_parse_ssh_public_key(const char *key_line, uint8_t *public_key_out);
int client_parse_gpg_private_key(const char *armored_key, uint8_t *secret_key_out);
int client_parse_gpg_public_key(const char *armored_key, uint8_t *public_key_out);
int client_parse_gpg_private_key_binary(const uint8_t *key_data, size_t key_data_len, uint8_t *secret_key_out);
int client_parse_gpg_public_key_binary(const uint8_t *key_data, size_t key_data_len, uint8_t *public_key_out);
int client_get_crypto_error_message(char *output, size_t output_size);
int client_configure_crypto_options(browser_transport_t *transport, const char *password, const uint8_t *identity_secret_key,
                                    const uint8_t *expected_server_public_key);

void client_cleanup(browser_transport_t *transport);

int client_generate_keypair(browser_transport_t *transport);

int client_get_connection_state(const browser_transport_t *transport);

int client_resolve_crypto_policy(browser_transport_t *transport, bool secure, int encryption);

asciichat_error_t client_handshake_begin(browser_transport_t *transport);

asciichat_error_t client_acds_handshake_begin(browser_transport_t *transport, uint8_t mode);

#ifdef __cplusplus
}
#endif
