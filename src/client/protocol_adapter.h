#pragma once

#include "connection.h"
#include <stdbool.h>

/* Start protocol workers for this attempt. The connection owner handles all
 * teardown, including a failure after only some workers have started. */
asciichat_error_t client_protocol_start(client_connection_t *connection);
bool client_protocol_connection_lost(client_connection_t *connection);
