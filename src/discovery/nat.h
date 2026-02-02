/**
 * @file discovery/nat.h
 * @brief NAT quality detection for discovery mode host selection
 * @ingroup discovery
 *
 * Detects NAT characteristics to determine the best host candidate.
 * Uses STUN, UPnP/NAT-PMP, and bandwidth measurements.
 */

#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/network/acip/acds.h>
#include <ascii-chat/platform/socket.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief NAT quality assessment result
 *
 * Contains all information needed to determine hosting suitability.
 */
typedef struct {
  // NAT detection results
  bool has_public_ip;        ///< STUN reflexive == local IP
  bool upnp_available;       ///< UPnP/NAT-PMP mapping succeeded
  uint16_t upnp_mapped_port; ///< Mapped external port (if upnp_available)
  acip_nat_type_t nat_type;  ///< NAT classification
  bool lan_reachable;        ///< Same subnet as peer
  uint32_t stun_latency_ms;  ///< RTT to STUN server
  char public_address[64];   ///< Public IP address
  uint16_t public_port;      ///< Public port

  // Bandwidth measurements
  uint32_t upload_kbps;    ///< Upload bandwidth in Kbps
  uint32_t download_kbps;  ///< Download bandwidth in Kbps
  uint16_t rtt_to_acds_ms; ///< Latency to ACDS
  uint8_t jitter_ms;       ///< Packet timing variance
  uint8_t packet_loss_pct; ///< Packet loss percentage

  // ICE candidate summary
  bool has_host_candidates;  ///< Local IP reachable
  bool has_srflx_candidates; ///< STUN worked
  bool has_relay_candidates; ///< TURN available

  // Detection status
  bool detection_complete; ///< All probes finished
  asciichat_error_t error; ///< Error if detection failed
} nat_quality_t;

/**
 * @brief Initialize NAT quality structure with defaults
 * @param quality Structure to initialize
 */
void nat_quality_init(nat_quality_t *quality);

/**
 * @brief Detect NAT quality using all available methods
 *
 * Runs STUN probe, UPnP check, and gathers ICE candidates in parallel.
 * Results are stored in the provided nat_quality_t structure.
 *
 * @param quality Output structure for results
 * @param stun_server STUN server URL (e.g., "stun:stun.l.google.com:19302")
 * @param local_port Local port to use for detection
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t nat_detect_quality(nat_quality_t *quality, const char *stun_server, uint16_t local_port);

/**
 * @brief Measure upload bandwidth to ACDS server
 *
 * Uploads a test payload and measures throughput.
 *
 * @param quality Structure to update with bandwidth results
 * @param acds_socket Connected socket to ACDS
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t nat_measure_bandwidth(nat_quality_t *quality, socket_t acds_socket);

/**
 * @brief Compare two NAT qualities and determine who should host
 *
 * Algorithm is deterministic - both sides get the same result.
 * Uses NAT tier priority with bandwidth as tiebreaker.
 *
 * @param ours Our NAT quality
 * @param theirs Peer's NAT quality
 * @param we_are_initiator True if we started the session
 * @return -1 = we host, 0 = equal (initiator wins), 1 = they host
 */
int nat_compare_quality(const nat_quality_t *ours, const nat_quality_t *theirs, bool we_are_initiator);

/**
 * @brief Convert nat_quality_t to acip_nat_quality_t for network transmission
 * @param quality Local NAT quality
 * @param session_id Session UUID
 * @param participant_id Participant UUID
 * @param out Output ACIP message structure
 */
void nat_quality_to_acip(const nat_quality_t *quality, const uint8_t session_id[16], const uint8_t participant_id[16],
                         acip_nat_quality_t *out);

/**
 * @brief Convert acip_nat_quality_t to nat_quality_t
 * @param acip ACIP message structure
 * @param out Output local NAT quality structure
 */
void nat_quality_from_acip(const acip_nat_quality_t *acip, nat_quality_t *out);

/**
 * @brief Get human-readable description of NAT type
 * @param type NAT type
 * @return String description
 */
const char *nat_type_to_string(acip_nat_type_t type);

/**
 * @brief Compute NAT tier for host selection (0=best, 4=worst)
 * @param quality NAT quality
 * @return Tier value
 */
int nat_compute_tier(const nat_quality_t *quality);

#ifdef __cplusplus
}
#endif
