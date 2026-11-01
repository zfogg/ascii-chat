#include "session/connection.h"
#include <ascii-chat/network/negotiation.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/tui/input.h>
#include <ascii-chat/util/password.h>
#include <stdio.h>

asciichat_error_t session_connection_establish(const char *name, const char *address, uint16_t port,
                                               const network_connect_config_t *config, network_connection_t **owner,
                                               char *password, size_t capacity, session_connection_prepare_fn prepare,
                                               void (*detach)(void *), void *context) {
  if (!owner || *owner || !password || capacity < PASSWORD_BUFFER_SIZE || !prepare)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid session establishment parameters");
  for (;;) {
    connection_endpoint_t endpoint;
    asciichat_error_t result = network_connection_open(name, address, port, config, owner, &endpoint);
    if (result != ASCIICHAT_OK)
      return result;
    result = prepare(*owner, &endpoint, password[0] ? password : NULL, context);
    if (result != ASCIICHAT_OK)
      return result;
    crypto_handshake_context_t *handshake = network_connection_handshake(*owner);
    handshake->defer_password_prompt = true;
    uint8_t mode = network_connection_crypto_mode(*owner);
    if (!GET_OPTION(no_auth))
      mode |= ACIP_CRYPTO_AUTH;
    asciichat_errno_scope_t negotiation_errors = errno_scope_begin();
    result = network_client_negotiate(network_connection_transport(*owner), handshake, mode, NETWORK_PROTOCOL_MEDIA);
    uint8_t failure = handshake->password_failure;
    if (result == ASCIICHAT_OK) {
      errno_scope_end(negotiation_errors, ASCIICHAT_ERRNO_HANDLED);
      network_connection_ready(*owner);
      return ASCIICHAT_OK;
    }
    if (!failure)
      return result;
    // Recover only the rejected negotiation; caller and cleanup errors remain pending.
    errno_scope_end(negotiation_errors, ASCIICHAT_ERRNO_DISMISSED);
    if (detach)
      detach(context);
    network_connection_destroy(owner);
    char target[320];
    snprintf(target, sizeof(target), "%s:%u", endpoint.host, endpoint.port);
    result = ui_input_password(target, password, capacity, failure & AUTH_FAIL_PASSWORD_INCORRECT);
    if (result != ASCIICHAT_OK)
      return result;
  }
}
