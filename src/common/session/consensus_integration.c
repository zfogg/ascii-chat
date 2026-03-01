/**
 * @file session/consensus_integration.c
 * @brief Integration helpers for session consensus in discovery mode
 * @ingroup session
 *
 * This file provides example integration code showing how modes (discovery, server, client, acds)
 * can integrate the ring consensus abstraction.
 *
 * Design Example for Discovery Mode:
 * 1. Create consensus when entering ACTIVE state
 * 2. Call consensus_process() in discovery_session_process() loop
 * 3. Hook up packet routing for RING_* consensus packets
 * 4. Update host info when election completes
 */

#include "session/consensus.h"
#include <ascii-chat/network/consensus/metrics.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/network/packet/packet.h>
#include <string.h>

/**
 * @brief Example callback: Send consensus packet via ACDS connection
 *
 * In discovery mode, packets would be relayed through ACDS.
 * In server mode, packets would go directly to clients.
 * In client/acds modes, packets would use appropriate transports.
 */
static asciichat_error_t consensus_send_via_discovery(void *context, const uint8_t next_participant_id[16],
                                                      const uint8_t *packet, size_t packet_size) {
  // context would be a discovery_session_t in real usage
  // This is a placeholder showing the pattern

  if (!context || !next_participant_id || !packet) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  // In real code:
  // 1. Serialize the packet with proper headers
  // 2. Send to ACDS with next_participant_id as recipient
  // 3. ACDS will relay to that participant

  log_debug("Consensus would send %zu bytes to participant %.*s via ACDS", packet_size, 16, next_participant_id);

  return ASCIICHAT_OK;
}

/**
 * @brief Example callback: Handle election result from consensus
 *
 * Called when leader elects a new host.
 * Mode updates its state to connect to/become the elected host.
 */
static asciichat_error_t consensus_on_election_result(void *context, const uint8_t host_id[16],
                                                      const char host_address[64], uint16_t host_port,
                                                      const uint8_t backup_id[16], const char backup_address[64],
                                                      uint16_t backup_port) {
  (void)backup_address; // unused in placeholder
  (void)backup_port;    // unused in placeholder

  // context would be a discovery_session_t in real usage
  if (!context || !host_id || !backup_id) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  // In real code:
  // 1. Store host_id, backup_id, addresses, ports
  // 2. If elected as host: Start hosting
  // 3. If not elected: Connect to host or schedule reconnection

  log_info("Consensus election result: host=%.*s backup=%.*s (addr=%s:%u)", 16, host_id, 16, backup_id, host_address,
           host_port);

  return ASCIICHAT_OK;
}

/**
 * @brief Example callback: Measure this participant's network metrics
 *
 * Collects NAT quality, bandwidth, RTT, etc.
 * These metrics are used by the ring consensus algorithm to select the best host.
 */
static asciichat_error_t consensus_get_metrics(void *context, const uint8_t my_id[16],
                                               participant_metrics_t *out_metrics) {
  // context would be a discovery_session_t in real usage
  if (!context || !my_id || !out_metrics) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  // In real code:
  // 1. Measure NAT tier via UPnP probe
  // 2. Estimate upload bandwidth
  // 3. Measure RTT to current host (or ACDS)
  // 4. Send STUN probes and count success rate
  // 5. Get public address from detected NAT result

  // Placeholder with dummy metrics
  memcpy(out_metrics->participant_id, my_id, 16);
  out_metrics->nat_tier = 1;               // Public NAT
  out_metrics->upload_kbps = htonl(50000); // 50 Mbps
  out_metrics->rtt_ns = htonl(25000000);   // 25ms
  out_metrics->stun_probe_success_pct = 95;
  SAFE_STRNCPY(out_metrics->public_address, "203.0.113.42", sizeof(out_metrics->public_address));
  out_metrics->public_port = htons(54321);
  out_metrics->connection_type = 0; // Direct connection
  out_metrics->measurement_time_ns = htonll(time_get_ns());
  out_metrics->measurement_window_ns = htonll(5000000000ULL); // 5 second measurement window

  log_debug("Consensus measured metrics: NAT tier=%d, upload=%u Kbps, RTT=%u ns", out_metrics->nat_tier,
            ntohl(out_metrics->upload_kbps), ntohl(out_metrics->rtt_ns));

  return ASCIICHAT_OK;
}

/**
 * @brief Get callbacks configured for discovery mode integration
 *
 * Returns a properly configured callback structure for session consensus.
 * The returned callbacks encapsulate the discovery session's packet sending,
 * election handling, and metrics measurement capabilities.
 *
 * @param context Discovery session pointer (void* for genericity)
 * @param out_callbacks Output: Configured callbacks
 * @return ASCIICHAT_OK on success
 */
asciichat_error_t consensus_get_discovery_callbacks(void *context, session_consensus_callbacks_t *out_callbacks) {
  if (!context || !out_callbacks) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: context=%p, out_callbacks=%p", context, out_callbacks);
  }

  // Configure callbacks for discovery mode
  out_callbacks->send_packet = consensus_send_via_discovery;
  out_callbacks->on_election = consensus_on_election_result;
  out_callbacks->get_metrics = consensus_get_metrics;
  out_callbacks->election = NULL; // Use default election algorithm
  out_callbacks->context = context;

  return ASCIICHAT_OK;
}

/**
 * @brief Example: Create session consensus for discovery mode
 *
 * Shows how a mode would create the consensus abstraction.
 * This is a standalone example - real discovery session would call this
 * when transitioning to ACTIVE state if participants found.
 *
 * @param session_id Session identifier (16 bytes)
 * @param my_id My participant identifier (16 bytes)
 * @param participant_ids Array of all participant IDs (up to 64)
 * @param num_participants Count of participants
 * @param discovery_context Mode-specific context (e.g., discovery_session_t)
 * @param out_consensus Output: Created consensus handle
 * @return ASCIICHAT_OK on success
 */
asciichat_error_t consensus_create_for_discovery(const uint8_t session_id[16], const uint8_t my_id[16],
                                                 const uint8_t participant_ids[64][16], int num_participants,
                                                 void *discovery_context, session_consensus_t **out_consensus) {
  (void)session_id; // Informational only, not needed by consensus itself

  if (!my_id || !participant_ids || !discovery_context || !out_consensus) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  if (num_participants < 1 || num_participants > 64) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid participant count: %d", num_participants);
  }

  // Determine if we're the ring leader (for now, assume not - ACDS would manage this)
  // In a real implementation, ACDS would tell us our position in the ring
  bool is_leader = false; // This would be determined by ACDS

  // Get callbacks configured for our mode
  session_consensus_callbacks_t callbacks = {0};
  asciichat_error_t err = consensus_get_discovery_callbacks(discovery_context, &callbacks);
  if (err != ASCIICHAT_OK) {
    return err;
  }

  // Create consensus with the callbacks
  return session_consensus_create(my_id, is_leader, participant_ids, num_participants, &callbacks, out_consensus);
}

/**
 * @brief Calculate how long to wait until next consensus processing
 *
 * Useful for modes to schedule when they should call consensus_process() next.
 *
 * @param consensus Consensus handle
 * @param current_timeout_ms Current event loop timeout in milliseconds
 * @return Suggested next timeout in milliseconds
 */
uint32_t consensus_suggest_timeout_ms(session_consensus_t *consensus, uint32_t current_timeout_ms) {
  if (!consensus) {
    return current_timeout_ms;
  }

  // Get time until next round in nanoseconds
  uint64_t time_to_next_ns = session_consensus_time_until_next_round(consensus);

  // Convert to milliseconds
  uint64_t time_to_next_ms = time_ns_to_ms(time_to_next_ns);

  // Return minimum of consensus deadline or current timeout
  if (time_to_next_ms == 0) {
    return 0; // Consensus needs immediate attention
  }

  if (time_to_next_ms < current_timeout_ms) {
    return (uint32_t)time_to_next_ms;
  }

  return current_timeout_ms;
}
