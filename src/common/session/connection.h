#pragma once
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/network/acip/transport.h>

typedef enum {
  SESSION_CONNECTION_STARTING,
  SESSION_CONNECTION_RUNNING,
  SESSION_CONNECTION_STOPPING,
  SESSION_CONNECTION_STOPPED,
} session_connection_state_t;

/* The enclosing owner supplies its resources. Interrupt must not free them;
 * join must retain every dependency on failure; release runs only after success.
 * Only request_stop may run outside the controlling thread. Callers must retain
 * the owner until request_stop callers and all workers have quiesced. */
typedef struct {
  void (*interrupt)(void *context);
  asciichat_error_t (*join)(void *context);
  void (*release)(void *context);
} session_connection_ops_t;

typedef struct {
  atomic_t state;
  thread_id_t owner_thread;
  session_connection_ops_t ops;
  void *context;
} session_connection_t;

void session_connection_init(session_connection_t *connection, session_connection_ops_t ops, void *context);
asciichat_error_t session_connection_start(session_connection_t *connection);
void session_connection_request_stop(session_connection_t *connection);
asciichat_error_t session_connection_stop(session_connection_t *connection);
/* Normal-thread interruption; retains the transport and its callback context. */
void session_connection_interrupt_transport(acip_transport_t *transport);
