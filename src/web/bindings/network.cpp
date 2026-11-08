#include <emscripten/bind.h>
extern "C" {
#include "../client.h"
#include "../security.h"
}
#include "network.h"
#include "byte_arrays.h"
#include <sodium.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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

EMSCRIPTEN_BINDINGS(ascii_chat_network) {
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
