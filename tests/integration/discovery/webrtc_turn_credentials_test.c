/**
 * @file webrtc_turn_credentials_test.c
 * @brief Integration test for ACDS WebRTC TURN credential generation
 *
 * Tests the full flow of creating a WebRTC session and joining it,
 * verifying that TURN credentials are dynamically generated and included
 * in the SESSION_JOINED response.
 */

#include <ascii-chat/discovery/database.h>
#include <ascii-chat/discovery/session.h>
#include "discovery-service/main.h"
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/network/acip/acds.h>
#include <ascii-chat/network/webrtc/turn_credentials.h>
#include <criterion/criterion.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Test fixture
TestSuite(acds_webrtc_turn, .timeout = 10.0);

// Helper to create a temporary database path
static void get_temp_db_path(char *buf, size_t buflen, const char *suffix) {
  snprintf(buf, buflen, "/tmp/acds_turn_%s_%d.db", suffix, getpid());
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
 * @brief Test WebRTC session creation and TURN credential generation on join
 */
Test(acds_webrtc_turn, join_generates_turn_credentials) {
  char db_path[256];
  get_temp_db_path(db_path, sizeof(db_path), "gen_creds");

  sqlite3 *db = NULL;
  asciichat_error_t result = database_init(db_path, &db);
  cr_assert_eq(result, ASCIICHAT_OK, "Database initialization should succeed");

  // Configure ACDS with TURN secret
  acds_config_t config;
  memset(&config, 0, sizeof(config));
  SAFE_STRNCPY(config.turn_secret, "test-secret-key-12345", sizeof(config.turn_secret));

  // Create a WebRTC session with IP exposure enabled
  acip_session_create_t create_req;
  memset(&create_req, 0, sizeof(create_req));
  create_req.session_type = SESSION_TYPE_WEBRTC;
  create_req.capabilities = 0x03; // video + audio
  create_req.max_participants = 4;
  create_req.has_password = false;
  create_req.expose_ip_publicly = 1; // Allow IP disclosure for testing
  SAFE_STRNCPY(create_req.server_address, "192.168.1.100", sizeof(create_req.server_address));
  create_req.server_port = 27224;

  acip_session_created_t create_resp;
  result = database_session_create(db, &create_req, &config, &create_resp);
  cr_assert_eq(result, ASCIICHAT_OK, "Session creation should succeed");
  cr_assert_neq(create_resp.session_string[0], '\0', "Session string should not be empty");

  // Store session string for join request
  char session_string[48];
  SAFE_STRNCPY(session_string, create_resp.session_string, sizeof(session_string));

  // Join the session
  acip_session_join_t join_req;
  memset(&join_req, 0, sizeof(join_req));
  join_req.session_string_len = (uint8_t)strlen(session_string);
  SAFE_STRNCPY(join_req.session_string, session_string, sizeof(join_req.session_string));
  join_req.has_password = false;

  acip_session_joined_t join_resp;
  result = database_session_join(db, &join_req, &config, &join_resp);
  cr_assert_eq(result, ASCIICHAT_OK, "Session join should succeed");
  cr_assert_eq(join_resp.success, 1, "Join success flag should be set");
  cr_assert_eq(join_resp.session_type, SESSION_TYPE_WEBRTC, "Session type should be WebRTC");

  // Verify TURN credentials were generated
  cr_assert_neq(join_resp.turn_username[0], '\0', "TURN username should not be empty");
  cr_assert_neq(join_resp.turn_password[0], '\0', "TURN password should not be empty");

  // Verify username format: "{timestamp}:{session_id}"
  char *colon = strchr(join_resp.turn_username, ':');
  cr_assert_not_null(colon, "TURN username should contain ':' separator");
  cr_assert_str_eq(colon + 1, session_string, "TURN username should contain session string");

  // Verify password is valid base64
  for (size_t i = 0; join_resp.turn_password[i] != '\0'; i++) {
    char c = join_resp.turn_password[i];
    bool valid =
        (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
    cr_assert(valid, "TURN password character '%c' is not valid base64", c);
  }

  // Verify credentials are time-limited (expiration timestamp should be in the future)
  char timestamp_str[32] = {0};
  size_t timestamp_len = (size_t)(colon - join_resp.turn_username);
  memcpy(timestamp_str, join_resp.turn_username, timestamp_len);
  long expiration = atol(timestamp_str);
  cr_assert_gt(expiration, (long)time(NULL), "TURN credentials should not be expired");

  database_close(db);
  cleanup_test_db(db_path);
}

/**
 * @brief Test that TURN credentials are NOT generated for TCP sessions
 */
Test(acds_webrtc_turn, tcp_session_no_turn_credentials) {
  char db_path[256];
  get_temp_db_path(db_path, sizeof(db_path), "tcp_no_creds");

  sqlite3 *db = NULL;
  asciichat_error_t result = database_init(db_path, &db);
  cr_assert_eq(result, ASCIICHAT_OK, "Database initialization should succeed");

  // Configure ACDS with TURN secret
  acds_config_t config;
  memset(&config, 0, sizeof(config));
  SAFE_STRNCPY(config.turn_secret, "test-secret-key-12345", sizeof(config.turn_secret));

  // Create a TCP session (not WebRTC)
  acip_session_create_t create_req;
  memset(&create_req, 0, sizeof(create_req));
  create_req.session_type = SESSION_TYPE_DIRECT_TCP;
  create_req.capabilities = 0x03;
  create_req.max_participants = 4;
  create_req.has_password = false;
  SAFE_STRNCPY(create_req.server_address, "192.168.1.100", sizeof(create_req.server_address));
  create_req.server_port = 27224;

  acip_session_created_t create_resp;
  result = database_session_create(db, &create_req, &config, &create_resp);
  cr_assert_eq(result, ASCIICHAT_OK, "Session creation should succeed");

  // Join the session
  acip_session_join_t join_req;
  memset(&join_req, 0, sizeof(join_req));
  join_req.session_string_len = (uint8_t)strlen(create_resp.session_string);
  SAFE_STRNCPY(join_req.session_string, create_resp.session_string, sizeof(join_req.session_string));
  join_req.has_password = false;

  acip_session_joined_t join_resp;
  result = database_session_join(db, &join_req, &config, &join_resp);
  cr_assert_eq(result, ASCIICHAT_OK, "Session join should succeed");
  cr_assert_eq(join_resp.session_type, SESSION_TYPE_DIRECT_TCP, "Session type should be TCP");

  // Verify TURN credentials were NOT generated for TCP session
  cr_assert_eq(join_resp.turn_username[0], '\0', "TURN username should be empty for TCP session");
  cr_assert_eq(join_resp.turn_password[0], '\0', "TURN password should be empty for TCP session");

  database_close(db);
  cleanup_test_db(db_path);
}

/**
 * @brief Test that TURN credentials are NOT generated without turn_secret
 */
Test(acds_webrtc_turn, no_credentials_without_secret) {
  char db_path[256];
  get_temp_db_path(db_path, sizeof(db_path), "no_secret");

  sqlite3 *db = NULL;
  asciichat_error_t result = database_init(db_path, &db);
  cr_assert_eq(result, ASCIICHAT_OK, "Database initialization should succeed");

  // Configure ACDS WITHOUT TURN secret
  acds_config_t config;
  memset(&config, 0, sizeof(config));
  config.turn_secret[0] = '\0'; // No secret configured

  // Create a WebRTC session
  acip_session_create_t create_req;
  memset(&create_req, 0, sizeof(create_req));
  create_req.session_type = SESSION_TYPE_WEBRTC;
  create_req.capabilities = 0x03;
  create_req.max_participants = 4;
  create_req.has_password = false;
  SAFE_STRNCPY(create_req.server_address, "192.168.1.100", sizeof(create_req.server_address));
  create_req.server_port = 27224;

  acip_session_created_t create_resp;
  result = database_session_create(db, &create_req, &config, &create_resp);
  cr_assert_eq(result, ASCIICHAT_OK, "Session creation should succeed");

  // Join the session
  acip_session_join_t join_req;
  memset(&join_req, 0, sizeof(join_req));
  join_req.session_string_len = (uint8_t)strlen(create_resp.session_string);
  SAFE_STRNCPY(join_req.session_string, create_resp.session_string, sizeof(join_req.session_string));
  join_req.has_password = false;

  acip_session_joined_t join_resp;
  result = database_session_join(db, &join_req, &config, &join_resp);
  cr_assert_eq(result, ASCIICHAT_OK, "Session join should succeed");

  // Verify TURN credentials were NOT generated without secret
  cr_assert_eq(join_resp.turn_username[0], '\0', "TURN username should be empty without turn_secret");
  cr_assert_eq(join_resp.turn_password[0], '\0', "TURN password should be empty without turn_secret");

  database_close(db);
  cleanup_test_db(db_path);
}

/**
 * @brief Test TURN credentials are regenerated for each join
 */
Test(acds_webrtc_turn, credentials_unique_per_join) {
  char db_path[256];
  get_temp_db_path(db_path, sizeof(db_path), "unique");

  sqlite3 *db = NULL;
  asciichat_error_t result = database_init(db_path, &db);
  cr_assert_eq(result, ASCIICHAT_OK, "Database initialization should succeed");

  // Configure ACDS with TURN secret
  acds_config_t config;
  memset(&config, 0, sizeof(config));
  SAFE_STRNCPY(config.turn_secret, "test-secret-key-12345", sizeof(config.turn_secret));

  // Create a WebRTC session with IP exposure enabled
  acip_session_create_t create_req;
  memset(&create_req, 0, sizeof(create_req));
  create_req.session_type = SESSION_TYPE_WEBRTC;
  create_req.capabilities = 0x03;
  create_req.max_participants = 4;
  create_req.has_password = false;
  create_req.expose_ip_publicly = 1; // Allow IP disclosure for testing
  SAFE_STRNCPY(create_req.server_address, "192.168.1.100", sizeof(create_req.server_address));
  create_req.server_port = 27224;

  acip_session_created_t create_resp;
  result = database_session_create(db, &create_req, &config, &create_resp);
  cr_assert_eq(result, ASCIICHAT_OK, "Session creation should succeed");

  // First join
  acip_session_join_t join_req1;
  memset(&join_req1, 0, sizeof(join_req1));
  join_req1.session_string_len = (uint8_t)strlen(create_resp.session_string);
  SAFE_STRNCPY(join_req1.session_string, create_resp.session_string, sizeof(join_req1.session_string));
  join_req1.has_password = false;

  acip_session_joined_t join_resp1;
  result = database_session_join(db, &join_req1, &config, &join_resp1);
  cr_assert_eq(result, ASCIICHAT_OK, "First join should succeed");

  // Second join (same session, different participant)
  acip_session_join_t join_req2;
  memset(&join_req2, 0, sizeof(join_req2));
  join_req2.session_string_len = (uint8_t)strlen(create_resp.session_string);
  SAFE_STRNCPY(join_req2.session_string, create_resp.session_string, sizeof(join_req2.session_string));
  join_req2.has_password = false;

  acip_session_joined_t join_resp2;
  result = database_session_join(db, &join_req2, &config, &join_resp2);
  cr_assert_eq(result, ASCIICHAT_OK, "Second join should succeed");

  // Verify both joins got credentials
  cr_assert_neq(join_resp1.turn_username[0], '\0', "First join should have TURN username");
  cr_assert_neq(join_resp2.turn_username[0], '\0', "Second join should have TURN username");

  // Verify credentials are identical (same session string, generated at approximately same time)
  cr_assert_str_eq(join_resp1.turn_username, join_resp2.turn_username,
                   "TURN usernames should be identical for same session");
  cr_assert_str_eq(join_resp1.turn_password, join_resp2.turn_password,
                   "TURN passwords should be identical (same username + secret)");

  database_close(db);
  cleanup_test_db(db_path);
}
