/** Browser-facing C interface implemented in peer_crypto.c. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <ascii-chat/asciichat_errno.h>

typedef struct browser_peer_crypto browser_peer_crypto_t;
typedef struct crypto_context_t crypto_context_t;

#ifdef __cplusplus
extern "C" {
#endif

browser_peer_crypto_t *peer_crypto_create(bool server, int encryption, const char *password, const uint8_t *identity,
                                          const uint8_t *expected_key);
asciichat_error_t peer_crypto_start(browser_peer_crypto_t *peer);
void peer_crypto_destroy(browser_peer_crypto_t *peer);
bool peer_crypto_ready(browser_peer_crypto_t *peer);
const char *peer_crypto_error(void);
asciichat_error_t peer_crypto_send(browser_peer_crypto_t *peer, const void *data, size_t length);
asciichat_error_t peer_crypto_receive(browser_peer_crypto_t *peer, const void *data, size_t length);
const void *peer_crypto_output_data(browser_peer_crypto_t *peer);
size_t peer_crypto_output_size(browser_peer_crypto_t *peer);
void peer_crypto_output_pop(browser_peer_crypto_t *peer);
const void *peer_crypto_received_data(browser_peer_crypto_t *peer);
size_t peer_crypto_received_size(browser_peer_crypto_t *peer);
crypto_context_t *peer_security_context(browser_peer_crypto_t *peer);
asciichat_error_t peer_crypto_tick(browser_peer_crypto_t *peer);
asciichat_error_t peer_crypto_quic_begin(browser_peer_crypto_t *peer);

#ifdef __cplusplus
}
#endif
