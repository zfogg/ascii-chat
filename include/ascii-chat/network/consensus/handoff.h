#include <ascii-chat/crypto/sizes.h>
#include <ascii-chat/common.h>
#pragma once

#include <ascii-chat/network/consensus/election.h>

/* Versioned control frames. Integer fields are explicitly encoded, never cast
 * from transport buffers. All deadlines belong to the receiver's clock. */
#define CONSENSUS_HANDOFF_PACKET 6090
#define CONSENSUS_HANDOFF_VERSION 1
#define CONSENSUS_HANDOFF_HEADER 64
#define CONSENSUS_HANDOFF_PROBE_BYTES 65536
#define CONSENSUS_HANDOFF_FRESH_MS 15000
#define CONSENSUS_HANDOFF_PREPARE_MS 5000
#define CONSENSUS_HANDOFF_RECOVERY_MS 15000

typedef enum {
  HANDOFF_SAMPLE_START = 1,
  HANDOFF_SAMPLE_DATA,
  HANDOFF_PREPARE,
  HANDOFF_READY,
  HANDOFF_PLAN,
  HANDOFF_COMMIT,
  HANDOFF_REJECT,
  HANDOFF_FAILED,
  HANDOFF_SAMPLE_ACK
} consensus_handoff_kind_t;

typedef struct {
  uint8_t kind;
  uint8_t session[SESSION_ID_SIZE];
  uint8_t participant[SESSION_ID_SIZE];
  uint32_t generation;
  uint32_t sequence;
  uint32_t upload_kbps;
  uint32_t rtt_ms;
  uint8_t capabilities; /* 1 = native TCP, 2 = WebRTC host */
  uint8_t nat_tier;     /* 255 = unknown */
  uint16_t port;
/**
 */
} consensus_handoff_message_t;

/**
 * @brief consensus handoff message is intentionally a small, concrete record. Fields such as `kind`, `generation`, `sequence`, `upload_kbps` give callers the identity and current value they need. It is normally borrowed from, or owned by, the surrounding subsystem—not independently managed.
 */
typedef struct {
  uint8_t id[SESSION_ID_SIZE];
  uint8_t capabilities;
  uint8_t nat_tier;
  uint16_t port;
  uint8_t pending_capabilities;
  uint8_t pending_nat_tier;
  uint16_t pending_port;
  uint32_t sequence;
  uint32_t upload_kbps;
  uint64_t probe_started_ms;
  uint64_t measured_ms;
  bool measuring;
  bool valid;
  bool rejected;
/**
 */
} consensus_handoff_peer_t;

typedef struct {
  uint8_t session[SESSION_ID_SIZE];
  uint8_t host[SESSION_ID_SIZE];
  uint8_t successor[SESSION_ID_SIZE];
  uint32_t generation;
  uint32_t round;
  uint32_t host_sequence; ///< Last committed round; preparation does not change host authority.
  uint64_t prepare_deadline_ms;
  bool ready;
  bool host_lost;
  bool failure_announced;
  uint64_t failure_deadline_ms;
  uint64_t monitoring_started_ms;
  bool monitoring;
  consensus_handoff_peer_t peers[MAX_SESSION_PARTICIPANTS];
  size_t count;
/**
 * @brief For consensus handoff peer, this is the live record that ties `capabilities`, `nat_tier`, `port`, `pending_capabilities` together. The owning code creates it, updates it while work is active, and releases its handles and pointed-to storage before freeing the record.
 * @brief consensus handoff keeps the values that move through the subsystem: `generation`, `round`, `host_sequence`, `prepare_deadline_ms`. Some members are pointers into caller- or subsystem-owned memory, so lifetime follows the API that populated them rather than the struct copy itself.
 */
} consensus_handoff_t;

asciichat_error_t consensus_handoff_decode(const void *data, size_t length, consensus_handoff_message_t *out);
void consensus_handoff_encode(const consensus_handoff_message_t *message, uint8_t out[CONSENSUS_HANDOFF_HEADER]);
void consensus_handoff_init(consensus_handoff_t *state, const uint8_t session[SESSION_ID_SIZE],
                            const uint8_t host[SESSION_ID_SIZE]);
asciichat_error_t consensus_handoff_join(consensus_handoff_t *state, const uint8_t id[SESSION_ID_SIZE]);
void consensus_handoff_leave(consensus_handoff_t *state, const uint8_t id[SESSION_ID_SIZE]);
/* Caller authenticates participant/session against the transport identity. */
asciichat_error_t consensus_handoff_receive(consensus_handoff_t *state, const consensus_handoff_message_t *message,
                                            size_t length, uint64_t now_ms);
/* Returns one action, or false when the state is unchanged. */
bool consensus_handoff_tick(consensus_handoff_t *state, uint64_t now_ms, consensus_handoff_message_t *action);

/* Caller authenticates the reporter; obsolete host reports are ignored. */
void consensus_handoff_host_lost(consensus_handoff_t *state, const uint8_t host[SESSION_ID_SIZE]);
