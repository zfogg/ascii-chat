#pragma once

#include <ascii-chat/network/acip/transport.h>

/** Native connection admission. Backends own listening and stream I/O; callers
 * receive complete ACIP transports. Browser/negotiated transports can be supplied
 * directly to the same session registration API without opening a listener.
 */
typedef struct acip_listener acip_listener_t;

asciichat_error_t acip_listener_open(const char *address, uint16_t port, size_t capacity, acip_listener_t **out);
/** Nonblocking. A successful call with *transport == NULL means no new peer.
 * A returned transport is owned by the caller. It must be destroyed before the
 * listener itself, and may be retained after stop() has closed its connection.
 */
asciichat_error_t acip_listener_accept(acip_listener_t *listener, acip_transport_t **transport, char *address,
                                       size_t address_size, int *port);
uint16_t acip_listener_port(const acip_listener_t *listener);
/** Stop admission and stream I/O, joining the backend thread. Idempotent. */
asciichat_error_t acip_listener_stop(acip_listener_t *listener);
void acip_listener_destroy(acip_listener_t *listener);
