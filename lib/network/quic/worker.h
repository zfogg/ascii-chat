#pragma once

#include "udp.h"
#include <libplatform/mutex.h>

/* Private endpoint worker. Run at most one worker for an endpoint. The endpoint, lane mutex, and callback context
 * remain owned by the caller and outlive the stopped worker. An accept callback
 * runs under the lane lock and must not block or call locking adapter methods.
 * A failure callback runs without that lock and must detach and remove the
 * failed connection before returning true; false stops the worker. */
typedef struct quic_worker quic_worker_t;
typedef bool (*quic_worker_failure_fn)(quic_connection_t *, asciichat_error_t, void *);

asciichat_error_t quic_worker_start(quic_udp_endpoint_t *endpoint, mutex_t *lane, quic_worker_failure_fn failure,
                                    void *context, quic_worker_t **out);
/* Variant: failure callback runs on the held endpoint lane and must not lock it. */
asciichat_error_t quic_worker_start_locked_failure(quic_udp_endpoint_t *endpoint, mutex_t *lane,
                                                   quic_worker_failure_fn failure, void *context, quic_worker_t **out);
/* Thread-safe while the worker and endpoint remain alive. */
void quic_worker_wake(void *worker);
/* Stops and joins the worker, then frees it. Returns its terminal error. */
asciichat_error_t quic_worker_stop(quic_worker_t **worker);
