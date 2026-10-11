#include <ascii-chat/network/packet/decoder.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/common.h>
#include <string.h>

void packet_decoder_reset(packet_decoder_t *decoder) {
  if (decoder) {
    SAFE_FREE(decoder->packet);
    memset(decoder, 0, sizeof(*decoder));
  }
}

bool packet_decoder_pending(const packet_decoder_t *decoder) {
  return decoder && decoder->header_used != 0;
}

asciichat_error_t packet_decoder_feed(packet_decoder_t *decoder, const void *data, size_t length,
                                      packet_decoder_fn emit, void *arg) {
  if (!decoder || (!data && length) || !emit || decoder->failed)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid packet decoder input");
  const uint8_t *input = data;
  while (length) {
    if (decoder->header_used < sizeof(packet_header_t)) {
      size_t take = MIN(length, sizeof(packet_header_t) - decoder->header_used);
      memcpy(decoder->header + decoder->header_used, input, take);
      decoder->header_used += take;
      input += take;
      length -= take;
      if (decoder->header_used != sizeof(packet_header_t))
        break;
      packet_header_t header;
      memcpy(&header, decoder->header, sizeof(header));
      size_t payload = NET_TO_HOST_U32(header.length);
      size_t limit = decoder->max_payload ? MIN(decoder->max_payload, MAX_PACKET_SIZE) : MAX_PACKET_SIZE;
      if (NET_TO_HOST_U64(header.magic) != PACKET_MAGIC || payload > limit) {
        decoder->failed = true;
        return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid incremental packet header");
      }
      decoder->size = sizeof(header) + payload;
      decoder->packet = SAFE_MALLOC(decoder->size, uint8_t *);
      memcpy(decoder->packet, &header, sizeof(header));
      decoder->used = sizeof(header);
    }
    size_t take = MIN(length, decoder->size - decoder->used);
    if (take)
      memcpy(decoder->packet + decoder->used, input, take);
    decoder->used += take;
    input += take;
    length -= take;
    if (decoder->used != decoder->size)
      break;
    packet_header_t header;
    memcpy(&header, decoder->packet, sizeof(header));
    size_t payload = decoder->size - sizeof(header);
    if (payload && asciichat_crc32(decoder->packet + sizeof(header), payload) != NET_TO_HOST_U32(header.crc32)) {
      decoder->failed = true;
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Incremental packet CRC mismatch");
    }
    asciichat_error_t result = emit(decoder->packet, decoder->size, arg);
    SAFE_FREE(decoder->packet);
    decoder->packet = NULL;
    decoder->header_used = decoder->used = decoder->size = 0;
    if (result != ASCIICHAT_OK) {
      decoder->failed = true;
      return result;
    }
  }
  return ASCIICHAT_OK;
}
