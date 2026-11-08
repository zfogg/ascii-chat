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

EM_JS(void, handoff_snapshot, (int kind, const uint8_t *id, uint32_t generation, uint32_t sequence),
      { Module.handoffResult = {kind, participant : HEAPU8.slice(id, id + 16), generation, sequence}; });

EM_JS(void, discovery_send, (acds_protocol_t * p, int type, const void *data, size_t length),
      { Module.discoverySend(p, type, HEAPU8.slice(data, data + length)); });
static asciichat_error_t browser_send(void *p, packet_type_t type, const void *payload, size_t length) {
  discovery_send(static_cast<acds_protocol_t *>(p), type, payload, length);
  return ASCIICHAT_OK;
}

EM_JS(void, discovery_snapshot,
      (int state, bool terminal, const char *error, const char *name, const uint8_t *participant,
       const uint8_t *session, const uint8_t *host, const char *user, const char *password, const char *address,
       unsigned port, unsigned session_type, unsigned quic_port, const char *webtransport_url),
      {
        Module.discoveryResult = {
          state,
          terminal : !!terminal,
          error : UTF8ToString(error),
          name : UTF8ToString(name),
          joined : {
            participantId : HEAPU8.slice(participant, participant + 16),
            sessionId : HEAPU8.slice(session, session + 16),
            hostId : HEAPU8.slice(host, host + 16),
            turnUsername : UTF8ToString(user),
            turnPassword : UTF8ToString(password),
            serverAddress : UTF8ToString(address),
            serverPort : port,
            sessionType : session_type,
            quicPort : quic_port,
            webtransportUrl : UTF8ToString(webtransport_url)
          }
        };
      });
static void discovery_snapshot_values(acds_protocol_t *p) {
  discovery_snapshot(p->state, p->terminal, p->error, p->name, p->joined.participant_id, p->joined.session_id,
                     p->joined.host_established ? p->joined.host_id : p->joined.initiator_id, p->joined.turn_username,
                     p->joined.turn_password, p->joined.server_address, p->joined.server_port, p->joined.session_type,
                     p->quic_port, p->webtransport_url);
}

EMSCRIPTEN_DECLARE_VAL_TYPE(OptionalHandoffBytes);

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
  void snapshot() {
    discovery_snapshot_values(session_);
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
static int handoff_decode(ProtocolBytes packet) {
  auto bytes = protocol_input(packet);
  consensus_handoff_message_t message{};
  const int result = consensus_handoff_decode(bytes.data(), bytes.size(), &message);
  if (result == ASCIICHAT_OK)
    handoff_snapshot(message.kind, message.participant, message.generation, message.sequence);
  return result;
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
