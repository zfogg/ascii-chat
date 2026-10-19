#pragma once
#include <ascii-chat/platform/event.h>
#include <ascii-chat/network/acip/transport.h>

/** Outbound ACIP adapter. Incoming packets are delivered by the connection's
 * decoder. Destroy after the stream close callback, when workers have drained.
 */
acip_transport_t *acip_async_tcp_transport_create(platform_event_stream_t *, crypto_context_t *);
void acip_async_tcp_transport_detach(acip_transport_t *);
bool acip_async_tcp_transport_is(const acip_transport_t *);
size_t acip_async_tcp_transport_queued(acip_transport_t *);
/** Native client adapter. Owns a loop thread; recv waits on a bounded packet queue.
 * Takes socket ownership when *socket becomes INVALID_SOCKET_VALUE.
 */
acip_transport_t *acip_async_tcp_client_create(socket_t *, crypto_context_t *);
/** Atomic-only cancellation request, suitable for the native signal path. */
