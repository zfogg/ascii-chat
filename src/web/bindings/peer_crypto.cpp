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
  int send(PeerBytes packet) {
    return input(packet, false);
  }
  int receive(PeerBytes packet) {
    return input(packet, true);
  }
  int tick() {
    return peer_crypto_tick(peer_.get());
  }
  int beginQuic() {
    return peer_crypto_quic_begin(peer_.get());
  }
  int initQuic() {
    return browser_security_init(peer_security_context(peer_.get()));
  }
  int tickQuic() {
    return browser_security_tick(peer_security_context(peer_.get()));
  }
  double quicEpoch() {
    return browser_security_epoch(peer_security_context(peer_.get()));
  }

  NullablePeerBytes takeOutput() {
    const void *data = peer_crypto_output_data(peer_.get());
    if (!data)
      return NullablePeerBytes(emscripten::val::null());
    emscripten::val output = binding_copy_output(data, peer_crypto_output_size(peer_.get()));
    peer_crypto_output_pop(peer_.get());
    return NullablePeerBytes(output);
  }
  NullablePeerBytes received() {
    const void *data = peer_crypto_received_data(peer_.get());
    const size_t length = peer_crypto_received_size(peer_.get());
    return NullablePeerBytes(data && length ? binding_copy_output(data, length) : emscripten::val::null());
  }
  QuicControlResult takeQuicOutput() {
    return network_quic_output(peer_security_context(peer_.get()));
  }
  QuicPacketResult quicPacket(bool seal, unsigned flow, unsigned delivery, PeerBytes packet) {
    return network_quic_packet(peer_security_context(peer_.get()), seal, flow, delivery, packet);
  }

private:
  int input(PeerBytes packet, bool receive) {
    auto bytes = binding_copy_input(packet);
    int result = receive ? peer_crypto_receive(peer_.get(), bytes.data(), bytes.size())
                         : peer_crypto_send(peer_.get(), bytes.data(), bytes.size());
    if (!bytes.empty())
      sodium_memzero(bytes.data(), bytes.size());
    return result;
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
      .function("send", &PeerCryptoBinding::send)
      .function("receive", &PeerCryptoBinding::receive)
      .function("tick", &PeerCryptoBinding::tick)
      .function("beginQuic", &PeerCryptoBinding::beginQuic)
      .function("initQuic", &PeerCryptoBinding::initQuic)
      .function("tickQuic", &PeerCryptoBinding::tickQuic)
      .function("quicEpoch", &PeerCryptoBinding::quicEpoch)
      .function("takeOutput", &PeerCryptoBinding::takeOutput)
      .function("received", &PeerCryptoBinding::received)
      .function("takeQuicOutput", &PeerCryptoBinding::takeQuicOutput)
      .function("quicPacket", &PeerCryptoBinding::quicPacket);
}
