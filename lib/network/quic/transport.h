#pragma once

#include "connection.h"
#include <ascii-chat/network/acip/transport.h>

/* Private single-lane bridge. The connection and its crypto context outlive
 * the returned transport. All operations, endpoint pumping, and destruction
 * must run on the same lane. recv is nonblocking and returns
 * ERROR_NETWORK_TIMEOUT when no complete packet is queued. The caller must
 * destroy the transport before destroying the connection. */
asciichat_error_t acip_quic_transport_attach(acip_quic_connection_t *connection, crypto_context_t *crypto,
                                             acip_transport_t **out);
/* As above, but adapter operations lock the borrowed lane mutex. The endpoint
 * owner must hold that same mutex for every operation on the connection,
 * including UDP pump, security transitions, and removal. The mutex outlives
 * the transport; do not call attach while already holding it. recv waits up to
 * receive_timeout_ns (one second if zero), and interrupt wakes receivers.
 * Stop and join all transport users before destroying the transport. */
asciichat_error_t acip_quic_transport_attach_serialized(acip_quic_connection_t *connection, crypto_context_t *crypto,
                                                        mutex_t *lane, acip_transport_t **out);
