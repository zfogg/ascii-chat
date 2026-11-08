#include <emscripten/bind.h>
extern "C" {
#include "../client.h"
#include "../common/options.h"
#include <session/av_state.h>
#include <ascii-chat/network/errors.h>
}
#include <sodium.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

EMSCRIPTEN_DECLARE_VAL_TYPE(ClientBytes);
EMSCRIPTEN_DECLARE_VAL_TYPE(ClientPacketResult);
EMSCRIPTEN_DECLARE_VAL_TYPE(ClientSettingsResult);
EMSCRIPTEN_DECLARE_VAL_TYPE(ClientFrameResult);

class AVStateBinding {
public:
  AVStateBinding() : state_(session_av_state_create()) {}
  ~AVStateBinding() {
    if (state_)
      session_av_state_destroy(state_);
  }
  AVStateBinding(const AVStateBinding &) = delete;
  AVStateBinding &operator=(const AVStateBinding &) = delete;
  bool valid() const {
    return state_ != nullptr;
  }
  unsigned generation() const {
    return session_av_state_generation(state_);
  }
  unsigned begin() {
    return session_av_state_begin(state_);
  }
  bool complete(unsigned generation) {
    return session_av_state_complete(state_, generation);
  }
  bool current(unsigned generation) {
    return session_av_state_is_current(state_, generation);
  }
  bool active() {
    return session_av_state_is_active(state_);
  }
  void beginStop() {
    session_av_state_begin_stop(state_);
  }
  void finishStop() {
    session_av_state_finish_stop(state_);
  }

private:
  session_av_state_t *state_;
};

static std::vector<uint8_t> client_copy_input(ClientBytes input) {
  const size_t length = input["length"].as<size_t>();
  std::vector<uint8_t> bytes(length);
  if (length)
    emscripten::val(emscripten::typed_memory_view(length, bytes.data())).call<void>("set", input);
  return bytes;
}

static emscripten::val client_copy_output(const uint8_t *data, size_t length) {
  emscripten::val output = emscripten::val::global("Uint8Array").new_(length);
  if (length)
    output.call<void>("set", emscripten::val(emscripten::typed_memory_view(length, data)));
  return output;
}

static ClientPacketResult packet_result(int code, const uint8_t *data, size_t length) {
  emscripten::val result = emscripten::val::object();
  result.set("code", code);
  result.set("bytes", client_copy_output(data, length));
  return ClientPacketResult(result);
}

static int set_server_address(const std::string &host, int port) {
  if (host.empty() || port <= 0 || port > 65535)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid client server address or port");
  const int result = client_set_server_address(host.c_str(), port);
  if (result)
    SET_ERRNO(ERROR_INVALID_STATE, "Cannot set client server address");
  return result;
}
static std::optional<std::string> client_public_key() {
  char *key = client_get_public_key_hex();
  if (!key) {
    SET_ERRNO(ERROR_CRYPTO_KEY, "Cannot get client public key");
    return std::nullopt;
  }
  std::string result(key);
  client_free_string(key);
  return result;
}
static int send_client_frame(ClientBytes rgba, int width, int height) {
  const size_t length = rgba["length"].as<size_t>();
  if (width <= 0 || height <= 0 || static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4 != length)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid client video frame: %dx%d, %zu bytes", width, height, length);
  auto bytes = client_copy_input(rgba);
  const int result = client_send_video_frame(bytes.data(), width, height);
  if (result)
    SET_ERRNO(ERROR_NETWORK, "Cannot send client video frame");
  return result;
}
static int handle_handshake(int stage, ClientBytes packet) {
  auto bytes = client_copy_input(packet);
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
  auto bytes = client_copy_input(payload);
  uint8_t empty = 0;
  const void *data = bytes.empty() ? &empty : bytes.data();
  const int result = discovery ? client_acds_handshake_receive(type, data, bytes.size())
                               : client_handshake_receive(type, data, bytes.size());
  if (!bytes.empty())
    sodium_memzero(bytes.data(), bytes.size());
  return result;
}
static ClientPacketResult crypt_packet(bool encrypt, ClientBytes packet) {
  auto input = client_copy_input(packet);
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
static std::optional<std::string> parse_packet(ClientBytes packet) {
  auto input = client_copy_input(packet);
  char *json = client_parse_packet(input.data(), input.size());
  if (!json) {
    SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Cannot parse client packet");
    return std::nullopt;
  }
  std::string result(json);
  client_free_string(json);
  return result;
}
static ClientPacketResult serialize_packet(unsigned type, ClientBytes payload, unsigned client_id) {
  if (type > UINT16_MAX) {
    const int code = SET_ERRNO(ERROR_INVALID_PARAM, "Invalid client packet type: %u", type);
    return packet_result(code, nullptr, 0);
  }
  auto input = client_copy_input(payload);
  std::vector<uint8_t> output(input.size() + 256);
  size_t length = 0;
  const int code = client_serialize_packet(static_cast<uint16_t>(type), input.empty() ? nullptr : input.data(),
                                           input.size(), client_id, output.data(), &length);
  if (code)
    SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Cannot serialize client packet");
  return packet_result(code, output.data(), code == 0 ? length : 0);
}
static ClientSettingsResult receive_settings(ClientBytes payload) {
  auto input = client_copy_input(payload);
  const int code = client_settings_receive(input.data(), input.size());
  char *json = code == 0 ? client_settings_values() : nullptr;
  emscripten::val result = emscripten::val::object();
  result.set("code", code);
  result.set("json", json ? emscripten::val(std::string(json)) : emscripten::val::null());
  if (json)
    browser_options_release(json);
  return ClientSettingsResult(result);
}
static std::optional<std::string> client_settings() {
  char *json = client_settings_values();
  if (!json)
    return std::nullopt;
  std::string result(json);
  browser_options_release(json);
  return result;
}
static ClientFrameResult validate_frame(ClientBytes encoded, unsigned width, unsigned height, unsigned checksum,
                                        unsigned flags, ClientBytes contract, unsigned original_size) {
  const size_t input_size = encoded["length"].as<size_t>();
  const size_t metadata_size = contract["length"].as<size_t>();
  const bool compressed = (flags & 2) != 0;
  if (!input_size || input_size > 16 * 1024 * 1024 || !original_size || original_size > 16 * 1024 * 1024 ||
      metadata_size > 275 || (!compressed && input_size != original_size)) {
    emscripten::val result = emscripten::val::object();
    result.set("code", static_cast<int>(SET_ERRNO(ERROR_INVALID_PARAM, "Invalid encoded frame or metadata size")));
    result.set("decodeFailed", false);
    result.set("bytes", client_copy_output(nullptr, 0));
    return ClientFrameResult(result);
  }
  auto input = client_copy_input(encoded);
  auto metadata = client_copy_input(contract);
  std::vector<uint8_t> decoded(compressed ? original_size : 0);
  const uint8_t *text = compressed ? decoded.data() : input.data();
  int code = compressed ? client_decode_frame_data(input.data(), input.size(), decoded.data(), decoded.size()) : 0;
  const bool decode_failed = code != 0;
  if (!code)
    code = client_validate_frame(reinterpret_cast<const char *>(text), original_size, width, height, checksum, flags,
                                 metadata.empty() ? nullptr : metadata.data(), metadata.size());
  emscripten::val result = emscripten::val::object();
  result.set("code", code);
  result.set("decodeFailed", decode_failed);
  result.set("bytes", client_copy_output(text, code == 0 ? original_size : 0));
  return ClientFrameResult(result);
}

EMSCRIPTEN_BINDINGS(ascii_chat_client_api) {
  emscripten::function("clientSettings", &client_settings);
  emscripten::function("client_cleanup", &client_cleanup);
  emscripten::function("client_generate_keypair", &client_generate_keypair);
  emscripten::function("client_get_connection_state", &client_get_connection_state);
  emscripten::function("client_resolve_crypto_policy", &client_resolve_crypto_policy);
  emscripten::function("client_handshake_begin", +[]() { return static_cast<int>(client_handshake_begin()); });
  emscripten::function(
      "client_acds_handshake_begin", +[](uint8_t mode) { return static_cast<int>(client_acds_handshake_begin(mode)); });
  emscripten::function(
      "network_error_action",
      +[](int code) { return static_cast<int>(network_error_action(static_cast<asciichat_error_t>(code))); });
  emscripten::register_type<ClientBytes>("ClientBytes", "Uint8Array");
  emscripten::register_type<ClientPacketResult>("ClientPacketResult", "{ code: number; bytes: Uint8Array }");
  emscripten::register_type<ClientSettingsResult>("ClientSettingsResult", "{ code: number; json: string | null }");
  emscripten::register_type<ClientFrameResult>("ClientFrameResult",
                                               "{ code: number; decodeFailed: boolean; bytes: Uint8Array }");
  emscripten::class_<AVStateBinding>("AVStateBinding")
      .constructor<>()
      .function("valid", &AVStateBinding::valid)
      .function("generation", &AVStateBinding::generation)
      .function("begin", &AVStateBinding::begin)
      .function("complete", &AVStateBinding::complete)
      .function("current", &AVStateBinding::current)
      .function("active", &AVStateBinding::active)
      .function("beginStop", &AVStateBinding::beginStop)
      .function("finishStop", &AVStateBinding::finishStop);
  emscripten::function("setServerAddress", &set_server_address);
  emscripten::function("clientPublicKey", &client_public_key);
  emscripten::function("sendClientFrame", &send_client_frame);
  emscripten::function("handleHandshake", &handle_handshake);
  emscripten::function("receiveHandshake", &receive_handshake);
  emscripten::function("cryptPacket", &crypt_packet);
  emscripten::function("parsePacketJson", &parse_packet);
  emscripten::function("serializePacket", &serialize_packet);
  emscripten::function("receiveSettings", &receive_settings);
  emscripten::function("validateFrame", &validate_frame);
}
