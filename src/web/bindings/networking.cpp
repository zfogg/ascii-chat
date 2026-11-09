#include <emscripten/bind.h>
extern "C" {
#include "../client.h"
#include "../security.h"
#include <ascii-chat/network/errors.h>
}
#include "networking.h"
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

static int init_client(OptionsPatch options) {
  int result = client_init();
  if (result)
    SET_ERRNO(ERROR_INIT, "Browser client initialization failed");
  return result ? result : initial_options(options);
}

static int initialize_client_options(OptionsPatch options) {
  return init_client(options);
}

// One JS-owned lifetime controls the module-global client handshake state.
class ClientSessionBinding {
public:
  explicit ClientSessionBinding(OptionsPatch options) {
    if (active_) {
      result_ = SET_ERRNO(ERROR_INVALID_STATE, "Client session is already initialized");
      return;
    }
    result_ = init_client(options);
    if (!result_) {
      active_ = true;
      owned_ = true;
    }
  }
  ~ClientSessionBinding() {
    if (owned_) {
      client_cleanup();
      active_ = false;
    }
  }
  ClientSessionBinding(const ClientSessionBinding &) = delete;
  ClientSessionBinding &operator=(const ClientSessionBinding &) = delete;
  bool valid() const {
    return owned_;
  }
  int error() const {
    return result_;
  }
  emscripten::val process(emscripten::val event) {
    if (!owned_)
      return result(SET_ERRNO(ERROR_INVALID_STATE, "Client session is not initialized"));
    const std::string type = event["type"].as<std::string>();
    if (type == "begin-handshake") {
      const bool discovery = !event["discovery"].isUndefined() && event["discovery"].as<bool>();
      const int code = discovery
                           ? static_cast<int>(client_acds_handshake_begin(event["mode"].as<uint8_t>()))
                           : static_cast<int>(client_handshake_begin());
      return result(code);
    }
    if (type == "state")
      return result(0);
    if (type == "generate-keypair") {
      const int code = client_generate_keypair();
      emscripten::val response = result(code);
      const uint8_t *key = client_public_key_bytes();
      if (!code && key) {
        std::array<char, 65> hex{};
        sodium_bin2hex(hex.data(), hex.size(), key, 32);
        response.set("publicKey", std::string(hex.data()));
      }
      return response;
    }
    if (type == "resolve-crypto-policy") {
      const int mode = client_resolve_crypto_policy(event["wss"].as<bool>(), event["encryption"].as<int>());
      emscripten::val response = result(mode < 0 ? SET_ERRNO(ERROR_INVALID_PARAM, "Invalid crypto policy") : 0);
      response.set("mode", mode);
      return response;
    }
    if (type == "receive-handshake") {
      auto bytes = binding_copy_input(event["payload"]);
      const unsigned packet_type = event["packetType"].as<unsigned>();
      const bool discovery = !event["discovery"].isUndefined() && event["discovery"].as<bool>();
      uint8_t empty = 0;
      const void *data = bytes.empty() ? &empty : bytes.data();
      const int code = discovery ? client_acds_handshake_receive(packet_type, data, bytes.size())
                                 : client_handshake_receive(packet_type, data, bytes.size());
      if (!bytes.empty())
        sodium_memzero(bytes.data(), bytes.size());
      return result(code);
    }
    if (type == "handle-handshake-stage") {
      auto bytes = binding_copy_input(event["packet"]);
      const int stage = event["stage"].as<int>();
      int code = -1;
      switch (stage) {
      case 0:
        code = client_handle_crypto_parameters(bytes.data(), bytes.size());
        break;
      case 1:
        code = client_handle_key_exchange_init(bytes.data(), bytes.size());
        break;
      case 2:
        code = client_handle_auth_challenge(bytes.data(), bytes.size());
        break;
      case 3:
        code = client_handle_handshake_complete(bytes.data(), bytes.size());
        break;
      default:
        code = SET_ERRNO(ERROR_INVALID_PARAM, "Invalid handshake stage: %d", stage);
        break;
      }
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
      const int code = encrypt ? client_encrypt_packet(input.data(), input.size(), output.data(), output.size(), &length)
                               : client_decrypt_packet(input.data(), input.size(), output.data(), output.size(), &length);
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
      return result(client_set_server_address(host.c_str(), port));
    }
    if (type == "quic-init")
      return result(browser_security_init(client_security_context()));
    if (type == "quic-state") {
      emscripten::val response = result(0);
      response.set("epoch", browser_security_epoch(client_security_context()));
      return response;
    }
    if (type == "quic-tick") {
      const int code = browser_security_tick(client_security_context());
      emscripten::val response = result(code);
      response.set("outputs", quic_outputs(client_security_context()));
      response.set("epoch", browser_security_epoch(client_security_context()));
      return response;
    }
    if (type == "quic-packet") {
      auto input = binding_copy_input(event["packet"]);
      const bool seal = event["operation"].as<std::string>() == "seal";
      std::vector<uint8_t> output(input.size() + (seal ? 94 : 0));
      size_t length = 0;
      uint64_t sequence = 0;
      const int code = browser_security_packet(client_security_context(), seal, event["flow"].as<unsigned>(),
                                               event["delivery"].as<unsigned>(), input.data(), input.size(),
                                               output.data(), output.size(), &length, &sequence);
      emscripten::val response = result(code);
      response.set("bytes", binding_copy_output(output.data(), code ? 0 : length));
      response.set("sequence", emscripten::val::global("BigInt")(std::to_string(sequence)));
      return response;
    }
    return result(SET_ERRNO(ERROR_INVALID_PARAM, "Unknown browser client event: %s", type.c_str()));
  }

private:
  static emscripten::val result(int code) {
    emscripten::val response = emscripten::val::object();
    response.set("code", code);
    response.set("state", client_get_connection_state());
    return response;
  }
  static emscripten::val quic_outputs(crypto_context_t *context) {
    emscripten::val outputs = emscripten::val::array();
    for (unsigned index = 0; index < 32; ++index) {
      uint8_t data[256];
      size_t length = 0;
      uint32_t flow = 0;
      const int code = browser_security_output(context, data, sizeof(data), &length, &flow);
      if (code) {
        outputs.call<void>("push", result(code));
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
  static bool active_;
  bool owned_ = false;
  int result_ = 0;
};
bool ClientSessionBinding::active_ = false;

static QuicControlResult take_quic_output(crypto_context_t *context) {
  uint8_t data[256];
  size_t length = 0;
  uint32_t flow = 0;
  const int code = browser_security_output(context, data, sizeof(data), &length, &flow);
  emscripten::val result = emscripten::val::object();
  result.set("code", code);
  result.set("flow", flow);
  result.set("packet", length ? binding_copy_output(data, length) : emscripten::val::null());
  return QuicControlResult(result);
}

static QuicPacketResult process_quic_packet(crypto_context_t *context, bool seal, unsigned flow, unsigned delivery,
                                            PeerBytes packet) {
  auto input_data = binding_copy_input(packet);
  std::vector<uint8_t> output(input_data.size() + (seal ? 94 : 0));
  size_t length = 0;
  uint64_t sequence = 0;
  int code = browser_security_packet(context, seal, flow, delivery, input_data.data(), input_data.size(), output.data(),
                                     output.size(), &length, &sequence);
  emscripten::val result = emscripten::val::object();
  result.set("code", code);
  result.set("packet", binding_copy_output(output.data(), length));
  result.set("sequence", emscripten::val::global("BigInt")(std::to_string(sequence)));
  if (!input_data.empty())
    sodium_memzero(input_data.data(), input_data.size());
  if (!output.empty())
    sodium_memzero(output.data(), output.size());
  return QuicPacketResult(result);
}

int network_peer_quic_begin(browser_peer_crypto_t *peer) {
  return peer_crypto_quic_begin(peer);
}
int network_peer_quic_init(browser_peer_crypto_t *peer) {
  return browser_security_init(peer_security_context(peer));
}
int network_peer_quic_tick(browser_peer_crypto_t *peer) {
  return browser_security_tick(peer_security_context(peer));
}
double network_peer_quic_epoch(browser_peer_crypto_t *peer) {
  return browser_security_epoch(peer_security_context(peer));
}
QuicControlResult network_peer_quic_output(browser_peer_crypto_t *peer) {
  return take_quic_output(peer_security_context(peer));
}
QuicPacketResult network_peer_quic_packet(browser_peer_crypto_t *peer, bool seal, unsigned flow, unsigned delivery,
                                          PeerBytes packet) {
  return process_quic_packet(peer_security_context(peer), seal, flow, delivery, packet);
}

EMSCRIPTEN_BINDINGS(ascii_chat_network) {
  emscripten::class_<ClientSessionBinding>("ClientSessionBinding")
      .constructor<OptionsPatch>()
      .function("valid", &ClientSessionBinding::valid)
      .function("error", &ClientSessionBinding::error)
      .function("process", &ClientSessionBinding::process);

  emscripten::function("initializeClientOptions", &initialize_client_options);

  emscripten::function(
      "network_error_action",
      +[](int code) { return static_cast<int>(network_error_action(static_cast<asciichat_error_t>(code))); });

  emscripten::register_type<QuicControlResult>("QuicControlResult",
                                               "{ code: number; flow: number; packet: Uint8Array | null }");
  emscripten::register_type<QuicPacketResult>("QuicPacketResult",
                                              "{ code: number; sequence: bigint; packet: Uint8Array }");
}
