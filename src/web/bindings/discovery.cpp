#include <emscripten/bind.h>
#include "protocol_bytes.h"
#include "byte_arrays.h"
extern "C" {
#include "../discovery.h"
}
#include <sodium.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

EMSCRIPTEN_DECLARE_VAL_TYPE(OptionalHandoffBytes);

static std::vector<uint8_t> protocol_input(ProtocolBytes input, bool terminate = false) {
  auto bytes = binding_copy_input(input);
  if (terminate)
    bytes.push_back(0);
  return bytes;
}

class DiscoveryBinding {
public:
  DiscoveryBinding() : session_(browser_discovery_new()) {}
  ~DiscoveryBinding() {
    if (session_)
      browser_discovery_destroy(session_);
  }
  DiscoveryBinding(const DiscoveryBinding &) = delete;
  DiscoveryBinding &operator=(const DiscoveryBinding &) = delete;
  bool valid() const {
    return session_ != nullptr;
  }
  uintptr_t callbackToken() const {
    return reinterpret_cast<uintptr_t>(session_);
  }
  int enter(bool create, const std::string &name, ProtocolBytes password) {
    auto secret = protocol_input(password, true);
    int result = browser_discovery_enter(session_, create, name.c_str(), reinterpret_cast<const char *>(secret.data()));
    sodium_memzero(secret.data(), secret.size());
    return result;
  }
  int receive(unsigned type, ProtocolBytes packet) {
    auto data = protocol_input(packet);
    uint8_t empty = 0;
    int result = browser_discovery_receive(session_, type, data.empty() ? &empty : data.data(), data.size());
    if (!data.empty())
      sodium_memzero(data.data(), data.size());
    return result;
  }
  int leave() {
    return browser_discovery_leave(session_);
  }
  void snapshot() {
    browser_discovery_snapshot(session_);
  }

private:
  acds_protocol_t *session_;
};

static OptionalHandoffBytes handoff_encode(int kind, ProtocolBytes session, ProtocolBytes participant,
                                           unsigned generation, unsigned sequence, int capabilities, int nat_tier,
                                           int port) {
  auto session_id = protocol_input(session);
  auto participant_id = protocol_input(participant);
  if (session_id.size() != 16 || participant_id.size() != 16) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Handoff identifiers must be 16 bytes");
    return OptionalHandoffBytes(emscripten::val::null());
  }
  uint8_t buffer[64];
  browser_handoff_encode(kind, session_id.data(), participant_id.data(), generation, sequence, capabilities, nat_tier,
                         port, buffer);
  emscripten::val result = emscripten::val::global("Uint8Array").new_(sizeof(buffer));
  result.call<void>("set", emscripten::val(emscripten::typed_memory_view(sizeof(buffer), buffer)));
  return OptionalHandoffBytes(result);
}
static int handoff_decode(ProtocolBytes packet) {
  auto bytes = protocol_input(packet);
  return browser_handoff_decode(bytes.data(), bytes.size());
}

EMSCRIPTEN_BINDINGS(ascii_chat_discovery) {
  emscripten::register_type<ProtocolBytes>("ProtocolBytes", "Uint8Array");
  emscripten::register_type<OptionalHandoffBytes>("OptionalHandoffBytes", "Uint8Array | null");
  emscripten::class_<DiscoveryBinding>("DiscoveryBinding")
      .constructor<>()
      .function("valid", &DiscoveryBinding::valid)
      .function("callbackToken", &DiscoveryBinding::callbackToken)
      .function("enter", &DiscoveryBinding::enter)
      .function("receive", &DiscoveryBinding::receive)
      .function("leave", &DiscoveryBinding::leave)
      .function("snapshot", &DiscoveryBinding::snapshot);
  emscripten::function("handoffEncode", &handoff_encode);
  emscripten::function("handoffDecode", &handoff_decode);
}
