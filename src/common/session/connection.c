#include "session/connection.h"

void session_connection_init(session_connection_t *connection, session_connection_ops_t ops, void *context) {
  connection->owner_thread = asciichat_thread_self();
  connection->ops = ops;
  connection->context = context;
  atomic_store_int(&connection->state, SESSION_CONNECTION_STARTING);
}

asciichat_error_t session_connection_start(session_connection_t *connection) {
  if (!connection || !asciichat_thread_equal(connection->owner_thread, asciichat_thread_self()))
    return SET_ERRNO(ERROR_INVALID_STATE, "Connection startup requires its controlling thread");
  int expected = SESSION_CONNECTION_STARTING;
  if (!atomic_cas_int(&connection->state, &expected, SESSION_CONNECTION_RUNNING))
    return SET_ERRNO(ERROR_INVALID_STATE, "Connection was stopped during startup");
  return ASCIICHAT_OK;
}

void session_connection_request_stop(session_connection_t *connection) {
  if (!connection || atomic_load_int(&connection->state) == SESSION_CONNECTION_STOPPED)
    return;
  atomic_store_int(&connection->state, SESSION_CONNECTION_STOPPING);
  if (connection->ops.interrupt)
    connection->ops.interrupt(connection->context);
}

asciichat_error_t session_connection_stop(session_connection_t *connection) {
  if (!connection || atomic_load_int(&connection->state) == SESSION_CONNECTION_STOPPED)
    return ASCIICHAT_OK;
  session_connection_request_stop(connection);
  if (!asciichat_thread_equal(connection->owner_thread, asciichat_thread_self()))
    return SET_ERRNO(ERROR_INVALID_STATE, "Connection cleanup requires its controlling thread");
  if (connection->ops.join) {
    asciichat_error_t result = connection->ops.join(connection->context);
    if (result != ASCIICHAT_OK)
      return result;
  }
  if (connection->ops.release)
    connection->ops.release(connection->context);
  atomic_store_int(&connection->state, SESSION_CONNECTION_STOPPED);
  return ASCIICHAT_OK;
}

void session_connection_interrupt_transport(acip_transport_t *transport) {
  acip_transport_interrupt(transport);
}
