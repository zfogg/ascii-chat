#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <stdint.h>

typedef struct client_connection client_connection_t;

/* Borrowed by application callbacks; published only on the controlling thread.
 * Workers are joined before the binding changes. Signal handlers never use it. */
client_connection_t *client_connection_current(void);
asciichat_error_t client_connection_create(client_connection_t **out);
asciichat_error_t client_connection_start(client_connection_t *connection, const char *address, uint16_t port);
void client_connection_request_stop(client_connection_t *connection);
asciichat_error_t client_connection_stop(client_connection_t *connection);
asciichat_error_t client_connection_destroy(client_connection_t **connection);
asciichat_error_t client_connection_checkpoint(const char *stage);
