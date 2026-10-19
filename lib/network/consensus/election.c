/**
 * @file network/consensus/election.c
 * @brief Deterministic election algorithm
 */

#include <ascii-chat/network/consensus/election.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/asciichat_errno.h>
#include <string.h>
#include <stdint.h>

uint32_t consensus_election_compute_score(const participant_metrics_t *metrics) {
  if (!metrics || metrics->nat_tier > 4 || metrics->stun_probe_success_pct > 100)
    return 0;

  // NAT tier: 0=LAN (best), 4=TURN (worst)
  // (4 - tier) * 1000 makes LAN score 4000, TURN score 0
  uint32_t nat_score = (4 - metrics->nat_tier) * 1000;

  // Bandwidth: higher is better
  // Divide by 10 to keep in reasonable scale (10Mbps = +1000)
  uint32_t bw_score = metrics->upload_kbps / 10;

  // Latency: lower is better
  // (500ms - rtt_ns) means 0ns RTT = +500, 500ms = 0, high latency = negative (but capped at 0)
  // 500ms = 500,000,000ns
  uint32_t rtt_score = (metrics->rtt_ns < 500 * NS_PER_MS) ? (500 - (metrics->rtt_ns / NS_PER_MS)) : 0;

  // STUN probe success: 0-100%
  uint32_t probe_score = metrics->stun_probe_success_pct;

  // Total score
  uint32_t total = nat_score + bw_score + rtt_score + probe_score;
  return total;
}

// Helper: find index of best and second-best
static bool ranks_before(const participant_metrics_t *metrics, const uint32_t *scores, int a, int b) {
  if (scores[a] != scores[b])
    return scores[a] > scores[b];
  if (metrics[a].nat_tier != metrics[b].nat_tier)
    return metrics[a].nat_tier < metrics[b].nat_tier;
  if (metrics[a].upload_kbps != metrics[b].upload_kbps)
    return metrics[a].upload_kbps > metrics[b].upload_kbps;
  return memcmp(metrics[a].participant_id, metrics[b].participant_id, 16) < 0;
}

static void find_best_two(const participant_metrics_t *metrics, const uint32_t *scores, int count, int *best,
                          int *second) {
  *best = 0;
  *second = 1;

  // Handle 1-participant case (shouldn't happen, but be safe)
  if (count < 2) {
    *second = -1;
    return;
  }

  // Swap if second is better than first
  if (ranks_before(metrics, scores, 1, 0)) {
    int tmp = *best;
    *best = *second;
    *second = tmp;
  }

  // Find best and second-best
  for (int i = 2; i < count; i++) {
    if (ranks_before(metrics, scores, i, *best)) {
      *second = *best;
      *best = i;
    } else if (ranks_before(metrics, scores, i, *second)) {
      *second = i;
    }
  }
}

asciichat_error_t consensus_election_choose_hosts(const participant_metrics_t *metrics, int num_metrics,
                                                  int *out_best_index, int *out_backup_index) {
  if (!metrics || !out_best_index || !out_backup_index || num_metrics < 1) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid election parameters");
  }

  for (int i = 0; i < num_metrics; ++i) {
    if (metrics[i].nat_tier > 4 || metrics[i].stun_probe_success_pct > 100)
      return SET_ERRNO(ERROR_INVALID_PARAM, "Malformed participant metrics");
    for (int j = 0; j < i; ++j)
      if (!memcmp(metrics[i].participant_id, metrics[j].participant_id, 16))
        return SET_ERRNO(ERROR_INVALID_PARAM, "Duplicate election participant");
  }

  // Compute scores for all participants
  uint32_t *scores = SAFE_MALLOC(sizeof(uint32_t) * num_metrics, uint32_t *);

  for (int i = 0; i < num_metrics; i++) {
    scores[i] = consensus_election_compute_score(&metrics[i]);
  }

  // Find best and second-best
  find_best_two(metrics, scores, num_metrics, out_best_index, out_backup_index);

  SAFE_FREE(scores);
  return ASCIICHAT_OK;
}

asciichat_error_t consensus_election_verify(const participant_metrics_t *metrics, int num_metrics,
                                            const uint8_t announced_host_id[16], const uint8_t announced_backup_id[16],
                                            bool *out_valid) {
  if (!metrics || !announced_host_id || !announced_backup_id || !out_valid) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid verification parameters");
  }

  // Find which indices the announced IDs correspond to
  int host_idx = -1, backup_idx = -1;

  for (int i = 0; i < num_metrics; i++) {
    if (memcmp(metrics[i].participant_id, announced_host_id, 16) == 0) {
      host_idx = i;
    }
    if (memcmp(metrics[i].participant_id, announced_backup_id, 16) == 0) {
      backup_idx = i;
    }
  }

  // If we can't find announced IDs, result is invalid
  if (host_idx < 0) {
    *out_valid = false;
    return ASCIICHAT_OK;
  }

  // Run our own election
  int computed_host, computed_backup;
  asciichat_error_t err = consensus_election_choose_hosts(metrics, num_metrics, &computed_host, &computed_backup);

  if (err != ASCIICHAT_OK) {
    return err;
  }

  // Check if computed result matches announced
  uint8_t no_backup[16] = {0};
  *out_valid =
      host_idx == computed_host && (computed_backup < 0 ? memcmp(announced_backup_id, no_backup, sizeof(no_backup)) == 0
                                                        : backup_idx == computed_backup);
  return ASCIICHAT_OK;
}
