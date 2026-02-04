/**
 * @file network/consensus/packets.h
 * @brief Ring consensus protocol packet structures
 * @ingroup consensus
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef _WIN32
#pragma pack(push, 1)
#endif

// ============================================================================
// Network Quality Metrics Structure
// ============================================================================

/**
 * Metrics for a single participant
 */
typedef struct {
  uint8_t participant_id[16];     // UUID of participant
  uint8_t nat_tier;               // 0=LAN, 1=Public, 2=UPnP, 3=STUN, 4=TURN
  uint32_t upload_kbps;           // Upload bandwidth in Kbps (network byte order)
  uint32_t rtt_ns;                // RTT to current host in nanoseconds (network byte order)
  uint8_t stun_probe_success_pct; // 0-100: percentage of successful STUN probes
  char public_address[64];        // Detected public IP address
  uint16_t public_port;           // Detected public port (network byte order)
  uint8_t connection_type;        // Direct=0, UPnP=1, STUN=2, TURN=3
  uint64_t measurement_time_ns;   // Unix ns when measured (network byte order)
  uint64_t measurement_window_ns; // Duration of measurement in ns (network byte order)
} __attribute__((packed)) participant_metrics_t;

// ============================================================================
// Ring Topology Packet
// ============================================================================

/**
 * Server -> All: Announces ring topology and participant order
 * Sent whenever participants join/leave
 */
typedef struct {
  uint8_t session_id[16];          // Session identifier
  uint8_t participant_ids[64][16]; // Up to 64 participant UUIDs in ring order
  uint8_t num_participants;        // Count of active participants
  uint8_t ring_leader_index;       // Index in participant_ids of ring leader
  uint32_t generation;             // Incremented each time ring changes (network byte order)
} __attribute__((packed)) acip_ring_members_t;

// ============================================================================
// Metrics Collection Packets
// ============================================================================

/**
 * Ring leader -> Previous in ring: Initiate metrics collection round
 */
typedef struct {
  uint8_t session_id[16];          // Session identifier
  uint8_t initiator_id[16];        // Ring leader participant ID
  uint32_t round_id;               // Collection round counter (network byte order)
  uint64_t collection_deadline_ns; // Unix ns deadline for collection (network byte order)
} __attribute__((packed)) acip_stats_collection_start_t;

/**
 * Any -> Next in ring: Pass metrics around the ring
 * Variable-length: header + participant_metrics_t[num_metrics]
 */
typedef struct {
  uint8_t session_id[16]; // Session identifier
  uint8_t sender_id[16];  // Who is relaying this packet
  uint32_t round_id;      // Collection round number (network byte order)
  uint8_t num_metrics;    // Number of participant_metrics_t following this header
  // Followed by: participant_metrics_t metrics[num_metrics]
} __attribute__((packed)) acip_stats_update_t;

// ============================================================================
// Election Result Packet
// ============================================================================

/**
 * Ring leader -> Server -> All: Announce host election decision
 * Variable-length: header + participant_metrics_t[num_participants]
 */
typedef struct {
  uint8_t session_id[16]; // Session identifier
  uint8_t leader_id[16];  // Ring leader who made the decision
  uint32_t round_id;      // Collection round number (network byte order)

  // Elected host
  uint8_t host_id[16];   // Best participant becomes host
  char host_address[64]; // Address to connect to
  uint16_t host_port;    // Port to connect to (network byte order)

  // Backup host
  uint8_t backup_id[16];   // Second-best becomes backup
  char backup_address[64]; // Backup address
  uint16_t backup_port;    // Backup port (network byte order)

  uint64_t elected_at_ns;   // Unix ns when elected (network byte order)
  uint8_t num_participants; // Count of metrics following
  // Followed by: participant_metrics_t metrics[num_participants]
} __attribute__((packed)) acip_ring_election_result_t;

// ============================================================================
// Acknowledgment Packet
// ============================================================================

/**
 * Any participant -> Server: Confirm receipt of election result
 */
typedef struct {
  uint8_t session_id[16];       // Session identifier
  uint8_t participant_id[16];   // Who is acknowledging
  uint32_t round_id;            // Which round they're acknowledging (network byte order)
  uint8_t ack_status;           // ACCEPTED=0, REJECTED=1
  uint8_t stored_host_id[16];   // Who they're storing as host (for verification)
  uint8_t stored_backup_id[16]; // Who they're storing as backup
} __attribute__((packed)) acip_stats_ack_t;

// ============================================================================
// Ring State Structure (In-Memory)
// ============================================================================

/**
 * Per-participant ring consensus state
 * Maintained by each client in discovery mode
 */
typedef struct {
  // Ring position
  uint8_t my_id[16];
  uint8_t next_participant_id[16];
  uint8_t prev_participant_id[16];
  int ring_position;
  bool am_ring_leader;

  // Current host info
  uint8_t current_host_id[16];
  char current_host_address[64];
  uint16_t current_host_port;

  // Backup info
  uint8_t backup_host_id[16];
  char backup_host_address[64];
  uint16_t backup_host_port;

  // Last election
  uint32_t last_round_id;
  participant_metrics_t *all_metrics; // Dynamically allocated
  int num_metrics_in_last_round;
} consensus_ring_state_t;

#ifdef _WIN32
#pragma pack(pop)
#endif

/**
 * @brief Get human-readable name for consensus packet type
 */
const char *consensus_packet_type_name(uint16_t type);

/**
 * @brief Get minimum packet size for consensus packet type
 */
size_t consensus_get_min_packet_size(uint16_t type);
