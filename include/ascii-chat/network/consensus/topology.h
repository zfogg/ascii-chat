/**
 * @file network/consensus/topology.h
 * @brief Ring topology management
 * @ingroup consensus
 */

#pragma once

#include "metrics.h"
#include <ascii-chat/asciichat_errno.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Opaque ring topology handle
 */
typedef struct consensus_topology consensus_topology_t;

/**
 * @brief Create new ring topology from participant list
 *
 * Sorts participants lexicographically and computes ring positions.
 * Returns error if my_id not in participant list.
 *
 * @param participant_ids Array of 16-byte UUIDs (up to 64 participants)
 * @param num_participants Number of participants (1-64)
 * @param my_id My 16-byte UUID (must be in participant_ids)
 * @param out_topology Output topology handle (caller must destroy)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t consensus_topology_create(const uint8_t participant_ids[64][16], int num_participants,
                                            const uint8_t my_id[16], consensus_topology_t **out_topology);

/**
 * @brief Destroy topology
 *
 * @param topology Topology handle (safe to pass NULL)
 */
void consensus_topology_destroy(consensus_topology_t *topology);

/**
 * @brief Get my position in ring (0 = first)
 *
 * @param topology Topology handle
 * @return My position in ring order, or -1 if invalid
 */
int consensus_topology_get_position(const consensus_topology_t *topology);

/**
 * @brief Check if I am the ring leader (last position)
 *
 * @param topology Topology handle
 * @return true if I am the leader, false otherwise
 */
bool consensus_topology_am_leader(const consensus_topology_t *topology);

/**
 * @brief Get next participant ID in ring
 *
 * Wraps around from last position to first.
 *
 * @param topology Topology handle
 * @param out_id Output 16-byte UUID of next participant
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t consensus_topology_get_next(const consensus_topology_t *topology, uint8_t out_id[16]);

/**
 * @brief Get previous participant ID in ring
 *
 * Wraps around from first position to last.
 *
 * @param topology Topology handle
 * @param out_id Output 16-byte UUID of previous participant
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t consensus_topology_get_prev(const consensus_topology_t *topology, uint8_t out_id[16]);

/**
 * @brief Get ring leader ID (last participant)
 *
 * @param topology Topology handle
 * @param out_id Output 16-byte UUID of ring leader
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t consensus_topology_get_leader(const consensus_topology_t *topology, uint8_t out_id[16]);

/**
 * @brief Get all participant IDs in ring order
 *
 * @param topology Topology handle
 * @param out_ids Array to fill with 16-byte UUIDs (must hold 64*16 bytes)
 * @param out_count Output number of participants
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t consensus_topology_get_all(const consensus_topology_t *topology, uint8_t out_ids[64][16],
                                             int *out_count);
