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
 * @brief Metrics collection structure for participant quality measurements
 *
 * Dynamic array storing network quality metrics for all participants
 * in a consensus session.
 */
typedef struct consensus_metrics_collection {
  participant_metrics_t *metrics; ///< Array of participant metrics
  int count;                      ///< Number of participants
  int capacity;                   ///< Allocated capacity
} consensus_metrics_collection_t;

asciichat_error_t consensus_metrics_measure(const uint8_t my_id[16], participant_metrics_t *out_metrics) {
  if (!my_id || !out_metrics)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid measurement parameters");
  memset(out_metrics, 0, sizeof(*out_metrics));
  memcpy(out_metrics->participant_id, my_id, 16);
  out_metrics->nat_tier = 4;
  return SET_ERRNO(ERROR_INVALID_STATE, "Network measurements require a session measurement provider");
}

asciichat_error_t consensus_metrics_to_wire(const participant_metrics_t *metrics, participant_metrics_t *out_wire) {
  if (!metrics || !out_wire) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
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
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
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
