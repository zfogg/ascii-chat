/**
 * @file tests/unit/network/consensus/test_metrics.c
 * @brief Tests for metrics collection and wire protocol
 */

#include <criterion/criterion.h>
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
TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(consensus_metrics, LOG_DEBUG, LOG_DEBUG, false, false);

Test(consensus_metrics, measure_basic) {
  uint8_t my_id[16];
  make_uuid(my_id, 42);

  participant_metrics_t metrics;
  asciichat_error_t err = consensus_metrics_measure(my_id, &metrics);

  cr_assert_eq(err, ASCIICHAT_OK, "Measurement should succeed");
  cr_assert_eq(memcmp(metrics.participant_id, my_id, 16), 0, "ID should match");
  cr_assert(metrics.nat_tier >= 0 && metrics.nat_tier <= 4, "NAT tier should be 0-4 range");
  cr_assert_gt(metrics.upload_kbps, 0, "Upload bandwidth should be positive");
  cr_assert_gt(metrics.rtt_ns, 0, "RTT should be positive");
  cr_assert(metrics.stun_probe_success_pct >= 0 && metrics.stun_probe_success_pct <= 100,
            "Success rate should be 0-100 range");
}

/**
 * Test: Serialize to wire format with network byte order conversion
 */
Test(consensus_metrics, wire_format_roundtrip) {
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
  cr_assert_eq(err, ASCIICHAT_OK, "Serialization should succeed");

  // Verify wire format has network byte order
  cr_assert_eq(wire.upload_kbps, endian_pack_u32(50000), "Upload should be in network order");
  cr_assert_eq(wire.rtt_ns, endian_pack_u32(25000000), "RTT should be in network order");
  cr_assert_eq(wire.public_port, endian_pack_u16(8080), "Port should be in network order");
  cr_assert_eq(wire.measurement_time_ns, endian_pack_u64(1704067200000000000ULL), "Time should be in network order");
  cr_assert_eq(wire.measurement_window_ns, endian_pack_u64(1000000000), "Window should be in network order");

  // Deserialize back
  participant_metrics_t deserialized;
  err = consensus_metrics_from_wire(&wire, &deserialized);
  cr_assert_eq(err, ASCIICHAT_OK, "Deserialization should succeed");

  // Verify roundtrip matches original
  cr_assert_eq(memcmp(deserialized.participant_id, original.participant_id, 16), 0, "ID should match after roundtrip");
  cr_assert_eq(deserialized.nat_tier, original.nat_tier, "NAT tier should match");
  cr_assert_eq(deserialized.upload_kbps, original.upload_kbps, "Upload should match");
  cr_assert_eq(deserialized.rtt_ns, original.rtt_ns, "RTT should match");
  cr_assert_eq(deserialized.stun_probe_success_pct, original.stun_probe_success_pct, "Success rate should match");
  cr_assert_eq(strcmp(deserialized.public_address, original.public_address), 0, "Address should match");
  cr_assert_eq(deserialized.public_port, original.public_port, "Port should match");
  cr_assert_eq(deserialized.measurement_time_ns, original.measurement_time_ns, "Time should match");
  cr_assert_eq(deserialized.measurement_window_ns, original.measurement_window_ns, "Window should match");
}

/**
 * Test: Wire format uses network byte order correctly
 */
Test(consensus_metrics, wire_format_byte_order) {
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
  cr_assert_neq(wire.upload_kbps, original.upload_kbps, "Upload should be byte-swapped");
  cr_assert_neq(wire.rtt_ns, original.rtt_ns, "RTT should be byte-swapped");
  cr_assert_neq(wire.public_port, original.public_port, "Port should be byte-swapped");
  cr_assert_neq(wire.measurement_time_ns, original.measurement_time_ns, "Time should be byte-swapped");
  cr_assert_neq(wire.measurement_window_ns, original.measurement_window_ns, "Window should be byte-swapped");

  // Verify single-byte fields are NOT modified
  cr_assert_eq(wire.nat_tier, original.nat_tier, "Single-byte field should not change");
  cr_assert_eq(wire.stun_probe_success_pct, original.stun_probe_success_pct, "Single-byte field should not change");
}

/**
 * Test: Collection creation and destruction
 */
Test(consensus_metrics, collection_create_destroy) {
  consensus_metrics_collection_t *collection = NULL;
  asciichat_error_t err = consensus_metrics_collection_create(&collection);

  cr_assert_eq(err, ASCIICHAT_OK, "Creation should succeed");
  cr_assert_neq(collection, NULL, "Collection should be allocated");

  consensus_metrics_collection_destroy(collection);
  // Safe to destroy NULL
  consensus_metrics_collection_destroy(NULL);
}

/**
 * Test: Accumulate metrics from multiple participants
 */
Test(consensus_metrics, accumulate_metrics) {
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
    cr_assert_eq(err, ASCIICHAT_OK, "Addition should succeed");
  }

  // Retrieve all metrics
  const participant_metrics_t *stored_metrics;
  int count = 0;
  asciichat_error_t err = consensus_metrics_collection_get(collection, &stored_metrics, &count);

  cr_assert_eq(err, ASCIICHAT_OK, "Get should succeed");
  cr_assert_eq(count, 3, "Should have 3 metrics");
  cr_assert_neq(stored_metrics, NULL, "Metrics pointer should be valid");

  // Verify each metric
  for (int i = 0; i < 3; i++) {
    cr_assert_eq(stored_metrics[i].nat_tier, metrics[i].nat_tier, "NAT tier should match for participant %d", i);
    cr_assert_eq(stored_metrics[i].upload_kbps, metrics[i].upload_kbps, "Upload should match for participant %d", i);
    cr_assert_eq(stored_metrics[i].rtt_ns, metrics[i].rtt_ns, "RTT should match for participant %d", i);
  }

  consensus_metrics_collection_destroy(collection);
}

/**
 * Test: Collection auto-resizes when capacity exceeded
 */
Test(consensus_metrics, collection_resize) {
  consensus_metrics_collection_t *collection = NULL;
  consensus_metrics_collection_create(&collection);

  // Add more than initial capacity (10)
  for (int i = 0; i < 20; i++) {
    participant_metrics_t metrics = {0};
    make_uuid(metrics.participant_id, i);
    metrics.nat_tier = i % 5;
    metrics.upload_kbps = 1000 + i * 100;

    asciichat_error_t err = consensus_metrics_collection_add(collection, &metrics);
    cr_assert_eq(err, ASCIICHAT_OK, "Addition should succeed even after resize");
  }

  // Verify all metrics were added
  const participant_metrics_t *stored_metrics;
  int count = 0;
  consensus_metrics_collection_get(collection, &stored_metrics, &count);

  cr_assert_eq(count, 20, "Should have 20 metrics after resize");

  // Spot-check some metrics
  cr_assert_eq(stored_metrics[0].nat_tier, 0, "First metric should match");
  cr_assert_eq(stored_metrics[19].nat_tier, 4, "Last metric should match");
  cr_assert_eq(stored_metrics[10].upload_kbps, 2000, "Middle metric should match");

  consensus_metrics_collection_destroy(collection);
}

/**
 * Test: STUN probe success rate is 0-100%
 */
Test(consensus_metrics, stun_probe_success) {
  uint8_t my_id[16];
  make_uuid(my_id, 777);

  participant_metrics_t metrics;
  consensus_metrics_measure(my_id, &metrics);

  // Verify success rate is valid
  cr_assert(metrics.stun_probe_success_pct >= 0 && metrics.stun_probe_success_pct <= 100,
            "Success rate should be 0-100 percent");

  // In current implementation, should be exactly 90
  cr_assert_eq(metrics.stun_probe_success_pct, 90, "Default success rate should be 90 percent");
}

/**
 * Test: Invalid parameters return errors
 */
Test(consensus_metrics, invalid_parameters) {
  participant_metrics_t metrics = {0};
  uint8_t id[16] = {0};

  // NULL output
  asciichat_error_t err = consensus_metrics_measure(id, NULL);
  cr_assert_neq(err, ASCIICHAT_OK, "Should reject NULL output");

  // NULL input
  err = consensus_metrics_measure(NULL, &metrics);
  cr_assert_neq(err, ASCIICHAT_OK, "Should reject NULL input");

  // Collection operations with NULL
  err = consensus_metrics_collection_add(NULL, &metrics);
  cr_assert_neq(err, ASCIICHAT_OK, "Should reject NULL collection");

  err = consensus_metrics_collection_get(NULL, NULL, NULL);
  cr_assert_neq(err, ASCIICHAT_OK, "Should reject NULL collection");
}
