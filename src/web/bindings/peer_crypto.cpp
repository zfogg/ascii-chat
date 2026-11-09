#include <emscripten/bind.h>
extern "C" {
#include "../peer_crypto.h"
#include "../client.h"
#include "../security.h"
}
#include "networking.h"
#include <sodium.h>
#include "byte_arrays.h"

#include <memory>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

EMSCRIPTEN_DECLARE_VAL_TYPE(NullablePeerBytes);

static int configure_crypto(PeerBytes password, PeerBytes identity, PeerBytes expected) {
  auto pass = binding_copy_input(password);
  auto secret = binding_copy_input(identity);
  auto key = binding_copy_input(expected);
  int result;
  if ((secret.empty() || secret.size() == 64) && (key.empty() || key.size() == 32)) {
    pass.push_back(0);
    result =
        client_configure_crypto_options(pass.size() > 1 ? reinterpret_cast<char *>(pass.data()) : nullptr,
                                        secret.empty() ? nullptr : secret.data(), key.empty() ? nullptr : key.data());
  } else {
    result = SET_ERRNO(ERROR_INVALID_PARAM, "Invalid client identity or verification key length");
  }
  if (!pass.empty())
    sodium_memzero(pass.data(), pass.size());
  if (!secret.empty())
    sodium_memzero(secret.data(), secret.size());
  if (!key.empty())
    sodium_memzero(key.data(), key.size());
  return result;
}

class PeerCryptoBinding {
public:
  PeerCryptoBinding(bool server, int encryption, PeerBytes password, PeerBytes identity, PeerBytes expected) {
    auto pass = binding_copy_input(password);
    auto secret = binding_copy_input(identity);
    auto key = binding_copy_input(expected);
    if ((!secret.empty() && secret.size() != 64) || (!key.empty() && key.size() != 32)) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Invalid peer identity or verification key length");
    } else {
      pass.push_back(0);
      peer_.reset(peer_crypto_create(server, encryption,
                                     pass.size() > 1 ? reinterpret_cast<char *>(pass.data()) : nullptr,
                                     secret.empty() ? nullptr : secret.data(), key.empty() ? nullptr : key.data()));
    }
    if (!pass.empty())
      sodium_memzero(pass.data(), pass.size());
    if (!secret.empty())
      sodium_memzero(secret.data(), secret.size());
    if (!key.empty())
      sodium_memzero(key.data(), key.size());
  }
  PeerCryptoBinding(const PeerCryptoBinding &) = delete;
  PeerCryptoBinding &operator=(const PeerCryptoBinding &) = delete;

  bool valid() const {
    return static_cast<bool>(peer_);
  }
  bool ready() const {
    return peer_ && peer_crypto_ready(peer_.get());
  }
  std::string error() const {
    return peer_crypto_error();
  }
  emscripten::val process(emscripten::val event) {
    emscripten::val result = emscripten::val::object();
    result.set("code", 0);
    const std::string type = event["type"].as<std::string>();
    if (type == "start") {
      result.set("code", static_cast<int>(peer_crypto_start(peer_.get())));
    } else if (type == "send" || type == "receive") {
      auto bytes = binding_copy_input(event["packet"]);
      const int code = input_bytes(bytes, type == "receive");
      result.set("code", code);
      if (!bytes.empty()) sodium_memzero(bytes.data(), bytes.size());
    } else if (type == "tick") {
      result.set("code", static_cast<int>(peer_crypto_tick(peer_.get())));
    } else if (type == "quic-begin") {
      result.set("code", peer_crypto_quic_begin(peer_.get()));
    } else if (type == "quic-init") {
      result.set("code", static_cast<int>(browser_security_init(peer_security_context(peer_.get()))));
    } else if (type == "quic-tick") {
      result.set("code", static_cast<int>(browser_security_tick(peer_security_context(peer_.get()))));
      result.set("epoch", browser_security_epoch(peer_security_context(peer_.get())));
      emscripten::val quic_outputs = emscripten::val::array();
      for (unsigned i = 0; i < 16; ++i) {
        auto output = network_quic_output(peer_security_context(peer_.get()));
        if (output["packet"].isNull()) break;
        quic_outputs.call<void>("push", output);
      }
      result.set("quicOutputs", quic_outputs);
    } else if (type == "quic-epoch") {
      result.set("epoch", browser_security_epoch(peer_security_context(peer_.get())));
    } else if (type == "quic-packet") {
      auto packet = binding_copy_input(event["packet"]);
      const bool seal = event["seal"].as<bool>();
      auto packet_result = network_quic_packet(peer_security_context(peer_.get()), seal, event["flow"].as<unsigned>(),
                                               event["delivery"].as<unsigned>(), PeerBytes(event["packet"]));
      emscripten::val packet_value = packet_result;
      result.set("code", packet_value["code"]);
      result.set("packet", packet_value["packet"]);
      result.set("sequence", packet_value["sequence"]);
      if (!packet.empty()) sodium_memzero(packet.data(), packet.size());
    } else {
      result.set("code", SET_ERRNO(ERROR_INVALID_PARAM, "Unknown peer crypto event"));
    }
    emscripten::val outputs = emscripten::val::array();
    for (unsigned i = 0; i < 16; ++i) {
      const void *data = peer_crypto_output_data(peer_.get());
      if (!data) break;
      outputs.call<void>("push", binding_copy_output(data, peer_crypto_output_size(peer_.get())));
      peer_crypto_output_pop(peer_.get());
    }
    result.set("outputs", outputs);
    const void *received = peer_crypto_received_data(peer_.get());
    const size_t received_size = peer_crypto_received_size(peer_.get());
    result.set("received", received && received_size ? binding_copy_output(received, received_size)
                                                       : emscripten::val::null());
    return result;
  }

private:
  int input_bytes(const std::vector<uint8_t> &bytes, bool receive) {
    return receive ? peer_crypto_receive(peer_.get(), bytes.data(), bytes.size())
                   : peer_crypto_send(peer_.get(), bytes.data(), bytes.size());
  }
  std::unique_ptr<browser_peer_crypto_t, decltype(&peer_crypto_destroy)> peer_{nullptr, &peer_crypto_destroy};
};

EMSCRIPTEN_BINDINGS(ascii_chat_peer_crypto) {
  emscripten::register_type<PeerBytes>("PeerBytes", "Uint8Array");
  emscripten::register_type<NullablePeerBytes>("NullablePeerBytes", "Uint8Array | null");
  emscripten::function("configureCrypto", &configure_crypto);
  emscripten::class_<PeerCryptoBinding>("PeerCryptoBinding")
      .constructor<bool, int, PeerBytes, PeerBytes, PeerBytes>()
      .function("valid", &PeerCryptoBinding::valid)
      .function("ready", &PeerCryptoBinding::ready)
      .function("error", &PeerCryptoBinding::error)
      .function("process", &PeerCryptoBinding::process);
}
