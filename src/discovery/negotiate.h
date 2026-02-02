/**
 * @file discovery/negotiate.h
 * @brief Host negotiation logic for discovery mode
 * @ingroup discovery
 *
 * Implements the host negotiation protocol where participants exchange
 * NAT quality information and determine who should become the host.
 */

#pragma once

#include <ascii-chat/asciichat_errno.h>
#include "nat.h"
#include <ascii-chat/platform/socket.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Negotiation state
 */
typedef enum {
  NEGOTIATE_STATE_INIT,          ///< Initial state
  NEGOTIATE_STATE_DETECTING_NAT, ///< Running NAT detection
  NEGOTIATE_STATE_WAITING_PEER,  ///< Waiting for peer's NAT quality
  NEGOTIATE_STATE_COMPARING,     ///< Comparing qualities
  NEGOTIATE_STATE_WE_HOST,       ///< We won, becoming host
  NEGOTIATE_STATE_THEY_HOST,     ///< They won, connecting as client
  NEGOTIATE_STATE_FAILED,        ///< Negotiation failed
  NEGOTIATE_STATE_COMPLETE       ///< Negotiation complete
} negotiate_state_t;

/**
 * @brief Host negotiation context
 */
typedef struct {
  // Session info
  uint8_t session_id[16];
  uint8_t participant_id[16];
  bool is_initiator; ///< Did we create this session?

  // NAT qualities
  nat_quality_t our_quality;  ///< Our NAT quality
  nat_quality_t peer_quality; ///< Peer's NAT quality (when received)
  bool peer_quality_received; ///< Have we received peer's quality?

  // State
  negotiate_state_t state;
  asciichat_error_t error;

  // Result
  bool we_are_host;        ///< True if we should become host
  char host_address[64];   ///< Host's address (ours if we_are_host)
  uint16_t host_port;      ///< Host's port
  uint8_t connection_type; ///< acip_connection_type_t
} negotiate_ctx_t;

/**
 * @brief Initialize negotiation context
 * @param ctx Context to initialize
 * @param session_id Session UUID
 * @param participant_id Our participant UUID
 * @param is_initiator True if we created the session
 */
void negotiate_init(negotiate_ctx_t *ctx, const uint8_t session_id[16], const uint8_t participant_id[16],
                    bool is_initiator);

/**
 * @brief Start NAT detection phase
 * @param ctx Negotiation context
 * @param stun_server STUN server URL
 * @param local_port Local port for detection
 * @return ASCIICHAT_OK on success
 */
asciichat_error_t negotiate_start_detection(negotiate_ctx_t *ctx, const char *stun_server, uint16_t local_port);

/**
 * @brief Process received peer NAT quality
 * @param ctx Negotiation context
 * @param peer_quality Peer's NAT quality from network
 * @return ASCIICHAT_OK on success
 */
asciichat_error_t negotiate_receive_peer_quality(negotiate_ctx_t *ctx, const acip_nat_quality_t *peer_quality);

/**
 * @brief Determine negotiation result
 *
 * Called after both parties have exchanged NAT quality.
 * Sets we_are_host and related fields.
 *
 * @param ctx Negotiation context
 * @return ASCIICHAT_OK on success
 */
asciichat_error_t negotiate_determine_result(negotiate_ctx_t *ctx);

/**
 * @brief Get current negotiation state
 * @param ctx Negotiation context
 * @return Current state
 */
negotiate_state_t negotiate_get_state(const negotiate_ctx_t *ctx);

/**
 * @brief Check if negotiation is complete
 * @param ctx Negotiation context
 * @return True if negotiation finished (success or failure)
 */
bool negotiate_is_complete(const negotiate_ctx_t *ctx);

/**
 * @brief Get negotiation error (if failed)
 * @param ctx Negotiation context
 * @return Error code or ASCIICHAT_OK if no error
 */
asciichat_error_t negotiate_get_error(const negotiate_ctx_t *ctx);

/**
 * @brief Elect future host from multiple participants (NEW P2P design)
 *
 * Called by quorum leader after collecting NAT quality from all participants.
 * Determines who should become host if current host dies.
 * Uses deterministic algorithm so all participants independently reach same result.
 *
 * @param collected_quality Array of NAT quality from all participants
 * @param participant_ids Array of participant IDs (in same order as quality array)
 * @param num_participants Number of participants
 * @param out_future_host_id Output: Elected future host's participant ID
 * @return ASCIICHAT_OK on success
 *
 * @note All participants compute this independently on their own collected data
 *       and reach the same result (deterministic algorithm).
 * @note If only one participant remains, that one is elected.
 * @note If all have same quality, uses participant ID lexicographic order as tiebreaker.
 */
asciichat_error_t negotiate_elect_future_host(const acip_nat_quality_t collected_quality[],
                                              const uint8_t participant_ids[][16], size_t num_participants,
                                              uint8_t out_future_host_id[16]);

#ifdef __cplusplus
}
#endif
