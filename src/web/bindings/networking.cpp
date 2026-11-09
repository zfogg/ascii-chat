#include <emscripten/bind.h>
extern "C" {
#include "../client.h"
#include "../security.h"
#include <ascii-chat/network/errors.h>
}
#include "networking.h"
#include "../common/opts.h"
#include "byte_arrays.h"
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

private:
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

static int client_quic_init() {
  return browser_security_init(client_security_context());
}
static int client_quic_tick() {
  return browser_security_tick(client_security_context());
}
static double client_quic_epoch() {
  return browser_security_epoch(client_security_context());
}
static QuicControlResult client_quic_output() {
  return take_quic_output(client_security_context());
}
static QuicPacketResult client_quic_packet(bool seal, unsigned flow, unsigned delivery, PeerBytes packet) {
  return process_quic_packet(client_security_context(), seal, flow, delivery, packet);
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

static int set_server_address(const std::string &host, int port) {
  if (host.empty() || port <= 0 || port > 65535)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid client server address or port");
  const int result = client_set_server_address(host.c_str(), port);
  if (result)
    SET_ERRNO(ERROR_INVALID_STATE, "Cannot set client server address");
  return result;
}
static std::optional<std::string> client_public_key() {
  const uint8_t *key = client_public_key_bytes();
  if (!key) {
    SET_ERRNO(ERROR_CRYPTO_KEY, "Cannot get client public key");
    return std::nullopt;
  }
  std::array<char, 65> hex{};
  sodium_bin2hex(hex.data(), hex.size(), key, 32);
  return std::string(hex.data());
}

EMSCRIPTEN_BINDINGS(ascii_chat_network) {
  emscripten::function("initClient", &init_client);
  emscripten::class_<ClientSessionBinding>("ClientSessionBinding")
      .constructor<OptionsPatch>()
      .function("valid", &ClientSessionBinding::valid)
      .function("error", &ClientSessionBinding::error);

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
  emscripten::function("setServerAddress", &set_server_address);
  emscripten::function("clientPublicKey", &client_public_key);

  emscripten::register_type<QuicControlResult>("QuicControlResult",
                                               "{ code: number; flow: number; packet: Uint8Array | null }");
  emscripten::register_type<QuicPacketResult>("QuicPacketResult",
                                              "{ code: number; sequence: bigint; packet: Uint8Array }");
  emscripten::function("clientQuicInit", &client_quic_init);
  emscripten::function("clientQuicTick", &client_quic_tick);
  emscripten::function("clientQuicEpoch", &client_quic_epoch);
  emscripten::function("clientQuicOutput", &client_quic_output);
  emscripten::function("clientQuicPacket", &client_quic_packet);
}
