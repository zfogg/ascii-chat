
/* Storage capacities in elements; text buffers include the terminating NUL. */
#define SESSION_ESTABLISH_TARGET_BUFFER_SIZE 320

#include "session/connection.h"
#include <ascii-chat/options/options.h>
#include <ascii-chat/tui/input.h>
#include <ascii-chat/util/password.h>
#include <ascii-chat/log/log.h>
#include <stdio.h>
#include <string.h>

asciichat_error_t session_connection_establish(const char *name, const char *address, uint16_t port,
                                               const network_connect_config_t *config, network_connection_t **owner,
                                               char *password, size_t capacity, network_connection_prepare_fn prepare,
                                               void (*detach)(void *), void *context) {
  if (!owner || *owner || !password || capacity < PASSWORD_BUFFER_SIZE || !prepare)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid session establishment parameters");
  const char *selected_address = address;
  char fallback_address[URL_BUFFER_SIZE];
  int fallback_port = GET_OPTION(quic_fallback_port);
  bool fallback_available = address && strncasecmp(address, "quic://", 7) == 0 && fallback_port > 0;
  if (fallback_available) {
    if (!GET_OPTION(server_key)[0] || GET_OPTION(no_encrypt) || GET_OPTION(no_auth))
      return SET_ERRNO(ERROR_USAGE, "QUIC TCP fallback requires --server-key and ACIP encryption and authentication");
    connection_endpoint_t endpoint;
    asciichat_error_t result = connection_endpoint_resolve(address, port, &endpoint);
    if (result != ASCIICHAT_OK)
      return result;
    if (strchr(endpoint.host, ':'))
      SAFE_SNPRINTF(fallback_address, sizeof(fallback_address), "tcp://[%s]:%d", endpoint.host, fallback_port);
    else
      SAFE_SNPRINTF(fallback_address, sizeof(fallback_address), "tcp://%s:%d", endpoint.host, fallback_port);
  }
  for (;;) {
    network_establish_config_t settings = {.name = name,
                                           .address = selected_address,
                                           .default_port = port,
                                           .connect = config,
                                           .password = password[0] ? password : NULL,
                                           .protocol = NETWORK_PROTOCOL_MEDIA,
                                           .authenticate = !GET_OPTION(no_auth),
                                           .prepare = prepare,
                                           .context = context};
    network_establish_result_t attempt;
    asciichat_errno_scope_t attempt_errors = errno_scope_begin();
    asciichat_error_t result = network_connection_establish(&settings, owner, &attempt);
    if (result == ASCIICHAT_OK) {
      errno_scope_end(attempt_errors, ASCIICHAT_ERRNO_HANDLED);
      return ASCIICHAT_OK;
    }
    if (fallback_available && result == ERROR_NETWORK_TIMEOUT && !*owner) {
      errno_scope_end(attempt_errors, ASCIICHAT_ERRNO_DISMISSED);
      selected_address = fallback_address;
      fallback_available = false;
      log_info("QUIC timed out; trying authenticated TCP fallback on port %d", fallback_port);
      continue;
    }
    if (!attempt.password_failure)
      return result;
    // Recover only the rejected negotiation; caller and cleanup errors remain pending.
    errno_scope_end(attempt_errors, ASCIICHAT_ERRNO_DISMISSED);
    if (detach)
      detach(context);
    network_connection_destroy(owner);
    char target[SESSION_ESTABLISH_TARGET_BUFFER_SIZE];
    snprintf(target, sizeof(target), "%s:%u", attempt.endpoint.host, attempt.endpoint.port);
    result = ui_input_password(target, password, capacity, attempt.password_failure & AUTH_FAIL_PASSWORD_INCORRECT);
    if (result != ASCIICHAT_OK)
      return result;
  }
}
