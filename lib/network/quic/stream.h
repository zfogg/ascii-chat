#pragma once

#include <ascii-chat/network/acip/traffic.h>
#include <ascii-chat/network/packet/decoder.h>

/* A stream starts with ACQS, wire version 1, a traffic class, and two zero
 * reserved bytes. It then carries complete ACIP packets in their existing
 * header+payload framing. The connection owns one stream per class and
 * direction; accepting duplicate classes is the endpoint's responsibility.
 * Plaintext types must match the stream class. Encrypted packet types must be
 * checked against that class after decryption by the application transport.
 */
#define ACIP_QUIC_STREAM_PREAMBLE_SIZE 8

typedef asciichat_error_t (*acip_quic_stream_emit_fn)(acip_traffic_class_t traffic_class, const void *packet,
                                                      size_t size, void *context);

typedef struct {
  uint8_t preamble[ACIP_QUIC_STREAM_PREAMBLE_SIZE];
  size_t preamble_used;
  acip_traffic_class_t traffic_class;
  packet_decoder_t packets;
  bool failed;
  bool finished;
} acip_quic_stream_reader_t;

asciichat_error_t quic_stream_preamble(acip_traffic_class_t traffic_class,
                                       uint8_t output[ACIP_QUIC_STREAM_PREAMBLE_SIZE]);
/** Feed on the connection's serialized receive lane. The callback borrows each
 * complete packet until it returns. Errors are terminal until reset. */
asciichat_error_t quic_stream_read(acip_quic_stream_reader_t *reader, const void *bytes, size_t length,
                                   acip_quic_stream_emit_fn emit, void *context);
/** Accept a clean FIN only at a packet boundary after a complete preamble. */
asciichat_error_t quic_stream_finish(acip_quic_stream_reader_t *reader);
void quic_stream_reset(acip_quic_stream_reader_t *reader);
