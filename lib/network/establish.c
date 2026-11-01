#include <ascii-chat/network/establish.h>

asciichat_error_t network_connection_establish(const network_establish_config_t *config, network_connection_t **owner,
                                               network_establish_result_t *attempt) {
  if (!config || !owner || *owner || !attempt || !config->prepare || config->protocol > NETWORK_PROTOCOL_DISCOVERY)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid connection establishment parameters");
  *attempt = (network_establish_result_t){0};
  asciichat_error_t result = network_connection_open(config->name, config->address, config->default_port,
                                                     config->connect, owner, &attempt->endpoint);
  if (result != ASCIICHAT_OK)
    return result;
  result = config->prepare(*owner, &attempt->endpoint, config->password, config->context);
  if (result != ASCIICHAT_OK)
    return result;
  crypto_handshake_context_t *handshake = network_connection_handshake(*owner);
  handshake->defer_password_prompt = true;
  uint8_t mode = network_connection_crypto_mode(*owner);
  if (config->authenticate)
    mode |= ACIP_CRYPTO_AUTH;
  result = network_client_negotiate(network_connection_transport(*owner), handshake, mode, config->protocol);
  attempt->password_failure = handshake->password_failure;
  if (result == ASCIICHAT_OK)
    network_connection_ready(*owner);
  return result;
}
