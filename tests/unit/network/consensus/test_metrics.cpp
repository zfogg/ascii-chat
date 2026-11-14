/**
 * @file tests/unit/network/consensus/test_metrics.c
 * @brief Tests for metrics collection and wire protocol
 */

#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/network/consensus/metrics.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/util/endian.h>
#include <string.h>

/**
 * Helper: Create test UUID
 */
static void make_uuid(uint8_t id[16], int value) {
  memset(id, 0, 16);
  id[0] = (value >> 24) & 0xFF;
  id[1] = (value >> 16) & 0xFF;
  id[2] = (value >> 8) & 0xFF;
  id[3] = value & 0xFF;
}

/**
 * Test: Basic measurement returns valid structure
 */
// Use verbose logging with debug level enabled and stdout/stderr not disabled
TEST_CASE("consensus_metrics/measure_basic", "[consensus_metrics]") {
  uint8_t my_id[16];
  make_uuid(my_id, 42);

  participant_metrics_t metrics;
  asciichat_error_t err = consensus_metrics_measure(my_id, &metrics);

  ASCIICHAT_REQUIRE_EQ(err, ERROR_INVALID_STATE, "A session observation provider is required");
  ASCIICHAT_REQUIRE_EQ(memcmp(metrics.participant_id, my_id, 16), 0, "ID should match");
  ASCIICHAT_REQUIRE(metrics.nat_tier >= 0 && metrics.nat_tier <= 4, "NAT tier should be 0-4 range");
  ASCIICHAT_REQUIRE_EQ(metrics.upload_kbps, 0, "Unknown upload must not become an observation");
  ASCIICHAT_REQUIRE_EQ(metrics.measurement_window_ns, 0, "Missing observations have no measurement window");
  ASCIICHAT_REQUIRE(metrics.stun_probe_success_pct >= 0 && metrics.stun_probe_success_pct <= 100,
            "Success rate should be 0-100 range");
}

/**
 * Test: Serialize to wire format with network byte order conversion
 */
TEST_CASE("consensus_metrics/wire_format_roundtrip", "[consensus_metrics]") {
  uint8_t my_id[16];
  make_uuid(my_id, 123);

  // Create original metrics
  participant_metrics_t original;
  memset(&original, 0, sizeof(original));
  memcpy(original.participant_id, my_id, 16);
  original.nat_tier = 2;
  original.upload_kbps = 50000;
  original.rtt_ns = 25000000;
  original.stun_probe_success_pct = 95;
  snprintf(original.public_address, sizeof(original.public_address), "192.168.1.1");
  original.public_port = 8080;
  original.connection_type = 1;
  original.measurement_time_ns = 1704067200000000000ULL;
  original.measurement_window_ns = 1000000000;

  // Serialize to wire format
  participant_metrics_t wire;
  asciichat_error_t err = consensus_metrics_to_wire(&original, &wire);
  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK, "Serialization should succeed");

  // Verify wire format has network byte order
  ASCIICHAT_REQUIRE_EQ(wire.upload_kbps, endian_pack_u32(50000), "Upload should be in network order");
  ASCIICHAT_REQUIRE_EQ(wire.rtt_ns, endian_pack_u32(25000000), "RTT should be in network order");
  ASCIICHAT_REQUIRE_EQ(wire.public_port, endian_pack_u16(8080), "Port should be in network order");
  ASCIICHAT_REQUIRE_EQ(wire.measurement_time_ns, endian_pack_u64(1704067200000000000ULL), "Time should be in network order");
  ASCIICHAT_REQUIRE_EQ(wire.measurement_window_ns, endian_pack_u64(1000000000), "Window should be in network order");

  // Deserialize back
  participant_metrics_t deserialized;
  err = consensus_metrics_from_wire(&wire, &deserialized);
  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK, "Deserialization should succeed");

  // Verify roundtrip matches original
  ASCIICHAT_REQUIRE_EQ(memcmp(deserialized.participant_id, original.participant_id, 16), 0, "ID should match after roundtrip");
  ASCIICHAT_REQUIRE_EQ(deserialized.nat_tier, original.nat_tier, "NAT tier should match");
  ASCIICHAT_REQUIRE_EQ(deserialized.upload_kbps, original.upload_kbps, "Upload should match");
  ASCIICHAT_REQUIRE_EQ(deserialized.rtt_ns, original.rtt_ns, "RTT should match");
  ASCIICHAT_REQUIRE_EQ(deserialized.stun_probe_success_pct, original.stun_probe_success_pct, "Success rate should match");
  ASCIICHAT_REQUIRE_EQ(strcmp(deserialized.public_address, original.public_address), 0, "Address should match");
  ASCIICHAT_REQUIRE_EQ(deserialized.public_port, original.public_port, "Port should match");
  ASCIICHAT_REQUIRE_EQ(deserialized.measurement_time_ns, original.measurement_time_ns, "Time should match");
  ASCIICHAT_REQUIRE_EQ(deserialized.measurement_window_ns, original.measurement_window_ns, "Window should match");
}

/**
 * Test: Wire format uses network byte order correctly
 */
TEST_CASE("consensus_metrics/wire_format_byte_order", "[consensus_metrics]") {
  participant_metrics_t original = {0};
  original.nat_tier = 3;
  original.upload_kbps = 0x12345678;
  original.rtt_ns = 0xABCD;
  original.public_port = 0x6789;
  original.measurement_time_ns = 0x0123456789ABCDEFULL;
  original.measurement_window_ns = 0xDEADBEEF;

  participant_metrics_t wire;
  consensus_metrics_to_wire(&original, &wire);

  // Verify byte swaps occurred
  ASCIICHAT_REQUIRE_NE(wire.upload_kbps, original.upload_kbps, "Upload should be byte-swapped");
  ASCIICHAT_REQUIRE_NE(wire.rtt_ns, original.rtt_ns, "RTT should be byte-swapped");
  ASCIICHAT_REQUIRE_NE(wire.public_port, original.public_port, "Port should be byte-swapped");
  ASCIICHAT_REQUIRE_NE(wire.measurement_time_ns, original.measurement_time_ns, "Time should be byte-swapped");
  ASCIICHAT_REQUIRE_NE(wire.measurement_window_ns, original.measurement_window_ns, "Window should be byte-swapped");

  // Verify single-byte fields are NOT modified
  ASCIICHAT_REQUIRE_EQ(wire.nat_tier, original.nat_tier, "Single-byte field should not change");
  ASCIICHAT_REQUIRE_EQ(wire.stun_probe_success_pct, original.stun_probe_success_pct, "Single-byte field should not change");
}

/**
 * Test: Collection creation and destruction
 */
TEST_CASE("consensus_metrics/collection_create_destroy", "[consensus_metrics]") {
  consensus_metrics_collection_t *collection = NULL;
  asciichat_error_t err = consensus_metrics_collection_create(&collection);

  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK, "Creation should succeed");
  ASCIICHAT_REQUIRE_NE(collection, NULL, "Collection should be allocated");

  consensus_metrics_collection_destroy(collection);
  // Safe to destroy NULL
  consensus_metrics_collection_destroy(NULL);
}

/**
 * Test: Accumulate metrics from multiple participants
 */
TEST_CASE("consensus_metrics/accumulate_metrics", "[consensus_metrics]") {
  consensus_metrics_collection_t *collection = NULL;
  consensus_metrics_collection_create(&collection);

  // Add metrics from 3 participants
  participant_metrics_t metrics[3];
  for (int i = 0; i < 3; i++) {
    make_uuid(metrics[i].participant_id, i + 1);
    metrics[i].nat_tier = i;
    metrics[i].upload_kbps = 50000 + i * 1000;
    metrics[i].rtt_ns = 20000000 + i * 5;
    metrics[i].stun_probe_success_pct = 90 + i;

    asciichat_error_t err = consensus_metrics_collection_add(collection, &metrics[i]);
    ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK, "Addition should succeed");
  }

  // Retrieve all metrics
  const participant_metrics_t *stored_metrics;
  int count = 0;
  asciichat_error_t err = consensus_metrics_collection_get(collection, &stored_metrics, &count);

  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK, "Get should succeed");
  ASCIICHAT_REQUIRE_EQ(count, 3, "Should have 3 metrics");
  ASCIICHAT_REQUIRE_NE(stored_metrics, NULL, "Metrics pointer should be valid");

  // Verify each metric
  for (int i = 0; i < 3; i++) {
    ASCIICHAT_REQUIRE_EQ(stored_metrics[i].nat_tier, metrics[i].nat_tier, "NAT tier should match for participant %d", i);
    ASCIICHAT_REQUIRE_EQ(stored_metrics[i].upload_kbps, metrics[i].upload_kbps, "Upload should match for participant %d", i);
    ASCIICHAT_REQUIRE_EQ(stored_metrics[i].rtt_ns, metrics[i].rtt_ns, "RTT should match for participant %d", i);
  }

  consensus_metrics_collection_destroy(collection);
}

/**
 * Test: Collection auto-resizes when capacity exceeded
 */
TEST_CASE("consensus_metrics/collection_resize", "[consensus_metrics]") {
  consensus_metrics_collection_t *collection = NULL;
  consensus_metrics_collection_create(&collection);

  // Add more than initial capacity (10)
  for (int i = 0; i < 20; i++) {
    participant_metrics_t metrics = {0};
    make_uuid(metrics.participant_id, i);
    metrics.nat_tier = i % 5;
    metrics.upload_kbps = 1000 + i * 100;

    asciichat_error_t err = consensus_metrics_collection_add(collection, &metrics);
    ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK, "Addition should succeed even after resize");
  }

  // Verify all metrics were added
  const participant_metrics_t *stored_metrics;
  int count = 0;
  consensus_metrics_collection_get(collection, &stored_metrics, &count);

  ASCIICHAT_REQUIRE_EQ(count, 20, "Should have 20 metrics after resize");

  // Spot-check some metrics
  ASCIICHAT_REQUIRE_EQ(stored_metrics[0].nat_tier, 0, "First metric should match");
  ASCIICHAT_REQUIRE_EQ(stored_metrics[19].nat_tier, 4, "Last metric should match");
  ASCIICHAT_REQUIRE_EQ(stored_metrics[10].upload_kbps, 2000, "Middle metric should match");

  consensus_metrics_collection_destroy(collection);
}

/**
 * Test: STUN probe success rate is 0-100%
 */
TEST_CASE("consensus_metrics/stun_probe_success", "[consensus_metrics]") {
  uint8_t my_id[16];
  make_uuid(my_id, 777);

  participant_metrics_t metrics;
  consensus_metrics_measure(my_id, &metrics);

  // Verify success rate is valid
  ASCIICHAT_REQUIRE(metrics.stun_probe_success_pct >= 0 && metrics.stun_probe_success_pct <= 100,
            "Success rate should be 0-100 percent");

  // In current implementation, should be exactly 90
  ASCIICHAT_REQUIRE_EQ(metrics.stun_probe_success_pct, 0, "No probes means no observed successes");
}

/**
 * Test: Invalid parameters return errors
 */
TEST_CASE("consensus_metrics/invalid_parameters", "[consensus_metrics]") {
  participant_metrics_t metrics = {0};
  uint8_t id[16] = {0};

  // NULL output
  asciichat_error_t err = consensus_metrics_measure(id, NULL);
  ASCIICHAT_REQUIRE_NE(err, ASCIICHAT_OK, "Should reject NULL output");

  // NULL input
  err = consensus_metrics_measure(NULL, &metrics);
  ASCIICHAT_REQUIRE_NE(err, ASCIICHAT_OK, "Should reject NULL input");

  // Collection operations with NULL
  err = consensus_metrics_collection_add(NULL, &metrics);
  ASCIICHAT_REQUIRE_NE(err, ASCIICHAT_OK, "Should reject NULL collection");

  err = consensus_metrics_collection_get(NULL, NULL, NULL);
  ASCIICHAT_REQUIRE_NE(err, ASCIICHAT_OK, "Should reject NULL collection");
}
