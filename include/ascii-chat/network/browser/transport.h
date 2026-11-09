#pragma once

#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/acip/security.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct browser_transport browser_transport_t;
typedef void (*browser_transport_output_callback_t)(const void *data, size_t length, void *user_data);

typedef enum browser_transport_state {
  BROWSER_TRANSPORT_DISCONNECTED = 0,
  BROWSER_TRANSPORT_CONNECTING = 1,
  BROWSER_TRANSPORT_HANDSHAKE = 2,
  BROWSER_TRANSPORT_CONNECTED = 3,
  BROWSER_TRANSPORT_ERROR = 4
} browser_transport_state_t;

browser_transport_t *browser_transport_create(void);
void browser_transport_destroy(browser_transport_t *browser);
/** Update the state reported by this transport and by acip_transport_is_connected(). */
void browser_transport_set_state(browser_transport_t *browser, browser_transport_state_t state);
browser_transport_state_t browser_transport_state(const browser_transport_t *browser);
void browser_transport_set_output_callback(browser_transport_t *browser, browser_transport_output_callback_t callback,
                                            void *user_data);
/** Convenience for browser event bridges that only have an open/closed signal. */
void browser_transport_set_connected(browser_transport_t *browser, bool connected);
bool browser_transport_is_connected(browser_transport_t *browser);
size_t browser_transport_queued_bytes(const browser_transport_t *browser);
bool browser_transport_writable(const browser_transport_t *browser, size_t additional_bytes);
acip_transport_t *browser_transport_base(browser_transport_t *browser);
void *browser_transport_client_state(browser_transport_t *browser);
void browser_transport_set_client_state(browser_transport_t *browser, void *state);
const void *browser_transport_output_data(browser_transport_t *browser);
size_t browser_transport_output_size(browser_transport_t *browser);
void browser_transport_output_pop(browser_transport_t *browser);
asciichat_error_t browser_transport_security_init(browser_transport_t *browser);
asciichat_error_t browser_transport_security_packet(browser_transport_t *browser, bool seal, uint32_t channel,
                                                     uint32_t delivery, const uint8_t *packet, size_t length,
                                                     uint8_t *output, size_t capacity, size_t *out_length,
                                                     uint64_t *sequence);
asciichat_error_t browser_transport_security_tick(browser_transport_t *browser);
asciichat_error_t browser_transport_security_output(browser_transport_t *browser, uint8_t *output, size_t capacity,
                                                    size_t *length, uint32_t *channel);
double browser_transport_security_epoch(browser_transport_t *browser);

#ifdef __cplusplus
}
#endif
