/* Check that ping telemetry reserves its zero and overlapping-ping sentinels. */
#include "client/connection_internal.h"
#include <ascii-chat/atomic.h>

static client_connection_t connection;

int main(void) {
  client_connection_mark_ping_sent(&connection, 0);
  if (atomic_load_u64(&connection.telemetry_ping_ns) != 1)
    return 1;
  client_connection_mark_ping_sent(&connection, 0);
  if (atomic_load_u64(&connection.telemetry_ping_ns) != UINT64_MAX)
    return 2;
  atomic_store_u64(&connection.telemetry_ping_ns, 0);
  client_connection_mark_ping_sent(&connection, 42);
  return atomic_load_u64(&connection.telemetry_ping_ns) == 42 ? 0 : 3;
}
