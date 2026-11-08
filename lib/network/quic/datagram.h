#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/network/acip/traffic.h>
#include <stddef.h>
#include <stdint.h>

/* Private ACQD v1 fragments carry an encrypted ACIP packet. The envelope is
 * not authenticated end-to-end: do not dispatch a completed packet until
 * the ACIP security codec authenticates it and checks replay state. */
#define ACIP_QUIC_DATAGRAM_HEADER_SIZE 28U
#define ACIP_QUIC_DATAGRAM_MAX_FRAGMENTS 4096U
#define ACIP_QUIC_DATAGRAM_SLOTS 8U
#define ACIP_QUIC_DATAGRAM_BUDGET (4U * 1024U * 1024U)
#define ACIP_QUIC_DATAGRAM_EXPIRY_NS UINT64_C(1000000000)

typedef struct {
  uint8_t *data;
  uint8_t *received;
  size_t cost;
  uint64_t id, started_ns;
  uint32_t total;
  uint16_t unit, count, received_count;
  acip_traffic_class_t traffic_class;
} acip_quic_datagram_slot_t;

typedef struct {
  acip_quic_datagram_slot_t slots[ACIP_QUIC_DATAGRAM_SLOTS];
  size_t bytes;
  uint64_t last_time_ns;
} acip_quic_datagram_reader_t;

/* unit is the payload capacity of every non-final fragment. IDs must be
 * unique per flow and key epoch. The output buffer is owned by the caller. */
asciichat_error_t quic_datagram_fragment(acip_traffic_class_t traffic_class, uint64_t id, const void *packet,
                                         size_t length, uint16_t unit, uint16_t index, void *out, size_t capacity,
                                         size_t *out_length);

/* Returns a buffer-owned complete encrypted packet in *out, or NULL if still
 * incomplete. Identical duplicates are ignored; conflicting fragments fail.
 * now_ns is monotonic and expiry is measured from the first fragment. */
asciichat_error_t quic_datagram_read(acip_quic_datagram_reader_t *reader, const void *data, size_t length,
                                     uint64_t now_ns, acip_traffic_class_t *traffic_class, uint8_t **out,
                                     size_t *out_length);
void quic_datagram_expire(acip_quic_datagram_reader_t *reader, uint64_t now_ns);
void quic_datagram_reset(acip_quic_datagram_reader_t *reader);
