/**
 * @file tests/unit/network/consensus/test_topology.c
 * @brief Ring topology unit tests
 */

#include <criterion/criterion.h>
#include <ascii-chat/network/consensus/topology.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>

// Test helper: create UUID for testing
static void make_uuid(uint8_t id[16], int value) {
  memset(id, 0, 16);
  id[0] = value & 0xFF;
}
// Use verbose logging with debug level enabled and stdout/stderr not disabled
TestSuite(topology);

Test(topology, create_and_destroy) {
  uint8_t participants[4][16] = {
      {3, 0, 0, 0}, // C
      {1, 0, 0, 0}, // A
      {2, 0, 0, 0}, // B
      {4, 0, 0, 0}, // D
  };
  uint8_t my_id[16];
  make_uuid(my_id, 2); // I am B

  consensus_topology_t *topo = NULL;
  asciichat_error_t err = consensus_topology_create(participants, 4, my_id, &topo);

  cr_assert_eq(err, ASCIICHAT_OK);
  cr_assert_neq(topo, NULL);
  consensus_topology_destroy(topo);
}

Test(topology, positions_sorted_lexicographically) {
  // Input: C, A, B, D -> should sort to A, B, C, D (indices 1, 2, 3, 4)
  uint8_t participants[4][16] = {
      {3, 0, 0, 0}, // C -> position 2
      {1, 0, 0, 0}, // A -> position 0
      {2, 0, 0, 0}, // B -> position 1
      {4, 0, 0, 0}, // D -> position 3
  };
  uint8_t my_id[16];
  make_uuid(my_id, 2); // I am B

  consensus_topology_t *topo = NULL;
  consensus_topology_create(participants, 4, my_id, &topo);

  // B should be at position 1
  cr_assert_eq(consensus_topology_get_position(topo), 1);
  cr_assert_eq(consensus_topology_am_leader(topo), false);

  consensus_topology_destroy(topo);
}

Test(topology, leader_is_last) {
  uint8_t participants[4][16] = {
      {3, 0, 0, 0},
      {1, 0, 0, 0},
      {2, 0, 0, 0},
      {4, 0, 0, 0},
  };
  uint8_t my_id[16];
  make_uuid(my_id, 4); // I am D (last when sorted)

  consensus_topology_t *topo = NULL;
  consensus_topology_create(participants, 4, my_id, &topo);

  cr_assert_eq(consensus_topology_get_position(topo), 3);
  cr_assert_eq(consensus_topology_am_leader(topo), true);

  consensus_topology_destroy(topo);
}

Test(topology, next_prev_circular) {
  uint8_t participants[4][16] = {
      {3, 0, 0, 0}, // C
      {1, 0, 0, 0}, // A
      {2, 0, 0, 0}, // B
      {4, 0, 0, 0}, // D
  };
  uint8_t my_id[16];
  make_uuid(my_id, 2); // B (position 1 in sorted: A, B, C, D)

  consensus_topology_t *topo = NULL;
  consensus_topology_create(participants, 4, my_id, &topo);

  uint8_t next[16], prev[16];
  consensus_topology_get_next(topo, next);
  consensus_topology_get_prev(topo, prev);

  uint8_t expected_next[16], expected_prev[16];
  make_uuid(expected_next, 3); // C
  make_uuid(expected_prev, 1); // A

  cr_assert_eq(memcmp(next, expected_next, 16), 0);
  cr_assert_eq(memcmp(prev, expected_prev, 16), 0);

  consensus_topology_destroy(topo);
}

Test(topology, get_leader) {
  uint8_t participants[4][16] = {
      {3, 0, 0, 0},
      {1, 0, 0, 0},
      {2, 0, 0, 0},
      {4, 0, 0, 0},
  };
  uint8_t my_id[16];
  make_uuid(my_id, 2); // B

  consensus_topology_t *topo = NULL;
  consensus_topology_create(participants, 4, my_id, &topo);

  uint8_t leader[16];
  asciichat_error_t err = consensus_topology_get_leader(topo, leader);

  cr_assert_eq(err, ASCIICHAT_OK);

  uint8_t expected_leader[16];
  make_uuid(expected_leader, 4); // D (last)
  cr_assert_eq(memcmp(leader, expected_leader, 16), 0);

  consensus_topology_destroy(topo);
}

Test(topology, get_all) {
  uint8_t participants[4][16] = {
      {3, 0, 0, 0},
      {1, 0, 0, 0},
      {2, 0, 0, 0},
      {4, 0, 0, 0},
  };
  uint8_t my_id[16];
  make_uuid(my_id, 2);

  consensus_topology_t *topo = NULL;
  consensus_topology_create(participants, 4, my_id, &topo);

  uint8_t all_ids[64][16];
  int count = 0;
  asciichat_error_t err = consensus_topology_get_all(topo, all_ids, &count);

  cr_assert_eq(err, ASCIICHAT_OK);
  cr_assert_eq(count, 4);

  // Should be sorted: A(1), B(2), C(3), D(4)
  uint8_t expected[4][16];
  make_uuid(expected[0], 1);
  make_uuid(expected[1], 2);
  make_uuid(expected[2], 3);
  make_uuid(expected[3], 4);

  for (int i = 0; i < 4; i++) {
    cr_assert_eq(memcmp(all_ids[i], expected[i], 16), 0, "Participant %d mismatch", i);
  }

  consensus_topology_destroy(topo);
}

Test(topology, invalid_my_id) {
  uint8_t participants[2][16] = {
      {1, 0, 0, 0},
      {2, 0, 0, 0},
  };
  uint8_t my_id[16];
  make_uuid(my_id, 99); // Not in participants

  consensus_topology_t *topo = NULL;
  asciichat_error_t err = consensus_topology_create(participants, 2, my_id, &topo);

  cr_assert_neq(err, ASCIICHAT_OK);
}

Test(topology, circular_wrap_first_to_last) {
  uint8_t participants[3][16] = {
      {1, 0, 0, 0},
      {2, 0, 0, 0},
      {3, 0, 0, 0},
  };
  uint8_t my_id[16];
  make_uuid(my_id, 1); // At position 0

  consensus_topology_t *topo = NULL;
  consensus_topology_create(participants, 3, my_id, &topo);

  uint8_t prev[16];
  consensus_topology_get_prev(topo, prev);

  uint8_t expected[16];
  make_uuid(expected, 3); // Last participant wraps to first
  cr_assert_eq(memcmp(prev, expected, 16), 0);

  consensus_topology_destroy(topo);
}

Test(topology, circular_wrap_last_to_first) {
  uint8_t participants[3][16] = {
      {1, 0, 0, 0},
      {2, 0, 0, 0},
      {3, 0, 0, 0},
  };
  uint8_t my_id[16];
  make_uuid(my_id, 3); // At position 2 (last)

  consensus_topology_t *topo = NULL;
  consensus_topology_create(participants, 3, my_id, &topo);

  uint8_t next[16];
  consensus_topology_get_next(topo, next);

  uint8_t expected[16];
  make_uuid(expected, 1); // First participant wraps from last
  cr_assert_eq(memcmp(next, expected, 16), 0);

  consensus_topology_destroy(topo);
}
