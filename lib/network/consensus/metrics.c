/**
 * @file network/consensus/metrics.c
 * @brief Ring consensus metrics collection and wire protocol implementation
 */

#include <ascii-chat/network/consensus/metrics.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/asciichat_errno.h>
#include <string.h>
#include <stdio.h>

/**
 * Metrics collection structure
 */
typedef struct consensus_metrics_collection {
  participant_metrics_t *metrics;
  int count;
  int capacity;
} consensus_metrics_collection_t;

/**
 * Default bandwidth estimate (50 Mbps = 50000 Kbps)
 */
#define DEFAULT_BANDWIDTH_KBPS 50000

/**
 * Default RTT estimate (25 milliseconds)
 */
#define DEFAULT_RTT_MS 25

/**
 * Number of STUN probes to send for success rate measurement
 */
#define NUM_STUN_PROBES 10

/**
 * Measure STUN probe success rate
 *
 * Simulates sending NUM_STUN_PROBES STUN binding requests and counts successes.
 * Returns success rate as 0-100 percentage.
 *
 * @return Success rate 0-100
 */
static uint8_t measure_stun_probe_success(void) {
  // TODO: Implement actual STUN probing in future
  // For now, return a reasonable default (90% success)
  // In production, this would:
  // 1. Send NUM_STUN_PROBES STUN Binding Request packets
  // 2. Count successful responses
  // 3. Return (successes * 100) / NUM_STUN_PROBES
  return 90;
}

/**
 * Convert NAT type to tier (0=best, 4=worst)
 *
 * Mapping:
 * - Open/LAN (no NAT) = 0
 * - Full cone NAT = 1
 * - Address-restricted NAT = 2
 * - Port-restricted NAT = 3
 * - Symmetric NAT = 4
 *
 * Note: This function is reserved for future use when NAT detection is integrated.
 * For now, we use a hardcoded default tier.
 */
__attribute__((unused)) static uint8_t nat_type_to_tier(int nat_type) {
  // ACIP_NAT_TYPE_* enum values:
  // 0 = Open (OPEN = no NAT, essentially LAN)
  // 1 = Full cone (best NAT)
  // 2 = Address-restricted
  // 3 = Port-restricted
  // 4 = Symmetric (worst NAT)

  if (nat_type < 0 || nat_type > 4) {
    return 4; // Default to worst case
  }
  return (uint8_t)nat_type;
}

asciichat_error_t consensus_metrics_measure(const uint8_t my_id[16], participant_metrics_t *out_metrics) {
  if (!my_id || !out_metrics) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters to consensus_metrics_measure");
  }

  // Initialize metrics structure
  memset(out_metrics, 0, sizeof(*out_metrics));

  // Copy participant ID
  memcpy(out_metrics->participant_id, my_id, 16);

  // NAT tier: Default to 1 (Public IP, no NAT needed)
  // TODO: In production, query NAT detection module
  out_metrics->nat_tier = 1;

  // Upload bandwidth: Default 50 Mbps
  out_metrics->upload_kbps = DEFAULT_BANDWIDTH_KBPS;

  // RTT: Default 25ms (25,000,000 nanoseconds)
  out_metrics->rtt_ns = DEFAULT_RTT_MS * NS_PER_MS;

  // STUN probe success rate: Measure by sending probes
  out_metrics->stun_probe_success_pct = measure_stun_probe_success();

  // Public address and port: Placeholder values
  snprintf(out_metrics->public_address, sizeof(out_metrics->public_address), "127.0.0.1");
  out_metrics->public_port = 27224;

  // Connection type: Default to direct
  out_metrics->connection_type = 0; // Direct connection

  // Measurement time: Current time in nanoseconds
  out_metrics->measurement_time_ns = time_get_realtime_ns();

  // Measurement window: 1 second in nanoseconds
  out_metrics->measurement_window_ns = 1 * NS_PER_SEC;

  return ASCIICHAT_OK;
}

asciichat_error_t consensus_metrics_to_wire(const participant_metrics_t *metrics, participant_metrics_t *out_wire) {
  if (!metrics || !out_wire) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters to consensus_metrics_to_wire");
  }

  // Copy entire structure first
  memcpy(out_wire, metrics, sizeof(*out_wire));

  // Convert multi-byte fields to network byte order
  out_wire->upload_kbps = endian_pack_u32(metrics->upload_kbps);
  out_wire->rtt_ns = endian_pack_u32(metrics->rtt_ns);
  out_wire->public_port = endian_pack_u16(metrics->public_port);
  out_wire->measurement_time_ns = endian_pack_u64(metrics->measurement_time_ns);
  out_wire->measurement_window_ns = endian_pack_u64(metrics->measurement_window_ns);

  return ASCIICHAT_OK;
}

asciichat_error_t consensus_metrics_from_wire(const participant_metrics_t *wire_metrics,
                                              participant_metrics_t *out_metrics) {
  if (!wire_metrics || !out_metrics) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters to consensus_metrics_from_wire");
  }

  // Copy entire structure first
  memcpy(out_metrics, wire_metrics, sizeof(*out_metrics));

  // Convert multi-byte fields from network byte order
  out_metrics->upload_kbps = endian_unpack_u32(wire_metrics->upload_kbps);
  out_metrics->rtt_ns = endian_unpack_u32(wire_metrics->rtt_ns);
  out_metrics->public_port = endian_unpack_u16(wire_metrics->public_port);
  out_metrics->measurement_time_ns = endian_unpack_u64(wire_metrics->measurement_time_ns);
  out_metrics->measurement_window_ns = endian_unpack_u64(wire_metrics->measurement_window_ns);

  return ASCIICHAT_OK;
}

asciichat_error_t consensus_metrics_collection_create(consensus_metrics_collection_t **out_collection) {
  if (!out_collection) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid output parameter");
  }

  consensus_metrics_collection_t *collection = SAFE_MALLOC(sizeof(*collection), consensus_metrics_collection_t *);
  memset(collection, 0, sizeof(*collection));

  collection->capacity = 10; // Start with capacity for 10 participants
  collection->metrics = SAFE_MALLOC(collection->capacity * sizeof(participant_metrics_t), participant_metrics_t *);
  collection->count = 0;

  *out_collection = collection;
  return ASCIICHAT_OK;
}

asciichat_error_t consensus_metrics_collection_add(consensus_metrics_collection_t *collection,
                                                   const participant_metrics_t *metrics) {
  if (!collection || !metrics) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters to collection_add");
  }

  // Resize if needed
  if (collection->count >= collection->capacity) {
    int new_capacity = collection->capacity * 2;
    participant_metrics_t *new_metrics =
        SAFE_MALLOC(new_capacity * sizeof(participant_metrics_t), participant_metrics_t *);

    if (!new_metrics) {
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate metrics array");
    }

    // Copy existing metrics
    if (collection->count > 0 && collection->metrics) {
      memcpy(new_metrics, collection->metrics, collection->count * sizeof(participant_metrics_t));
      SAFE_FREE(collection->metrics);
    }

    collection->metrics = new_metrics;
    collection->capacity = new_capacity;
  }

  // Add new metrics
  if (!collection->metrics) {
    return SET_ERRNO(ERROR_MEMORY, "Collection metrics array is NULL");
  }

  memcpy(&collection->metrics[collection->count], metrics, sizeof(*metrics));
  collection->count++;

  return ASCIICHAT_OK;
}

asciichat_error_t consensus_metrics_collection_get(const consensus_metrics_collection_t *collection,
                                                   const participant_metrics_t **out_metrics, int *out_count) {
  if (!collection || !out_metrics || !out_count) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters to collection_get");
  }

  *out_metrics = collection->metrics;
  *out_count = collection->count;

  return ASCIICHAT_OK;
}

void consensus_metrics_collection_destroy(consensus_metrics_collection_t *collection) {
  if (!collection) {
    return;
  }

  if (collection->metrics) {
    SAFE_FREE(collection->metrics);
  }

  SAFE_FREE(collection);
}
