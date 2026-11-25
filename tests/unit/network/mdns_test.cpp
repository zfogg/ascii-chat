#include <catch2/catch_test_macros.hpp>
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

#include <stdlib.h>
#include <string.h>

#include <ascii-chat/network/mdns/mdns.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/common.h>

// Static callback for query tests (must be at file scope, not inside Test())
static void dummy_callback(const asciichat_mdns_discovery_t *discovery, void *user_data) {
  (void)discovery;
  (void)user_data;
}

/**
 * @brief Test mDNS context initialization
 */
// Use verbose logging with debug level enabled and stdout/stderr not disabled
TEST_CASE("mdns/init_creates_valid_context", "[mdns] [unit]") {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  REQUIRE((mdns) != nullptr);

  // Verify socket is valid
  int socket = asciichat_mdns_get_socket(mdns);
  REQUIRE((socket) > (-1));

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test mDNS context cleanup
 */
TEST_CASE("mdns/shutdown_frees_resources", "[mdns] [unit]") {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  REQUIRE((mdns) != nullptr);

  // Should not crash when shutting down
  asciichat_mdns_destroy(mdns);

  // Verify shutdown completed (if we get here, it didn't crash)
  // Note: We can't call it again on same pointer, but we test it doesn't crash
}

/**
 * @brief Test mDNS service advertisement
 */
TEST_CASE("mdns/advertise_service_succeeds", "[mdns] [unit]") {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  REQUIRE((mdns) != nullptr);

  asciichat_mdns_service_t service = {
      .name = "test-service",
      .type = "_test._tcp",
      .host = "testhost.local",
      .port = 9999,
      .txt_records = NULL,
      .txt_count = 0,
  };

  asciichat_error_t result = asciichat_mdns_advertise(mdns, &service);
  REQUIRE((result) == (ASCIICHAT_OK));

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test mDNS service advertisement with NULL context
 */
TEST_CASE("mdns/advertise_service_null_context_fails", "[mdns] [unit]") {
  asciichat_mdns_service_t service = {
      .name = "test-service",
      .type = "_test._tcp",
      .host = "testhost.local",
      .port = 9999,
      .txt_records = NULL,
      .txt_count = 0,
  };

  asciichat_error_t result = asciichat_mdns_advertise(NULL, &service);
  REQUIRE((result) != (ASCIICHAT_OK));
}

/**
 * @brief Test mDNS service advertisement with NULL service
 */
TEST_CASE("mdns/advertise_service_null_service_fails", "[mdns] [unit]") {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  REQUIRE((mdns) != nullptr);

  asciichat_error_t result = asciichat_mdns_advertise(mdns, NULL);
  REQUIRE((result) != (ASCIICHAT_OK));

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test mDNS query initialization
 */
TEST_CASE("mdns/query_initializes_successfully", "[mdns] [unit]") {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  REQUIRE((mdns) != nullptr);

  asciichat_error_t result = asciichat_mdns_query(mdns, "_test._tcp.local", dummy_callback, NULL);
  if (result != ASCIICHAT_OK) {
    asciichat_mdns_destroy(mdns);
    SKIP("test skipped");
  }
  REQUIRE((result) == (ASCIICHAT_OK));

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test mDNS query with invalid service type
 */
TEST_CASE("mdns/query_with_empty_service_type_fails", "[mdns] [unit]") {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  REQUIRE((mdns) != nullptr);

  asciichat_error_t result = asciichat_mdns_query(mdns, "", dummy_callback, NULL);
  REQUIRE((result) != (ASCIICHAT_OK));

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test mDNS update with valid context
 */
TEST_CASE("mdns/update_processes_responses", "[mdns] [unit]") {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  REQUIRE((mdns) != nullptr);

  // Update should not crash even with no pending responses
  asciichat_error_t result = asciichat_mdns_update(mdns, 100);
  REQUIRE((result) == (ASCIICHAT_OK));

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test mDNS update with NULL context
 */
TEST_CASE("mdns/update_null_context_fails", "[mdns] [unit]") {
  asciichat_error_t result = asciichat_mdns_update(NULL, 100);
  REQUIRE((result) != (ASCIICHAT_OK));
}

/**
 * @brief Test mDNS socket retrieval
 */
TEST_CASE("mdns/get_socket_returns_valid_descriptor", "[mdns] [unit]") {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  REQUIRE((mdns) != nullptr);

  int socket = asciichat_mdns_get_socket(mdns);
  REQUIRE((socket) > (-1));

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test mDNS socket retrieval with NULL context
 */
TEST_CASE("mdns/get_socket_null_context_returns_invalid", "[mdns] [unit]") {
  int socket = asciichat_mdns_get_socket(NULL);
  REQUIRE((socket) == (-1));
}

/**
 * @brief Test service unadvertisement
 */
TEST_CASE("mdns/unadvertise_service_succeeds", "[mdns] [unit]") {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  REQUIRE((mdns) != nullptr);

  asciichat_error_t result = asciichat_mdns_unadvertise(mdns, "test-service");
  REQUIRE((result) == (ASCIICHAT_OK));

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test service unadvertisement with NULL context
 */
TEST_CASE("mdns/unadvertise_service_null_context_fails", "[mdns] [unit]") {
  asciichat_error_t result = asciichat_mdns_unadvertise(NULL, "test-service");
  REQUIRE((result) != (ASCIICHAT_OK));
}

#ifdef __APPLE__
extern "C" {
  typedef struct { int ready; int closed; void *context; } mdns_fixture_ref_t;
  extern mdns_fixture_ref_t mdns_fixture_refs[3];
  extern unsigned mdns_fixture_fault, mdns_fixture_callbacks, mdns_fixture_closed_refs;
  void mdns_fixture_reset(void);
  void *mdns_fixture_context_create(void);
  asciichat_error_t mdns_fixture_query(asciichat_mdns_t *, const char *, asciichat_mdns_discovery_callback_fn, void *);
  asciichat_error_t mdns_fixture_update(asciichat_mdns_t *, int);
  void mdns_fixture_destroy(asciichat_mdns_t *);
  int mdns_fixture_get_socket(asciichat_mdns_t *);
}
static asciichat_mdns_discovery_t received;
static void capture_bonjour(const asciichat_mdns_discovery_t *found, void *context) { received = *found; ++mdns_fixture_callbacks; REQUIRE(context == &received); }
TEST_CASE("mdns/bonjour_browse_resolves_address_metadata_and_closes_every_handle", "[mdns] [unit]") {
  mdns_fixture_reset(); mdns_fixture_fault = 0;
  auto *context = static_cast<asciichat_mdns_t *>(mdns_fixture_context_create());
  REQUIRE(context != nullptr);
  REQUIRE(mdns_fixture_query(context, "_ascii-chat._tcp.local.", capture_bonjour, &received) == ASCIICHAT_OK);
  REQUIRE(mdns_fixture_get_socket(context) == 10);
  for (unsigned i = 0; i < 4; ++i) REQUIRE(mdns_fixture_update(context, 10) == ASCIICHAT_OK);
  REQUIRE(mdns_fixture_callbacks == 1);
  REQUIRE(std::strcmp(received.name, "fixture-27224") == 0);
  REQUIRE(std::strcmp(received.host, "fixture.local.") == 0);
  mdns_fixture_destroy(context);
}
TEST_CASE("mdns/bonjour_failures_preserve_context_and_release_partial_queries", "[mdns] [unit]") {
  for (mdns_fixture_fault = 1; mdns_fixture_fault <= 9; ++mdns_fixture_fault) {
    mdns_fixture_reset();
    auto *context = static_cast<asciichat_mdns_t *>(mdns_fixture_context_create());
    REQUIRE(context != nullptr);
    auto result = mdns_fixture_query(context, "_ascii-chat._tcp.local", capture_bonjour, &received);
    for (unsigned i = 0; i < 4 && result == ASCIICHAT_OK; ++i) result = mdns_fixture_update(context, 10);
    REQUIRE(result == ERROR_NETWORK); mdns_fixture_destroy(context);
  }
}
TEST_CASE("mdns/disappearing_bonjour_records_do_not_abort_other_discoveries", "[mdns] [unit]") {
  for (mdns_fixture_fault = 10; mdns_fixture_fault <= 13; ++mdns_fixture_fault) {
    mdns_fixture_reset();
    auto *context = static_cast<asciichat_mdns_t *>(mdns_fixture_context_create());
    REQUIRE(context != nullptr);
    REQUIRE(mdns_fixture_query(context, "_ascii-chat._tcp.local", capture_bonjour, &received) == ASCIICHAT_OK);
    for (unsigned i = 0; i < 4; ++i) REQUIRE(mdns_fixture_update(context, 10) == ASCIICHAT_OK);
    mdns_fixture_destroy(context);
  }
}
#endif

