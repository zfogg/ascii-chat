
/* Storage capacities in elements; text buffers include the terminating NUL. */
#define SESSION_ESTABLISH_TARGET_BUFFER_SIZE 320

#include "session/connection.h"
#include <ascii-chat/options/options.h>
#include <ascii-chat/tui/input.h>
#include <ascii-chat/util/password.h>
#include <stdio.h>

asciichat_error_t session_connection_establish(const char *name, const char *address, uint16_t port,
                                               const network_connect_config_t *config, network_connection_t **owner,
                                               char *password, size_t capacity, network_connection_prepare_fn prepare,
                                               void (*detach)(void *), void *context) {
  if (!owner || *owner || !password || capacity < PASSWORD_BUFFER_SIZE || !prepare)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid session establishment parameters");
  for (;;) {
    network_establish_config_t settings = {.name = name,
                                           .address = address,
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
