#include <criterion/criterion.h>
#include "session/connection.h"
#include <ascii-chat/network/negotiation.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/util/password.h>
#include <string.h>

/* Isolate dial/prompt boundaries while exercising the production retry owner. */
static int opens, destroys, negotiations, prompts, detached;
static asciichat_error_t dial_result, prepare_result, negotiate_result, prompt_result;
static bool challenge;
static bool record_negotiation_errors, record_cleanup_error;
static uint8_t expected_crypto_mode;
static network_protocol_t expected_protocol;
static asciichat_errno_scope_t test_errors;
static crypto_handshake_context_t handshake;
static network_connection_t *sentinel = (network_connection_t *)&handshake;
static acip_transport_t transport;
static asciichat_error_t dial(const char *name, const char *address, uint16_t port,
                              const network_connect_config_t *config, network_connection_t **out,
                              connection_endpoint_t *endpoint) {
  (void)name;
  (void)address;
  (void)port;
  (void)config;
  opens++;
  if (dial_result != ASCIICHAT_OK)
    return SET_ERRNO(dial_result, "dial cause");
  *out = sentinel;
  memset(endpoint, 0, sizeof(*endpoint));
  strcpy(endpoint->host, "resolved-host");
  endpoint->port = 1234;
  return ASCIICHAT_OK;
}
static void destroy(network_connection_t **owner) {
  cr_assert_eq(*owner, sentinel);
  cr_assert_eq(detached, destroys + 1, "Aliases must be detached before owner destruction");
  destroys++;
  *owner = NULL;
  if (record_cleanup_error)
    SET_ERRNO(ERROR_THREAD, "cleanup cause");
}
static crypto_handshake_context_t *security(network_connection_t *owner) {
  cr_assert_eq(owner, sentinel);
  return &handshake;
}
static acip_transport_t *get_transport(network_connection_t *owner) {
  cr_assert_eq(owner, sentinel);
  return &transport;
}
static uint8_t get_mode(network_connection_t *owner) {
  (void)owner;
  return ACIP_CRYPTO_ENCRYPT;
}
static void ready(network_connection_t *owner) {
  cr_assert_eq(owner, sentinel);
}
static void detach(void *context) {
  cr_assert_eq(context, &handshake);
  detached++;
}
static asciichat_error_t negotiate(acip_transport_t *t, crypto_handshake_context_t *ctx, uint8_t crypto_mode,
                                   network_protocol_t profile) {
  cr_assert_eq(t, &transport);
  cr_assert_eq(profile, expected_protocol);
  cr_assert_eq(crypto_mode, expected_crypto_mode);
  cr_assert(ctx->defer_password_prompt);
  negotiations++;
  ctx->password_failure = challenge && negotiations == 1 ? AUTH_FAIL_PASSWORD_INCORRECT : 0;
  if (ctx->password_failure) {
    if (record_negotiation_errors) {
      SET_ERRNO(ERROR_CRYPTO, "password proof mismatch");
      SET_ERRNO(ERROR_CRYPTO_AUTH, "password rejected");
    }
    return ERROR_CRYPTO_AUTH;
  }
  return negotiate_result == ASCIICHAT_OK ? ASCIICHAT_OK : SET_ERRNO(negotiate_result, "negotiation cause");
}
static asciichat_error_t prompt(const char *target, char *password, size_t capacity, bool incorrect) {
  cr_assert_str_eq(target, "resolved-host:1234");
  cr_assert(incorrect && capacity >= PASSWORD_BUFFER_SIZE);
  prompts++;
  strcpy(password, "replacement");
  return prompt_result;
}
#define session_connection_establish fixture_establish
#define network_connection_open dial
#define network_connection_destroy destroy
#define network_connection_handshake security
#define network_connection_transport get_transport
#define network_connection_crypto_mode get_mode
#define network_connection_ready ready
#define network_client_negotiate negotiate
#define ui_input_password prompt
#define network_connection_establish fixture_network_establish
#include "../../../lib/network/establish.c"
#include "session/establish.c"

static asciichat_error_t prepare(network_connection_t *owner, const connection_endpoint_t *endpoint,
                                 const char *password, void *context) {
  cr_assert_eq(owner, sentinel);
  cr_assert_eq(context, &handshake);
  cr_assert_str_eq(endpoint->host, "resolved-host");
  cr_assert_str_eq(password, opens == 1 ? "initial" : "replacement");
  return prepare_result;
}
static void setup(void) {
  options_t options = options_t_new();
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  cr_assert_eq(options_state_set(&options), ASCIICHAT_OK);
  opens = destroys = negotiations = prompts = detached = 0;
  dial_result = prepare_result = negotiate_result = prompt_result = ASCIICHAT_OK;
  challenge = false;
  record_negotiation_errors = true;
  record_cleanup_error = false;
  expected_crypto_mode = ACIP_CRYPTO_FULL;
  expected_protocol = NETWORK_PROTOCOL_MEDIA;
  test_errors = errno_scope_begin();
  memset(&handshake, 0, sizeof(handshake));
}
static void teardown(void) {
  errno_scope_end(test_errors, ASCIICHAT_ERRNO_HANDLED);
  options_state_destroy();
}
TestSuite(session_establishment, .init = setup, .fini = teardown);
static asciichat_error_t establish(network_connection_t **owner) {
  char password[PASSWORD_BUFFER_SIZE] = "initial";
  return fixture_establish("fixture", "input", 0, NULL, owner, password, sizeof(password), prepare, detach, &handshake);
}
Test(session_establishment, retries_password_on_a_fresh_owner) {
  challenge = true;
  network_connection_t *owner = NULL;
  cr_assert_eq(establish(&owner), ASCIICHAT_OK);
  cr_assert_eq(owner, sentinel);
  cr_assert_eq(opens, 2);
  cr_assert_eq(destroys, 1);
  cr_assert_eq(prompts, 1);
}
Test(session_establishment, retains_failed_prepare_for_caller_cleanup) {
  prepare_result = ERROR_CRYPTO_AUTH;
  network_connection_t *owner = NULL;
  cr_assert_eq(establish(&owner), ERROR_CRYPTO_AUTH);
  cr_assert_eq(owner, sentinel);
  cr_assert_eq(negotiations, 0);
  cr_assert_eq(destroys, 0);
}
Test(session_establishment, does_not_retry_protocol_failure) {
  negotiate_result = ERROR_NETWORK_PROTOCOL;
  network_connection_t *owner = NULL;
  cr_assert_eq(establish(&owner), ERROR_NETWORK_PROTOCOL);
  cr_assert_eq(owner, sentinel);
  cr_assert_eq(opens, 1);
  cr_assert_eq(prompts, 0);
  asciichat_error_context_t cause;
  cr_assert(HAS_ERRNO(&cause));
  cr_assert_eq(cause.code, ERROR_NETWORK_PROTOCOL);
  cr_assert_str_eq(cause.context_message, "negotiation cause");
}
Test(session_establishment, password_retry_dismisses_only_negotiation_errors) {
  SET_ERRNO(ERROR_INVALID_STATE, "earlier operation");
  asciichat_error_context_t earlier, remaining;
  cr_assert(HAS_ERRNO(&earlier));
  challenge = true;
  network_connection_t *owner = NULL;
  cr_assert_eq(establish(&owner), ASCIICHAT_OK);
  cr_assert(HAS_ERRNO(&remaining));
  cr_assert_eq(remaining.error_id, earlier.error_id);
  cr_assert_not(errno_has_code(ERROR_CRYPTO));
  cr_assert_not(errno_has_code(ERROR_CRYPTO_AUTH));
}
Test(session_establishment, password_retry_without_recorded_errors_preserves_earlier_error) {
  SET_ERRNO(ERROR_INVALID_STATE, "earlier operation");
  asciichat_error_context_t earlier, remaining;
  cr_assert(HAS_ERRNO(&earlier));
  challenge = true;
  record_negotiation_errors = false;
  network_connection_t *owner = NULL;
  cr_assert_eq(establish(&owner), ASCIICHAT_OK);
  cr_assert(HAS_ERRNO(&remaining));
  cr_assert_eq(remaining.error_id, earlier.error_id);
}
Test(session_establishment, password_retry_preserves_cleanup_errors) {
  challenge = true;
  record_cleanup_error = true;
  network_connection_t *owner = NULL;
  cr_assert_eq(establish(&owner), ASCIICHAT_OK);
  asciichat_error_context_t cause;
  cr_assert(HAS_ERRNO(&cause));
  cr_assert_eq(cause.code, ERROR_THREAD);
  cr_assert_str_eq(cause.context_message, "cleanup cause");
  cr_assert_not(errno_has_code(ERROR_CRYPTO_AUTH));
}
Test(session_establishment, prompt_cancellation_has_no_live_owner) {
  challenge = true;
  prompt_result = ERROR_NETWORK;
  network_connection_t *owner = NULL;
  cr_assert_eq(establish(&owner), ERROR_NETWORK);
  cr_assert_null(owner);
  cr_assert_eq(destroys, 1);
  cr_assert_eq(opens, 1);
}
Test(session_establishment, dial_failure_preserves_cause) {
  dial_result = ERROR_NETWORK_CONNECT;
  network_connection_t *owner = NULL;
  cr_assert_eq(establish(&owner), ERROR_NETWORK_CONNECT);
  cr_assert_null(owner);
  asciichat_error_context_t cause;
  cr_assert(HAS_ERRNO(&cause));
  cr_assert_str_eq(cause.context_message, "dial cause");
}
Test(session_establishment, rejects_overwriting_an_existing_owner) {
  network_connection_t *owner = sentinel;
  cr_assert_eq(establish(&owner), ERROR_INVALID_PARAM);
  cr_assert_eq(owner, sentinel);
  cr_assert_eq(opens, 0);
}
Test(session_establishment, network_attempt_reports_challenge_without_prompting_or_disposing) {
  network_establish_config_t config = {.name = "fixture",
                                       .address = "input",
                                       .password = "initial",
                                       .protocol = NETWORK_PROTOCOL_MEDIA,
                                       .authenticate = true,
                                       .prepare = prepare,
                                       .context = &handshake};
  network_connection_t *owner = NULL;
  network_establish_result_t attempt;
  challenge = true;
  cr_assert_eq(fixture_network_establish(&config, &owner, &attempt), ERROR_CRYPTO_AUTH);
  cr_assert_eq(owner, sentinel);
  cr_assert_eq(attempt.password_failure, AUTH_FAIL_PASSWORD_INCORRECT);
  cr_assert_str_eq(attempt.endpoint.host, "resolved-host");
  cr_assert_eq(opens, 1);
  cr_assert_eq(prompts, 0);
  cr_assert_eq(destroys, 0);
  cr_assert(errno_has_code(ERROR_CRYPTO_AUTH));
}
Test(session_establishment, network_attempt_uses_explicit_protocol_and_authentication_policy) {
  network_establish_config_t config = {.name = "fixture",
                                       .address = "input",
                                       .password = "initial",
                                       .protocol = NETWORK_PROTOCOL_DISCOVERY,
                                       .authenticate = false,
                                       .prepare = prepare,
                                       .context = &handshake};
  network_connection_t *owner = NULL;
  network_establish_result_t attempt;
  expected_crypto_mode = ACIP_CRYPTO_ENCRYPT;
  expected_protocol = NETWORK_PROTOCOL_DISCOVERY;
  cr_assert_eq(fixture_network_establish(&config, &owner, &attempt), ASCIICHAT_OK);
  cr_assert_eq(owner, sentinel);
  cr_assert_eq(attempt.password_failure, 0);
  cr_assert_eq(opens, 1);
  cr_assert_eq(prompts, 0);
  cr_assert_eq(destroys, 0);
}
