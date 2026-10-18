#pragma once

#include "connection.h"
#include <stdbool.h>

/* Start protocol workers for this attempt. The connection owner handles all
 * teardown, including a failure after only some workers have started. */
int protocol_start_connection(client_connection_t *connection);
bool protocol_connection_lost(void);
