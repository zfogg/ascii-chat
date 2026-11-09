#include <emscripten/bind.h>
#include "protocol_bytes.h"
#include "byte_arrays.h"
extern "C" {
#include <ascii-chat/network/acip/acds_protocol.h>
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

EMSCRIPTEN_DECLARE_VAL_TYPE(DiscoverySnapshotValue);

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
    close();
  }
  DiscoveryBinding(const DiscoveryBinding &) = delete;
  DiscoveryBinding &operator=(const DiscoveryBinding &) = delete;
  bool valid() const {
    return session_ != nullptr;
  }
  uintptr_t callbackToken() const {
    return reinterpret_cast<uintptr_t>(session_);
  }
  void close() {
    if (!session_)
      return;
    acds_protocol_destroy(session_);
    delete session_;
    session_ = nullptr;
  }
  emscripten::val process(emscripten::val event) {
    if (!session_)
      return result(SET_ERRNO(ERROR_INVALID_STATE, "Discovery session is closed"));
    const std::string type = event["type"].as<std::string>();
    if (type == "enter")
      return result(enter(event["create"].as<bool>(), event["name"].as<std::string>(), ProtocolBytes(event["password"])));
    if (type == "receive")
      return result(receive(event["packetType"].as<unsigned>(), ProtocolBytes(event["packet"])));
    if (type == "leave")
      return result(leave());
    if (type == "snapshot")
      return snapshot();
    return result(SET_ERRNO(ERROR_INVALID_PARAM, "Unknown discovery event"));
  }
  emscripten::val processBatch(emscripten::val events) {
    emscripten::val batch = emscripten::val::array();
    if (!session_) {
      batch.call<void>("push", result(SET_ERRNO(ERROR_INVALID_STATE, "Discovery session is closed")));
      return batch;
    }
    const unsigned count = events["length"].as<unsigned>();
    if (count > 128) {
      batch.call<void>("push", result(SET_ERRNO(ERROR_INVALID_PARAM, "Discovery event batch exceeds 128 events")));
      return batch;
    }
    for (unsigned index = 0; index < count; ++index)
      batch.call<void>("push", process(events[index]));
    return batch;
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

  static emscripten::val result(int code) {
    emscripten::val output = emscripten::val::object();
    output.set("code", code);
    return output;
  }

private:
  acds_protocol_t *session_;
};

EMSCRIPTEN_BINDINGS(ascii_chat_discovery) {
  emscripten::register_type<DiscoverySnapshotValue>(
      "DiscoverySnapshotValue",
      "{ state: number; terminal: boolean; error: string; name: string; joined: { participantId: Uint8Array; "
      "sessionId: Uint8Array; hostId: Uint8Array; turnUsername: string; turnPassword: string; serverAddress: string; "
      "serverPort: number; sessionType: number; quicPort: number; webtransportUrl: string } }");
  emscripten::register_type<ProtocolBytes>("ProtocolBytes", "Uint8Array");
  emscripten::class_<DiscoveryBinding>("DiscoveryBinding")
      .constructor<>()
      .function("valid", &DiscoveryBinding::valid)
      .function("callbackToken", &DiscoveryBinding::callbackToken)
      .function("close", &DiscoveryBinding::close)
      .function("process", &DiscoveryBinding::process)
      .function("processBatch", &DiscoveryBinding::processBatch);
}
