/**
 * @file mdns_test.c
 * @brief Unit tests for mDNS service discovery (lib/network/mdns.c)
 *
 * Tests mDNS abstraction layer functionality:
 * - Context initialization and cleanup
 * - Service advertisement
 * - mDNS querying
 * - Response parsing and callbacks
 * - Error handling
 */

#include <criterion/criterion.h>
#include <stdlib.h>
#include <string.h>

#include <ascii-chat/network/mdns/mdns.h>
#include <ascii-chat/common.h>

// Static callback for query tests (must be at file scope, not inside Test())
static void dummy_callback(const asciichat_mdns_discovery_t *discovery, void *user_data) {
  (void)discovery;
  (void)user_data;
}

/**
 * @brief Test mDNS context initialization
 */
Test(mdns, init_creates_valid_context) {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  cr_assert_not_null(mdns, "mDNS context should be initialized");

  // Verify socket is valid
  int socket = asciichat_mdns_get_socket(mdns);
  cr_assert_gt(socket, -1, "mDNS socket should be valid");

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test mDNS context cleanup
 */
Test(mdns, shutdown_frees_resources) {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  cr_assert_not_null(mdns, "mDNS should initialize successfully");

  // Should not crash when shutting down
  asciichat_mdns_destroy(mdns);

  // Verify shutdown completed (if we get here, it didn't crash)
  // Note: We can't call it again on same pointer, but we test it doesn't crash
}

/**
 * @brief Test mDNS service advertisement
 */
Test(mdns, advertise_service_succeeds) {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  cr_assert_not_null(mdns, "mDNS should initialize");

  asciichat_mdns_service_t service = {
      .name = "test-service",
      .type = "_test._tcp",
      .host = "testhost.local",
      .port = 9999,
      .txt_records = NULL,
      .txt_count = 0,
  };

  asciichat_error_t result = asciichat_mdns_advertise(mdns, &service);
  cr_assert_eq(result, ASCIICHAT_OK, "Service advertisement should succeed");

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test mDNS service advertisement with NULL context
 */
Test(mdns, advertise_service_null_context_fails) {
  asciichat_mdns_service_t service = {
      .name = "test-service",
      .type = "_test._tcp",
      .host = "testhost.local",
      .port = 9999,
      .txt_records = NULL,
      .txt_count = 0,
  };

  asciichat_error_t result = asciichat_mdns_advertise(NULL, &service);
  cr_assert_neq(result, ASCIICHAT_OK, "Should fail with NULL context");
}

/**
 * @brief Test mDNS service advertisement with NULL service
 */
Test(mdns, advertise_service_null_service_fails) {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  cr_assert_not_null(mdns, "mDNS should initialize");

  asciichat_error_t result = asciichat_mdns_advertise(mdns, NULL);
  cr_assert_neq(result, ASCIICHAT_OK, "Should fail with NULL service");

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test mDNS query initialization
 */
Test(mdns, query_initializes_successfully) {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  cr_assert_not_null(mdns, "mDNS should initialize");

  asciichat_error_t result = asciichat_mdns_query(mdns, "_test._tcp.local", dummy_callback, NULL);
  cr_assert_eq(result, ASCIICHAT_OK, "Query should initialize successfully");

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test mDNS query with invalid service type
 */
Test(mdns, query_with_empty_service_type_fails) {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  cr_assert_not_null(mdns, "mDNS should initialize");

  asciichat_error_t result = asciichat_mdns_query(mdns, "", dummy_callback, NULL);
  cr_assert_neq(result, ASCIICHAT_OK, "Query with empty service type should fail");

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test mDNS update with valid context
 */
Test(mdns, update_processes_responses) {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  cr_assert_not_null(mdns, "mDNS should initialize");

  // Update should not crash even with no pending responses
  asciichat_error_t result = asciichat_mdns_update(mdns, 100);
  cr_assert_eq(result, ASCIICHAT_OK, "Update should succeed");

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test mDNS update with NULL context
 */
Test(mdns, update_null_context_fails) {
  asciichat_error_t result = asciichat_mdns_update(NULL, 100);
  cr_assert_neq(result, ASCIICHAT_OK, "Update with NULL context should fail");
}

/**
 * @brief Test mDNS socket retrieval
 */
Test(mdns, get_socket_returns_valid_descriptor) {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  cr_assert_not_null(mdns, "mDNS should initialize");

  int socket = asciichat_mdns_get_socket(mdns);
  cr_assert_gt(socket, -1, "Socket should be valid file descriptor");

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test mDNS socket retrieval with NULL context
 */
Test(mdns, get_socket_null_context_returns_invalid) {
  int socket = asciichat_mdns_get_socket(NULL);
  cr_assert_eq(socket, -1, "Socket should be invalid for NULL context");
}

/**
 * @brief Test service unadvertisement
 */
Test(mdns, unadvertise_service_succeeds) {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  cr_assert_not_null(mdns, "mDNS should initialize");

  asciichat_error_t result = asciichat_mdns_unadvertise(mdns, "test-service");
  cr_assert_eq(result, ASCIICHAT_OK, "Unadvertisement should succeed");

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test service unadvertisement with NULL context
 */
Test(mdns, unadvertise_service_null_context_fails) {
  asciichat_error_t result = asciichat_mdns_unadvertise(NULL, "test-service");
  cr_assert_neq(result, ASCIICHAT_OK, "Should fail with NULL context");
}
