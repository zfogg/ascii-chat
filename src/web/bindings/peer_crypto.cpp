#include <emscripten/bind.h>
extern "C" {
#include "../peer_crypto.h"
#include "../client.h"
#include "../security.h"
}
#include <sodium.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

EMSCRIPTEN_DECLARE_VAL_TYPE(PeerBytes);
EMSCRIPTEN_DECLARE_VAL_TYPE(NullablePeerBytes);
EMSCRIPTEN_DECLARE_VAL_TYPE(QuicControlResult);
EMSCRIPTEN_DECLARE_VAL_TYPE(QuicPacketResult);

static std::vector<uint8_t> copy_input(PeerBytes bytes) {
  const size_t length = bytes["length"].as<size_t>();
  std::vector<uint8_t> copy;
  copy.reserve(length + 1);
  copy.resize(length);
  if (length) {
    emscripten::val view = emscripten::val(emscripten::typed_memory_view(length, copy.data()));
    view.call<void>("set", bytes);
  }
  return copy;
}

static emscripten::val copy_output(const void *data, size_t length) {
  emscripten::val output = emscripten::val::global("Uint8Array").new_(length);
  if (length) {
    emscripten::val view = emscripten::val(emscripten::typed_memory_view(length, static_cast<const uint8_t *>(data)));
    output.call<void>("set", view);
  }
  return output;
}

static int configure_crypto(PeerBytes password, PeerBytes identity, PeerBytes expected) {
  auto pass = copy_input(password);
  auto secret = copy_input(identity);
  auto key = copy_input(expected);
  int result = -1;
  if ((secret.empty() || secret.size() == 64) && (key.empty() || key.size() == 32)) {
    pass.push_back(0);
    result =
        client_configure_crypto_options(pass.size() > 1 ? reinterpret_cast<char *>(pass.data()) : nullptr,
                                        secret.empty() ? nullptr : secret.data(), key.empty() ? nullptr : key.data());
  }
  if (!pass.empty())
    sodium_memzero(pass.data(), pass.size());
  if (!secret.empty())
    sodium_memzero(secret.data(), secret.size());
  if (!key.empty())
    sodium_memzero(key.data(), key.size());
  return result;
}

static QuicControlResult take_quic_output(crypto_context_t *context) {
  uint8_t data[256];
  size_t length = 0;
  uint32_t flow = 0;
  const int code = browser_security_output(context, data, sizeof(data), &length, &flow);
  emscripten::val result = emscripten::val::object();
  result.set("code", code);
  result.set("flow", flow);
  result.set("packet", length ? copy_output(data, length) : emscripten::val::null());
  return QuicControlResult(result);
}

static QuicPacketResult process_quic_packet(crypto_context_t *context, bool seal, unsigned flow, unsigned delivery,
                                            PeerBytes packet) {
  auto input_data = copy_input(packet);
  std::vector<uint8_t> output(input_data.size() + (seal ? 94 : 0));
  size_t length = 0;
  uint64_t sequence = 0;
  int code = browser_security_packet(context, seal, flow, delivery, input_data.data(), input_data.size(), output.data(),
                                     output.size(), &length, &sequence);
  emscripten::val result = emscripten::val::object();
  result.set("code", code);
  result.set("packet", copy_output(output.data(), length));
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

class PeerCryptoBinding {
public:
  PeerCryptoBinding(bool server, int encryption, PeerBytes password, PeerBytes identity, PeerBytes expected) {
    auto pass = copy_input(password);
    auto secret = copy_input(identity);
    auto key = copy_input(expected);
    if ((!secret.empty() && secret.size() != 64) || (!key.empty() && key.size() != 32)) {
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
    return peer_crypto_quic_begin(peer_);
  }
  int initQuic() {
    return browser_security_init(peer_security_context(peer_));
  }
  int tickQuic() {
    return browser_security_tick(peer_security_context(peer_));
  }
  double quicEpoch() {
    return browser_security_epoch(peer_security_context(peer_));
  }

  NullablePeerBytes takeOutput() {
    const void *data = peer_crypto_output_data(peer_);
    if (!data)
      return NullablePeerBytes(emscripten::val::null());
    emscripten::val output = copy_output(data, peer_crypto_output_size(peer_));
    peer_crypto_output_pop(peer_);
    return NullablePeerBytes(output);
  }
  NullablePeerBytes received() {
    const void *data = peer_crypto_received_data(peer_);
    const size_t length = peer_crypto_received_size(peer_);
    return NullablePeerBytes(data && length ? copy_output(data, length) : emscripten::val::null());
  }
  QuicControlResult takeQuicOutput() {
    return take_quic_output(peer_security_context(peer_));
  }
  QuicPacketResult quicPacket(bool seal, unsigned flow, unsigned delivery, PeerBytes packet) {
    return process_quic_packet(peer_security_context(peer_), seal, flow, delivery, packet);
  }

private:
  int input(PeerBytes packet, bool receive) {
    auto bytes = copy_input(packet);
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
  emscripten::register_type<QuicControlResult>("QuicControlResult",
                                               "{ code: number; flow: number; packet: Uint8Array | null }");
  emscripten::register_type<QuicPacketResult>("QuicPacketResult",
                                              "{ code: number; sequence: bigint; packet: Uint8Array }");
  emscripten::function("configureCrypto", &configure_crypto);
  emscripten::function("clientQuicInit", &client_quic_init);
  emscripten::function("clientQuicTick", &client_quic_tick);
  emscripten::function("clientQuicEpoch", &client_quic_epoch);
  emscripten::function("clientQuicOutput", &client_quic_output);
  emscripten::function("clientQuicPacket", &client_quic_packet);
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
