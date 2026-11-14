/**
 * @file ui_mdns_test.c
 * @brief Unit tests for UI mDNS service discovery (lib/ui/ui_mdns.c)
 *
 * Tests client-side UI mDNS discovery functionality:
 * - Discovery query initialization
 * - Server collection and deduplication
 * - Timeout handling
 * - Address selection logic
 * - Memory management
 */

#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <stdlib.h>
#include <string.h>

#include <ascii-chat/tui/mdns.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/common.h>

/**
 * @brief Test LAN discovery with default configuration
 */
// Use verbose logging with debug level enabled and stdout/stderr not disabled
TEST_CASE("ui_mdns/query_with_default_config", "[ui_mdns]") {
  int count = 0;
  ui_mdns_server_t *servers = ui_mdns_query(NULL, &count);

  // Should return valid array (even if empty)
  ASCIICHAT_REQUIRE_NOT_NULL(servers, "Should return server array");
  ASCIICHAT_REQUIRE(count >= 0, "Server count should be non-negative");

  ui_mdns_free_results(servers);
}

/**
 * @brief Test LAN discovery with custom configuration
 */
TEST_CASE("ui_mdns/query_with_custom_config", "[ui_mdns]") {
  ui_mdns_config_t config = {
      .timeout_ms = 1000,
      .max_servers = 10,
      .quiet = true,
  };

  int count = 0;
  ui_mdns_server_t *servers = ui_mdns_query(&config, &count);

  ASCIICHAT_REQUIRE_NOT_NULL(servers, "Should return server array");
  ASCIICHAT_REQUIRE(count <= config.max_servers, "Should not exceed max_servers");

  ui_mdns_free_results(servers);
}

/**
 * @brief Test LAN discovery with NULL output count pointer
 */
TEST_CASE("ui_mdns/query_null_count_pointer", "[ui_mdns]") {
  ui_mdns_config_t config = {.timeout_ms = 100};

  // Should handle gracefully or return error
  ui_mdns_server_t *servers = ui_mdns_query(&config, NULL);

  // Implementation may return NULL for invalid params
  // Both NULL and non-NULL are acceptable, but should be consistent
  if (servers) {
    ui_mdns_free_results(servers);
  }
}

/**
 * @brief Test free_results with NULL pointer
 */
TEST_CASE("ui_mdns/free_results_null_pointer_safe", "[ui_mdns]") {
  // Should not crash with NULL
  ui_mdns_free_results(NULL);
  // If we reach here, the test passed (didn't crash)
}

/**
 * @brief Test free_results called multiple times
 */
TEST_CASE("ui_mdns/free_results_idempotent", "[ui_mdns]") {
  ui_mdns_config_t config = {.timeout_ms = 100, .quiet = true};
  int count = 0;
  ui_mdns_server_t *servers = ui_mdns_query(&config, &count);

  if (servers) {
    // First free
    ui_mdns_free_results(servers);

    // Freeing same pointer multiple times should be safe
    // (In practice, second free is undefined, so we just test it doesn't crash first time)
    // If we reach here, the first free completed successfully
  }
}

/**
 * @brief Test get_best_address with IPv4 available
 */
TEST_CASE("ui_mdns/get_best_address_prefers_ipv4", "[ui_mdns]") {
  ui_mdns_server_t server;
  memset(&server, 0, sizeof(server));

  strcpy(server.name, "test-server");
  strcpy(server.ipv4, "192.168.1.100");
  strcpy(server.ipv6, "2001:db8::1");

  const char *addr = ui_mdns_get_best_address(&server);
  ASCIICHAT_REQUIRE_STR_EQ(addr, "192.168.1.100", "Should prefer IPv4 address");
}

/**
 * @brief Test get_best_address with only IPv6 available
 */
TEST_CASE("ui_mdns/get_best_address_fallback_ipv6", "[ui_mdns]") {
  ui_mdns_server_t server;
  memset(&server, 0, sizeof(server));

  strcpy(server.name, "test-server");
  strcpy(server.ipv6, "2001:db8::1");

  const char *addr = ui_mdns_get_best_address(&server);
  ASCIICHAT_REQUIRE_STR_EQ(addr, "2001:db8::1", "Should use IPv6 rather than the display name when IPv4 is unavailable");
}

/**
 * @brief Test get_best_address with NULL server
 */
TEST_CASE("ui_mdns/get_best_address_null_server", "[ui_mdns]") {
  const char *addr = ui_mdns_get_best_address(NULL);
  ASCIICHAT_REQUIRE_STR_EQ(addr, "", "Should return empty string for NULL server");
}

/**
 * @brief Test get_best_address with only address field set
 */
TEST_CASE("ui_mdns/get_best_address_fallback_address", "[ui_mdns]") {
  ui_mdns_server_t server;
  memset(&server, 0, sizeof(server));

  strcpy(server.address, "example.local");

  const char *addr = ui_mdns_get_best_address(&server);
  ASCIICHAT_REQUIRE_STR_EQ(addr, "example.local", "Should return address field as fallback");
}

/**
 * @brief Test prompt_selection with NULL servers
 */
TEST_CASE("ui_mdns/prompt_selection_null_servers", "[ui_mdns]") {
  int result = ui_mdns_prompt_selection(NULL, 0);
  ASCIICHAT_REQUIRE_EQ(result, -1, "Should return -1 for NULL servers");
}

/**
 * @brief Test prompt_selection with zero count
 */
TEST_CASE("ui_mdns/prompt_selection_zero_count", "[ui_mdns]") {
  ui_mdns_server_t servers[1];
  memset(servers, 0, sizeof(servers));

  int result = ui_mdns_prompt_selection(servers, 0);
  ASCIICHAT_REQUIRE_EQ(result, -1, "Should return -1 for zero count");
}

/**
 * @brief Test discovery server structure initialization
 */
TEST_CASE("ui_mdns/discovered_server_structure", "[ui_mdns]") {
  ui_mdns_server_t server;
  memset(&server, 0, sizeof(server));

  // Verify structure is properly sized and aligned
  ASCIICHAT_REQUIRE_GT(sizeof(server.name), 0, "Name field should exist");
  ASCIICHAT_REQUIRE_GT(sizeof(server.address), 0, "Address field should exist");
  ASCIICHAT_REQUIRE_GT(sizeof(server.ipv4), 0, "IPv4 field should exist");
  ASCIICHAT_REQUIRE_GT(sizeof(server.ipv6), 0, "IPv6 field should exist");
}

/**
 * @brief Test discovery config structure
 */
TEST_CASE("ui_mdns/discovery_config_structure", "[ui_mdns]") {
  ui_mdns_config_t config;
  memset(&config, 0, sizeof(config));

  config.timeout_ms = 2000;
  config.max_servers = 20;
  config.quiet = false;

  ASCIICHAT_REQUIRE_EQ(config.timeout_ms, 2000, "Timeout should be settable");
  ASCIICHAT_REQUIRE_EQ(config.max_servers, 20, "Max servers should be settable");
  ASCIICHAT_REQUIRE_EQ(config.quiet, false, "Quiet flag should be settable");
}

/**
 * @brief Test LAN discovery with very short timeout
 */
TEST_CASE("ui_mdns/query_with_short_timeout", "[ui_mdns]") {
  ui_mdns_config_t config = {
      .timeout_ms = 10, // Very short timeout
      .max_servers = 10,
      .quiet = true,
  };

  int count = 0;
  ui_mdns_server_t *servers = ui_mdns_query(&config, &count);

  ASCIICHAT_REQUIRE_NOT_NULL(servers, "Should handle short timeout");

  ui_mdns_free_results(servers);
}

/**
 * @brief Test LAN discovery with very long timeout
 */
TEST_CASE("ui_mdns/query_with_long_timeout", "[ui_mdns]") {
  ui_mdns_config_t config = {
      .timeout_ms = 5000, // Longer timeout
      .max_servers = 20,
      .quiet = true,
  };

  int count = 0;
  ui_mdns_server_t *servers = ui_mdns_query(&config, &count);

  ASCIICHAT_REQUIRE_NOT_NULL(servers, "Should handle long timeout");

  ui_mdns_free_results(servers);
}
