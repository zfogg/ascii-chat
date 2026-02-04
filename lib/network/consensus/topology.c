/**
 * @file network/consensus/topology.c
 * @brief Ring topology implementation
 */

#include <ascii-chat/network/consensus/topology.h>
#include <ascii-chat/common.h>
#include <ascii-chat/asciichat_errno.h>
#include <string.h>

typedef struct consensus_topology {
  uint8_t participant_ids[64][16]; // Sorted order
  int num_participants;
  int my_position; // My index in sorted list
} consensus_topology_t;

// Helper: find position of UUID in array
static int find_uuid_position(const uint8_t uuids[64][16], int count, const uint8_t target[16]) {
  for (int i = 0; i < count; i++) {
    if (memcmp(uuids[i], target, 16) == 0) {
      return i;
    }
  }
  return -1;
}

// Comparison function for qsort
static int qsort_uuid_compare(const void *a, const void *b) {
  return memcmp(a, b, 16);
}

asciichat_error_t consensus_topology_create(const uint8_t participant_ids[64][16], int num_participants,
                                            const uint8_t my_id[16], consensus_topology_t **out_topology) {
  if (!participant_ids || !my_id || !out_topology || num_participants <= 0 || num_participants > 64) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid topology creation parameters");
  }

  consensus_topology_t *topology = SAFE_MALLOC(sizeof(*topology), consensus_topology_t *);
  memset(topology, 0, sizeof(*topology));

  // Copy and sort participant IDs lexicographically
  memcpy(topology->participant_ids, participant_ids, num_participants * 16);
  topology->num_participants = num_participants;

  qsort(topology->participant_ids, num_participants, 16, qsort_uuid_compare);

  // Find my position
  topology->my_position = find_uuid_position(topology->participant_ids, num_participants, my_id);
  if (topology->my_position < 0) {
    SAFE_FREE(topology);
    return SET_ERRNO(ERROR_INVALID_PARAM, "My UUID not found in participant list");
  }

  *out_topology = topology;
  return ASCIICHAT_OK;
}

void consensus_topology_destroy(consensus_topology_t *topology) {
  if (topology) {
    SAFE_FREE(topology);
  }
}

int consensus_topology_get_position(const consensus_topology_t *topology) {
  if (!topology)
    return -1;
  return topology->my_position;
}

bool consensus_topology_am_leader(const consensus_topology_t *topology) {
  if (!topology)
    return false;
  return topology->my_position == (topology->num_participants - 1);
}

asciichat_error_t consensus_topology_get_next(const consensus_topology_t *topology, uint8_t out_id[16]) {
  if (!topology || !out_id) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  int next_pos = (topology->my_position + 1) % topology->num_participants;
  memcpy(out_id, topology->participant_ids[next_pos], 16);
  return ASCIICHAT_OK;
}

asciichat_error_t consensus_topology_get_prev(const consensus_topology_t *topology, uint8_t out_id[16]) {
  if (!topology || !out_id) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  int prev_pos = (topology->my_position - 1 + topology->num_participants) % topology->num_participants;
  memcpy(out_id, topology->participant_ids[prev_pos], 16);
  return ASCIICHAT_OK;
}

asciichat_error_t consensus_topology_get_leader(const consensus_topology_t *topology, uint8_t out_id[16]) {
  if (!topology || !out_id) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  memcpy(out_id, topology->participant_ids[topology->num_participants - 1], 16);
  return ASCIICHAT_OK;
}

asciichat_error_t consensus_topology_get_all(const consensus_topology_t *topology, uint8_t out_ids[64][16],
                                             int *out_count) {
  if (!topology || !out_ids || !out_count) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  memcpy(out_ids, topology->participant_ids, topology->num_participants * 16);
  *out_count = topology->num_participants;
  return ASCIICHAT_OK;
}
