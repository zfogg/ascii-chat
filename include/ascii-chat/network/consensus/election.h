/**
 * @file network/consensus/election.h
 * @brief Deterministic host election algorithm
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "packets.h"
#include <ascii-chat/common.h>

/**
 * @brief Compute deterministic score for a participant
 *
 * Higher score = better host candidate
 *
 * score = (4 - nat_tier) * 1000     // NAT tier: 0=best, 4=worst
 *       + (upload_kbps / 10)         // Bandwidth bonus
 *       + (500 - rtt_ms)             // Latency bonus
 *       + stun_probe_success_pct     // Network stability
 */
uint32_t consensus_election_compute_score(const participant_metrics_t *metrics);

/**
 * @brief Elect best and backup host from metrics
 *
 * Returns indices of best and second-best participants.
 * Deterministic tie-breaking ensures identical result on all clients.
 */
asciichat_error_t consensus_election_choose_hosts(const participant_metrics_t *metrics, int num_metrics,
                                                  int *out_best_index,  // Index of best host
                                                  int *out_backup_index // Index of backup host
);

/**
 * @brief Verify election result matches expected scores
 *
 * Independent verification that leader's decision is correct.
 * Used by all clients to validate election before accepting.
 */
asciichat_error_t consensus_election_verify(const participant_metrics_t *metrics, int num_metrics,
                                            const uint8_t announced_host_id[16], const uint8_t announced_backup_id[16],
                                            bool *out_valid);
