#include <emscripten/bind.h>
#include "protocol_bytes.h"
#include "byte_arrays.h"
extern "C" {
#include <ascii-chat/network/acip/acds_protocol.h>
#include <ascii-chat/network/consensus/handoff.h>
#include <ascii-chat/common.h>
}
#include <emscripten.h>
#include <sodium.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

EM_JS(void, discovery_send, (acds_protocol_t * p, int type, const void *data, size_t length),
      { Module.discoverySend(p, type, HEAPU8.slice(data, data + length)); });
static asciichat_error_t browser_send(void *p, packet_type_t type, const void *payload, size_t length) {
  discovery_send(static_cast<acds_protocol_t *>(p), type, payload, length);
  return ASCIICHAT_OK;
}

EMSCRIPTEN_DECLARE_VAL_TYPE(OptionalHandoffBytes);
EMSCRIPTEN_DECLARE_VAL_TYPE(DiscoverySnapshotValue);
EMSCRIPTEN_DECLARE_VAL_TYPE(HandoffActionValue);

static std::vector<uint8_t> protocol_input(ProtocolBytes input, bool terminate = false) {
  auto bytes = binding_copy_input(input);
  if (terminate)
    bytes.push_back(0);
  return bytes;
}

class DiscoveryBinding {
public:
  DiscoveryBinding() : session_(new acds_protocol_t{}) {
    acds_protocol_init(session_, browser_send, session_);
  }
  ~DiscoveryBinding() {
    if (session_)
      acds_protocol_destroy(session_);
    delete session_;
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
    int result = acds_protocol_enter(session_, create, name.c_str(), reinterpret_cast<const char *>(secret.data()));
    sodium_memzero(secret.data(), secret.size());
    return result;
  }
  int receive(unsigned type, ProtocolBytes packet) {
    auto data = protocol_input(packet);
    uint8_t empty = 0;
    int result = acds_protocol_receive(session_, static_cast<packet_type_t>(type), data.empty() ? &empty : data.data(),
                                       data.size());
    if (!data.empty())
      sodium_memzero(data.data(), data.size());
    return result;
  }
  int leave() {
    return acds_protocol_leave(session_);
  }
  DiscoverySnapshotValue snapshot() {
    const auto &joined = session_->joined;
    emscripten::val member = emscripten::val::object();
    member.set("participantId", binding_copy_output(joined.participant_id, SESSION_ID_SIZE));
    member.set("sessionId", binding_copy_output(joined.session_id, SESSION_ID_SIZE));
    member.set("hostId",
               binding_copy_output(joined.host_established ? joined.host_id : joined.initiator_id, SESSION_ID_SIZE));
    member.set("turnUsername", std::string(joined.turn_username));
    member.set("turnPassword", std::string(joined.turn_password));
    member.set("serverAddress", std::string(joined.server_address));
    member.set("serverPort", joined.server_port);
    member.set("sessionType", joined.session_type);
    member.set("quicPort", session_->quic_port);
    member.set("webtransportUrl", std::string(session_->webtransport_url));
    emscripten::val result = emscripten::val::object();
    result.set("state", static_cast<int>(session_->state));
    result.set("terminal", session_->terminal);
    result.set("error", std::string(session_->error));
    result.set("name", std::string(session_->name));
    result.set("joined", member);
    return DiscoverySnapshotValue(result);
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
  consensus_handoff_message_t message{};
  message.kind = static_cast<uint8_t>(kind);
  message.generation = generation;
  message.sequence = sequence;
  message.capabilities = static_cast<uint8_t>(capabilities);
  message.nat_tier = static_cast<uint8_t>(nat_tier);
  message.port = static_cast<uint16_t>(port);
  memcpy(message.session, session_id.data(), SESSION_ID_SIZE);
  memcpy(message.participant, participant_id.data(), SESSION_ID_SIZE);
  consensus_handoff_encode(&message, buffer);
  emscripten::val result = emscripten::val::global("Uint8Array").new_(sizeof(buffer));
  result.call<void>("set", emscripten::val(emscripten::typed_memory_view(sizeof(buffer), buffer)));
  return OptionalHandoffBytes(result);
}
static HandoffActionValue handoff_decode(ProtocolBytes packet) {
  auto bytes = protocol_input(packet);
  consensus_handoff_message_t message{};
  if (consensus_handoff_decode(bytes.data(), bytes.size(), &message) != ASCIICHAT_OK)
    return HandoffActionValue(emscripten::val::null());
  emscripten::val result = emscripten::val::object();
  result.set("kind", message.kind);
  result.set("participant", binding_copy_output(message.participant, SESSION_ID_SIZE));
  result.set("generation", message.generation);
  result.set("sequence", message.sequence);
  return HandoffActionValue(result);
}

EMSCRIPTEN_BINDINGS(ascii_chat_discovery) {
  emscripten::register_type<DiscoverySnapshotValue>(
      "DiscoverySnapshotValue",
      "{ state: number; terminal: boolean; error: string; name: string; joined: { participantId: Uint8Array; "
      "sessionId: Uint8Array; hostId: Uint8Array; turnUsername: string; turnPassword: string; serverAddress: string; "
      "serverPort: number; sessionType: number; quicPort: number; webtransportUrl: string } }");
  emscripten::register_type<HandoffActionValue>(
      "HandoffActionValue", "{ kind: number; participant: Uint8Array; generation: number; sequence: number } | null");
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
