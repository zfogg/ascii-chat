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
/* For an endpoint accept callback already holding lane. The returned adapter
 * uses lane for later calls; no adapter method may be called until it is
 * released. */
asciichat_error_t acip_quic_transport_attach_on_lane(acip_quic_connection_t *connection, crypto_context_t *crypto,
                                                     mutex_t *lane, acip_transport_t **out);
asciichat_error_t acip_quic_transport_attach_on_lane_with_waker(acip_quic_connection_t *connection, mutex_t *lane,
                                                                void (*wake)(void *), void *context,
                                                                void (*on_destroy)(acip_transport_t *, void *),
                                                                void *destroy_context, acip_transport_t **out);
/* Report an endpoint-reported connection failure after releasing the lane
 * mutex. Pending receives wake and return the supplied error. Once this
 * returns, the endpoint owner may remove the detached connection on the lane. */
asciichat_error_t acip_quic_transport_fail(acip_transport_t *transport, asciichat_error_t reason);
/* The endpoint owner already holds the adapter's lane. */
asciichat_error_t acip_quic_transport_fail_on_lane(acip_transport_t *transport, asciichat_error_t reason);
/* Only for an unclaimed adapter that has never been exposed to a caller. */
asciichat_error_t acip_quic_transport_destroy_unclaimed_on_lane(acip_transport_t **transport, asciichat_error_t reason);
asciichat_error_t acip_quic_transport_set_waker_on_lane(acip_transport_t *transport, void (*wake)(void *),
                                                        void *context);
/* Called on the lane before a locally initiated close detaches its receiver. */
void acip_quic_transport_set_close_on_lane(acip_transport_t *transport, void (*close)(acip_quic_connection_t *, void *),
                                           void *context);
/* Configure while the transport is live. Called under the lane lock after a
 * packet is admitted; the callback must not block or reenter transport APIs.
 * Clear it and stop producers before destroying its context. */
asciichat_error_t acip_quic_transport_set_waker(acip_transport_t *transport, void (*wake)(void *), void *context);
/* After the local ACIP handshake, begin the authenticated readiness exchange.
 * The borrowed crypto context must outlive the connection. */
asciichat_error_t acip_quic_transport_begin_security(acip_transport_t *transport, crypto_context_t *crypto);
bool acip_quic_transport_security_ready(acip_transport_t *transport);
asciichat_error_t acip_quic_transport_allow_media(acip_transport_t *transport);
