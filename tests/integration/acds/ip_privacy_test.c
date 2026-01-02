/**
 * @file ip_privacy_test.c
 * @brief Integration tests for ACDS IP privacy controls
 *
 * Validates that server IP addresses are only disclosed after proper authentication:
 * - Password verification for password-protected sessions
 * - Explicit opt-in via expose_ip_publicly flag
 * - IP withheld for sessions without either mechanism
 *
 * This prevents IP address leakage to unauthenticated clients who only know
 * the session string.
 */

#include "acds/session.h"
#include "acds/main.h"
#include "asciichat_errno.h"
#include "network/acip/acds.h"
#include <criterion/criterion.h>
#include <string.h>

// Test fixture
TestSuite(acds_ip_privacy, .timeout = 10.0);

/**
 * @brief Test that IP is revealed for password-protected session with correct password
 */
Test(acds_ip_privacy, password_protected_reveals_ip) {
  session_registry_t registry;
  asciichat_error_t result = session_registry_init(&registry);
  cr_assert_eq(result, ASCIICHAT_OK, "Registry initialization should succeed");

  acds_config_t config;
  memset(&config, 0, sizeof(config));

  // Create password-protected session
  acip_session_create_t create_req;
  memset(&create_req, 0, sizeof(create_req));
  create_req.session_type = SESSION_TYPE_DIRECT_TCP;
  create_req.capabilities = 0x03;
  create_req.max_participants = 4;
  create_req.has_password = 1;
  create_req.expose_ip_publicly = 0; // No explicit opt-in
  SAFE_STRNCPY(create_req.server_address, "192.168.1.100", sizeof(create_req.server_address));
  create_req.server_port = 27224;

  // Hash password "test-password-123"
  // TODO: Use proper Argon2id hashing when password hashing is implemented
  // For now, just use a placeholder hash
  memset(create_req.password_hash, 0xAB, sizeof(create_req.password_hash));

  acip_session_created_t create_resp;
  result = session_create(&registry, &create_req, &config, &create_resp);
  cr_assert_eq(result, ASCIICHAT_OK, "Session creation should succeed");

  // Join with correct password
  acip_session_join_t join_req;
  memset(&join_req, 0, sizeof(join_req));
  join_req.session_string_len = (uint8_t)strlen(create_resp.session_string);
  SAFE_STRNCPY(join_req.session_string, create_resp.session_string, sizeof(join_req.session_string));
  join_req.has_password = 1;
  SAFE_STRNCPY(join_req.password, "test-password-123", sizeof(join_req.password));

  acip_session_joined_t join_resp;
  result = session_join(&registry, &join_req, &config, &join_resp);
  cr_assert_eq(result, ASCIICHAT_OK, "Session join should succeed");
  cr_assert_eq(join_resp.success, 1, "Join should be successful");

  // Verify IP is revealed (password was verified)
  cr_assert_str_eq(join_resp.server_address, "192.168.1.100",
                   "Server address should be revealed after password verification");
  cr_assert_eq(join_resp.server_port, 27224, "Server port should be revealed");
  cr_assert_eq(join_resp.session_type, SESSION_TYPE_DIRECT_TCP, "Session type should be revealed");

  session_registry_destroy(&registry);
}

/**
 * @brief Test that IP is withheld for session without password or opt-in
 */
Test(acds_ip_privacy, no_password_no_optin_withholds_ip) {
  session_registry_t registry;
  asciichat_error_t result = session_registry_init(&registry);
  cr_assert_eq(result, ASCIICHAT_OK, "Registry initialization should succeed");

  acds_config_t config;
  memset(&config, 0, sizeof(config));

  // Create session WITHOUT password and WITHOUT expose_ip_publicly
  acip_session_create_t create_req;
  memset(&create_req, 0, sizeof(create_req));
  create_req.session_type = SESSION_TYPE_DIRECT_TCP;
  create_req.capabilities = 0x03;
  create_req.max_participants = 4;
  create_req.has_password = 0;           // No password
  create_req.expose_ip_publicly = 0;     // No explicit opt-in
  SAFE_STRNCPY(create_req.server_address, "192.168.1.100", sizeof(create_req.server_address));
  create_req.server_port = 27224;

  acip_session_created_t create_resp;
  result = session_create(&registry, &create_req, &config, &create_resp);
  cr_assert_eq(result, ASCIICHAT_OK, "Session creation should succeed");

  // Join session (no password required)
  acip_session_join_t join_req;
  memset(&join_req, 0, sizeof(join_req));
  join_req.session_string_len = (uint8_t)strlen(create_resp.session_string);
  SAFE_STRNCPY(join_req.session_string, create_resp.session_string, sizeof(join_req.session_string));
  join_req.has_password = 0;

  acip_session_joined_t join_resp;
  result = session_join(&registry, &join_req, &config, &join_resp);
  cr_assert_eq(result, ASCIICHAT_OK, "Session join should succeed");
  cr_assert_eq(join_resp.success, 1, "Join should be successful");

  // Verify IP is WITHHELD (security control active)
  cr_assert_eq(join_resp.server_address[0], '\0',
               "Server address should be withheld without password or opt-in");
  cr_assert_eq(join_resp.server_port, 0, "Server port should be zero");
  cr_assert_eq(join_resp.session_type, 0, "Session type should be zero");

  session_registry_destroy(&registry);
}

/**
 * @brief Test that IP is revealed for session with explicit expose_ip_publicly opt-in
 */
Test(acds_ip_privacy, explicit_optin_reveals_ip) {
  session_registry_t registry;
  asciichat_error_t result = session_registry_init(&registry);
  cr_assert_eq(result, ASCIICHAT_OK, "Registry initialization should succeed");

  acds_config_t config;
  memset(&config, 0, sizeof(config));

  // Create session with explicit IP exposure opt-in
  acip_session_create_t create_req;
  memset(&create_req, 0, sizeof(create_req));
  create_req.session_type = SESSION_TYPE_DIRECT_TCP;
  create_req.capabilities = 0x03;
  create_req.max_participants = 4;
  create_req.has_password = 0;           // No password
  create_req.expose_ip_publicly = 1;     // Explicit opt-in
  SAFE_STRNCPY(create_req.server_address, "203.0.113.42", sizeof(create_req.server_address));
  create_req.server_port = 8080;

  acip_session_created_t create_resp;
  result = session_create(&registry, &create_req, &config, &create_resp);
  cr_assert_eq(result, ASCIICHAT_OK, "Session creation should succeed");

  // Join session (no password)
  acip_session_join_t join_req;
  memset(&join_req, 0, sizeof(join_req));
  join_req.session_string_len = (uint8_t)strlen(create_resp.session_string);
  SAFE_STRNCPY(join_req.session_string, create_resp.session_string, sizeof(join_req.session_string));
  join_req.has_password = 0;

  acip_session_joined_t join_resp;
  result = session_join(&registry, &join_req, &config, &join_resp);
  cr_assert_eq(result, ASCIICHAT_OK, "Session join should succeed");
  cr_assert_eq(join_resp.success, 1, "Join should be successful");

  // Verify IP is revealed (explicit opt-in)
  cr_assert_str_eq(join_resp.server_address, "203.0.113.42",
                   "Server address should be revealed with explicit opt-in");
  cr_assert_eq(join_resp.server_port, 8080, "Server port should be revealed");
  cr_assert_eq(join_resp.session_type, SESSION_TYPE_DIRECT_TCP, "Session type should be revealed");

  session_registry_destroy(&registry);
}

/**
 * @brief Test that IP is withheld for password-protected session with WRONG password
 */
Test(acds_ip_privacy, wrong_password_withholds_ip) {
  session_registry_t registry;
  asciichat_error_t result = session_registry_init(&registry);
  cr_assert_eq(result, ASCIICHAT_OK, "Registry initialization should succeed");

  acds_config_t config;
  memset(&config, 0, sizeof(config));

  // Create password-protected session
  acip_session_create_t create_req;
  memset(&create_req, 0, sizeof(create_req));
  create_req.session_type = SESSION_TYPE_DIRECT_TCP;
  create_req.capabilities = 0x03;
  create_req.max_participants = 4;
  create_req.has_password = 1;
  create_req.expose_ip_publicly = 0;
  SAFE_STRNCPY(create_req.server_address, "192.168.1.100", sizeof(create_req.server_address));
  create_req.server_port = 27224;
  memset(create_req.password_hash, 0xAB, sizeof(create_req.password_hash));

  acip_session_created_t create_resp;
  result = session_create(&registry, &create_req, &config, &create_resp);
  cr_assert_eq(result, ASCIICHAT_OK, "Session creation should succeed");

  // Join with WRONG password
  acip_session_join_t join_req;
  memset(&join_req, 0, sizeof(join_req));
  join_req.session_string_len = (uint8_t)strlen(create_resp.session_string);
  SAFE_STRNCPY(join_req.session_string, create_resp.session_string, sizeof(join_req.session_string));
  join_req.has_password = 1;
  SAFE_STRNCPY(join_req.password, "wrong-password-456", sizeof(join_req.password));

  acip_session_joined_t join_resp;
  result = session_join(&registry, &join_req, &config, &join_resp);

  // Join should fail with wrong password
  // NOTE: This depends on password verification implementation in session_join()
  // If password verification is not yet implemented, this test may need adjustment
  cr_assert_neq(result, ASCIICHAT_OK, "Session join should fail with wrong password");

  session_registry_destroy(&registry);
}

/**
 * @brief Test that WebRTC sessions follow the same IP privacy rules
 */
Test(acds_ip_privacy, webrtc_session_ip_privacy) {
  session_registry_t registry;
  asciichat_error_t result = session_registry_init(&registry);
  cr_assert_eq(result, ASCIICHAT_OK, "Registry initialization should succeed");

  acds_config_t config;
  memset(&config, 0, sizeof(config));

  // Create WebRTC session WITHOUT password and WITHOUT expose_ip_publicly
  acip_session_create_t create_req;
  memset(&create_req, 0, sizeof(create_req));
  create_req.session_type = SESSION_TYPE_WEBRTC;
  create_req.capabilities = 0x03;
  create_req.max_participants = 4;
  create_req.has_password = 0;
  create_req.expose_ip_publicly = 0;
  SAFE_STRNCPY(create_req.server_address, "192.168.1.100", sizeof(create_req.server_address));
  create_req.server_port = 27224;

  acip_session_created_t create_resp;
  result = session_create(&registry, &create_req, &config, &create_resp);
  cr_assert_eq(result, ASCIICHAT_OK, "Session creation should succeed");

  // Join session
  acip_session_join_t join_req;
  memset(&join_req, 0, sizeof(join_req));
  join_req.session_string_len = (uint8_t)strlen(create_resp.session_string);
  SAFE_STRNCPY(join_req.session_string, create_resp.session_string, sizeof(join_req.session_string));
  join_req.has_password = 0;

  acip_session_joined_t join_resp;
  result = session_join(&registry, &join_req, &config, &join_resp);
  cr_assert_eq(result, ASCIICHAT_OK, "Session join should succeed");
  cr_assert_eq(join_resp.success, 1, "Join should be successful");

  // Verify IP is WITHHELD (WebRTC sessions follow same privacy rules)
  cr_assert_eq(join_resp.server_address[0], '\0',
               "WebRTC session IP should be withheld without password or opt-in");
  cr_assert_eq(join_resp.server_port, 0, "Server port should be zero");
  cr_assert_eq(join_resp.session_type, 0, "Session type should be zero");

  // TURN credentials should also NOT be generated (IP not revealed)
  cr_assert_eq(join_resp.turn_username[0], '\0', "TURN username should be empty");
  cr_assert_eq(join_resp.turn_password[0], '\0', "TURN password should be empty");

  session_registry_destroy(&registry);
}
