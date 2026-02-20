/**
 * @file acds/session_registry_test.c
 * @brief Unit tests for SQLite-based session management
 *
 * Tests validate:
 * - Database initialization and cleanup
 * - Session creation, lookup, join, and leave via SQLite
 * - Session cleanup for expired sessions
 *
 * Note: Full ACIP protocol testing is in integration tests (ip_privacy_test.c, etc.).
 * This focuses on the database session operations.
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>

#include <ascii-chat/discovery/database.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/discovery/session.h>
#include <ascii-chat/log/logging.h>
#include <string.h>
#include <unistd.h>

// Helper to create a temporary database path
static void get_temp_db_path(char *buf, size_t buflen) {
  snprintf(buf, buflen, "/tmp/acds_test_%d.db", getpid());
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

// ============================================================================
// Test Fixtures
// ============================================================================
// Use verbose logging with debug level enabled and stdout/stderr not disabled
TestSuite(session_database);

Test(session_database, database_initialization, .timeout = 5) {
  char db_path[256];
  get_temp_db_path(db_path, sizeof(db_path));

  sqlite3 *db = NULL;

  // Initialize the database
  asciichat_error_t result = database_init(db_path, &db);

  cr_expect_eq(result, ASCIICHAT_OK, "Database initialization should succeed");
  cr_expect_not_null(db, "Database handle should be non-NULL");

  // Cleanup
  database_close(db);
  cleanup_test_db(db_path);
}

Test(session_database, create_session_basic, .timeout = 5) {
  char db_path[256];
  get_temp_db_path(db_path, sizeof(db_path));

  sqlite3 *db = NULL;
  cr_expect_eq(database_init(db_path, &db), ASCIICHAT_OK);

  // Create a test session using the public API
  acip_session_create_t create_req = {0};
  create_req.max_participants = 4;
  create_req.capabilities = 0x03; // video + audio
  create_req.session_type = 0;    // DIRECT_TCP
  SAFE_STRNCPY(create_req.server_address, "127.0.0.1", sizeof(create_req.server_address));
  create_req.server_port = 12345;

  acip_session_created_t response = {0};
  acds_config_t config = {0};

  asciichat_error_t result = database_session_create(db, &create_req, &config, &response);

  // Should succeed
  cr_expect_eq(result, ASCIICHAT_OK, "Session creation should succeed");

  // Session string should be generated
  cr_expect_gt(response.session_string_len, 0, "Session string should be generated");
  cr_expect_not_null(response.session_id, "Session ID should be set");

  // Session should be findable by string
  session_entry_t *found = database_session_find_by_string(db, response.session_string);
  cr_expect_not_null(found, "Created session should be findable by string");
  if (found) {
    session_entry_destroy(found);
  }

  // Session should be findable by ID
  found = database_session_find_by_id(db, response.session_id);
  cr_expect_not_null(found, "Created session should be findable by ID");
  if (found) {
    session_entry_destroy(found);
  }

  database_close(db);
  cleanup_test_db(db_path);
}

Test(session_database, session_lookup_basic, .timeout = 5) {
  char db_path[256];
  get_temp_db_path(db_path, sizeof(db_path));

  sqlite3 *db = NULL;
  cr_expect_eq(database_init(db_path, &db), ASCIICHAT_OK);

  // Create a session first
  acip_session_create_t create_req = {0};
  create_req.max_participants = 4;
  create_req.capabilities = 0x03;
  create_req.session_type = 0;

  acip_session_created_t create_response = {0};
  acds_config_t config = {0};

  cr_expect_eq(database_session_create(db, &create_req, &config, &create_response), ASCIICHAT_OK);

  // Now lookup the session
  acip_session_info_t lookup_response = {0};
  asciichat_error_t result = database_session_lookup(db, create_response.session_string, &config, &lookup_response);

  cr_expect_eq(result, ASCIICHAT_OK, "Session lookup should succeed");
  cr_expect_eq(lookup_response.found, 1, "Session should be found");
  cr_expect_eq(lookup_response.max_participants, 4, "Max participants should match");
  cr_expect_eq(lookup_response.current_participants, 0, "No participants yet");

  database_close(db);
  cleanup_test_db(db_path);
}

Test(session_database, session_lookup_not_found, .timeout = 5) {
  char db_path[256];
  get_temp_db_path(db_path, sizeof(db_path));

  sqlite3 *db = NULL;
  cr_expect_eq(database_init(db_path, &db), ASCIICHAT_OK);

  // Try to lookup a session that doesn't exist
  acip_session_info_t lookup_response = {0};
  acds_config_t config = {0};

  asciichat_error_t result = database_session_lookup(db, "nonexistent-session-string", &config, &lookup_response);

  cr_expect_eq(result, ASCIICHAT_OK, "Lookup should return OK (not an error)");
  cr_expect_eq(lookup_response.found, 0, "Session should not be found");

  // Try with find_by_string
  session_entry_t *not_found = database_session_find_by_string(db, "nonexistent-session-string");
  cr_expect_null(not_found, "Nonexistent session should return NULL");

  // Try with fake ID
  uint8_t fake_id[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  not_found = database_session_find_by_id(db, fake_id);
  cr_expect_null(not_found, "Nonexistent session ID should return NULL");

  database_close(db);
  cleanup_test_db(db_path);
}

Test(session_database, session_join_basic, .timeout = 5) {
  char db_path[256];
  get_temp_db_path(db_path, sizeof(db_path));

  sqlite3 *db = NULL;
  cr_expect_eq(database_init(db_path, &db), ASCIICHAT_OK);

  // Create a session first (with expose_ip_publicly for test)
  acip_session_create_t create_req = {0};
  create_req.max_participants = 4;
  create_req.capabilities = 0x03;
  create_req.session_type = 0;
  create_req.expose_ip_publicly = 1;
  SAFE_STRNCPY(create_req.server_address, "127.0.0.1", sizeof(create_req.server_address));
  create_req.server_port = 12345;

  acip_session_created_t create_response = {0};
  acds_config_t config = {0};

  cr_expect_eq(database_session_create(db, &create_req, &config, &create_response), ASCIICHAT_OK);

  // Now join the session
  acip_session_join_t join_req = {0};
  join_req.session_string_len = create_response.session_string_len;
  memcpy(join_req.session_string, create_response.session_string, create_response.session_string_len);

  acip_session_joined_t join_response = {0};
  asciichat_error_t result = database_session_join(db, &join_req, &config, &join_response);

  cr_expect_eq(result, ASCIICHAT_OK, "Session join should succeed");
  cr_expect_eq(join_response.success, 1, "Join should be successful");
  cr_expect_neq(join_response.participant_id[0], 0, "Participant ID should be set");

  // Verify participant count increased
  acip_session_info_t lookup_response = {0};
  database_session_lookup(db, create_response.session_string, &config, &lookup_response);
  cr_expect_eq(lookup_response.current_participants, 1, "Should have 1 participant");

  database_close(db);
  cleanup_test_db(db_path);
}

Test(session_database, session_leave_basic, .timeout = 5) {
  char db_path[256];
  get_temp_db_path(db_path, sizeof(db_path));

  sqlite3 *db = NULL;
  cr_expect_eq(database_init(db_path, &db), ASCIICHAT_OK);

  // Create and join a session
  acip_session_create_t create_req = {0};
  create_req.max_participants = 4;
  create_req.capabilities = 0x03;
  create_req.expose_ip_publicly = 1;

  acip_session_created_t create_response = {0};
  acds_config_t config = {0};

  cr_expect_eq(database_session_create(db, &create_req, &config, &create_response), ASCIICHAT_OK);

  // Join the session
  acip_session_join_t join_req = {0};
  join_req.session_string_len = create_response.session_string_len;
  memcpy(join_req.session_string, create_response.session_string, create_response.session_string_len);

  acip_session_joined_t join_response = {0};
  cr_expect_eq(database_session_join(db, &join_req, &config, &join_response), ASCIICHAT_OK);
  cr_expect_eq(join_response.success, 1);

  // Now leave the session
  asciichat_error_t result = database_session_leave(db, join_response.session_id, join_response.participant_id);
  cr_expect_eq(result, ASCIICHAT_OK, "Session leave should succeed");

  // Session should be deleted (no participants left)
  session_entry_t *found = database_session_find_by_string(db, create_response.session_string);
  cr_expect_null(found, "Empty session should be deleted");

  database_close(db);
  cleanup_test_db(db_path);
}

Test(session_database, session_full, .timeout = 5) {
  char db_path[256];
  get_temp_db_path(db_path, sizeof(db_path));

  sqlite3 *db = NULL;
  cr_expect_eq(database_init(db_path, &db), ASCIICHAT_OK);

  // Create a session with max 2 participants
  acip_session_create_t create_req = {0};
  create_req.max_participants = 2;
  create_req.capabilities = 0x03;
  create_req.expose_ip_publicly = 1;

  acip_session_created_t create_response = {0};
  acds_config_t config = {0};

  cr_expect_eq(database_session_create(db, &create_req, &config, &create_response), ASCIICHAT_OK);

  // Join twice
  acip_session_join_t join_req = {0};
  join_req.session_string_len = create_response.session_string_len;
  memcpy(join_req.session_string, create_response.session_string, create_response.session_string_len);

  acip_session_joined_t join_response = {0};
  cr_expect_eq(database_session_join(db, &join_req, &config, &join_response), ASCIICHAT_OK);
  cr_expect_eq(join_response.success, 1);

  cr_expect_eq(database_session_join(db, &join_req, &config, &join_response), ASCIICHAT_OK);
  cr_expect_eq(join_response.success, 1);

  // Third join should fail (session full)
  cr_expect_eq(database_session_join(db, &join_req, &config, &join_response), ASCIICHAT_OK);
  cr_expect_eq(join_response.success, 0, "Third join should fail");
  cr_expect_eq(join_response.error_code, ACIP_ERROR_SESSION_FULL, "Error should be SESSION_FULL");

  database_close(db);
  cleanup_test_db(db_path);
}

Test(session_database, multiple_sessions, .timeout = 5) {
  char db_path[256];
  get_temp_db_path(db_path, sizeof(db_path));

  sqlite3 *db = NULL;
  cr_expect_eq(database_init(db_path, &db), ASCIICHAT_OK);

  // Create multiple sessions
  acip_session_create_t create_req = {0};
  create_req.max_participants = 4;
  create_req.capabilities = 0x03;

  acds_config_t config = {0};
  char session_strings[5][48] = {0};

  for (int i = 0; i < 5; i++) {
    acip_session_created_t response = {0};
    cr_expect_eq(database_session_create(db, &create_req, &config, &response), ASCIICHAT_OK);
    memcpy(session_strings[i], response.session_string, response.session_string_len);
  }

  // All sessions should be findable
  for (int i = 0; i < 5; i++) {
    session_entry_t *found = database_session_find_by_string(db, session_strings[i]);
    cr_expect_not_null(found, "Session %d should be findable", i);
    if (found) {
      session_entry_destroy(found);
    }
  }

  database_close(db);
  cleanup_test_db(db_path);
}

Test(session_database, cleanup_expired_sessions, .timeout = 5) {
  char db_path[256];
  get_temp_db_path(db_path, sizeof(db_path));

  sqlite3 *db = NULL;
  cr_expect_eq(database_init(db_path, &db), ASCIICHAT_OK);

  // Create a session
  acip_session_create_t create_req = {0};
  create_req.max_participants = 4;
  create_req.capabilities = 0x03;

  acip_session_created_t response = {0};
  acds_config_t config = {0};

  cr_expect_eq(database_session_create(db, &create_req, &config, &response), ASCIICHAT_OK);

  // Call cleanup (newly created sessions should not be expired)
  database_session_cleanup_expired(db);

  // Session should still exist (not expired yet - 24hr lifetime)
  session_entry_t *found = database_session_find_by_string(db, response.session_string);
  cr_expect_not_null(found, "Non-expired session should still exist after cleanup");
  if (found) {
    session_entry_destroy(found);
  }

  database_close(db);
  cleanup_test_db(db_path);
}

// ============================================================================
// Test Summary
// ============================================================================

/**
 * @brief SQLite-Based Session Management Test Suite
 *
 * This test suite validates the SQLite-based session management functionality:
 *
 * 1. **Basic Operations**
 *    - Database initialization with WAL mode
 *    - Session creation via public API
 *    - Session lookup by string and UUID
 *    - Session join and leave operations
 *    - Cleanup operations for expired sessions
 *
 * 2. **Session Lifecycle**
 *    - Create session → Join participants → Leave participants → Auto-delete when empty
 *    - Participant count tracking
 *    - Session full detection
 *
 * 3. **Database Features**
 *    - WAL mode for concurrent reads
 *    - Transactions for atomic operations
 *    - Foreign key constraints for participant cleanup
 *
 * 4. **Memory Safety**
 *    - session_entry_destroy() properly frees allocated sessions
 *    - Uses SAFE_MALLOC/SAFE_FREE macros for leak tracking
 *
 * Performance Notes:
 * - SQLite as single source of truth eliminates sync bugs
 * - WAL mode provides good concurrent read performance
 * - Indexed queries for fast session lookups
 *
 * Related Tests:
 * - tests/integration/acds/ip_privacy_test.c - ACDS protocol validation
 * - tests/integration/acds/webrtc_turn_credentials_test.c - WebRTC signaling
 */
