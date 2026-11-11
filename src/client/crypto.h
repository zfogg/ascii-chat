#pragma once
#include "connection.h"
#include <ascii-chat/crypto/crypto.h>

asciichat_error_t client_crypto_init(client_connection_t *connection, const char *password);
