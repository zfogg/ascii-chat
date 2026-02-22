/**
 * @file session/consensus_integration.h
 * @brief Integration helpers for session consensus across modes
 * @ingroup session
 *
 * Provides example integration patterns and helpers for modes to use
 * the ring consensus abstraction. Shows how different modes (discovery,
 * server, client, acds) would integrate consensus with their specific
 * transport and metrics collection mechanisms.
 */

#pragma once

#include "session/consensus.h"
#include <ascii-chat/asciichat_errno.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get consensus callbacks configured for discovery mode
 *
 * Returns a callback structure showing how discovery mode would
 * integrate consensus:
 * - Send packets via ACDS relay
 * - Handle election by storing host info
 * - Measure NAT quality metrics
 *
 * @param context Opaque context (typically discovery_session_t*)
 * @param out_callbacks Output: Configured callback structure
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t consensus_get_discovery_callbacks(void *context, session_consensus_callbacks_t *out_callbacks);

/**
 * @brief Create session consensus for discovery mode
 *
 * Convenience function showing the typical pattern for creating consensus
 * in discovery mode. Calls consensus_create() with discovery-specific
 * callback configuration.
 *
 * Real usage:
 * - When ACDS gives us participant list -> create consensus
 * - Call consensus_process() in discovery_session_process() loop
 * - Route RING_* packets to consensus handlers
 * - Update host on election results
 *
 * @param session_id Session identifier (16 bytes) - informational
 * @param my_id Our participant ID (16 bytes)
 * @param participant_ids Array of all participant IDs (up to 64)
 * @param num_participants Count of participants
 * @param discovery_context Discovery session context (opaque)
 * @param out_consensus Output: Created consensus handle
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t consensus_create_for_discovery(const uint8_t session_id[16], const uint8_t my_id[16],
                                                 const uint8_t participant_ids[64][16], int num_participants,
                                                 void *discovery_context, session_consensus_t **out_consensus);

/**
 * @brief Suggest next timeout for consensus processing
 *
 * Helper for modes to schedule consensus processing appropriately.
 * Returns the minimum of consensus deadline or current timeout.
 *
 * Usage in mode's main loop:
 * ```c
 * uint32_t suggested_timeout = consensus_suggest_timeout_ms(consensus, current_timeout);
 * // Use suggested_timeout in event wait instead of current_timeout
 * ```
 *
 * @param consensus Consensus handle (NULL safe)
 * @param current_timeout_ms Current event loop timeout in milliseconds
 * @return Suggested next timeout in milliseconds
 */
uint32_t consensus_suggest_timeout_ms(session_consensus_t *consensus, uint32_t current_timeout_ms);

#ifdef __cplusplus
}
#endif
