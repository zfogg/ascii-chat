#include <emscripten/bind.h>
extern "C" {
#include "../client.h"
#include <ascii-chat/network/browser/security.h>
#include <ascii-chat/network/errors.h>
#include <ascii-chat/network/browser/transport.h>
#include <ascii-chat/network/consensus/handoff.h>
}
#include "../common/opts.h"
#include "byte_arrays.h"
#include "client_bytes.h"
#include <ascii-chat/common.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/util/endian.h>
#include <sodium.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <optional>
#include <vector>

class ClientSessionBinding {
public:
  static int errorAction(int code) {
    return static_cast<int>(network_error_action(static_cast<asciichat_error_t>(code)));
  }
  explicit ClientSessionBinding(OptionsPatch options) {
    transport_.reset(browser_transport_create());
    if (transport_) {
      result_ = client_init(transport_.get());
      if (!result_)
        result_ = initial_options(options);
      else
        SET_ERRNO(ERROR_INIT, "Browser client initialization failed");
    } else {
      result_ = SET_ERRNO(ERROR_MEMORY, "Browser transport allocation failed");
    }
    if (result_ && transport_)
      client_cleanup(transport_.get());
    if (!result_) {
      owned_ = true;
    }
  }
  ~ClientSessionBinding() {
    close();
  }
  ClientSessionBinding(const ClientSessionBinding &) = delete;
  ClientSessionBinding &operator=(const ClientSessionBinding &) = delete;
  bool valid() const {
    return owned_;
  }
  emscripten::val protocolInfo() const {
    emscripten::val info = emscripten::val::object();
    info.set("protocolVersion", 1);
    info.set("connectionStates", emscripten::val::array());
    const char *states[] = {"disconnected", "connecting", "handshake", "connected", "error"};
    for (unsigned index = 0; index < 5; ++index)
      info["connectionStates"].set(index, states[index]);
    info.set("cryptoModes", emscripten::val::object());
    info["cryptoModes"].set("none", ACIP_CRYPTO_NONE);
    info["cryptoModes"].set("auth", ACIP_CRYPTO_AUTH);
    info["cryptoModes"].set("encrypt", ACIP_CRYPTO_ENCRYPT);
    return info;
  }
  int error() const {
    return result_;
  }
  emscripten::val settingsReceive(ClientBytes payload) {
    auto input = binding_copy_input(payload);
    const int code = client_settings_receive(transport_.get(), input.data(), input.size());
    char *json = code == 0 ? client_settings_values(transport_.get()) : nullptr;
    emscripten::val response = emscripten::val::object();
    response.set("code", code);
    response.set("json", json ? emscripten::val(std::string(json)) : emscripten::val::null());
    if (json)
      SAFE_FREE(json);
    return response;
  }
  emscripten::val settingsValues() const {
    char *json = client_settings_values(transport_.get());
    emscripten::val response = emscripten::val::object();
    response.set("json", json ? emscripten::val(std::string(json)) : emscripten::val::null());
    if (json)
      SAFE_FREE(json);
    return response;
  }
  emscripten::val validateFrame(ClientBytes encoded, unsigned width, unsigned height, unsigned checksum,
                                unsigned flags, ClientBytes contract, unsigned original_size) {
    const size_t input_size = encoded["length"].as<size_t>();
    const size_t metadata_size = contract["length"].as<size_t>();
    const bool compressed = (flags & 2) != 0;
    emscripten::val response = emscripten::val::object();
    if (!input_size || input_size > 16 * 1024 * 1024 || !original_size || original_size > 16 * 1024 * 1024 ||
        metadata_size > 275 || (!compressed && input_size != original_size)) {
      response.set("code", static_cast<int>(SET_ERRNO(ERROR_INVALID_PARAM, "Invalid encoded frame or metadata size")));
      response.set("decodeFailed", false);
      response.set("bytes", binding_copy_output(nullptr, 0));
      return response;
    }
    auto input = binding_copy_input(encoded);
    auto metadata = binding_copy_input(contract);
    std::vector<uint8_t> decoded(compressed ? original_size : 0);
    const uint8_t *text = compressed ? decoded.data() : input.data();
    int code = compressed ? client_decode_frame_data(input.data(), input.size(), decoded.data(), decoded.size()) : 0;
    const bool decode_failed = code != 0;
    if (!code)
      code = client_validate_frame(transport_.get(), reinterpret_cast<const char *>(text), original_size, width, height,
                                   checksum, flags, metadata.empty() ? nullptr : metadata.data(), metadata.size());
    response.set("code", code);
    response.set("decodeFailed", decode_failed);
    response.set("bytes", binding_copy_output(text, code == 0 ? original_size : 0));
    return response;
  }
  void close() {
    if (!owned_)
      return;
    client_cleanup(transport_.get());
    owned_ = false;
  }
  emscripten::val process(emscripten::val event) {
    if (!owned_)
      return result(SET_ERRNO(ERROR_INVALID_STATE, "Client session is not initialized"));
    const std::string type = event["type"].as<std::string>();
    if (type == "begin-handshake") {
      const bool discovery = !event["discovery"].isUndefined() && event["discovery"].as<bool>();
      const int code = discovery
                           ? static_cast<int>(client_acds_handshake_begin(transport_.get(), event["mode"].as<uint8_t>()))
                           : static_cast<int>(client_handshake_begin(transport_.get()));
      return result(code);
    }
    if (type == "configure-crypto") {
      auto pass = binding_copy_input(event["password"]);
      auto secret = binding_copy_input(event["identity"]);
      auto key = binding_copy_input(event["expected"]);
      int code;
      if ((secret.empty() || secret.size() == 64) && (key.empty() || key.size() == 32)) {
        pass.push_back(0);
        code = client_configure_crypto_options(
            transport_.get(),
            pass.size() > 1 ? reinterpret_cast<char *>(pass.data()) : nullptr,
            secret.empty() ? nullptr : secret.data(), key.empty() ? nullptr : key.data());
      } else {
        code = SET_ERRNO(ERROR_INVALID_PARAM, "Invalid client identity or verification key length");
      }
      if (!pass.empty())
        sodium_memzero(pass.data(), pass.size());
      if (!secret.empty())
        sodium_memzero(secret.data(), secret.size());
      if (!key.empty())
        sodium_memzero(key.data(), key.size());
      return result(code);
    }
    if (type == "transport-open") {
      browser_transport_set_state(transport_.get(), BROWSER_TRANSPORT_CONNECTING);
      return result(0);
    }
    if (type == "transport-connected") {
      browser_transport_set_state(transport_.get(), BROWSER_TRANSPORT_CONNECTED);
      return result(0);
    }
    if (type == "transport-closed") {
      browser_transport_set_state(transport_.get(), BROWSER_TRANSPORT_DISCONNECTED);
      return result(0);
    }
    if (type == "state" || type == "snapshot") {
      emscripten::val response = result(0);
      if (type == "snapshot") {
        response.set("ready", client_get_connection_state(transport_.get()) == BROWSER_TRANSPORT_CONNECTED);
        response.set("queuedBytes", browser_transport_queued_bytes(transport_.get()));
        response.set("writable", browser_transport_writable(transport_.get(), 0));
      }
      return response;
    }
    if (type == "generate-keypair") {
      const int code = client_generate_keypair(transport_.get());
      emscripten::val response = result(code);
      const uint8_t *key = client_public_key_bytes(transport_.get());
      if (!code && key) {
        std::array<char, 65> hex{};
        sodium_bin2hex(hex.data(), hex.size(), key, 32);
        response.set("publicKey", std::string(hex.data()));
      }
      return response;
    }
    if (type == "resolve-crypto-policy") {
      const int mode = client_resolve_crypto_policy(transport_.get(), event["wss"].as<bool>(), event["encryption"].as<int>());
      emscripten::val response = result(mode < 0 ? SET_ERRNO(ERROR_INVALID_PARAM, "Invalid crypto policy") : 0);
      response.set("mode", mode);
      return response;
    }
    if (type == "receive-handshake-packet") {
      auto bytes = binding_copy_input(event["payload"]);
      const bool discovery = !event["discovery"].isUndefined() && event["discovery"].as<bool>();
      packet_header_t header{};
      const int validation = bytes.size() < sizeof(packet_header_t)
                                 ? SET_ERRNO(ERROR_INVALID_PARAM, "Client handshake packet is shorter than its header")
                                 : packet_validate(bytes.data(), bytes.size(), 0, &header);
      const void *payload = bytes.size() > sizeof(packet_header_t) ? bytes.data() + sizeof(packet_header_t) : nullptr;
      const size_t payload_size = bytes.size() > sizeof(packet_header_t) ? bytes.size() - sizeof(packet_header_t) : 0;
      const int code = validation
                           ? validation
                           : (discovery ? client_acds_handshake_receive(transport_.get(), NET_TO_HOST_U16(header.type),
                                                                          payload, payload_size)
                                        : client_handshake_receive(transport_.get(), NET_TO_HOST_U16(header.type),
                                                                    payload, payload_size));
      if (!bytes.empty())
        sodium_memzero(bytes.data(), bytes.size());
      return result(code);
    }
    if (type == "serialize") {
      const unsigned packet_type = event["packetType"].as<unsigned>();
      const unsigned client_id = event["clientId"].isUndefined() ? 0 : event["clientId"].as<unsigned>();
      auto input = binding_copy_input(event["payload"]);
      std::vector<uint8_t> output(input.size() + 256);
      size_t length = 0;
      int code = packet_type > UINT16_MAX
                     ? SET_ERRNO(ERROR_INVALID_PARAM, "Invalid client packet type: %u", packet_type)
                     : packet_encode(static_cast<packet_type_t>(packet_type), input.empty() ? nullptr : input.data(),
                                     input.size(), client_id, output.data(), output.size(), &length);
      if (code)
        SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Cannot serialize client packet");
      emscripten::val response = result(code);
      response.set("bytes", binding_copy_output(output.data(), code ? 0 : length));
      return response;
    }
    if (type == "crypt") {
      const bool encrypt = event["encrypt"].as<bool>();
      auto input = binding_copy_input(event["packet"]);
      std::vector<uint8_t> output(input.size() + (encrypt ? 72 : 0));
      size_t length = 0;
      const int code = encrypt
                           ? client_encrypt_packet(transport_.get(), input.data(), input.size(), output.data(), output.size(), &length)
                           : client_decrypt_packet(transport_.get(), input.data(), input.size(), output.data(), output.size(), &length);
      emscripten::val response = result(code);
      response.set("bytes", binding_copy_output(output.data(), code ? 0 : length));
      if (!input.empty())
        sodium_memzero(input.data(), input.size());
      if (!output.empty())
        sodium_memzero(output.data(), output.size());
      return response;
    }
    if (type == "parse") {
      auto input = binding_copy_input(event["packet"]);
      packet_header_t header{};
      const int code = input.size() < sizeof(packet_header_t)
                           ? SET_ERRNO(ERROR_INVALID_PARAM, "Client packet is shorter than its header")
                           : packet_validate(input.data(), input.size(), 0, &header);
      emscripten::val response = result(code);
      if (!code) {
        response.set("packet", emscripten::val::object());
        response["packet"].set("type", NET_TO_HOST_U16(header.type));
        response["packet"].set("length", NET_TO_HOST_U32(header.length));
        response["packet"].set("client_id", NET_TO_HOST_U32(header.client_id));
        response["packet"].set("crc32", NET_TO_HOST_U32(header.crc32));
      }
      return response;
    }
    if (type == "set-server") {
      const std::string host = event["host"].as<std::string>();
      const int port = event["port"].as<int>();
      if (host.empty() || port <= 0 || port > 65535)
        return result(SET_ERRNO(ERROR_INVALID_PARAM, "Invalid client server address or port"));
      return result(client_set_server_address(transport_.get(), host.c_str(), port));
    }
    if (type == "quic-init")
      return result(browser_security_init(client_security_context(transport_.get())));
    if (type == "quic-state") {
      emscripten::val response = result(0);
      response.set("epoch", browser_security_epoch(client_security_context(transport_.get())));
      return response;
    }
    if (type == "quic-tick") {
      const int code = browser_security_tick(client_security_context(transport_.get()));
      emscripten::val response = drainOutputs();
      if (code && response["code"].as<int>() == 0)
        response.set("code", code);
      return response;
    }
    if (type == "quic-packet") {
      auto input = binding_copy_input(event["packet"]);
      const bool seal = event["operation"].as<std::string>() == "seal";
      std::vector<uint8_t> output(input.size() + (seal ? 94 : 0));
      size_t length = 0;
      uint64_t sequence = 0;
      const int code = browser_security_packet(client_security_context(transport_.get()), seal, event["flow"].as<unsigned>(),
                                               event["delivery"].as<unsigned>(), input.data(), input.size(),
                                               output.data(), output.size(), &length, &sequence);
      emscripten::val response = result(code);
      response.set("bytes", binding_copy_output(output.data(), code ? 0 : length));
      response.set("sequence", emscripten::val::global("BigInt")(std::to_string(sequence)));
      return response;
    }
    if (type == "handoff-encode") {
      auto session = binding_copy_input(event["session"]);
      auto participant = binding_copy_input(event["participant"]);
      if (session.size() != SESSION_ID_SIZE || participant.size() != SESSION_ID_SIZE)
        return result(SET_ERRNO(ERROR_INVALID_PARAM, "Handoff identifiers must be 16 bytes"));
      consensus_handoff_message_t message{};
      message.kind = static_cast<uint8_t>(event["kind"].as<int>());
      message.generation = event["generation"].as<unsigned>();
      message.sequence = event["sequence"].as<unsigned>();
      message.capabilities = static_cast<uint8_t>(event["capabilities"].as<int>());
      message.nat_tier = static_cast<uint8_t>(event["natTier"].as<int>());
      message.port = static_cast<uint16_t>(event["port"].as<int>());
      memcpy(message.session, session.data(), SESSION_ID_SIZE);
      memcpy(message.participant, participant.data(), SESSION_ID_SIZE);
      uint8_t bytes[CONSENSUS_HANDOFF_HEADER];
      consensus_handoff_encode(&message, bytes);
      emscripten::val response = result(0);
      response.set("bytes", binding_copy_output(bytes, sizeof(bytes)));
      return response;
    }
    if (type == "handoff-decode") {
      auto bytes = binding_copy_input(event["packet"]);
      consensus_handoff_message_t message{};
      const int code = consensus_handoff_decode(bytes.data(), bytes.size(), &message);
      emscripten::val response = result(code);
      if (!code) {
        emscripten::val action = emscripten::val::object();
        action.set("kind", message.kind);
        action.set("participant", binding_copy_output(message.participant, SESSION_ID_SIZE));
        action.set("generation", message.generation);
        action.set("sequence", message.sequence);
        response.set("action", action);
      }
      return response;
    }
    return result(SET_ERRNO(ERROR_INVALID_PARAM, "Unknown browser client event: %s", type.c_str()));
  }
  emscripten::val drainOutputs() const {
    emscripten::val response = result(0);
    int output_code = 0;
    response.set("outputs", quic_outputs(client_security_context(transport_.get()), &output_code));
    if (!response["code"].as<int>() && output_code)
      response.set("code", output_code);
    response.set("epoch", browser_security_epoch(client_security_context(transport_.get())));
    return response;
  }
  emscripten::val processBatch(emscripten::val events) {
    if (!owned_)
      return emscripten::val::array();
    const unsigned count = events["length"].as<unsigned>();
    if (count > 256) {
      emscripten::val batch = emscripten::val::array();
      batch.call<void>("push", result(SET_ERRNO(ERROR_INVALID_PARAM, "Client event batch exceeds 256 events")));
      return batch;
    }
    emscripten::val batch = emscripten::val::array();
    for (unsigned index = 0; index < count; ++index)
      batch.call<void>("push", process(events[index]));
    return batch;
  }

private:
  emscripten::val result(int code) const {
    emscripten::val response = emscripten::val::object();
    response.set("code", code);
    response.set("state", client_get_connection_state(transport_.get()));
    if (code) {
      asciichat_error_context_t context{};
      const bool has_context = errno_has(&context);
      response.set("message", has_context && context.context_message[0]
                                  ? std::string(context.context_message)
                                  : std::string(asciichat_error_string(static_cast<asciichat_error_t>(code))));
      response.set("retryable", network_error_action(static_cast<asciichat_error_t>(code)) == NETWORK_ERROR_RETRY);
    }
    return response;
  }
  static emscripten::val quic_outputs(crypto_context_t *context, int *output_code) {
    emscripten::val outputs = emscripten::val::array();
    for (unsigned index = 0; index < 32; ++index) {
      uint8_t data[256];
      size_t length = 0;
      uint32_t flow = 0;
      const int code = browser_security_output(context, data, sizeof(data), &length, &flow);
      if (code) {
        if (output_code)
          *output_code = code;
        break;
      }
      if (!length)
        break;
      emscripten::val output = emscripten::val::object();
      output.set("flow", flow);
      output.set("packet", binding_copy_output(data, length));
      outputs.call<void>("push", output);
    }
    return outputs;
  }
  std::unique_ptr<browser_transport_t, decltype(&browser_transport_destroy)> transport_{nullptr,
                                                                                         &browser_transport_destroy};
  bool owned_ = false;
  int result_ = 0;
};
EMSCRIPTEN_BINDINGS(ascii_chat_network) {
  emscripten::register_type<ClientBytes>("ClientBytes", "Uint8Array");
  emscripten::class_<ClientSessionBinding>("ClientSessionBinding")
      .constructor<OptionsPatch>()
      .function("valid", &ClientSessionBinding::valid)
      .function("protocolInfo", &ClientSessionBinding::protocolInfo)
      .function("error", &ClientSessionBinding::error)
      .function("settingsReceive", &ClientSessionBinding::settingsReceive)
      .function("settingsValues", &ClientSessionBinding::settingsValues)
      .function("validateFrame", &ClientSessionBinding::validateFrame)
      .function("close", &ClientSessionBinding::close)
      .function("process", &ClientSessionBinding::process)
      .function("processBatch", &ClientSessionBinding::processBatch)
      .function("drainOutputs", &ClientSessionBinding::drainOutputs);

}
