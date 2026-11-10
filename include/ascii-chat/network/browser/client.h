/** Browser-facing C interface implemented in client.c. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/network/packet/packet.h>

typedef struct browser_transport browser_transport_t;

#ifdef __cplusplus
extern "C" {
#endif

int browser_client_init(browser_transport_t *transport);
/** True only after application protocol negotiation and authentication complete. */
bool browser_client_ready(browser_transport_t *transport);
/** Clear protocol readiness when the underlying connection is replaced or closed. */
void browser_client_reset_handshake(browser_transport_t *transport);
int browser_client_set_server_address(browser_transport_t *transport, const char *server_host, int server_port);
const uint8_t *browser_client_public_key_bytes(browser_transport_t *transport);
asciichat_error_t browser_client_handshake_receive(browser_transport_t *transport, uint32_t type, const void *payload,
                                                   size_t length);
asciichat_error_t browser_client_acds_handshake_receive(browser_transport_t *transport, uint32_t type,
                                                        const void *payload, size_t length);
int browser_client_encrypt_packet(browser_transport_t *transport, const uint8_t *plaintext, size_t plaintext_len,
                                  uint8_t *ciphertext, size_t ciphertext_size, size_t *out_len);
int browser_client_decrypt_packet(browser_transport_t *transport, const uint8_t *ciphertext, size_t ciphertext_len,
                                  uint8_t *plaintext, size_t plaintext_size, size_t *out_len);
typedef struct {
  /** Wire header in network byte order, valid when handshake is false. */
  packet_header_t header;
  bool encrypted;
  bool handshake;
  size_t payload_length;
} browser_client_packet_result_t;

/** Encode and, if requested, encrypt one message into caller-owned output. */
asciichat_error_t browser_client_send_packet(browser_transport_t *transport, uint32_t type, uint32_t client_id,
                                             bool encrypt, const uint8_t *payload, size_t payload_length,
                                             uint8_t *output, size_t output_capacity, size_t *output_length);
/** Validate and route a handshake or copy a decoded message into caller-owned payload. */
asciichat_error_t browser_client_receive_packet(browser_transport_t *transport, bool discovery, const uint8_t *input,
                                                size_t input_length, uint8_t *payload, size_t payload_capacity,
                                                browser_client_packet_result_t *result);
asciichat_error_t browser_client_settings_receive(browser_transport_t *transport, const void *payload, size_t length);
char *browser_client_settings_values(browser_transport_t *transport);
asciichat_error_t browser_client_validate_frame(browser_transport_t *transport, const char *text, size_t length,
                                                uint32_t width, uint32_t height, uint32_t checksum, uint32_t flags,
                                                const void *metadata, size_t metadata_length);
asciichat_error_t browser_client_decode_frame_data(const void *encoded, size_t encoded_length, void *output,
                                                   size_t output_length);
int browser_client_parse_ssh_private_key(const char *key_path, uint8_t *secret_key_out);
int browser_client_parse_ssh_public_key(const char *key_line, uint8_t *public_key_out);
int browser_client_parse_gpg_private_key(const char *armored_key, uint8_t *secret_key_out);
int browser_client_parse_gpg_public_key(const char *armored_key, uint8_t *public_key_out);
int browser_client_parse_gpg_private_key_binary(const uint8_t *key_data, size_t key_data_len, uint8_t *secret_key_out);
int browser_client_parse_gpg_public_key_binary(const uint8_t *key_data, size_t key_data_len, uint8_t *public_key_out);
int browser_client_get_crypto_error_message(char *output, size_t output_size);
int browser_client_configure_crypto_options(browser_transport_t *transport, const char *password,
                                            const uint8_t *identity_secret_key,
                                            const uint8_t *expected_server_public_key);

void browser_client_cleanup(browser_transport_t *transport);

int browser_client_generate_keypair(browser_transport_t *transport);

int browser_client_resolve_crypto_policy(browser_transport_t *transport, bool secure, int encryption);

asciichat_error_t browser_client_handshake_begin(browser_transport_t *transport);

asciichat_error_t browser_client_acds_handshake_begin(browser_transport_t *transport, uint8_t mode);

#ifdef __cplusplus
}
#endif
