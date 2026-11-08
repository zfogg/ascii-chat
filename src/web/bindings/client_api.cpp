#include <emscripten/bind.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

EMSCRIPTEN_DECLARE_VAL_TYPE(ClientBytes);
EMSCRIPTEN_DECLARE_VAL_TYPE(ClientPacketResult);
EMSCRIPTEN_DECLARE_VAL_TYPE(ClientSettingsResult);
EMSCRIPTEN_DECLARE_VAL_TYPE(ClientFrameResult);

extern "C" {
int client_set_server_address(const char *, int);
char *client_get_public_key_hex(void);
int client_handle_crypto_parameters(const uint8_t *, size_t);
int client_handle_key_exchange_init(const uint8_t *, size_t);
int client_handle_auth_challenge(const uint8_t *, size_t);
int client_handle_handshake_complete(const uint8_t *, size_t);
int client_handshake_receive(uint32_t, const void *, size_t);
int client_acds_handshake_receive(uint32_t, const void *, size_t);
int client_encrypt_packet(const uint8_t *, size_t, uint8_t *, size_t, size_t *);
int client_decrypt_packet(const uint8_t *, size_t, uint8_t *, size_t, size_t *);
char *client_parse_packet(const uint8_t *, size_t);
int client_serialize_packet(uint16_t, const uint8_t *, size_t, uint32_t, uint8_t *, size_t *);
void client_free_string(char *);
int client_settings_receive(const void *, size_t);
char *client_settings_values(void);
void wasm_options_release(char *);
int client_validate_frame(const char *, size_t, uint32_t, uint32_t, uint32_t, uint32_t, const void *, size_t);
int client_decode_frame_data(const void *, size_t, void *, size_t);
int client_send_video_frame(const uint8_t *, int, int);
void sodium_memzero(void *, size_t);
void *session_av_state_create(void);
void session_av_state_destroy(void *);
uint32_t session_av_state_generation(const void *);
uint32_t session_av_state_begin(void *);
bool session_av_state_complete(void *, uint32_t);
bool session_av_state_is_current(void *, uint32_t);
bool session_av_state_is_active(void *);
void session_av_state_begin_stop(void *);
void session_av_state_finish_stop(void *);
}

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
  void *state_;
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
  return client_set_server_address(host.c_str(), port);
}
static std::optional<std::string> client_public_key() {
  char *key = client_get_public_key_hex();
  if (!key)
    return std::nullopt;
  std::string result(key);
  client_free_string(key);
  return result;
}
static int send_client_frame(ClientBytes rgba, int width, int height) {
  auto bytes = client_copy_input(rgba);
  return client_send_video_frame(bytes.data(), width, height);
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
  }
  if (!bytes.empty())
    sodium_memzero(bytes.data(), bytes.size());
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
  if (!json)
    return std::nullopt;
  std::string result(json);
  client_free_string(json);
  return result;
}
static ClientPacketResult serialize_packet(unsigned type, ClientBytes payload, unsigned client_id) {
  auto input = client_copy_input(payload);
  std::vector<uint8_t> output(input.size() + 256);
  size_t length = 0;
  const int code = client_serialize_packet(static_cast<uint16_t>(type), input.empty() ? nullptr : input.data(),
                                           input.size(), client_id, output.data(), &length);
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
    wasm_options_release(json);
  return ClientSettingsResult(result);
}
static ClientFrameResult validate_frame(ClientBytes encoded, unsigned width, unsigned height, unsigned checksum,
                                        unsigned flags, ClientBytes contract, unsigned original_size) {
  const size_t input_size = encoded["length"].as<size_t>();
  const size_t metadata_size = contract["length"].as<size_t>();
  const bool compressed = (flags & 2) != 0;
  if (!input_size || input_size > 16 * 1024 * 1024 || !original_size || original_size > 16 * 1024 * 1024 ||
      metadata_size > 275 || (!compressed && input_size != original_size)) {
    emscripten::val result = emscripten::val::object();
    result.set("code", -1);
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
