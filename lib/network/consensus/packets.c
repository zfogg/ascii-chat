/**
 * @file network/consensus/packets.c
 * @brief Ring consensus packet definitions and helpers
 */

#include <ascii-chat/network/consensus/packets.h>
#include <string.h>

const char *consensus_packet_type_name(uint16_t type) {
  switch (type) {
  case 6100: // PACKET_TYPE_RING_MEMBERS
    return "RING_MEMBERS";
  case 6101: // PACKET_TYPE_STATS_COLLECTION_START
    return "STATS_COLLECTION_START";
  case 6102: // PACKET_TYPE_STATS_UPDATE
    return "STATS_UPDATE";
  case 6103: // PACKET_TYPE_RING_ELECTION_RESULT
    return "RING_ELECTION_RESULT";
  case 6104: // PACKET_TYPE_STATS_ACK
    return "STATS_ACK";
  default:
    return "UNKNOWN_CONSENSUS_PACKET";
  }
}

size_t consensus_get_min_packet_size(uint16_t type) {
  switch (type) {
  case 6100: // PACKET_TYPE_RING_MEMBERS
    return sizeof(acip_ring_members_t);
  case 6101: // PACKET_TYPE_STATS_COLLECTION_START
    return sizeof(acip_stats_collection_start_t);
  case 6102: // PACKET_TYPE_STATS_UPDATE
    return sizeof(acip_stats_update_t);
  case 6103: // PACKET_TYPE_RING_ELECTION_RESULT
    return sizeof(acip_ring_election_result_t);
  case 6104: // PACKET_TYPE_STATS_ACK
    return sizeof(acip_stats_ack_t);
  default:
    return 0;
  }
}
