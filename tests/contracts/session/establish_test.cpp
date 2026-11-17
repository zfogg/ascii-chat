#include <catch2/catch_test_macros.hpp>
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
static asciichat_error_t dial(const char *name, const char *address, uint16_t port, const network_connect_config_t *config, network_connection_t **out, connection_endpoint_t *endpoint) {
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
  REQUIRE((*owner) == (sentinel));
  REQUIRE((detached) == (destroys + 1));
  destroys++;
  *owner = NULL;
  if (record_cleanup_error)
    SET_ERRNO(ERROR_THREAD, "cleanup cause");
}
static crypto_handshake_context_t *security(network_connection_t *owner) {
  REQUIRE((owner) == (sentinel));
  return &handshake;
}
static acip_transport_t *get_transport(network_connection_t *owner) {
  REQUIRE((owner) == (sentinel));
  return &transport;
}
static uint8_t get_mode(network_connection_t *owner) {
  (void)owner;
  return ACIP_CRYPTO_ENCRYPT;
}
static void ready(network_connection_t *owner) {
  REQUIRE((owner) == (sentinel));
}
static void detach(void *context) {
  REQUIRE((context) == (&handshake));
  detached++;
}
static asciichat_error_t negotiate(acip_transport_t *t, crypto_handshake_context_t *ctx, uint8_t crypto_mode, network_protocol_t profile) {
  REQUIRE((t) == (&transport));
  REQUIRE((profile) == (expected_protocol));
  REQUIRE((crypto_mode) == (expected_crypto_mode));
  REQUIRE(ctx->defer_password_prompt);
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
  REQUIRE(std::strcmp((target), ("resolved-host:1234")) == 0);
  REQUIRE((incorrect && capacity >= PASSWORD_BUFFER_SIZE));
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

static asciichat_error_t prepare(network_connection_t *owner, const connection_endpoint_t *endpoint, const char *password, void *context) {
  REQUIRE((owner) == (sentinel));
  REQUIRE((context) == (&handshake));
  REQUIRE(std::strcmp((endpoint->host), ("resolved-host")) == 0);
  REQUIRE((std::strcmp((password), (opens == 1 || fallback_test ? "initial" : "replacement")) == 0));
  return prepare_result;
}
static void setup(void) {
  options_t options = options_t_new();
  REQUIRE((options_state_init()) == (ASCIICHAT_OK));
  REQUIRE((options_state_set(&options)) == (ASCIICHAT_OK));
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
TEST_CASE("session_establishment/retries_password_on_a_fresh_owner", "[session_establishment] [contract]") {
  challenge = true;
  network_connection_t *owner = NULL;
  REQUIRE((establish(&owner)) == (ASCIICHAT_OK));
  REQUIRE((owner) == (sentinel));
  REQUIRE((opens) == (2));
  REQUIRE((destroys) == (1));
  REQUIRE((prompts) == (1));
}
TEST_CASE("session_establishment/retains_failed_prepare_for_caller_cleanup", "[session_establishment] [contract]") {
  prepare_result = ERROR_CRYPTO_AUTH;
  network_connection_t *owner = NULL;
  REQUIRE((establish(&owner)) == (ERROR_CRYPTO_AUTH));
  REQUIRE((owner) == (sentinel));
  REQUIRE((negotiations) == (0));
  REQUIRE((destroys) == (0));
}
TEST_CASE("session_establishment/does_not_retry_protocol_failure", "[session_establishment] [contract]") {
  negotiate_result = ERROR_NETWORK_PROTOCOL;
  network_connection_t *owner = NULL;
  REQUIRE((establish(&owner)) == (ERROR_NETWORK_PROTOCOL));
  REQUIRE((owner) == (sentinel));
  REQUIRE((opens) == (1));
  REQUIRE((prompts) == (0));
  asciichat_error_context_t cause;
  REQUIRE(HAS_ERRNO(&cause));
  REQUIRE((cause.code) == (ERROR_NETWORK_PROTOCOL));
  REQUIRE(std::strcmp((cause.context_message), ("negotiation cause")) == 0);
}

static void configure_fallback(void) {
  options_t options = options_t_new();
  options.quic_fallback_port = 4567;
  SAFE_STRNCPY(options.server_key, "pinned-key", sizeof(options.server_key));
  REQUIRE((options_state_set(&options)) == (ASCIICHAT_OK));
}

static asciichat_error_t establish_quic(network_connection_t **owner) {
  char password[PASSWORD_BUFFER_SIZE] = "initial";
  return fixture_establish("fixture", "quic://127.0.0.1:1234", 0, NULL, owner, password, sizeof(password), prepare, detach, &handshake);
}

TEST_CASE("session_establishment/falls_back_only_after_quic_open_timeout", "[session_establishment] [contract]") {
  configure_fallback();
  fallback_test = true;
  first_dial_result = ERROR_NETWORK_TIMEOUT;
  network_connection_t *owner = NULL;
  REQUIRE((establish_quic(&owner)) == (ASCIICHAT_OK));
  REQUIRE((opens) == (2));
  REQUIRE(std::strcmp((dialed_address), ("tcp://127.0.0.1:4567")) == 0);
  REQUIRE((owner) == (sentinel));
  REQUIRE(!(errno_has_code(ERROR_NETWORK_TIMEOUT)));
}

TEST_CASE("session_establishment/never_falls_back_after_negotiation_timeout", "[session_establishment] [contract]") {
  configure_fallback();
  negotiate_result = ERROR_NETWORK_TIMEOUT;
  network_connection_t *owner = NULL;
  REQUIRE((establish_quic(&owner)) == (ERROR_NETWORK_TIMEOUT));
  REQUIRE((opens) == (1));
  REQUIRE((owner) == (sentinel));
}

TEST_CASE("session_establishment/never_falls_back_after_tls_failure_or_cancellation", "[session_establishment] [contract]") {
  configure_fallback();
  network_connection_t *owner = NULL;
  first_dial_result = ERROR_CRYPTO;
  REQUIRE((establish_quic(&owner)) == (ERROR_CRYPTO));
  REQUIRE((opens) == (1));
  opens = 0;
  first_dial_result = ERROR_NETWORK_CONNECT;
  REQUIRE((establish_quic(&owner)) == (ERROR_NETWORK_CONNECT));
  REQUIRE((opens) == (1));
}
TEST_CASE("session_establishment/password_retry_dismisses_only_negotiation_errors", "[session_establishment] [contract]") {
  SET_ERRNO(ERROR_INVALID_STATE, "earlier operation");
  asciichat_error_context_t earlier, remaining;
  REQUIRE(HAS_ERRNO(&earlier));
  challenge = true;
  network_connection_t *owner = NULL;
  REQUIRE((establish(&owner)) == (ASCIICHAT_OK));
  REQUIRE(HAS_ERRNO(&remaining));
  REQUIRE((remaining.error_id) == (earlier.error_id));
  REQUIRE(!(errno_has_code(ERROR_CRYPTO)));
  REQUIRE(!(errno_has_code(ERROR_CRYPTO_AUTH)));
}
TEST_CASE("session_establishment/password_retry_without_recorded_errors_preserves_earlier_error", "[session_establishment] [contract]") {
  SET_ERRNO(ERROR_INVALID_STATE, "earlier operation");
  asciichat_error_context_t earlier, remaining;
  REQUIRE(HAS_ERRNO(&earlier));
  challenge = true;
  record_negotiation_errors = false;
  network_connection_t *owner = NULL;
  REQUIRE((establish(&owner)) == (ASCIICHAT_OK));
  REQUIRE(HAS_ERRNO(&remaining));
  REQUIRE((remaining.error_id) == (earlier.error_id));
}
TEST_CASE("session_establishment/password_retry_preserves_cleanup_errors", "[session_establishment] [contract]") {
  challenge = true;
  record_cleanup_error = true;
  network_connection_t *owner = NULL;
  REQUIRE((establish(&owner)) == (ASCIICHAT_OK));
  asciichat_error_context_t cause;
  REQUIRE(HAS_ERRNO(&cause));
  REQUIRE((cause.code) == (ERROR_THREAD));
  REQUIRE(std::strcmp((cause.context_message), ("cleanup cause")) == 0);
  REQUIRE(!(errno_has_code(ERROR_CRYPTO_AUTH)));
}
TEST_CASE("session_establishment/prompt_cancellation_has_no_live_owner", "[session_establishment] [contract]") {
  challenge = true;
  prompt_result = ERROR_NETWORK;
  network_connection_t *owner = NULL;
  REQUIRE((establish(&owner)) == (ERROR_NETWORK));
  REQUIRE((owner) == nullptr);
  REQUIRE((destroys) == (1));
  REQUIRE((opens) == (1));
}
TEST_CASE("session_establishment/dial_failure_preserves_cause", "[session_establishment] [contract]") {
  dial_result = ERROR_NETWORK_CONNECT;
  network_connection_t *owner = NULL;
  REQUIRE((establish(&owner)) == (ERROR_NETWORK_CONNECT));
  REQUIRE((owner) == nullptr);
  asciichat_error_context_t cause;
  REQUIRE(HAS_ERRNO(&cause));
  REQUIRE(std::strcmp((cause.context_message), ("dial cause")) == 0);
}
TEST_CASE("session_establishment/rejects_overwriting_an_existing_owner", "[session_establishment] [contract]") {
  network_connection_t *owner = sentinel;
  REQUIRE((establish(&owner)) == (ERROR_INVALID_PARAM));
  REQUIRE((owner) == (sentinel));
  REQUIRE((opens) == (0));
}
TEST_CASE("session_establishment/network_attempt_reports_challenge_without_prompting_or_disposing", "[session_establishment] [contract]") {
  network_establish_config_t config = {
      .name = "fixture", .address = "input", .password = "initial", .protocol = NETWORK_PROTOCOL_MEDIA, .authenticate = true, .prepare = prepare, .context = &handshake};
  network_connection_t *owner = NULL;
  network_establish_result_t attempt;
  challenge = true;
  REQUIRE((fixture_network_establish(&config, &owner, &attempt)) == (ERROR_CRYPTO_AUTH));
  REQUIRE((owner) == (sentinel));
  REQUIRE((attempt.password_failure) == (AUTH_FAIL_PASSWORD_INCORRECT));
  REQUIRE(std::strcmp((attempt.endpoint.host), ("resolved-host")) == 0);
  REQUIRE((opens) == (1));
  REQUIRE((prompts) == (0));
  REQUIRE((destroys) == (0));
  REQUIRE(errno_has_code(ERROR_CRYPTO_AUTH));
}
TEST_CASE("session_establishment/network_attempt_uses_explicit_protocol_and_authentication_policy", "[session_establishment] [contract]") {
  network_establish_config_t config = {
      .name = "fixture", .address = "input", .password = "initial", .protocol = NETWORK_PROTOCOL_DISCOVERY, .authenticate = false, .prepare = prepare, .context = &handshake};
  network_connection_t *owner = NULL;
  network_establish_result_t attempt;
  expected_crypto_mode = ACIP_CRYPTO_ENCRYPT;
  expected_protocol = NETWORK_PROTOCOL_DISCOVERY;
  REQUIRE((fixture_network_establish(&config, &owner, &attempt)) == (ASCIICHAT_OK));
  REQUIRE((owner) == (sentinel));
  REQUIRE((attempt.password_failure) == (0));
  REQUIRE((opens) == (1));
  REQUIRE((prompts) == (0));
  REQUIRE((destroys) == (0));
}
