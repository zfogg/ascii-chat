/**
 * @file network/consensus/metrics.h
 * @brief Ring consensus metrics collection and wire protocol
 * @ingroup consensus
 *
 * Handles measurement, serialization, and deserialization of network quality
 * metrics for transmission around the consensus ring. Metrics include:
 * - NAT tier classification (0=LAN, 1=Public, 2=UPnP, 3=STUN, 4=TURN)
 * - Upload bandwidth (Kbps)
 * - Round-trip time (milliseconds)
 * - STUN probe success rate (0-100%)
 * - Public address and port information
 *
 * Wire format uses network byte order (big-endian) for all multi-byte values.
 */

#pragma once

#include "packets.h"
#include <ascii-chat/asciichat_errno.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque metrics collection handle
 */
typedef struct consensus_metrics_collection consensus_metrics_collection_t;

/**
 * @brief Measure participant network quality metrics
 *
 * Collects all metrics needed for host selection:
 * - NAT tier from detected NAT type (0-4)
 * - Bandwidth estimate (default 50 Mbps if not available)
 * - RTT from keepalive pings (default 25ms if not available)
 * - STUN probe success rate by sending 10 probes
 *
 * The measurement includes the participant's UUID and timestamps.
 *
 * @param my_id Participant UUID (16 bytes)
 * @param out_metrics Output metrics structure (caller-allocated)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note out_metrics must point to valid participant_metrics_t allocated by caller
 * @note Measurement window and timestamp are set automatically
 */
asciichat_error_t consensus_metrics_measure(const uint8_t my_id[16], participant_metrics_t *out_metrics);

/**
 * @brief Serialize metrics to wire format with network byte order
 *
 * Converts host-order values to network byte order (big-endian) for
 * transmission over the network. Multi-byte fields are converted:
 * - upload_kbps (uint32_t)
 * - rtt_ms (uint16_t)
 * - public_port (uint16_t)
 * - measurement_time_ms (uint64_t)
 * - measurement_window_ms (uint32_t)
 *
 * @param metrics Input metrics in host byte order
 * @param out_wire Output metrics in network byte order
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note out_wire must point to valid participant_metrics_t allocated by caller
 */
asciichat_error_t consensus_metrics_to_wire(const participant_metrics_t *metrics, participant_metrics_t *out_wire);

/**
 * @brief Deserialize metrics from wire format
 *
 * Converts network byte order values back to host byte order. Inverse
 * operation of consensus_metrics_to_wire().
 *
 * @param wire_metrics Input metrics in network byte order
 * @param out_metrics Output metrics in host byte order
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note out_metrics must point to valid participant_metrics_t allocated by caller
 */
asciichat_error_t consensus_metrics_from_wire(const participant_metrics_t *wire_metrics,
                                              participant_metrics_t *out_metrics);

/**
 * @brief Create empty metrics collection
 *
 * Allocates a collection structure for accumulating metrics from multiple
 * participants during a collection round.
 *
 * @param out_collection Output collection handle
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note Must be freed with consensus_metrics_destroy()
 */
asciichat_error_t consensus_metrics_collection_create(consensus_metrics_collection_t **out_collection);

/**
 * @brief Add metrics to collection
 *
 * Accumulates metrics from a participant into the collection. Metrics are
 * stored in dynamically-allocated array.
 *
 * @param collection Collection to add to
 * @param metrics Metrics to add
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t consensus_metrics_collection_add(consensus_metrics_collection_t *collection,
                                                   const participant_metrics_t *metrics);

/**
 * @brief Get all accumulated metrics
 *
 * Retrieves the accumulated metrics array from collection.
 *
 * @param collection Collection to query
 * @param out_metrics Pointer to receive metrics array (not copied, owned by collection)
 * @param out_count Pointer to receive number of metrics
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note out_metrics points to internal collection storage - do not modify or free
 * @note Valid only until collection is destroyed
 */
asciichat_error_t consensus_metrics_collection_get(const consensus_metrics_collection_t *collection,
                                                   const participant_metrics_t **out_metrics, int *out_count);

/**
 * @brief Destroy metrics collection
 *
 * Frees all allocated memory in the collection, including the metrics array.
 *
 * @param collection Collection to destroy (can be NULL)
 */
void consensus_metrics_collection_destroy(consensus_metrics_collection_t *collection);

#ifdef __cplusplus
}
#endif
