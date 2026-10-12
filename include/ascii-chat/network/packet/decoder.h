#pragma once
#include <ascii-chat/network/packet/packet.h>

/** Incremental wire framing. Feed only on the owning connection's serial lane.
 * Callback borrows a complete header+payload until it returns. It must copy any
 * bytes it retains. An error is terminal: reset before feeding a new connection.
 */
typedef struct {
  uint8_t header[sizeof(packet_header_t)];
  size_t header_used;
  uint8_t *packet;
  size_t used, size;
  size_t max_payload; ///< Zero uses MAX_PACKET_SIZE; set before the first feed.
  bool failed;
} packet_decoder_t;
typedef asciichat_error_t (*packet_decoder_fn)(const void *, size_t, void *);
asciichat_error_t packet_decoder_feed(packet_decoder_t *, const void *, size_t, packet_decoder_fn, void *);
void packet_decoder_reset(packet_decoder_t *);
bool packet_decoder_pending(const packet_decoder_t *);
