#include <emscripten/bind.h>
extern "C" {
#include "../peer_crypto.h"
#include "../client.h"
}
#include "network.h"
#include <sodium.h>
#include "byte_arrays.h"

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
      peer_ = nullptr;
    } else {
      pass.push_back(0);
      peer_ = peer_crypto_create(server, encryption, pass.size() > 1 ? reinterpret_cast<char *>(pass.data()) : nullptr,
                                 secret.empty() ? nullptr : secret.data(), key.empty() ? nullptr : key.data());
    }
    if (!pass.empty())
      sodium_memzero(pass.data(), pass.size());
    if (!secret.empty())
      sodium_memzero(secret.data(), secret.size());
    if (!key.empty())
      sodium_memzero(key.data(), key.size());
  }
  ~PeerCryptoBinding() {
    if (peer_)
      peer_crypto_destroy(peer_);
  }
  PeerCryptoBinding(const PeerCryptoBinding &) = delete;
  PeerCryptoBinding &operator=(const PeerCryptoBinding &) = delete;

  bool valid() const {
    return peer_ != nullptr;
  }
  bool ready() const {
    return peer_ && peer_crypto_ready(peer_);
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
    return peer_crypto_tick(peer_);
  }
  int beginQuic() {
    return network_peer_quic_begin(peer_);
  }
  int initQuic() {
    return network_peer_quic_init(peer_);
  }
  int tickQuic() {
    return network_peer_quic_tick(peer_);
  }
  double quicEpoch() {
    return network_peer_quic_epoch(peer_);
  }

  NullablePeerBytes takeOutput() {
    const void *data = peer_crypto_output_data(peer_);
    if (!data)
      return NullablePeerBytes(emscripten::val::null());
    emscripten::val output = binding_copy_output(data, peer_crypto_output_size(peer_));
    peer_crypto_output_pop(peer_);
    return NullablePeerBytes(output);
  }
  NullablePeerBytes received() {
    const void *data = peer_crypto_received_data(peer_);
    const size_t length = peer_crypto_received_size(peer_);
    return NullablePeerBytes(data && length ? binding_copy_output(data, length) : emscripten::val::null());
  }
  QuicControlResult takeQuicOutput() {
    return network_peer_quic_output(peer_);
  }
  QuicPacketResult quicPacket(bool seal, unsigned flow, unsigned delivery, PeerBytes packet) {
    return network_peer_quic_packet(peer_, seal, flow, delivery, packet);
  }

private:
  int input(PeerBytes packet, bool receive) {
    auto bytes = binding_copy_input(packet);
    int result = receive ? peer_crypto_receive(peer_, bytes.data(), bytes.size())
                         : peer_crypto_send(peer_, bytes.data(), bytes.size());
    if (!bytes.empty())
      sodium_memzero(bytes.data(), bytes.size());
    return result;
  }
  browser_peer_crypto_t *peer_ = nullptr;
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
