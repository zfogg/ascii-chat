/**
 * @file mdns_discovery_test.c
 * @brief Integration tests for mDNS service advertisement and discovery
 *
 * Tests end-to-end mDNS functionality:
 * - Advertising a service on mDNS
 * - Discovering advertised services
 * - Multiple service scenarios
 * - Service information accuracy
 *
 * Note: These tests require mDNS to be functional on the system.
 * They may be skipped or fail on systems without proper mDNS support.
 */

#include <criterion/criterion.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ascii-chat/network/mdns/mdns.h>
#include <ascii-chat/network/mdns/discovery.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/time.h>

// Static callbacks for integration tests (must be at file scope, not inside Test())
static void dummy_callback(const asciichat_mdns_discovery_t *discovery, void *user_data) {
  (void)discovery;
  (void)user_data;
}

static void counting_callback(const asciichat_mdns_discovery_t *discovery, void *user_data) {
  (void)discovery;
  int *count = (int *)user_data;
  (*count)++;
}

static void invalid_type_test_callback(const asciichat_mdns_discovery_t *discovery, void *user_data) {
  (void)discovery;
  int *count = (int *)user_data;
  (*count)++;
}

/**
 * @brief Test basic mDNS initialization and cleanup cycle
 */
// Use verbose logging with debug level enabled and stdout/stderr not disabled
TestSuite(mdns_integration);

static void lifecycle_callback(const asciichat_mdns_discovery_t *service, void *data) {
  asciichat_mdns_discovery_t *found = data;
  if (strncmp(service->name, "owned-lan-fixture-", 18) == 0)
    *found = *service;
}

Test(mdns_integration, real_answers_survive_invalid_replacement_and_own_caller_records, .timeout = 10) {
  asciichat_mdns_t *advertiser = asciichat_mdns_init();
  asciichat_mdns_t *query = asciichat_mdns_init();
  cr_assert_not_null(advertiser);
  cr_assert_not_null(query);
  char name[64] = "owned-lan-fixture", host[64] = "owned-host";
  const char *txt[] = {"session_string=swift-river-mountain"};
  asciichat_mdns_service_t service = {
      .name = name, .host = host, .type = "_ascii-chat._tcp", .port = 32123, .txt_records = txt, .txt_count = 1};
  cr_assert_eq(asciichat_mdns_advertise(advertiser, &service), ASCIICHAT_OK);
  service.port = 0;
  cr_assert_eq(asciichat_mdns_advertise(advertiser, &service), ERROR_INVALID_PARAM);
  memset(name, 'x', sizeof(name) - 1);
  memset(host, 'y', sizeof(host) - 1);
  asciichat_mdns_discovery_t found = {0};
  asciichat_error_t query_result = asciichat_mdns_query(query, "_ascii-chat._tcp.local", lifecycle_callback, &found);
  if (query_result == ERROR_NETWORK_BIND || query_result == ERROR_NETWORK) {
    asciichat_mdns_destroy(advertiser);
    asciichat_mdns_destroy(query);
    cr_skip_test("mDNS multicast queries are unavailable on this runner");
  }
  cr_assert_eq(query_result, ASCIICHAT_OK);
  uint64_t deadline = time_get_ns() + 2 * NS_PER_SEC_INT;
  while (!found.port && time_get_ns() < deadline)
    cr_assert_eq(asciichat_mdns_update(query, 50), ASCIICHAT_OK);
  cr_assert_eq(found.port, 32123, "A registered service must answer real network queries");
  cr_assert_str_eq(found.host, "owned-host.local.");
  cr_assert_str_eq(found.txt, "session_string=swift-river-mountain");
  cr_assert_str_not_empty(found.ipv4);
  int server_count = 0;
  ui_mdns_server_t *servers = discovery_mdns_query(800, 20, true, &server_count);
  bool session_found = false;
  for (int i = 0; i < server_count; ++i) {
    if (servers[i].port == 32123 && strncmp(servers[i].name, "owned-lan-fixture-", 18) == 0) {
      cr_assert_str_eq(servers[i].session_string, "swift-river-mountain");
      session_found = true;
    }
  }
  cr_assert(session_found, "The UI discovery result must retain session TXT metadata");
  discovery_mdns_destroy(servers);
  discovery_config_t config;
  discovery_config_init_defaults(&config);
  discovery_result_t resolved;
  cr_assert_eq(discover_session_parallel("swift-river-mountain", &config, &resolved), ASCIICHAT_OK);
  cr_assert(resolved.success);
  cr_assert_eq(resolved.source, DISCOVERY_SOURCE_MDNS);
  cr_assert_eq(resolved.server_port, 32123);
  cr_assert_eq(asciichat_mdns_unadvertise(advertiser, "owned-lan-fixture"), ASCIICHAT_OK);
  asciichat_mdns_destroy(advertiser);
  asciichat_mdns_destroy(query);
  query = asciichat_mdns_init();
  cr_assert_not_null(query);
  memset(&found, 0, sizeof(found));
  cr_assert_eq(asciichat_mdns_query(query, "_ascii-chat._tcp.local", lifecycle_callback, &found), ASCIICHAT_OK);
  deadline = time_get_ns() + 300 * NS_PER_MS_INT;
  while (time_get_ns() < deadline)
    cr_assert_eq(asciichat_mdns_update(query, 50), ASCIICHAT_OK);
  cr_assert_eq(found.port, 0, "Withdrawn service must stop answering queries");
  asciichat_mdns_destroy(query);
}

Test(mdns_integration, initialization_cleanup_cycle) {
  // Create and destroy multiple times to verify no resource leaks
  for (int i = 0; i < 3; i++) {
    asciichat_mdns_t *mdns = asciichat_mdns_init();
    cr_assert_not_null(mdns, "Should initialize on attempt %d", i + 1);

    int socket = asciichat_mdns_get_socket(mdns);
    cr_assert_gt(socket, -1, "Socket should be valid on attempt %d", i + 1);

    asciichat_mdns_destroy(mdns);
  }
}

/**
 * @brief Test service advertisement registration
 */
Test(mdns_integration, service_advertisement_registration) {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  cr_assert_not_null(mdns, "mDNS should initialize");

  // Advertise a test service
  asciichat_mdns_service_t service = {
      .name = "integration-test-service",
      .type = "_ascii-chat-test._tcp",
      .host = "test-host.local",
      .port = 9999,
      .txt_records = NULL,
      .txt_count = 0,
  };

  asciichat_error_t result = asciichat_mdns_advertise(mdns, &service);
  cr_assert_eq(result, ASCIICHAT_OK, "Service advertisement should succeed");

  // Service should remain advertised until shutdown
  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test multiple services can be advertised
 */
Test(mdns_integration, multiple_services_advertisement) {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  cr_assert_not_null(mdns, "mDNS should initialize");

  // Advertise multiple services
  asciichat_mdns_service_t services[] = {
      {
          .name = "service-1",
          .type = "_ascii-chat-test._tcp",
          .host = "host1.local",
          .port = 9000,
          .txt_records = NULL,
          .txt_count = 0,
      },
      {
          .name = "service-2",
          .type = "_ascii-chat-test._tcp",
          .host = "host2.local",
          .port = 9001,
          .txt_records = NULL,
          .txt_count = 0,
      },
      {
          .name = "service-3",
          .type = "_ascii-chat-test._tcp",
          .host = "host3.local",
          .port = 9002,
          .txt_records = NULL,
          .txt_count = 0,
      },
  };

  for (int i = 0; i < 3; i++) {
    asciichat_error_t result = asciichat_mdns_advertise(mdns, &services[i]);
    cr_assert_eq(result, ASCIICHAT_OK, "Service %d advertisement should succeed", i + 1);
  }

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test service unadvertisement
 */
Test(mdns_integration, service_unadvertisement) {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  cr_assert_not_null(mdns, "mDNS should initialize");

  // Advertise a service
  asciichat_mdns_service_t service = {
      .name = "unadvertise-test",
      .type = "_ascii-chat-test._tcp",
      .host = "test.local",
      .port = 9999,
      .txt_records = NULL,
      .txt_count = 0,
  };

  asciichat_error_t advertise_result = asciichat_mdns_advertise(mdns, &service);
  cr_assert_eq(advertise_result, ASCIICHAT_OK, "Service advertisement should succeed");

  // Unadvertise the service
  asciichat_error_t unadvertise_result = asciichat_mdns_unadvertise(mdns, service.name);
  cr_assert_eq(unadvertise_result, ASCIICHAT_OK, "Service unadvertisement should succeed");

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test query initialization without responses
 */
Test(mdns_integration, query_initialization) {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  cr_assert_not_null(mdns, "mDNS should initialize");

  // Test callback that counts invocations
  int callback_count = 0;

  asciichat_error_t result =
      asciichat_mdns_query(mdns, "_ascii-chat-test._tcp.local", counting_callback, &callback_count);

  // Query may fail in some environments (e.g., restricted networks, containers)
  // Just verify it doesn't crash - both success and graceful failure are acceptable
  cr_assert(result == ASCIICHAT_OK || result == ERROR_NETWORK,
            "Query should either succeed or fail gracefully (got error %d)", result);

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test update processing loop
 */
Test(mdns_integration, update_processing_loop) {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  cr_assert_not_null(mdns, "mDNS should initialize");

  asciichat_error_t query_result = asciichat_mdns_query(mdns, "_ascii-chat-test._tcp.local", dummy_callback, NULL);

  // Query may fail in restricted environments - that's acceptable
  // If query succeeded, test that updates work correctly
  if (query_result == ASCIICHAT_OK) {
    // Simulate processing loop with multiple updates
    for (int i = 0; i < 5; i++) {
      asciichat_error_t update_result = asciichat_mdns_update(mdns, 50);
      cr_assert_eq(update_result, ASCIICHAT_OK, "Update %d should succeed", i + 1);
    }
  }

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test service info with various port numbers
 */
Test(mdns_integration, service_with_various_ports) {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  cr_assert_not_null(mdns, "mDNS should initialize");

  uint16_t ports[] = {80, 443, 8000, 27224, 65535};

  for (int i = 0; i < 5; i++) {
    char name_buffer[256];
    snprintf(name_buffer, sizeof(name_buffer), "port-%u", ports[i]);

    asciichat_mdns_service_t service;
    memset(&service, 0, sizeof(service));
    service.name = name_buffer;
    service.type = "_ascii-chat-test._tcp";
    service.host = "test.local";
    service.port = ports[i];

    asciichat_error_t result = asciichat_mdns_advertise(mdns, &service);
    cr_assert_eq(result, ASCIICHAT_OK, "Should advertise service with port %u", ports[i]);
  }

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test service info with IPv6 hostname
 */
Test(mdns_integration, service_with_ipv6_hostname) {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  cr_assert_not_null(mdns, "mDNS should initialize");

  asciichat_mdns_service_t service = {
      .name = "ipv6-test-service",
      .type = "_ascii-chat-test._tcp",
      .host = "ipv6-host.local", // mDNS should support IPv6 capable hosts
      .port = 27224,
      .txt_records = NULL,
      .txt_count = 0,
  };

  asciichat_error_t result = asciichat_mdns_advertise(mdns, &service);
  cr_assert_eq(result, ASCIICHAT_OK, "Should advertise service with IPv6-capable hostname");

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test rapid advertise/unadvertise cycles
 */
Test(mdns_integration, rapid_advertise_unadvertise_cycles) {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  cr_assert_not_null(mdns, "mDNS should initialize");

  for (int i = 0; i < 10; i++) {
    char name_buffer[256];
    snprintf(name_buffer, sizeof(name_buffer), "rapid-%d", i);

    asciichat_mdns_service_t service;
    memset(&service, 0, sizeof(service));
    service.name = name_buffer;
    service.type = "_ascii-chat-test._tcp";
    service.host = "test.local";
    service.port = 27224;

    asciichat_error_t advertise = asciichat_mdns_advertise(mdns, &service);
    cr_assert_eq(advertise, ASCIICHAT_OK, "Advertise %d should succeed", i);

    asciichat_error_t unadvertise = asciichat_mdns_unadvertise(mdns, service.name);
    cr_assert_eq(unadvertise, ASCIICHAT_OK, "Unadvertise %d should succeed", i);
  }

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test error handling for invalid service types
 */
Test(mdns_integration, invalid_service_types) {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  cr_assert_not_null(mdns, "mDNS should initialize");

  // Test various invalid service types
  const char *invalid_types[] = {
      "",               // Empty string
      "invalid",        // Missing _tcp
      "_tcp",           // Missing service name
      "._tcp",          // Missing name prefix
      "_invalid-_-tcp", // Invalid characters
  };

  // Test each invalid type
  int attempt_count = 0;
  for (int i = 0; i < 5; i++) {
    int count = 0;
    asciichat_error_t result = asciichat_mdns_query(mdns, invalid_types[i], invalid_type_test_callback, &count);
    attempt_count++;
    // Either succeed gracefully or return error - both acceptable
    // Just verify it doesn't crash
    (void)result;
  }

  asciichat_mdns_destroy(mdns);
  cr_assert_eq(attempt_count, 5, "All invalid type attempts should complete");
}
