#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include "session/connection.h"
#include <ascii-chat/network/negotiation.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/util/password.h>
#include <string.h>

/* Isolate dial/prompt boundaries while exercising the production retry owner. */
static int opens, destroys, negotiations, prompts, detached;
static asciichat_error_t dial_result, prepare_result, negotiate_result, prompt_result;
static asciichat_error_t first_dial_result;
static char dialed_address[URL_BUFFER_SIZE];
static bool fallback_test;
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
  (void)port;
  (void)config;
  opens++;
  SAFE_STRNCPY(dialed_address, address, sizeof(dialed_address));
  if (opens == 1 && first_dial_result != ASCIICHAT_OK)
    return SET_ERRNO(first_dial_result, "initial dial cause");
  if (dial_result != ASCIICHAT_OK)
    return SET_ERRNO(dial_result, "dial cause");
  *out = sentinel;
  memset(endpoint, 0, sizeof(*endpoint));
  strcpy(endpoint->host, "resolved-host");
  endpoint->port = 1234;
  return ASCIICHAT_OK;
}
static void destroy(network_connection_t **owner) {
  ASCIICHAT_REQUIRE_EQ(*owner, sentinel);
  ASCIICHAT_REQUIRE_EQ(detached, destroys + 1, "Aliases must be detached before owner destruction");
  destroys++;
  *owner = NULL;
  if (record_cleanup_error)
    SET_ERRNO(ERROR_THREAD, "cleanup cause");
}
static crypto_handshake_context_t *security(network_connection_t *owner) {
  ASCIICHAT_REQUIRE_EQ(owner, sentinel);
  return &handshake;
}
static acip_transport_t *get_transport(network_connection_t *owner) {
  ASCIICHAT_REQUIRE_EQ(owner, sentinel);
  return &transport;
}
static uint8_t get_mode(network_connection_t *owner) {
  (void)owner;
  return ACIP_CRYPTO_ENCRYPT;
}
static void ready(network_connection_t *owner) {
  ASCIICHAT_REQUIRE_EQ(owner, sentinel);
}
static void detach(void *context) {
  ASCIICHAT_REQUIRE_EQ(context, &handshake);
  detached++;
}
static asciichat_error_t negotiate(acip_transport_t *t, crypto_handshake_context_t *ctx, uint8_t crypto_mode,
                                   network_protocol_t profile) {
  ASCIICHAT_REQUIRE_EQ(t, &transport);
  ASCIICHAT_REQUIRE_EQ(profile, expected_protocol);
  ASCIICHAT_REQUIRE_EQ(crypto_mode, expected_crypto_mode);
  ASCIICHAT_REQUIRE(ctx->defer_password_prompt);
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
  ASCIICHAT_REQUIRE_STR_EQ(target, "resolved-host:1234");
  ASCIICHAT_REQUIRE(incorrect && capacity >= PASSWORD_BUFFER_SIZE);
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
  ASCIICHAT_REQUIRE_EQ(owner, sentinel);
  ASCIICHAT_REQUIRE_EQ(context, &handshake);
  ASCIICHAT_REQUIRE_STR_EQ(endpoint->host, "resolved-host");
  ASCIICHAT_REQUIRE_STR_EQ(password, opens == 1 || fallback_test ? "initial" : "replacement");
  return prepare_result;
}
static void setup(void) {
  options_t options = options_t_new();
  ASCIICHAT_REQUIRE_EQ(options_state_init(), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(options_state_set(&options), ASCIICHAT_OK);
  opens = destroys = negotiations = prompts = detached = 0;
  dial_result = prepare_result = negotiate_result = prompt_result = ASCIICHAT_OK;
  first_dial_result = ASCIICHAT_OK;
  fallback_test = false;
  dialed_address[0] = '\0';
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
static asciichat_error_t establish(network_connection_t **owner) {
  char password[PASSWORD_BUFFER_SIZE] = "initial";
  return fixture_establish("fixture", "input", 0, NULL, owner, password, sizeof(password), prepare, detach, &handshake);
}
TEST_CASE("session_establishment/retries_password_on_a_fresh_owner", "[session_establishment]") {
  challenge = true;
  network_connection_t *owner = NULL;
  ASCIICHAT_REQUIRE_EQ(establish(&owner), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(owner, sentinel);
  ASCIICHAT_REQUIRE_EQ(opens, 2);
  ASCIICHAT_REQUIRE_EQ(destroys, 1);
  ASCIICHAT_REQUIRE_EQ(prompts, 1);
}
TEST_CASE("session_establishment/retains_failed_prepare_for_caller_cleanup", "[session_establishment]") {
  prepare_result = ERROR_CRYPTO_AUTH;
  network_connection_t *owner = NULL;
  ASCIICHAT_REQUIRE_EQ(establish(&owner), ERROR_CRYPTO_AUTH);
  ASCIICHAT_REQUIRE_EQ(owner, sentinel);
  ASCIICHAT_REQUIRE_EQ(negotiations, 0);
  ASCIICHAT_REQUIRE_EQ(destroys, 0);
}
TEST_CASE("session_establishment/does_not_retry_protocol_failure", "[session_establishment]") {
  negotiate_result = ERROR_NETWORK_PROTOCOL;
  network_connection_t *owner = NULL;
  ASCIICHAT_REQUIRE_EQ(establish(&owner), ERROR_NETWORK_PROTOCOL);
  ASCIICHAT_REQUIRE_EQ(owner, sentinel);
  ASCIICHAT_REQUIRE_EQ(opens, 1);
  ASCIICHAT_REQUIRE_EQ(prompts, 0);
  asciichat_error_context_t cause;
  ASCIICHAT_REQUIRE(HAS_ERRNO(&cause));
  ASCIICHAT_REQUIRE_EQ(cause.code, ERROR_NETWORK_PROTOCOL);
  ASCIICHAT_REQUIRE_STR_EQ(cause.context_message, "negotiation cause");
}

static void configure_fallback(void) {
  options_t options = options_t_new();
  options.quic_fallback_port = 4567;
  SAFE_STRNCPY(options.server_key, "pinned-key", sizeof(options.server_key));
  ASCIICHAT_REQUIRE_EQ(options_state_set(&options), ASCIICHAT_OK);
}

static asciichat_error_t establish_quic(network_connection_t **owner) {
  char password[PASSWORD_BUFFER_SIZE] = "initial";
  return fixture_establish("fixture", "quic://127.0.0.1:1234", 0, NULL, owner, password, sizeof(password), prepare,
                           detach, &handshake);
}

TEST_CASE("session_establishment/falls_back_only_after_quic_open_timeout", "[session_establishment]") {
  configure_fallback();
  fallback_test = true;
  first_dial_result = ERROR_NETWORK_TIMEOUT;
  network_connection_t *owner = NULL;
  ASCIICHAT_REQUIRE_EQ(establish_quic(&owner), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(opens, 2);
  ASCIICHAT_REQUIRE_STR_EQ(dialed_address, "tcp://127.0.0.1:4567");
  ASCIICHAT_REQUIRE_EQ(owner, sentinel);
  ASCIICHAT_REQUIRE_NOT(errno_has_code(ERROR_NETWORK_TIMEOUT));
}

TEST_CASE("session_establishment/never_falls_back_after_negotiation_timeout", "[session_establishment]") {
  configure_fallback();
  negotiate_result = ERROR_NETWORK_TIMEOUT;
  network_connection_t *owner = NULL;
  ASCIICHAT_REQUIRE_EQ(establish_quic(&owner), ERROR_NETWORK_TIMEOUT);
  ASCIICHAT_REQUIRE_EQ(opens, 1);
  ASCIICHAT_REQUIRE_EQ(owner, sentinel);
}

TEST_CASE("session_establishment/never_falls_back_after_tls_failure_or_cancellation", "[session_establishment]") {
  configure_fallback();
  network_connection_t *owner = NULL;
  first_dial_result = ERROR_CRYPTO;
  ASCIICHAT_REQUIRE_EQ(establish_quic(&owner), ERROR_CRYPTO);
  ASCIICHAT_REQUIRE_EQ(opens, 1);
  opens = 0;
  first_dial_result = ERROR_NETWORK_CONNECT;
  ASCIICHAT_REQUIRE_EQ(establish_quic(&owner), ERROR_NETWORK_CONNECT);
  ASCIICHAT_REQUIRE_EQ(opens, 1);
}
TEST_CASE("session_establishment/password_retry_dismisses_only_negotiation_errors", "[session_establishment]") {
  SET_ERRNO(ERROR_INVALID_STATE, "earlier operation");
  asciichat_error_context_t earlier, remaining;
  ASCIICHAT_REQUIRE(HAS_ERRNO(&earlier));
  challenge = true;
  network_connection_t *owner = NULL;
  ASCIICHAT_REQUIRE_EQ(establish(&owner), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(HAS_ERRNO(&remaining));
  ASCIICHAT_REQUIRE_EQ(remaining.error_id, earlier.error_id);
  ASCIICHAT_REQUIRE_NOT(errno_has_code(ERROR_CRYPTO));
  ASCIICHAT_REQUIRE_NOT(errno_has_code(ERROR_CRYPTO_AUTH));
}
TEST_CASE("session_establishment/password_retry_without_recorded_errors_preserves_earlier_error", "[session_establishment]") {
  SET_ERRNO(ERROR_INVALID_STATE, "earlier operation");
  asciichat_error_context_t earlier, remaining;
  ASCIICHAT_REQUIRE(HAS_ERRNO(&earlier));
  challenge = true;
  record_negotiation_errors = false;
  network_connection_t *owner = NULL;
  ASCIICHAT_REQUIRE_EQ(establish(&owner), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(HAS_ERRNO(&remaining));
  ASCIICHAT_REQUIRE_EQ(remaining.error_id, earlier.error_id);
}
TEST_CASE("session_establishment/password_retry_preserves_cleanup_errors", "[session_establishment]") {
  challenge = true;
  record_cleanup_error = true;
  network_connection_t *owner = NULL;
  ASCIICHAT_REQUIRE_EQ(establish(&owner), ASCIICHAT_OK);
  asciichat_error_context_t cause;
  ASCIICHAT_REQUIRE(HAS_ERRNO(&cause));
  ASCIICHAT_REQUIRE_EQ(cause.code, ERROR_THREAD);
  ASCIICHAT_REQUIRE_STR_EQ(cause.context_message, "cleanup cause");
  ASCIICHAT_REQUIRE_NOT(errno_has_code(ERROR_CRYPTO_AUTH));
}
TEST_CASE("session_establishment/prompt_cancellation_has_no_live_owner", "[session_establishment]") {
  challenge = true;
  prompt_result = ERROR_NETWORK;
  network_connection_t *owner = NULL;
  ASCIICHAT_REQUIRE_EQ(establish(&owner), ERROR_NETWORK);
  ASCIICHAT_REQUIRE_NULL(owner);
  ASCIICHAT_REQUIRE_EQ(destroys, 1);
  ASCIICHAT_REQUIRE_EQ(opens, 1);
}
TEST_CASE("session_establishment/dial_failure_preserves_cause", "[session_establishment]") {
  dial_result = ERROR_NETWORK_CONNECT;
  network_connection_t *owner = NULL;
  ASCIICHAT_REQUIRE_EQ(establish(&owner), ERROR_NETWORK_CONNECT);
  ASCIICHAT_REQUIRE_NULL(owner);
  asciichat_error_context_t cause;
  ASCIICHAT_REQUIRE(HAS_ERRNO(&cause));
  ASCIICHAT_REQUIRE_STR_EQ(cause.context_message, "dial cause");
}
TEST_CASE("session_establishment/rejects_overwriting_an_existing_owner", "[session_establishment]") {
  network_connection_t *owner = sentinel;
  ASCIICHAT_REQUIRE_EQ(establish(&owner), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(owner, sentinel);
  ASCIICHAT_REQUIRE_EQ(opens, 0);
}
TEST_CASE("session_establishment/network_attempt_reports_challenge_without_prompting_or_disposing", "[session_establishment]") {
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
  ASCIICHAT_REQUIRE_EQ(fixture_network_establish(&config, &owner, &attempt), ERROR_CRYPTO_AUTH);
  ASCIICHAT_REQUIRE_EQ(owner, sentinel);
  ASCIICHAT_REQUIRE_EQ(attempt.password_failure, AUTH_FAIL_PASSWORD_INCORRECT);
  ASCIICHAT_REQUIRE_STR_EQ(attempt.endpoint.host, "resolved-host");
  ASCIICHAT_REQUIRE_EQ(opens, 1);
  ASCIICHAT_REQUIRE_EQ(prompts, 0);
  ASCIICHAT_REQUIRE_EQ(destroys, 0);
  ASCIICHAT_REQUIRE(errno_has_code(ERROR_CRYPTO_AUTH));
}
TEST_CASE("session_establishment/network_attempt_uses_explicit_protocol_and_authentication_policy", "[session_establishment]") {
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
  ASCIICHAT_REQUIRE_EQ(fixture_network_establish(&config, &owner, &attempt), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(owner, sentinel);
  ASCIICHAT_REQUIRE_EQ(attempt.password_failure, 0);
  ASCIICHAT_REQUIRE_EQ(opens, 1);
  ASCIICHAT_REQUIRE_EQ(prompts, 0);
  ASCIICHAT_REQUIRE_EQ(destroys, 0);
}
