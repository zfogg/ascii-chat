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
  uint8_t session[16];
  uint8_t participant[16];
  uint32_t generation;
  uint32_t sequence;
  uint32_t upload_kbps;
  uint32_t rtt_ms;
  uint8_t capabilities; /* 1 = native TCP, 2 = WebRTC host */
  uint8_t nat_tier;     /* 255 = unknown */
  uint16_t port;
} consensus_handoff_message_t;

typedef struct {
  uint8_t id[16];
  uint8_t capabilities;
  uint8_t nat_tier;
  uint16_t port;
  uint32_t sequence;
  uint32_t upload_kbps;
  uint64_t probe_started_ms;
  uint64_t measured_ms;
  bool measuring;
  bool valid;
  bool rejected;
} consensus_handoff_peer_t;

typedef struct {
  uint8_t session[16];
  uint8_t host[16];
  uint8_t successor[16];
  uint32_t generation;
  uint32_t round;
  uint64_t prepare_deadline_ms;
  bool ready;
  bool host_lost;
  bool failure_announced;
  uint64_t failure_deadline_ms;
  consensus_handoff_peer_t peers[PARTICIPANT_LIMIT];
  size_t count;
} consensus_handoff_t;

asciichat_error_t consensus_handoff_decode(const void *data, size_t length, consensus_handoff_message_t *out);
void consensus_handoff_encode(const consensus_handoff_message_t *message, uint8_t out[CONSENSUS_HANDOFF_HEADER]);
void consensus_handoff_init(consensus_handoff_t *state, const uint8_t session[16], const uint8_t host[16]);
asciichat_error_t consensus_handoff_join(consensus_handoff_t *state, const uint8_t id[16]);
void consensus_handoff_leave(consensus_handoff_t *state, const uint8_t id[16]);
/* Caller authenticates participant/session against the transport identity. */
asciichat_error_t consensus_handoff_receive(consensus_handoff_t *state, const consensus_handoff_message_t *message,
                                            size_t length, uint64_t now_ms);
/* Returns one action, or false when the state is unchanged. */
bool consensus_handoff_tick(consensus_handoff_t *state, uint64_t now_ms, consensus_handoff_message_t *action);
