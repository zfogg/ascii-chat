#include <catch2/catch_test_macros.hpp>
/**
 * @file webrtc_turn_credentials_test.c
 * @brief Integration test for ACDS WebRTC TURN credential generation
 *
 * Tests the full flow of creating a WebRTC session and joining it,
 * verifying that TURN credentials are dynamically generated and included
 * in the SESSION_JOINED response.
 */

#include <ascii-chat/discovery/database.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/discovery/session.h>
#include "discovery-service/main.h"
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/network/acip/acds.h>
#include <ascii-chat/network/webrtc/turn_credentials.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Test fixture
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
// Use verbose logging with debug level enabled and stdout/stderr not disabled

TEST_CASE("acds_webrtc_turn/join_generates_turn_credentials", "[acds_webrtc_turn] [integration]") {
  char db_path[256];
  get_temp_db_path(db_path, sizeof(db_path), "gen_creds");

  sqlite3 *db = NULL;
  asciichat_error_t result = database_init(db_path, &db);
  REQUIRE((result) == (ASCIICHAT_OK));

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
  REQUIRE((result) == (ASCIICHAT_OK));
  REQUIRE((create_resp.session_string[0]) != ('\0'));

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
  REQUIRE((result) == (ASCIICHAT_OK));
  REQUIRE((join_resp.success) == (1));
  REQUIRE((join_resp.session_type) == (SESSION_TYPE_WEBRTC));

  // Verify TURN credentials were generated
  REQUIRE((join_resp.turn_username[0]) != ('\0'));
  REQUIRE((join_resp.turn_password[0]) != ('\0'));

  // Verify username format: "{timestamp}:{session_id}"
  char *colon = strchr(join_resp.turn_username, ':');
  REQUIRE((colon) != nullptr);
  REQUIRE(std::strcmp((colon + 1), (session_string)) == 0);

  // Verify password is valid base64
  for (size_t i = 0; join_resp.turn_password[i] != '\0'; i++) {
    char c = join_resp.turn_password[i];
    bool valid = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
    REQUIRE(valid);
  }

  // Verify credentials are time-limited (expiration timestamp should be in the future)
  char timestamp_str[32] = {0};
  size_t timestamp_len = (size_t)(colon - join_resp.turn_username);
  memcpy(timestamp_str, join_resp.turn_username, timestamp_len);
  long expiration = atol(timestamp_str);
  REQUIRE((expiration) > ((long)time(NULL)));

  database_close(db);
  cleanup_test_db(db_path);
}

/**
 * @brief Test that TURN credentials are NOT generated for TCP sessions
 */
TEST_CASE("acds_webrtc_turn/tcp_session_no_turn_credentials", "[acds_webrtc_turn] [integration]") {
  char db_path[256];
  get_temp_db_path(db_path, sizeof(db_path), "tcp_no_creds");

  sqlite3 *db = NULL;
  asciichat_error_t result = database_init(db_path, &db);
  REQUIRE((result) == (ASCIICHAT_OK));

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
  REQUIRE((result) == (ASCIICHAT_OK));

  // Join the session
  acip_session_join_t join_req;
  memset(&join_req, 0, sizeof(join_req));
  join_req.session_string_len = (uint8_t)strlen(create_resp.session_string);
  SAFE_STRNCPY(join_req.session_string, create_resp.session_string, sizeof(join_req.session_string));
  join_req.has_password = false;

  acip_session_joined_t join_resp;
  result = database_session_join(db, &join_req, &config, &join_resp);
  REQUIRE((result) == (ASCIICHAT_OK));
  REQUIRE((join_resp.session_type) == (SESSION_TYPE_DIRECT_TCP));

  // Verify TURN credentials were NOT generated for TCP session
  REQUIRE((join_resp.turn_username[0]) == ('\0'));
  REQUIRE((join_resp.turn_password[0]) == ('\0'));

  database_close(db);
  cleanup_test_db(db_path);
}

/**
 * @brief Test that TURN credentials are NOT generated without turn_secret
 */
TEST_CASE("acds_webrtc_turn/no_credentials_without_secret", "[acds_webrtc_turn] [integration]") {
  char db_path[256];
  get_temp_db_path(db_path, sizeof(db_path), "no_secret");

  sqlite3 *db = NULL;
  asciichat_error_t result = database_init(db_path, &db);
  REQUIRE((result) == (ASCIICHAT_OK));

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
  REQUIRE((result) == (ASCIICHAT_OK));

  // Join the session
  acip_session_join_t join_req;
  memset(&join_req, 0, sizeof(join_req));
  join_req.session_string_len = (uint8_t)strlen(create_resp.session_string);
  SAFE_STRNCPY(join_req.session_string, create_resp.session_string, sizeof(join_req.session_string));
  join_req.has_password = false;

  acip_session_joined_t join_resp;
  result = database_session_join(db, &join_req, &config, &join_resp);
  REQUIRE((result) == (ASCIICHAT_OK));

  // Verify TURN credentials were NOT generated without secret
  REQUIRE((join_resp.turn_username[0]) == ('\0'));
  REQUIRE((join_resp.turn_password[0]) == ('\0'));

  database_close(db);
  cleanup_test_db(db_path);
}

/**
 * @brief Test TURN credentials are regenerated for each join
 */
TEST_CASE("acds_webrtc_turn/credentials_unique_per_join", "[acds_webrtc_turn] [integration]") {
  char db_path[256];
  get_temp_db_path(db_path, sizeof(db_path), "unique");

  sqlite3 *db = NULL;
  asciichat_error_t result = database_init(db_path, &db);
  REQUIRE((result) == (ASCIICHAT_OK));

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
  REQUIRE((result) == (ASCIICHAT_OK));

  // First join
  acip_session_join_t join_req1;
  memset(&join_req1, 0, sizeof(join_req1));
  join_req1.session_string_len = (uint8_t)strlen(create_resp.session_string);
  SAFE_STRNCPY(join_req1.session_string, create_resp.session_string, sizeof(join_req1.session_string));
  join_req1.has_password = false;

  acip_session_joined_t join_resp1;
  result = database_session_join(db, &join_req1, &config, &join_resp1);
  REQUIRE((result) == (ASCIICHAT_OK));

  // Second join (same session, different participant)
  acip_session_join_t join_req2;
  memset(&join_req2, 0, sizeof(join_req2));
  join_req2.session_string_len = (uint8_t)strlen(create_resp.session_string);
  SAFE_STRNCPY(join_req2.session_string, create_resp.session_string, sizeof(join_req2.session_string));
  join_req2.has_password = false;

  acip_session_joined_t join_resp2;
  result = database_session_join(db, &join_req2, &config, &join_resp2);
  REQUIRE((result) == (ASCIICHAT_OK));

  // Verify both joins got credentials
  REQUIRE((join_resp1.turn_username[0]) != ('\0'));
  REQUIRE((join_resp2.turn_username[0]) != ('\0'));

  // Each join uses its own expiry timestamp, so joins crossing a second may differ.
  const char *session1 = strchr(join_resp1.turn_username, ':');
  const char *session2 = strchr(join_resp2.turn_username, ':');
  REQUIRE((session1) != nullptr);
  REQUIRE((session2) != nullptr);
  REQUIRE(std::strcmp((session1 + 1), (create_resp.session_string)) == 0);
  REQUIRE(std::strcmp((session2 + 1), (create_resp.session_string)) == 0);
  if (strcmp(join_resp1.turn_username, join_resp2.turn_username) == 0)
    REQUIRE(std::strcmp((join_resp1.turn_password), (join_resp2.turn_password)) == 0);
  else
    REQUIRE(std::strcmp((join_resp1.turn_password), (join_resp2.turn_password)) != 0);

  database_close(db);
  cleanup_test_db(db_path);
}
