#include <criterion/criterion.h>
#include <ascii-chat/network/consensus/election.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>

// Helper: create metrics
static participant_metrics_t make_metrics(int nat_tier, uint32_t upload_kbps, uint16_t rtt_ms, uint8_t probe_pct) {
  participant_metrics_t m = {0};
  m.nat_tier = nat_tier;
  m.upload_kbps = upload_kbps;
  m.rtt_ns = (uint16_t)rtt_ms * 1000000;
  m.stun_probe_success_pct = probe_pct;
  return m;
}

static void set_id(participant_metrics_t *m, int id) {
  memset(m->participant_id, 0, 16);
  m->participant_id[0] = id & 0xFF;
}
// Use verbose logging with debug level enabled and stdout/stderr not disabled
TestSuite(election);

Test(election, score_computation) {
  // LAN, 100Mbps, 20ms RTT, 98% success
  participant_metrics_t m = make_metrics(0, 100000, 20, 98);

  // Expected score:
  // (4-0)*1000 + 100000/10 + (500-20) + 98
  // = 4000 + 10000 + 480 + 98 = 14578
  uint32_t score = consensus_election_compute_score(&m);
  cr_assert_eq(score, 14578);
}

Test(election, stun_only_network) {
  // STUN NAT, 10Mbps, 50ms, 85% success
  participant_metrics_t m = make_metrics(3, 10000, 50, 85);

  // Expected: (4-3)*1000 + 10000/10 + (500-50) + 85
  // = 1000 + 1000 + 450 + 85 = 2535
  uint32_t score = consensus_election_compute_score(&m);
  cr_assert_eq(score, 2535);
}

Test(election, choose_best_two) {
  // 4 participants
  participant_metrics_t metrics[4] = {
      make_metrics(1, 50000, 30, 95),  // Score: 3000+5000+470+95 = 8565
      make_metrics(3, 10000, 50, 85),  // Score: 1000+1000+450+85 = 2535
      make_metrics(2, 100000, 20, 98), // Score: 2000+10000+480+98 = 12578 (best)
      make_metrics(1, 75000, 25, 96),  // Score: 3000+7500+475+96 = 11071 (second)
  };

  set_id(&metrics[0], 0);
  set_id(&metrics[1], 1);
  set_id(&metrics[2], 2);
  set_id(&metrics[3], 3);

  int best, backup;
  asciichat_error_t err = consensus_election_choose_hosts(metrics, 4, &best, &backup);

  cr_assert_eq(err, ASCIICHAT_OK);
  cr_assert_eq(best, 2);   // Participant 2 has highest score
  cr_assert_eq(backup, 3); // Participant 3 has second-highest
}

Test(election, verify_correct) {
  participant_metrics_t metrics[2] = {
      make_metrics(0, 50000, 30, 95),
      make_metrics(1, 75000, 25, 96), // Better score
  };

  set_id(&metrics[0], 0x01);
  set_id(&metrics[1], 0x02);

  uint8_t announced_host[16], announced_backup[16];
  memset(announced_host, 0, 16);
  memset(announced_backup, 0, 16);
  announced_host[0] = 0x02;   // Participant 1 (index 1)
  announced_backup[0] = 0x01; // Participant 0 (index 0)

  bool valid = false;
  asciichat_error_t err = consensus_election_verify(metrics, 2, announced_host, announced_backup, &valid);

  cr_assert_eq(err, ASCIICHAT_OK);
  cr_assert_eq(valid, true);
}

Test(election, verify_incorrect) {
  participant_metrics_t metrics[2] = {
      make_metrics(0, 50000, 30, 95),
      make_metrics(1, 75000, 25, 96), // Better score
  };

  set_id(&metrics[0], 0x01);
  set_id(&metrics[1], 0x02);

  uint8_t announced_host[16], announced_backup[16];
  memset(announced_host, 0, 16);
  memset(announced_backup, 0, 16);
  announced_host[0] = 0x01;   // Wrong! Should be participant 1
  announced_backup[0] = 0x02; // Wrong! Should be participant 0

  bool valid = false;
  consensus_election_verify(metrics, 2, announced_host, announced_backup, &valid);

  cr_assert_eq(valid, false);
}
