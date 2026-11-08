#include <emscripten/bind.h>
#include "client_bytes.h"
extern "C" {
#include "../client.h"
#include <ascii-chat/network/errors.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/endian.h>
}
#include <sodium.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

static std::optional<std::string> parse_packet(ClientBytes packet) {
  auto input = binding_copy_input(packet);
  if (input.size() < sizeof(packet_header_t)) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Client packet is shorter than its header");
    return std::nullopt;
  }
  packet_header_t header;
  if (packet_validate(input.data(), input.size(), 0, &header) != ASCIICHAT_OK)
    return std::nullopt;
  emscripten::val result = emscripten::val::object();
  result.set("type", NET_TO_HOST_U16(header.type));
  result.set("length", NET_TO_HOST_U32(header.length));
  result.set("client_id", NET_TO_HOST_U32(header.client_id));
  result.set("crc32", NET_TO_HOST_U32(header.crc32));
  return emscripten::val::global("JSON").call<std::string>("stringify", result);
}
static ClientPacketResult serialize_packet(unsigned type, ClientBytes payload, unsigned client_id) {
  if (type > UINT16_MAX) {
    const int code = SET_ERRNO(ERROR_INVALID_PARAM, "Invalid client packet type: %u", type);
    return packet_result(code, nullptr, 0);
  }
  auto input = binding_copy_input(payload);
  std::vector<uint8_t> output(input.size() + 256);
  size_t length = 0;
  const int code = packet_encode(static_cast<packet_type_t>(type), input.empty() ? nullptr : input.data(), input.size(),
                                 client_id, output.data(), output.size(), &length);
  if (code)
    SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Cannot serialize client packet");
  return packet_result(code, output.data(), code == 0 ? length : 0);
}
static int handle_handshake(int stage, ClientBytes packet) {
  auto bytes = binding_copy_input(packet);
  int result = -1;
  switch (stage) {
  case 0:
    result = client_handle_crypto_parameters(bytes.data(), bytes.size());
    break;
  case 1:
    result = client_handle_key_exchange_init(bytes.data(), bytes.size());
    break;
  case 2:
    result = client_handle_auth_challenge(bytes.data(), bytes.size());
    break;
  case 3:
    result = client_handle_handshake_complete(bytes.data(), bytes.size());
    break;
  default:
    result = SET_ERRNO(ERROR_INVALID_PARAM, "Invalid handshake stage: %d", stage);
    break;
  }
  if (!bytes.empty())
    sodium_memzero(bytes.data(), bytes.size());
  if (result && stage >= 0 && stage <= 3)
    SET_ERRNO(ERROR_CRYPTO_HANDSHAKE, "Client handshake stage %d failed", stage);
  return result;
}
static int receive_handshake(bool discovery, unsigned type, ClientBytes payload) {
  auto bytes = binding_copy_input(payload);
  uint8_t empty = 0;
  const void *data = bytes.empty() ? &empty : bytes.data();
  const int result = discovery ? client_acds_handshake_receive(type, data, bytes.size())
                               : client_handshake_receive(type, data, bytes.size());
  if (!bytes.empty())
    sodium_memzero(bytes.data(), bytes.size());
  return result;
}
static ClientPacketResult crypt_packet(bool encrypt, ClientBytes packet) {
  auto input = binding_copy_input(packet);
  std::vector<uint8_t> output(input.size() + (encrypt ? 72 : 0));
  size_t length = 0;
  const int code = encrypt ? client_encrypt_packet(input.data(), input.size(), output.data(), output.size(), &length)
                           : client_decrypt_packet(input.data(), input.size(), output.data(), output.size(), &length);
  ClientPacketResult result = packet_result(code, output.data(), code == 0 ? length : 0);
  if (!input.empty())
    sodium_memzero(input.data(), input.size());
  if (!output.empty())
    sodium_memzero(output.data(), output.size());
  return result;
}

EMSCRIPTEN_BINDINGS(ascii_chat_client_protocol) {
  emscripten::register_type<ClientBytes>("ClientBytes", "Uint8Array");
  emscripten::register_type<ClientPacketResult>("ClientPacketResult", "{ code: number; bytes: Uint8Array }");
  emscripten::function("handleHandshake", &handle_handshake);
  emscripten::function("receiveHandshake", &receive_handshake);
  emscripten::function("cryptPacket", &crypt_packet);

  emscripten::function("parsePacketJson", &parse_packet);
  emscripten::function("serializePacket", &serialize_packet);
}
