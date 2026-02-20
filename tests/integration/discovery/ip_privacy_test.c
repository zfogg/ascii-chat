/**
 * @file ip_privacy_test.c
 * @brief Integration tests for ACDS IP privacy controls
 *
 * Validates that server IP addresses are only disclosed after proper authentication:
 * - Password verification for password-protected sessions
 * - Explicit opt-in via expose_ip_publicly flag
 * - IP withheld for sessions without either mechanism
 *
 * Trigger: Testing Criterion to JUnit XML conversion for codecov test result metadata.
 *
 * This prevents IP address leakage to unauthenticated clients who only know
 * the session string.
 */

#include <ascii-chat/discovery/database.h>
#include <ascii-chat/discovery/session.h>
#include "discovery-service/main.h"
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/network/acip/acds.h>
#include <criterion/criterion.h>
#include <string.h>
#include <sodium.h>
#include <unistd.h>

// Test fixture
TestSuite(acds_ip_privacy, .timeout = 10.0);

// Helper to create a temporary database path
static void get_temp_db_path(char *buf, size_t buflen, const char *suffix) {
  snprintf(buf, buflen, "/tmp/acds_ip_privacy_%s_%d.db", suffix, getpid());
}

// Helper to clean up test database
static void cleanup_test_db(const char *path) {
  unlink(path);
  // Also remove WAL and SHM files
  char wal_path[256], shm_path[256];
  snprintf(wal_path, sizeof(wal_path), "%s-wal", path);
  snprintf(shm_path, sizeof(shm_path), "%s-shm", path);
  unlink(wal_path);
  unlink(shm_path);
}

/**
 * @brief Test that IP is revealed for password-protected session with correct password
 */
Test(acds_ip_privacy, password_protected_reveals_ip) {
  char db_path[256];
  get_temp_db_path(db_path, sizeof(db_path), "passwd_reveal");

  sqlite3 *db = NULL;
  asciichat_error_t result = database_init(db_path, &db);
  cr_assert_eq(result, ASCIICHAT_OK, "Database initialization should succeed");

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

  // Hash password "test-password-123" using Argon2id
  const char *password = "test-password-123";
  int hash_result = crypto_pwhash_str((char *)create_req.password_hash, password, strlen(password),
                                      crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE);
  cr_assert_eq(hash_result, 0, "Password hashing should succeed");

  acip_session_created_t create_resp;
  result = database_session_create(db, &create_req, &config, &create_resp);
  cr_assert_eq(result, ASCIICHAT_OK, "Session creation should succeed");

  // Join with correct password
  acip_session_join_t join_req;
  memset(&join_req, 0, sizeof(join_req));
  join_req.session_string_len = (uint8_t)strlen(create_resp.session_string);
  SAFE_STRNCPY(join_req.session_string, create_resp.session_string, sizeof(join_req.session_string));
  join_req.has_password = 1;
  SAFE_STRNCPY(join_req.password, "test-password-123", sizeof(join_req.password));

  acip_session_joined_t join_resp;
  result = database_session_join(db, &join_req, &config, &join_resp);
  cr_assert_eq(result, ASCIICHAT_OK, "Session join should succeed");
  cr_assert_eq(join_resp.success, 1, "Join should be successful");

  // Verify IP is revealed (password was verified)
  cr_assert_str_eq(join_resp.server_address, "192.168.1.100",
                   "Server address should be revealed after password verification");
  cr_assert_eq(join_resp.server_port, 27224, "Server port should be revealed");
  cr_assert_eq(join_resp.session_type, SESSION_TYPE_DIRECT_TCP, "Session type should be revealed");

  database_close(db);
  cleanup_test_db(db_path);
}

/**
 * @brief Test that IP is withheld for session without password or opt-in
 */
Test(acds_ip_privacy, no_password_no_optin_withholds_ip) {
  char db_path[256];
  get_temp_db_path(db_path, sizeof(db_path), "no_passwd");

  sqlite3 *db = NULL;
  asciichat_error_t result = database_init(db_path, &db);
  cr_assert_eq(result, ASCIICHAT_OK, "Database initialization should succeed");

  acds_config_t config;
  memset(&config, 0, sizeof(config));

  // Create session WITHOUT password and WITHOUT expose_ip_publicly
  acip_session_create_t create_req;
  memset(&create_req, 0, sizeof(create_req));
  create_req.session_type = SESSION_TYPE_DIRECT_TCP;
  create_req.capabilities = 0x03;
  create_req.max_participants = 4;
  create_req.has_password = 0;       // No password
  create_req.expose_ip_publicly = 0; // No explicit opt-in
  SAFE_STRNCPY(create_req.server_address, "192.168.1.100", sizeof(create_req.server_address));
  create_req.server_port = 27224;

  acip_session_created_t create_resp;
  result = database_session_create(db, &create_req, &config, &create_resp);
  cr_assert_eq(result, ASCIICHAT_OK, "Session creation should succeed");

  // Join session (no password required)
  acip_session_join_t join_req;
  memset(&join_req, 0, sizeof(join_req));
  join_req.session_string_len = (uint8_t)strlen(create_resp.session_string);
  SAFE_STRNCPY(join_req.session_string, create_resp.session_string, sizeof(join_req.session_string));
  join_req.has_password = 0;

  acip_session_joined_t join_resp;
  result = database_session_join(db, &join_req, &config, &join_resp);
  cr_assert_eq(result, ASCIICHAT_OK, "Session join should succeed");
  cr_assert_eq(join_resp.success, 1, "Join should be successful");

  // Verify IP is WITHHELD (security control active)
  cr_assert_eq(join_resp.server_address[0], '\0', "Server address should be withheld without password or opt-in");
  cr_assert_eq(join_resp.server_port, 0, "Server port should be zero");
  cr_assert_eq(join_resp.session_type, 0, "Session type should be zero");

  database_close(db);
  cleanup_test_db(db_path);
}

/**
 * @brief Test that IP is revealed for session with explicit expose_ip_publicly opt-in
 */
Test(acds_ip_privacy, explicit_optin_reveals_ip) {
  char db_path[256];
  get_temp_db_path(db_path, sizeof(db_path), "optin");

  sqlite3 *db = NULL;
  asciichat_error_t result = database_init(db_path, &db);
  cr_assert_eq(result, ASCIICHAT_OK, "Database initialization should succeed");

  acds_config_t config;
  memset(&config, 0, sizeof(config));

  // Create session with explicit IP exposure opt-in
  acip_session_create_t create_req;
  memset(&create_req, 0, sizeof(create_req));
  create_req.session_type = SESSION_TYPE_DIRECT_TCP;
  create_req.capabilities = 0x03;
  create_req.max_participants = 4;
  create_req.has_password = 0;       // No password
  create_req.expose_ip_publicly = 1; // Explicit opt-in
  SAFE_STRNCPY(create_req.server_address, "203.0.113.42", sizeof(create_req.server_address));
  create_req.server_port = 8080;

  acip_session_created_t create_resp;
  result = database_session_create(db, &create_req, &config, &create_resp);
  cr_assert_eq(result, ASCIICHAT_OK, "Session creation should succeed");

  // Join session (no password)
  acip_session_join_t join_req;
  memset(&join_req, 0, sizeof(join_req));
  join_req.session_string_len = (uint8_t)strlen(create_resp.session_string);
  SAFE_STRNCPY(join_req.session_string, create_resp.session_string, sizeof(join_req.session_string));
  join_req.has_password = 0;

  acip_session_joined_t join_resp;
  result = database_session_join(db, &join_req, &config, &join_resp);
  cr_assert_eq(result, ASCIICHAT_OK, "Session join should succeed");
  cr_assert_eq(join_resp.success, 1, "Join should be successful");

  // Verify IP is revealed (explicit opt-in)
  cr_assert_str_eq(join_resp.server_address, "203.0.113.42", "Server address should be revealed with explicit opt-in");
  cr_assert_eq(join_resp.server_port, 8080, "Server port should be revealed");
  cr_assert_eq(join_resp.session_type, SESSION_TYPE_DIRECT_TCP, "Session type should be revealed");

  database_close(db);
  cleanup_test_db(db_path);
}

/**
 * @brief Test that IP is withheld for password-protected session with WRONG password
 */
Test(acds_ip_privacy, wrong_password_withholds_ip) {
  char db_path[256];
  get_temp_db_path(db_path, sizeof(db_path), "wrong_passwd");

  sqlite3 *db = NULL;
  asciichat_error_t result = database_init(db_path, &db);
  cr_assert_eq(result, ASCIICHAT_OK, "Database initialization should succeed");

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

  // Hash the correct password
  const char *password = "test-password-123";
  int hash_result = crypto_pwhash_str((char *)create_req.password_hash, password, strlen(password),
                                      crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE);
  cr_assert_eq(hash_result, 0, "Password hashing should succeed");

  acip_session_created_t create_resp;
  result = database_session_create(db, &create_req, &config, &create_resp);
  cr_assert_eq(result, ASCIICHAT_OK, "Session creation should succeed");

  // Join with WRONG password
  acip_session_join_t join_req;
  memset(&join_req, 0, sizeof(join_req));
  join_req.session_string_len = (uint8_t)strlen(create_resp.session_string);
  SAFE_STRNCPY(join_req.session_string, create_resp.session_string, sizeof(join_req.session_string));
  join_req.has_password = 1;
  SAFE_STRNCPY(join_req.password, "wrong-password-456", sizeof(join_req.password));

  acip_session_joined_t join_resp;
  result = database_session_join(db, &join_req, &config, &join_resp);

  // database_session_join returns ASCIICHAT_OK but sets success=0 and error_code when password is wrong
  cr_assert_eq(result, ASCIICHAT_OK, "database_session_join should return OK");
  cr_assert_eq(join_resp.success, 0, "Join should fail with wrong password");
  cr_assert_eq(join_resp.error_code, ACIP_ERROR_INVALID_PASSWORD, "Error code should be INVALID_PASSWORD");

  database_close(db);
  cleanup_test_db(db_path);
}

/**
 * @brief Test that WebRTC sessions follow the same IP privacy rules
 */
Test(acds_ip_privacy, webrtc_session_ip_privacy) {
  char db_path[256];
  get_temp_db_path(db_path, sizeof(db_path), "webrtc");

  sqlite3 *db = NULL;
  asciichat_error_t result = database_init(db_path, &db);
  cr_assert_eq(result, ASCIICHAT_OK, "Database initialization should succeed");

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
  result = database_session_create(db, &create_req, &config, &create_resp);
  cr_assert_eq(result, ASCIICHAT_OK, "Session creation should succeed");

  // Join session
  acip_session_join_t join_req;
  memset(&join_req, 0, sizeof(join_req));
  join_req.session_string_len = (uint8_t)strlen(create_resp.session_string);
  SAFE_STRNCPY(join_req.session_string, create_resp.session_string, sizeof(join_req.session_string));
  join_req.has_password = 0;

  acip_session_joined_t join_resp;
  result = database_session_join(db, &join_req, &config, &join_resp);
  cr_assert_eq(result, ASCIICHAT_OK, "Session join should succeed");
  cr_assert_eq(join_resp.success, 1, "Join should be successful");

  // Verify IP is WITHHELD (WebRTC sessions follow same privacy rules)
  cr_assert_eq(join_resp.server_address[0], '\0', "WebRTC session IP should be withheld without password or opt-in");
  cr_assert_eq(join_resp.server_port, 0, "Server port should be zero");
  cr_assert_eq(join_resp.session_type, 0, "Session type should be zero");

  // TURN credentials should also NOT be generated (IP not revealed)
  cr_assert_eq(join_resp.turn_username[0], '\0', "TURN username should be empty");
  cr_assert_eq(join_resp.turn_password[0], '\0', "TURN password should be empty");

  database_close(db);
  cleanup_test_db(db_path);
}
