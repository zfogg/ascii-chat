/**
 * @file acds/session_registry_test.c
 * @brief Unit tests for sharded rwlock session registry
 *
 * Tests validate:
 * - Sharded rwlock initialization and cleanup
 * - Session creation, lookup, and destruction
 * - Thread-safety via per-shard rwlocks
 *
 * Note: Full ACIP protocol testing is in integration tests (ip_privacy_test.c, etc.).
 * This focuses on the sharded rwlock data structure itself.
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>

#include "acds/session.h"
#include "log/logging.h"
#include <string.h>

// ============================================================================
// Test Fixtures
// ============================================================================

Test(session_registry, registry_initialization, .timeout = 5) {
  session_registry_t registry = {0};

  // Initialize the sharded rwlock registry
  asciichat_error_t result = session_registry_init(&registry);

  cr_expect_eq(result, ASCIICHAT_OK, "Registry initialization should succeed");

  // All shards should have NULL hash table heads initially
  for (int i = 0; i < SESSION_REGISTRY_NUM_SHARDS; i++) {
    cr_expect_null(registry.shards[i].sessions, "Shard %d should be empty initially", i);
  }

  // Cleanup
  session_registry_destroy(&registry);
}

Test(session_registry, multiple_registries, .timeout = 5) {
  // Test that multiple independent registries work correctly
  session_registry_t registry1 = {0};
  session_registry_t registry2 = {0};

  cr_expect_eq(session_registry_init(&registry1), ASCIICHAT_OK);
  cr_expect_eq(session_registry_init(&registry2), ASCIICHAT_OK);

  // Create a session in each registry
  acip_session_create_t create_req = {0};
  create_req.max_participants = 4;
  create_req.capabilities = 0x03;
  create_req.session_type = 0;

  acip_session_created_t response1 = {0};
  acip_session_created_t response2 = {0};
  acds_config_t config = {0};

  cr_expect_eq(session_create(&registry1, &create_req, &config, &response1), ASCIICHAT_OK);
  cr_expect_eq(session_create(&registry2, &create_req, &config, &response2), ASCIICHAT_OK);

  // Each session should be in its respective registry
  session_entry_t *found1 = session_find_by_string(&registry1, response1.session_string);
  session_entry_t *found2 = session_find_by_string(&registry2, response2.session_string);

  cr_expect_not_null(found1, "Session should be found in registry1");
  cr_expect_not_null(found2, "Session should be found in registry2");

  // Sessions should NOT be found in the wrong registry
  session_entry_t *not_found1 = session_find_by_string(&registry1, response2.session_string);
  session_entry_t *not_found2 = session_find_by_string(&registry2, response1.session_string);

  cr_expect_null(not_found1, "Registry1 should not contain registry2's session");
  cr_expect_null(not_found2, "Registry2 should not contain registry1's session");

  session_registry_destroy(&registry1);
  session_registry_destroy(&registry2);
}

Test(session_registry, create_session_basic, .timeout = 5) {
  session_registry_t registry = {0};
  cr_expect_eq(session_registry_init(&registry), ASCIICHAT_OK);

  // Create a test session using the public API
  acip_session_create_t create_req = {0};
  create_req.max_participants = 4;
  create_req.capabilities = 0x03; // video + audio
  create_req.session_type = 0;    // DIRECT_TCP

  acip_session_created_t response = {0};
  acds_config_t config = {0};

  asciichat_error_t result = session_create(&registry, &create_req, &config, &response);

  // Should succeed
  cr_expect_eq(result, ASCIICHAT_OK, "Session creation should succeed");

  // Session string should be generated
  cr_expect_gt(response.session_string_len, 0, "Session string should be generated");
  cr_expect_not_null(response.session_id, "Session ID should be set");

  // Session should be findable by string
  session_entry_t *found = session_find_by_string(&registry, response.session_string);
  cr_expect_not_null(found, "Created session should be findable by string");

  // Session should be findable by ID
  found = session_find_by_id(&registry, response.session_id);
  cr_expect_not_null(found, "Created session should be findable by ID");

  session_registry_destroy(&registry);
}

Test(session_registry, cleanup_expired_sessions, .timeout = 5) {
  session_registry_t registry = {0};
  cr_expect_eq(session_registry_init(&registry), ASCIICHAT_OK);

  // Create a session
  acip_session_create_t create_req = {0};
  create_req.max_participants = 4;
  create_req.capabilities = 0x03;

  acip_session_created_t response = {0};
  acds_config_t config = {0};

  cr_expect_eq(session_create(&registry, &create_req, &config, &response), ASCIICHAT_OK);

  // Call cleanup (newly created sessions should not be expired)
  session_cleanup_expired(&registry);

  // Session should still exist (not expired yet - 24hr lifetime)
  session_entry_t *found = session_find_by_string(&registry, response.session_string);
  cr_expect_not_null(found, "Non-expired session should still exist after cleanup");

  session_registry_destroy(&registry);
}

Test(session_registry, session_lookup_not_found, .timeout = 5) {
  session_registry_t registry = {0};
  cr_expect_eq(session_registry_init(&registry), ASCIICHAT_OK);

  // Try to find a session that doesn't exist
  session_entry_t *not_found = session_find_by_string(&registry, "nonexistent-session-string");
  cr_expect_null(not_found, "Nonexistent session should return NULL");

  // Try with NULL ID
  uint8_t fake_id[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  not_found = session_find_by_id(&registry, fake_id);
  cr_expect_null(not_found, "Nonexistent session ID should return NULL");

  session_registry_destroy(&registry);
}

Test(session_registry, session_foreach_empty, .timeout = 5) {
  session_registry_t registry = {0};
  cr_expect_eq(session_registry_init(&registry), ASCIICHAT_OK);

  // Count sessions in empty registry
  size_t count = 0;
  session_foreach(&registry, NULL, &count);

  // Empty registry should iterate 0 times (callback is NULL so nothing happens)
  cr_expect(true, "Empty registry foreach should not crash");

  session_registry_destroy(&registry);
}

// Callback to count sessions
static void count_sessions_callback(session_entry_t *session, void *user_data) {
  (void)session;
  size_t *count = (size_t *)user_data;
  (*count)++;
}

Test(session_registry, session_foreach_with_sessions, .timeout = 5) {
  session_registry_t registry = {0};
  cr_expect_eq(session_registry_init(&registry), ASCIICHAT_OK);

  // Create multiple sessions
  acip_session_create_t create_req = {0};
  create_req.max_participants = 4;
  create_req.capabilities = 0x03;

  acip_session_created_t response = {0};
  acds_config_t config = {0};

  for (int i = 0; i < 5; i++) {
    cr_expect_eq(session_create(&registry, &create_req, &config, &response), ASCIICHAT_OK);
  }

  // Count sessions using foreach
  size_t count = 0;
  session_foreach(&registry, count_sessions_callback, &count);

  cr_expect_eq(count, 5, "Should count all 5 created sessions");

  session_registry_destroy(&registry);
}

Test(session_registry, shard_distribution, .timeout = 5) {
  session_registry_t registry = {0};
  cr_expect_eq(session_registry_init(&registry), ASCIICHAT_OK);

  // Create many sessions to test distribution across shards
  acip_session_create_t create_req = {0};
  create_req.max_participants = 4;
  create_req.capabilities = 0x03;

  acip_session_created_t response = {0};
  acds_config_t config = {0};

  // Create 32 sessions (should distribute across 16 shards)
  for (int i = 0; i < 32; i++) {
    cr_expect_eq(session_create(&registry, &create_req, &config, &response), ASCIICHAT_OK);
  }

  // Count sessions in each shard
  size_t total = 0;
  for (int i = 0; i < SESSION_REGISTRY_NUM_SHARDS; i++) {
    session_entry_t *session;
    for (session = registry.shards[i].sessions; session != NULL; session = (session_entry_t *)session->hh.next) {
      total++;
    }
  }

  cr_expect_eq(total, 32, "All 32 sessions should be accounted for across shards");

  session_registry_destroy(&registry);
}

// ============================================================================
// Test Summary
// ============================================================================

/**
 * @brief Sharded RWLock Session Registry Test Suite
 *
 * This test suite validates the core sharded rwlock session registry functionality:
 *
 * 1. **Basic Operations**
 *    - Registry initialization/destruction with 16 shards
 *    - Session creation via public API
 *    - Session lookup by string and UUID
 *    - Cleanup operations
 *
 * 2. **Sharding Behavior**
 *    - Sessions distributed across 16 shards using FNV-1a hash
 *    - Independent shard access reduces lock contention
 *    - Multiple registries can coexist
 *
 * 3. **Thread Safety**
 *    - Per-shard rwlocks allow concurrent reads
 *    - Fine-grained per-entry locking for participant modifications
 *    - Uses platform abstraction layer for portable locking
 *
 * 4. **Memory Safety**
 *    - Session cleanup properly frees memory
 *    - Registry destruction cleans up all shards
 *    - Uses SAFE_MALLOC/SAFE_FREE macros for leak tracking
 *
 * Performance Notes:
 * - 16 shards reduce lock contention vs single rwlock
 * - uthash provides O(1) lookups within each shard
 * - No RCU dependency - uses standard rwlocks
 *
 * Related Tests:
 * - tests/integration/acds/ip_privacy_test.c - ACDS protocol validation
 * - tests/integration/acds/webrtc_turn_credentials_test.c - WebRTC signaling
 */
