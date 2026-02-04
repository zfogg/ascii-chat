#include <criterion/criterion.h>
#include <ascii-chat/network/consensus/packets.h>
#include <string.h>

Test(consensus_packets, ring_members_size) {
  // Verify struct is properly packed
  // session_id: 16 bytes
  // participant_ids: 64 * 16 = 1024 bytes
  // num_participants: 1 byte
  // ring_leader_index: 1 byte
  // generation: 4 bytes
  // Total: 16 + 1024 + 1 + 1 + 4 = 1046 bytes
  cr_assert_eq(sizeof(acip_ring_members_t), 16 + 64 * 16 + 1 + 1 + 4);
}

Test(consensus_packets, metrics_size) {
  // participant_metrics_t should be predictable size
  // participant_id: 16 bytes
  // nat_tier: 1 byte
  // upload_kbps: 4 bytes
  // rtt_ms: 2 bytes
  // stun_probe_success_pct: 1 byte
  // public_address: 64 bytes
  // public_port: 2 bytes
  // connection_type: 1 byte
  // measurement_time_ms: 8 bytes
  // measurement_window_ms: 4 bytes
  // Total: 16 + 1 + 4 + 2 + 1 + 64 + 2 + 1 + 8 + 4 = 103 bytes
  size_t expected = 16 + 1 + 4 + 2 + 1 + 64 + 2 + 1 + 8 + 4;
  cr_assert_eq(sizeof(participant_metrics_t), expected);
}

Test(consensus_packets, packet_names) {
  cr_assert_str_eq(consensus_packet_type_name(6100), "RING_MEMBERS");
  cr_assert_str_eq(consensus_packet_type_name(6101), "STATS_COLLECTION_START");
  cr_assert_str_eq(consensus_packet_type_name(6102), "STATS_UPDATE");
  cr_assert_str_eq(consensus_packet_type_name(6103), "RING_ELECTION_RESULT");
  cr_assert_str_eq(consensus_packet_type_name(6104), "STATS_ACK");
}

Test(consensus_packets, packet_names_unknown) {
  const char *name = consensus_packet_type_name(9999);
  cr_assert_str_eq(name, "UNKNOWN_CONSENSUS_PACKET");
}

Test(consensus_packets, min_sizes) {
  cr_assert_gt(consensus_get_min_packet_size(6100), 0);
  cr_assert_gt(consensus_get_min_packet_size(6101), 0);
  cr_assert_gt(consensus_get_min_packet_size(6102), 0);
  cr_assert_gt(consensus_get_min_packet_size(6103), 0);
  cr_assert_gt(consensus_get_min_packet_size(6104), 0);
}

Test(consensus_packets, min_sizes_unknown) {
  size_t size = consensus_get_min_packet_size(9999);
  cr_assert_eq(size, 0);
}

Test(consensus_packets, stats_collection_start_size) {
  // session_id: 16 bytes
  // initiator_id: 16 bytes
  // round_id: 4 bytes
  // collection_deadline_ms: 8 bytes
  // Total: 16 + 16 + 4 + 8 = 44 bytes
  size_t expected = 16 + 16 + 4 + 8;
  cr_assert_eq(sizeof(acip_stats_collection_start_t), expected);
}

Test(consensus_packets, stats_update_size) {
  // session_id: 16 bytes
  // sender_id: 16 bytes
  // round_id: 4 bytes
  // num_metrics: 1 byte
  // Total: 16 + 16 + 4 + 1 = 37 bytes
  size_t expected = 16 + 16 + 4 + 1;
  cr_assert_eq(sizeof(acip_stats_update_t), expected);
}

Test(consensus_packets, stats_ack_size) {
  // session_id: 16 bytes
  // participant_id: 16 bytes
  // round_id: 4 bytes
  // ack_status: 1 byte
  // stored_host_id: 16 bytes
  // stored_backup_id: 16 bytes
  // Total: 16 + 16 + 4 + 1 + 16 + 16 = 69 bytes
  size_t expected = 16 + 16 + 4 + 1 + 16 + 16;
  cr_assert_eq(sizeof(acip_stats_ack_t), expected);
}

Test(consensus_packets, ring_election_result_size) {
  // session_id: 16 bytes
  // leader_id: 16 bytes
  // round_id: 4 bytes
  // host_id: 16 bytes
  // host_address: 64 bytes
  // host_port: 2 bytes
  // backup_id: 16 bytes
  // backup_address: 64 bytes
  // backup_port: 2 bytes
  // elected_at_ms: 8 bytes
  // num_participants: 1 byte
  // Total: 16 + 16 + 4 + 16 + 64 + 2 + 16 + 64 + 2 + 8 + 1 = 209 bytes
  size_t expected = 16 + 16 + 4 + 16 + 64 + 2 + 16 + 64 + 2 + 8 + 1;
  cr_assert_eq(sizeof(acip_ring_election_result_t), expected);
}
