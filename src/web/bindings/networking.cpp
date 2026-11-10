#include <emscripten/bind.h>
extern "C" {
#include <ascii-chat/network/browser/client.h>
#include <ascii-chat/network/errors.h>
#include <ascii-chat/network/browser/transport.h>
}
#include "../common/opts.h"
#include "byte_arrays.h"
#include "client_bytes.h"
#include <ascii-chat/common.h>
#include <ascii-chat/common/protocol_constants.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/network/acip/messages.h>
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
      result_ = browser_client_init(transport_.get());
      if (!result_)
        result_ = initial_options(options);
      else
        SET_ERRNO(ERROR_INIT, "Browser client initialization failed");
    } else {
      result_ = SET_ERRNO(ERROR_MEMORY, "Browser transport allocation failed");
    }
    if (result_ && transport_)
      browser_client_cleanup(transport_.get());
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
    info.set("protocolVersion", PROTOCOL_VERSION_MAJOR);
    info.set("protocolMinorVersion", PROTOCOL_VERSION_MINOR);
    info.set("protocolPatchVersion", PROTOCOL_VERSION_PATCH);
    info.set("connectionStates", emscripten::val::array());
    const char *states[] = {"disconnected", "connecting", "handshake", "connected", "error"};
    for (unsigned index = 0; index < 5; ++index)
      info["connectionStates"].set(index, states[index]);
    info.set("cryptoModes", emscripten::val::object());
    info["cryptoModes"].set("none", ACIP_CRYPTO_NONE);
    info["cryptoModes"].set("auth", ACIP_CRYPTO_AUTH);
    info["cryptoModes"].set("encrypt", ACIP_CRYPTO_ENCRYPT);
    info.set("packetTypes", emscripten::val::object());
    const std::pair<const char *, packet_type_t> packet_types[] = {
        {"PROTOCOL_VERSION", PACKET_TYPE_PROTOCOL_VERSION},
        {"CRYPTO_CLIENT_HELLO", PACKET_TYPE_CRYPTO_CLIENT_HELLO},
        {"CRYPTO_CAPABILITIES", PACKET_TYPE_CRYPTO_CAPABILITIES},
        {"CRYPTO_PARAMETERS", PACKET_TYPE_CRYPTO_PARAMETERS},
        {"CRYPTO_KEY_EXCHANGE_INIT", PACKET_TYPE_CRYPTO_KEY_EXCHANGE_INIT},
        {"CRYPTO_KEY_EXCHANGE_RESP", PACKET_TYPE_CRYPTO_KEY_EXCHANGE_RESP},
        {"CRYPTO_AUTH_CHALLENGE", PACKET_TYPE_CRYPTO_AUTH_CHALLENGE},
        {"CRYPTO_AUTH_RESPONSE", PACKET_TYPE_CRYPTO_AUTH_RESPONSE},
        {"CRYPTO_AUTH_FAILED", PACKET_TYPE_CRYPTO_AUTH_FAILED},
        {"CRYPTO_SERVER_AUTH_RESP", PACKET_TYPE_CRYPTO_SERVER_AUTH_RESP},
        {"CRYPTO_HANDSHAKE_COMPLETE", PACKET_TYPE_CRYPTO_HANDSHAKE_COMPLETE},
        {"CRYPTO_NO_ENCRYPTION", PACKET_TYPE_CRYPTO_NO_ENCRYPTION},
        {"QUIC_READY", PACKET_TYPE_QUIC_READY},
        {"QUIC_KEYFRAME_REQUEST", PACKET_TYPE_QUIC_KEYFRAME_REQUEST},
        {"CRYPTO_EPOCH_MARKER", PACKET_TYPE_CRYPTO_EPOCH_MARKER},
        {"CRYPTO_REKEY_NEEDED", PACKET_TYPE_CRYPTO_REKEY_NEEDED},
        {"CRYPTO_EPOCH_ACK", PACKET_TYPE_CRYPTO_EPOCH_ACK},
        {"ENCRYPTED", PACKET_TYPE_ENCRYPTED},
        {"CRYPTO_REKEY_REQUEST", PACKET_TYPE_CRYPTO_REKEY_REQUEST},
        {"CRYPTO_REKEY_RESPONSE", PACKET_TYPE_CRYPTO_REKEY_RESPONSE},
        {"CRYPTO_REKEY_COMPLETE", PACKET_TYPE_CRYPTO_REKEY_COMPLETE},
        {"SIZE_MESSAGE", PACKET_TYPE_SIZE_MESSAGE},
        {"AUDIO_MESSAGE", PACKET_TYPE_AUDIO_MESSAGE},
        {"TEXT_MESSAGE", PACKET_TYPE_TEXT_MESSAGE},
        {"ERROR_MESSAGE", PACKET_TYPE_ERROR_MESSAGE},
        {"REMOTE_LOG", PACKET_TYPE_REMOTE_LOG},
        {"ASCII_FRAME", PACKET_TYPE_ASCII_FRAME},
        {"IMAGE_FRAME", PACKET_TYPE_IMAGE_FRAME},
        {"IMAGE_FRAME_H265", PACKET_TYPE_IMAGE_FRAME_H265},
        {"IMAGE_FRAME_H264", PACKET_TYPE_IMAGE_FRAME_H264},
        {"AUDIO_BATCH", PACKET_TYPE_AUDIO_BATCH},
        {"AUDIO_OPUS_BATCH", PACKET_TYPE_AUDIO_OPUS_BATCH},
        {"CLIENT_CAPABILITIES", PACKET_TYPE_CLIENT_CAPABILITIES},
        {"PING", PACKET_TYPE_PING},
        {"PONG", PACKET_TYPE_PONG},
        {"CLIENT_JOIN", PACKET_TYPE_CLIENT_JOIN},
        {"CLIENT_LEAVE", PACKET_TYPE_CLIENT_LEAVE},
        {"STREAM_START", PACKET_TYPE_STREAM_START},
        {"STREAM_STOP", PACKET_TYPE_STREAM_STOP},
        {"CLEAR_CONSOLE", PACKET_TYPE_CLEAR_CONSOLE},
        {"SERVER_STATE", PACKET_TYPE_SERVER_STATE},
    };
    for (const auto &[name, type] : packet_types)
      info["packetTypes"].set(name, static_cast<unsigned>(type));
    info.set("browserCapabilities", emscripten::val::object());
    info["browserCapabilities"].set("webtransport", browser_capabilities_.webtransport);
    info["browserCapabilities"].set("datagrams", browser_capabilities_.datagrams);
    info["browserCapabilities"].set("hevc", browser_capabilities_.hevc);
    info["browserCapabilities"].set("webrtc", browser_capabilities_.webrtc);
    return info;
  }
  int error() const {
    return result_;
  }
  emscripten::val snapshot() const {
    if (!owned_)
      return result(SET_ERRNO(ERROR_INVALID_STATE, "Client session is not initialized"));
    emscripten::val response = result(0);
    response.set("ready", browser_client_ready(transport_.get()));
    response.set("queuedBytes", browser_transport_queued_bytes(transport_.get()));
    response.set("writable", browser_transport_writable(transport_.get(), 0));
    return response;
  }
  void close() {
    if (!owned_)
      return;
    browser_client_cleanup(transport_.get());
    owned_ = false;
  }
  emscripten::val process(emscripten::val event) {
    if (!owned_)
      return result(SET_ERRNO(ERROR_INVALID_STATE, "Client session is not initialized"));
    const std::string type = event["type"].as<std::string>();
    if (type == "frame-validate") {
      const ClientBytes encoded(event["encoded"]);
      const ClientBytes contract(event["contract"]);
      const unsigned width = event["width"].as<unsigned>();
      const unsigned height = event["height"].as<unsigned>();
      const unsigned checksum = event["checksum"].as<unsigned>();
      const unsigned flags = event["flags"].as<unsigned>();
      const unsigned original_size = event["originalSize"].as<unsigned>();
      const size_t input_size = encoded["length"].as<size_t>();
      const size_t metadata_size = contract["length"].as<size_t>();
      const bool compressed = (flags & 2) != 0;
      emscripten::val response = result(0);
      if (!input_size || input_size > 16 * 1024 * 1024 || !original_size || original_size > 16 * 1024 * 1024 ||
          metadata_size > 275 || (!compressed && input_size != original_size)) {
        response.set("code",
                     static_cast<int>(SET_ERRNO(ERROR_INVALID_PARAM, "Invalid encoded frame or metadata size")));
        response.set("ok", false);
        response.set("decodeFailed", false);
        response.set("bytes", binding_copy_output(nullptr, 0));
        return response;
      }
      auto input = binding_copy_input(encoded);
      auto metadata = binding_copy_input(contract);
      std::vector<uint8_t> decoded(compressed ? original_size : 0);
      const uint8_t *text = compressed ? decoded.data() : input.data();
      int code =
          compressed ? browser_client_decode_frame_data(input.data(), input.size(), decoded.data(), decoded.size()) : 0;
      const bool decode_failed = code != 0;
      if (!code)
        code = browser_client_validate_frame(transport_.get(), reinterpret_cast<const char *>(text), original_size,
                                             width, height, checksum, flags,
                                             metadata.empty() ? nullptr : metadata.data(), metadata.size());
      response.set("code", code);
      response.set("ok", code == 0);
      response.set("decodeFailed", decode_failed);
      response.set("bytes", binding_copy_output(text, code == 0 ? original_size : 0));
      return response;
    }
    if (type == "settings-receive") {
      auto input = binding_copy_input(event["payload"]);
      const int code = browser_client_settings_receive(transport_.get(), input.data(), input.size());
      emscripten::val response = result(code);
      char *json = code == 0 ? browser_client_settings_values(transport_.get()) : nullptr;
      response.set("json", json ? emscripten::val(std::string(json)) : emscripten::val::null());
      if (json)
        SAFE_FREE(json);
      return response;
    }
    if (type == "settings-values") {
      emscripten::val response = result(0);
      char *json = browser_client_settings_values(transport_.get());
      response.set("json", json ? emscripten::val(std::string(json)) : emscripten::val::null());
      if (json)
        SAFE_FREE(json);
      return response;
    }
    if (type == "begin-handshake") {
      const bool discovery = !event["discovery"].isUndefined() && event["discovery"].as<bool>();
      const int code =
          discovery
              ? static_cast<int>(browser_client_acds_handshake_begin(transport_.get(), event["mode"].as<uint8_t>()))
              : static_cast<int>(browser_client_handshake_begin(transport_.get()));
      return code ? result(code) : drainOutputs();
    }
    if (type == "configure-crypto") {
      auto pass = binding_copy_input(event["password"]);
      auto secret = binding_copy_input(event["identity"]);
      auto key = binding_copy_input(event["expected"]);
      int code;
      if ((secret.empty() || secret.size() == 64) && (key.empty() || key.size() == 32)) {
        pass.push_back(0);
        code = browser_client_configure_crypto_options(
            transport_.get(), pass.size() > 1 ? reinterpret_cast<char *>(pass.data()) : nullptr,
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
      browser_client_reset_handshake(transport_.get());
      browser_transport_set_state(transport_.get(), BROWSER_TRANSPORT_CONNECTING);
      return result(0);
    }
    if (type == "transport-connected") {
      browser_transport_set_state(transport_.get(), BROWSER_TRANSPORT_CONNECTED);
      return result(0);
    }
    if (type == "transport-closed") {
      browser_client_reset_handshake(transport_.get());
      browser_transport_set_state(transport_.get(), BROWSER_TRANSPORT_DISCONNECTED);
      return result(0);
    }
    if (type == "snapshot")
      return snapshot();
    if (type == "drain-outputs")
      return drainOutputs();
    if (type == "state")
      return result(0);
    if (type == "generate-keypair") {
      const int code = browser_client_generate_keypair(transport_.get());
      emscripten::val response = result(code);
      const uint8_t *key = browser_client_public_key_bytes(transport_.get());
      if (!code && key) {
        std::array<char, 65> hex{};
        sodium_bin2hex(hex.data(), hex.size(), key, 32);
        response.set("publicKey", std::string(hex.data()));
      }
      return response;
    }
    if (type == "send-packet") {
      const unsigned packet_type = event["packetType"].as<unsigned>();
      const unsigned client_id = event["clientId"].isUndefined() ? 0 : event["clientId"].as<unsigned>();
      const bool encrypt = !event["encrypt"].isUndefined() && event["encrypt"].as<bool>();
      auto input = binding_copy_input(event["payload"]);
      std::vector<uint8_t> plaintext(input.size() + sizeof(packet_header_t) + 256);
      size_t plaintext_length = 0;
      int code = packet_type > UINT16_MAX
                     ? SET_ERRNO(ERROR_INVALID_PARAM, "Invalid client packet type: %u", packet_type)
                     : packet_encode(static_cast<packet_type_t>(packet_type), input.empty() ? nullptr : input.data(),
                                     input.size(), client_id, plaintext.data(), plaintext.size(), &plaintext_length);
      if (!code && encrypt) {
        std::vector<uint8_t> ciphertext(plaintext_length + 128);
        size_t ciphertext_length = 0;
        code = browser_client_encrypt_packet(transport_.get(), plaintext.data(), plaintext_length, ciphertext.data(),
                                             ciphertext.size(), &ciphertext_length);
        if (!code) {
          std::vector<uint8_t> wrapped(ciphertext_length + sizeof(packet_header_t) + 256);
          size_t wrapped_length = 0;
          code = packet_encode(PACKET_TYPE_ENCRYPTED, ciphertext.data(), ciphertext_length, client_id, wrapped.data(),
                               wrapped.size(), &wrapped_length);
          auto response = result(code);
          response.set("bytes", binding_copy_output(wrapped.data(), code ? 0 : wrapped_length));
          return response;
        }
      }
      auto response = result(code);
      response.set("bytes", binding_copy_output(plaintext.data(), code || encrypt ? 0 : plaintext_length));
      return response;
    }
    if (type == "receive-transport-packet") {
      if (event["packet"].isUndefined() || event["packet"].isNull())
        return result(SET_ERRNO(ERROR_INVALID_PARAM, "Transport packet is required"));
      auto input = binding_copy_input(event["packet"]);
      packet_header_t header{};
      int code = input.size() < sizeof(packet_header_t)
                     ? SET_ERRNO(ERROR_INVALID_PARAM, "Client packet is shorter than its header")
                     : packet_validate(input.data(), input.size(), 0, &header);
      if (code) {
        if (!input.empty())
          sodium_memzero(input.data(), input.size());
        return result(code);
      }
      const bool discovery = !event["discovery"].isUndefined() && event["discovery"].as<bool>();
      const packet_type_t outer_type = static_cast<packet_type_t>(NET_TO_HOST_U16(header.type));
      const size_t header_size = sizeof(packet_header_t);
      if (!browser_client_ready(transport_.get()) && packet_is_handshake_type(outer_type)) {
        code = discovery ? browser_client_acds_handshake_receive(transport_.get(), outer_type,
                                                                 input.data() + header_size, input.size() - header_size)
                         : browser_client_handshake_receive(transport_.get(), outer_type, input.data() + header_size,
                                                            input.size() - header_size);
        if (!input.empty())
          sodium_memzero(input.data(), input.size());
        return code ? result(code) : drainOutputs();
      }
      std::vector<uint8_t> packet(input.begin(), input.end());
      if (outer_type == PACKET_TYPE_ENCRYPTED) {
        std::vector<uint8_t> plaintext(input.size() - header_size + 128);
        size_t plaintext_length = 0;
        code = browser_client_decrypt_packet(transport_.get(), input.data() + header_size, input.size() - header_size,
                                             plaintext.data(), plaintext.size(), &plaintext_length);
        if (!code) {
          packet.assign(plaintext.begin(), plaintext.begin() + plaintext_length);
          if (packet.size() < header_size)
            code = SET_ERRNO(ERROR_INVALID_PARAM, "Decrypted client packet is shorter than its header");
          else
            code = packet_validate(packet.data(), packet.size(), 0, &header);
        }
      }
      if (code) {
        if (!input.empty())
          sodium_memzero(input.data(), input.size());
        return result(code);
      }
      const packet_type_t packet_type = static_cast<packet_type_t>(NET_TO_HOST_U16(header.type));
      auto response = result(0);
      response.set("packet", emscripten::val::object());
      response["packet"].set("type", static_cast<unsigned>(packet_type));
      response["packet"].set("length", NET_TO_HOST_U32(header.length));
      response["packet"].set("client_id", NET_TO_HOST_U32(header.client_id));
      response["packet"].set("crc32", NET_TO_HOST_U32(header.crc32));
      response.set("encrypted", outer_type == PACKET_TYPE_ENCRYPTED);
      response.set("kind", acip_is_media_packet(packet_type)       ? "media"
                           : acip_is_control_packet(packet_type)   ? "control"
                           : packet_is_handshake_type(packet_type) ? "crypto"
                           : acip_is_message_packet(packet_type)   ? "message"
                                                                   : "unknown");
      response.set("payload", binding_copy_output(packet.data() + header_size, packet.size() - header_size));
      if (!input.empty())
        sodium_memzero(input.data(), input.size());
      return response;
    }
    if (type == "resolve-crypto-policy") {
      const int mode = browser_client_resolve_crypto_policy(transport_.get(), event["wss"].as<bool>(),
                                                            event["encryption"].as<int>());
      emscripten::val response = result(mode < 0 ? SET_ERRNO(ERROR_INVALID_PARAM, "Invalid crypto policy") : 0);
      response.set("mode", mode);
      return response;
    }
    if (type == "configure-browser-capabilities") {
      browser_capabilities_.webtransport =
          event["webtransport"].isUndefined() ? false : event["webtransport"].as<bool>();
      browser_capabilities_.datagrams = event["datagrams"].isUndefined() ? false : event["datagrams"].as<bool>();
      const size_t max_datagram_payload =
          event["maxDatagramPayload"].isUndefined() ? 0 : event["maxDatagramPayload"].as<size_t>();
      browser_capabilities_.hevc = event["hevc"].isUndefined() ? false : event["hevc"].as<bool>();
      browser_capabilities_.webrtc = event["webrtc"].isUndefined() ? false : event["webrtc"].as<bool>();
      browser_transport_set_capabilities(transport_.get(), browser_capabilities_.datagrams, max_datagram_payload);
      emscripten::val response = result(0);
      response.set("capabilities", emscripten::val::object());
      response["capabilities"].set("webtransport", browser_capabilities_.webtransport);
      response["capabilities"].set("datagrams", browser_capabilities_.datagrams);
      response["capabilities"].set("hevc", browser_capabilities_.hevc);
      response["capabilities"].set("webrtc", browser_capabilities_.webrtc);
      return response;
    }
    if (type == "set-server") {
      const std::string host = event["host"].as<std::string>();
      const int port = event["port"].as<int>();
      if (host.empty() || port <= 0 || port > 65535)
        return result(SET_ERRNO(ERROR_INVALID_PARAM, "Invalid client server address or port"));
      return result(browser_client_set_server_address(transport_.get(), host.c_str(), port));
    }
    if (type == "quic-event") {
      const std::string operation = event["operation"].as<std::string>();
      if (operation == "init")
        return result(browser_transport_security_init(transport_.get()));
      if (operation == "state") {
        emscripten::val response = result(0);
        response.set("epoch", browser_transport_security_epoch(transport_.get()));
        return response;
      }
      if (operation == "tick") {
        const int code = browser_transport_security_tick(transport_.get());
        emscripten::val response = drainOutputs();
        if (code && response["code"].as<int>() == 0)
          response.set("code", code);
        return response;
      }
      if (operation != "seal" && operation != "open")
        return result(SET_ERRNO(ERROR_INVALID_PARAM, "Unknown QUIC operation: %s", operation.c_str()));
      if (event["packet"].isUndefined() || event["packet"].isNull() || event["flow"].isUndefined() ||
          event["delivery"].isUndefined())
        return result(SET_ERRNO(ERROR_INVALID_PARAM, "QUIC packet, flow, and delivery are required"));
      auto input = binding_copy_input(event["packet"]);
      const bool seal = operation == "seal";
      std::vector<uint8_t> output(input.size() + (seal ? 94 : 0));
      size_t length = 0;
      uint64_t sequence = 0;
      const int code = browser_transport_security_packet(transport_.get(), seal, event["flow"].as<unsigned>(),
                                                         event["delivery"].as<unsigned>(), input.data(), input.size(),
                                                         output.data(), output.size(), &length, &sequence);
      emscripten::val response = result(code);
      response.set("bytes", binding_copy_output(output.data(), code ? 0 : length));
      response.set("sequence", emscripten::val::global("BigInt")(std::to_string(sequence)));
      if (!input.empty())
        sodium_memzero(input.data(), input.size());
      if (!output.empty())
        sodium_memzero(output.data(), output.size());
      return response;
    }
    return result(SET_ERRNO(ERROR_INVALID_PARAM, "Unknown browser client event: %s", type.c_str()));
  }
  emscripten::val drainOutputs() {
    emscripten::val response = result(0);
    int output_code = 0;
    response.set("outputs", browser_outputs(&output_code));
    if (!response["code"].as<int>() && output_code)
      response.set("code", output_code);
    response.set("epoch", browser_transport_security_epoch(transport_.get()));
    response.set("queuedBytes", browser_transport_queued_bytes(transport_.get()));
    response.set("writable", browser_transport_writable(transport_.get(), 0));
    return response;
  }
  emscripten::val processBatch(emscripten::val events) {
    if (!owned_) {
      emscripten::val batch = emscripten::val::array();
      batch.call<void>("push", result(SET_ERRNO(ERROR_INVALID_STATE, "Client session is not initialized")));
      return batch;
    }
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
    response.set("ok", code == 0);
    response.set("code", code);
    response.set("state", static_cast<int>(browser_transport_state(transport_.get())));
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
  emscripten::val browser_outputs(int *output_code) {
    emscripten::val outputs = emscripten::val::array();
    while (browser_transport_output_size(transport_.get())) {
      const size_t length = browser_transport_output_size(transport_.get());
      emscripten::val output = emscripten::val::object();
      output.set("flow", 0);
      output.set("packet", binding_copy_output(browser_transport_output_data(transport_.get()), length));
      outputs.call<void>("push", output);
      browser_transport_output_pop(transport_.get());
    }
    if (browser_transport_base(transport_.get())->crypto_ctx) {
      for (unsigned index = 0; index < 32; ++index) {
        uint8_t data[256];
        size_t length = 0;
        uint32_t flow = 0;
        const int code = browser_transport_security_output(transport_.get(), data, sizeof(data), &length, &flow);
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
    }
    return outputs;
  }
  std::unique_ptr<browser_transport_t, decltype(&browser_transport_destroy)> transport_{nullptr,
                                                                                        &browser_transport_destroy};
  struct {
    bool webtransport = false;
    bool datagrams = false;
    bool hevc = false;
    bool webrtc = false;
  } browser_capabilities_;
  bool owned_ = false;
  int result_ = 0;
};
EMSCRIPTEN_BINDINGS(ascii_chat_network) {
  emscripten::register_type<ClientBytes>("ClientBytes", "Uint8Array");
  emscripten::class_<ClientSessionBinding>("ClientSessionBinding")
      .constructor<OptionsPatch>()
      .class_function("errorAction", &ClientSessionBinding::errorAction)
      .function("valid", &ClientSessionBinding::valid)
      .function("protocolInfo", &ClientSessionBinding::protocolInfo)
      .function("error", &ClientSessionBinding::error)
      .function("close", &ClientSessionBinding::close)
      .function("process", &ClientSessionBinding::process)
      .function("processBatch", &ClientSessionBinding::processBatch);
}
