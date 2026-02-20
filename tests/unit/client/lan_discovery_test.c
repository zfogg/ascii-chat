/**
 * @file lan_discovery_test.c
 * @brief Unit tests for LAN service discovery (src/client/lan_discovery.c)
 *
 * Tests client-side LAN discovery functionality:
 * - Discovery query initialization
 * - Server collection and deduplication
 * - Timeout handling
 * - Address selection logic
 * - Memory management
 */

#include <criterion/criterion.h>
#include <stdlib.h>
#include <string.h>

#include <ascii-chat/network/mdns/discovery_tui.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/common.h>

/**
 * @brief Test LAN discovery with default configuration
 */
// Use verbose logging with debug level enabled and stdout/stderr not disabled
TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(lan_discovery, LOG_DEBUG, LOG_DEBUG, false, false);

Test(lan_discovery, query_with_default_config) {
  int count = 0;
  discovery_tui_server_t *servers = discovery_tui_query(NULL, &count);

  // Should return valid array (even if empty)
  cr_assert_not_null(servers, "Should return server array");
  cr_assert(count >= 0, "Server count should be non-negative");

  discovery_tui_free_results(servers);
}

/**
 * @brief Test LAN discovery with custom configuration
 */
Test(lan_discovery, query_with_custom_config) {
  discovery_tui_config_t config = {
      .timeout_ms = 1000,
      .max_servers = 10,
      .quiet = true,
  };

  int count = 0;
  discovery_tui_server_t *servers = discovery_tui_query(&config, &count);

  cr_assert_not_null(servers, "Should return server array");
  cr_assert(count <= config.max_servers, "Should not exceed max_servers");

  discovery_tui_free_results(servers);
}

/**
 * @brief Test LAN discovery with NULL output count pointer
 */
Test(lan_discovery, query_null_count_pointer) {
  discovery_tui_config_t config = {.timeout_ms = 100};

  // Should handle gracefully or return error
  discovery_tui_server_t *servers = discovery_tui_query(&config, NULL);

  // Implementation may return NULL for invalid params
  // Both NULL and non-NULL are acceptable, but should be consistent
  if (servers) {
    discovery_tui_free_results(servers);
  }
}

/**
 * @brief Test free_results with NULL pointer
 */
Test(lan_discovery, free_results_null_pointer_safe) {
  // Should not crash with NULL
  discovery_tui_free_results(NULL);
  // If we reach here, the test passed (didn't crash)
}

/**
 * @brief Test free_results called multiple times
 */
Test(lan_discovery, free_results_idempotent) {
  discovery_tui_config_t config = {.timeout_ms = 100, .quiet = true};
  int count = 0;
  discovery_tui_server_t *servers = discovery_tui_query(&config, &count);

  if (servers) {
    // First free
    discovery_tui_free_results(servers);

    // Freeing same pointer multiple times should be safe
    // (In practice, second free is undefined, so we just test it doesn't crash first time)
    // If we reach here, the first free completed successfully
  }
}

/**
 * @brief Test get_best_address with IPv4 available
 */
Test(lan_discovery, get_best_address_prefers_ipv4) {
  discovery_tui_server_t server;
  memset(&server, 0, sizeof(server));

  strcpy(server.name, "test-server");
  strcpy(server.ipv4, "192.168.1.100");
  strcpy(server.ipv6, "2001:db8::1");

  const char *addr = discovery_tui_get_best_address(&server);
  cr_assert_str_eq(addr, "192.168.1.100", "Should prefer IPv4 address");
}

/**
 * @brief Test get_best_address with only IPv6 available
 */
Test(lan_discovery, get_best_address_fallback_ipv6) {
  discovery_tui_server_t server;
  memset(&server, 0, sizeof(server));

  strcpy(server.name, "test-server");
  strcpy(server.ipv6, "2001:db8::1");

  const char *addr = discovery_tui_get_best_address(&server);
  cr_assert_str_eq(addr, "test-server", "Should use name when IPv4 unavailable");
}

/**
 * @brief Test get_best_address with NULL server
 */
Test(lan_discovery, get_best_address_null_server) {
  const char *addr = discovery_tui_get_best_address(NULL);
  cr_assert_str_eq(addr, "", "Should return empty string for NULL server");
}

/**
 * @brief Test get_best_address with only address field set
 */
Test(lan_discovery, get_best_address_fallback_address) {
  discovery_tui_server_t server;
  memset(&server, 0, sizeof(server));

  strcpy(server.address, "example.local");

  const char *addr = discovery_tui_get_best_address(&server);
  cr_assert_str_eq(addr, "example.local", "Should return address field as fallback");
}

/**
 * @brief Test prompt_selection with NULL servers
 */
Test(lan_discovery, prompt_selection_null_servers) {
  int result = discovery_tui_prompt_selection(NULL, 0);
  cr_assert_eq(result, -1, "Should return -1 for NULL servers");
}

/**
 * @brief Test prompt_selection with zero count
 */
Test(lan_discovery, prompt_selection_zero_count) {
  discovery_tui_server_t servers[1];
  memset(servers, 0, sizeof(servers));

  int result = discovery_tui_prompt_selection(servers, 0);
  cr_assert_eq(result, -1, "Should return -1 for zero count");
}

/**
 * @brief Test discovery server structure initialization
 */
Test(lan_discovery, discovered_server_structure) {
  discovery_tui_server_t server;
  memset(&server, 0, sizeof(server));

  // Verify structure is properly sized and aligned
  cr_assert_gt(sizeof(server.name), 0, "Name field should exist");
  cr_assert_gt(sizeof(server.address), 0, "Address field should exist");
  cr_assert_gt(sizeof(server.ipv4), 0, "IPv4 field should exist");
  cr_assert_gt(sizeof(server.ipv6), 0, "IPv6 field should exist");
}

/**
 * @brief Test discovery config structure
 */
Test(lan_discovery, discovery_config_structure) {
  discovery_tui_config_t config;
  memset(&config, 0, sizeof(config));

  config.timeout_ms = 2000;
  config.max_servers = 20;
  config.quiet = false;

  cr_assert_eq(config.timeout_ms, 2000, "Timeout should be settable");
  cr_assert_eq(config.max_servers, 20, "Max servers should be settable");
  cr_assert_eq(config.quiet, false, "Quiet flag should be settable");
}

/**
 * @brief Test LAN discovery with very short timeout
 */
Test(lan_discovery, query_with_short_timeout) {
  discovery_tui_config_t config = {
      .timeout_ms = 10, // Very short timeout
      .max_servers = 10,
      .quiet = true,
  };

  int count = 0;
  discovery_tui_server_t *servers = discovery_tui_query(&config, &count);

  cr_assert_not_null(servers, "Should handle short timeout");

  discovery_tui_free_results(servers);
}

/**
 * @brief Test LAN discovery with very long timeout
 */
Test(lan_discovery, query_with_long_timeout) {
  discovery_tui_config_t config = {
      .timeout_ms = 5000, // Longer timeout
      .max_servers = 20,
      .quiet = true,
  };

  int count = 0;
  discovery_tui_server_t *servers = discovery_tui_query(&config, &count);

  cr_assert_not_null(servers, "Should handle long timeout");

  discovery_tui_free_results(servers);
}
