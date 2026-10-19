#pragma once
#include "connection.h"
#include <ascii-chat/crypto/crypto.h>

asciichat_error_t client_crypto_init(client_connection_t *connection);
bool crypto_client_should_rekey(client_connection_t *connection);
asciichat_error_t crypto_client_initiate_rekey(client_connection_t *connection);
asciichat_error_t crypto_client_process_rekey_request(client_connection_t *connection, const uint8_t *packet,
                                                      size_t packet_len);
asciichat_error_t crypto_client_send_rekey_response(client_connection_t *connection);
asciichat_error_t crypto_client_process_rekey_response(client_connection_t *connection, const uint8_t *packet,
                                                       size_t packet_len);
asciichat_error_t crypto_client_send_rekey_complete(client_connection_t *connection);
