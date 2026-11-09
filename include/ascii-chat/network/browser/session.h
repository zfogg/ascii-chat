/** Browser-facing ACIP session built on the shared transport and security code. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <ascii-chat/asciichat_errno.h>

typedef struct browser_session browser_session_t;
typedef struct crypto_context_t crypto_context_t;

#ifdef __cplusplus
extern "C" {
#endif

browser_session_t *browser_session_create(bool server, int encryption, const char *password, const uint8_t *identity,
                                          const uint8_t *expected_key);
asciichat_error_t browser_session_start(browser_session_t *peer);
void browser_session_set_connected(browser_session_t *peer, bool connected);
void browser_session_destroy(browser_session_t *peer);
bool browser_session_ready(browser_session_t *peer);
size_t browser_session_queued_bytes(const browser_session_t *peer);
bool browser_session_writable(const browser_session_t *peer, size_t additional_bytes);
const char *browser_session_error(void);
asciichat_error_t browser_session_send(browser_session_t *peer, const void *data, size_t length);
asciichat_error_t browser_session_receive(browser_session_t *peer, const void *data, size_t length);
const void *browser_session_output_data(browser_session_t *peer);
size_t browser_session_output_size(browser_session_t *peer);
void browser_session_output_pop(browser_session_t *peer);
const void *browser_session_received_data(browser_session_t *peer);
size_t browser_session_received_size(browser_session_t *peer);
crypto_context_t *browser_session_security_context(browser_session_t *peer);
asciichat_error_t browser_session_tick(browser_session_t *peer);
asciichat_error_t browser_session_quic_begin(browser_session_t *peer);

#ifdef __cplusplus
}
#endif
