#include "datagram.h"

#include <ascii-chat/buffer.h>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/util/endian.h>
#include <string.h>

#define MAX_WIRE_PACKET (sizeof(packet_header_t) + CRYPTO_MAX_PLAINTEXT_SIZE + CRYPTO_NONCE_SIZE + CRYPTO_MAC_SIZE)
#define FLOW_BUDGET (ACIP_QUIC_DATAGRAM_BUDGET / 2)
#define FLOW_SLOTS (ACIP_QUIC_DATAGRAM_SLOTS / 2)

static bool media_flow(acip_traffic_class_t flow) {
  return flow == ACIP_TRAFFIC_AUDIO || flow == ACIP_TRAFFIC_VISUAL;
}

static void put16(uint8_t *p, uint16_t value) {
  value = HOST_TO_NET_U16(value);
  memcpy(p, &value, sizeof(value));
}
static void put32(uint8_t *p, uint32_t value) {
  value = HOST_TO_NET_U32(value);
  memcpy(p, &value, sizeof(value));
}
static void put64(uint8_t *p, uint64_t value) {
  value = HOST_TO_NET_U64(value);
  memcpy(p, &value, sizeof(value));
}
static uint16_t get16(const uint8_t *p) {
  uint16_t value;
  memcpy(&value, p, sizeof(value));
  return NET_TO_HOST_U16(value);
}
static uint32_t get32(const uint8_t *p) {
  uint32_t value;
  memcpy(&value, p, sizeof(value));
  return NET_TO_HOST_U32(value);
}
static uint64_t get64(const uint8_t *p) {
  uint64_t value;
  memcpy(&value, p, sizeof(value));
  return NET_TO_HOST_U64(value);
}

asciichat_error_t quic_datagram_fragment(acip_traffic_class_t flow, uint64_t id, const void *packet, size_t length,
                                              uint16_t unit, uint16_t index, void *out, size_t capacity,
                                              size_t *out_length) {
  if (!out_length)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing QUIC datagram output length");
  *out_length = 0;
  if (!media_flow(flow) || !id || !packet || !length || length > MAX_WIRE_PACKET || !unit || !out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC datagram fragment parameters");
  size_t count = (length + unit - 1) / unit;
  if (count > ACIP_QUIC_DATAGRAM_MAX_FRAGMENTS || index >= count)
    return SET_ERRNO(ERROR_NETWORK_SIZE, "Invalid QUIC datagram fragment count or index");
  size_t offset = (size_t)index * unit;
  size_t part = length - offset < unit ? length - offset : unit;
  if (capacity < ACIP_QUIC_DATAGRAM_HEADER_SIZE + part)
    return SET_ERRNO(ERROR_BUFFER_FULL, "QUIC datagram output buffer is too small");
  uint8_t *wire = out;
  memcpy(wire, "ACQD", sizeof("ACQD") - 1);
  wire[4] = 1;
  wire[5] = (uint8_t)flow;
  wire[6] = wire[7] = 0;
  put64(wire + 8, id);
  put32(wire + 16, (uint32_t)length);
  put16(wire + 20, unit);
  put16(wire + 22, index);
  put16(wire + 24, (uint16_t)count);
  wire[26] = wire[27] = 0;
  memcpy(wire + ACIP_QUIC_DATAGRAM_HEADER_SIZE, (const uint8_t *)packet + offset, part);
  *out_length = ACIP_QUIC_DATAGRAM_HEADER_SIZE + part;
  return ASCIICHAT_OK;
}

static void clear_slot(acip_quic_datagram_reader_t *reader, acip_quic_datagram_slot_t *slot) {
  if (!slot->data)
    return;
  reader->bytes -= slot->cost;
  buffer_release(slot->data);
  buffer_release(slot->received);
  memset(slot, 0, sizeof(*slot));
}

void quic_datagram_expire(acip_quic_datagram_reader_t *reader, uint64_t now_ns) {
  if (!reader || now_ns < reader->last_time_ns)
    return;
  reader->last_time_ns = now_ns;
  for (size_t i = 0; i < ACIP_QUIC_DATAGRAM_SLOTS; ++i) {
    acip_quic_datagram_slot_t *slot = &reader->slots[i];
    if (slot->data && now_ns - slot->started_ns >= ACIP_QUIC_DATAGRAM_EXPIRY_NS)
      clear_slot(reader, slot);
  }
}

void quic_datagram_reset(acip_quic_datagram_reader_t *reader) {
  if (!reader)
    return;
  for (size_t i = 0; i < ACIP_QUIC_DATAGRAM_SLOTS; ++i)
    clear_slot(reader, &reader->slots[i]);
  memset(reader, 0, sizeof(*reader));
}

asciichat_error_t quic_datagram_read(acip_quic_datagram_reader_t *reader, const void *data, size_t length,
                                          uint64_t now_ns, acip_traffic_class_t *flow_out, uint8_t **out,
                                          size_t *out_length) {
  if (!out || !out_length || !flow_out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing QUIC datagram output");
  *out = NULL;
  *out_length = 0;
  if (!reader || !data || length < ACIP_QUIC_DATAGRAM_HEADER_SIZE || now_ns < reader->last_time_ns)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC datagram input");
  const uint8_t *wire = data;
  acip_traffic_class_t flow = (acip_traffic_class_t)wire[5];
  uint64_t id = get64(wire + 8);
  uint32_t total = get32(wire + 16);
  uint16_t unit = get16(wire + 20), index = get16(wire + 22), count = get16(wire + 24);
  if (memcmp(wire, "ACQD", sizeof("ACQD") - 1) || wire[4] != 1 || !media_flow(flow) || wire[6] || wire[7] || wire[26] ||
      wire[27] || !id || !total || total > MAX_WIRE_PACKET || !unit || !count ||
      count > ACIP_QUIC_DATAGRAM_MAX_FRAGMENTS || count != ((size_t)total + unit - 1) / unit || index >= count)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid QUIC datagram envelope");
  size_t offset = (size_t)index * unit;
  size_t part = total - offset < unit ? total - offset : unit;
  if (length != ACIP_QUIC_DATAGRAM_HEADER_SIZE + part)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid QUIC datagram fragment size");
  quic_datagram_expire(reader, now_ns);
  size_t first = flow == ACIP_TRAFFIC_AUDIO ? 0 : FLOW_SLOTS;
  acip_quic_datagram_slot_t *slot = NULL, *empty = NULL;
  size_t flow_bytes = 0;
  for (size_t i = first; i < first + FLOW_SLOTS; ++i) {
    acip_quic_datagram_slot_t *candidate = &reader->slots[i];
    flow_bytes += candidate->cost;
    if (candidate->data && candidate->id == id)
      slot = candidate;
    else if (!candidate->data && !empty)
      empty = candidate;
  }
  if (slot && (slot->total != total || slot->unit != unit || slot->count != count))
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Conflicting QUIC datagram fragments");
  if (!slot) {
    if (!empty || total > FLOW_BUDGET - flow_bytes)
      return SET_ERRNO(ERROR_BUFFER_FULL, "QUIC datagram reassembly budget exhausted");
    size_t bitmap_size = (count + 7U) / 8U;
    uint8_t *packet = buffer_alloc(NULL, total);
    uint8_t *bitmap = buffer_alloc(NULL, bitmap_size);
    if (!packet || !bitmap) {
      buffer_release(packet);
      buffer_release(bitmap);
      return SET_ERRNO(ERROR_MEMORY, "Cannot allocate QUIC datagram reassembly");
    }
    size_t cost = buffer_capacity(packet) + buffer_capacity(bitmap);
    if (cost > FLOW_BUDGET - flow_bytes) {
      buffer_release(packet);
      buffer_release(bitmap);
      return SET_ERRNO(ERROR_BUFFER_FULL, "QUIC datagram reassembly budget exhausted");
    }
    memset(bitmap, 0, bitmap_size);
    *empty = (acip_quic_datagram_slot_t){.data = packet,
                                         .received = bitmap,
                                         .cost = cost,
                                         .id = id,
                                         .started_ns = now_ns,
                                         .total = total,
                                         .unit = unit,
                                         .count = count,
                                         .traffic_class = flow};
    reader->bytes += cost;
    slot = empty;
  }
  uint8_t mask = (uint8_t)(1U << (index % 8));
  if (slot->received[index / 8] & mask) {
    if (memcmp(slot->data + offset, wire + ACIP_QUIC_DATAGRAM_HEADER_SIZE, part))
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Conflicting duplicate QUIC datagram fragment");
    return ASCIICHAT_OK;
  }
  memcpy(slot->data + offset, wire + ACIP_QUIC_DATAGRAM_HEADER_SIZE, part);
  slot->received[index / 8] |= mask;
  if (++slot->received_count == slot->count) {
    *out = slot->data;
    *out_length = slot->total;
    *flow_out = flow;
    slot->data = NULL;
    reader->bytes -= slot->cost;
    buffer_release(slot->received);
    memset(slot, 0, sizeof(*slot));
  }
  return ASCIICHAT_OK;
}
