#include <emscripten/bind.h>
#include "protocol_bytes.h"
#include "byte_arrays.h"
extern "C" {
#include <ascii-chat/network/acip/acds_protocol.h>
#include <ascii-chat/common.h>
}
#include <ascii-chat/network/consensus/handoff.h>
#include <sodium.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

EMSCRIPTEN_DECLARE_VAL_TYPE(DiscoverySnapshotValue);

class DiscoveryBinding;
static asciichat_error_t browser_send(void *user_data, packet_type_t type, const void *payload, size_t length);

static std::vector<uint8_t> protocol_input(ProtocolBytes input, bool terminate = false) {
  auto bytes = binding_copy_input(input);
  if (terminate)
    bytes.push_back(0);
  return bytes;
}

class HandoffBinding {
public:
  HandoffBinding(ProtocolBytes session, ProtocolBytes participant) {
    session_ = binding_copy_input(session);
    participant_ = binding_copy_input(participant);
    if (session_.size() != SESSION_ID_SIZE || participant_.size() != SESSION_ID_SIZE) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Handoff identifiers must be 16 bytes");
      session_.clear();
      participant_.clear();
    }
  }
  HandoffBinding(const HandoffBinding &) = delete;
  HandoffBinding &operator=(const HandoffBinding &) = delete;
  bool valid() const {
    return session_.size() == SESSION_ID_SIZE && participant_.size() == SESSION_ID_SIZE;
  }
  int error() const {
    return valid() ? 0 : static_cast<int>(ERROR_INVALID_PARAM);
  }
  void close() {
    session_.clear();
    participant_.clear();
  }
  emscripten::val process(emscripten::val event) {
    if (!valid())
      return result(SET_ERRNO(ERROR_INVALID_STATE, "Handoff session is closed"));
    const std::string type = event["type"].as<std::string>();
    if (type == "sample")
      return sample();
    if (type == "receive")
      return receive(ProtocolBytes(event["packet"]));
    if (type == "ack") {
      const auto bytes = encode(static_cast<uint8_t>(event["kind"].as<int>()), event["generation"].as<unsigned>(),
                                event["sequence"].as<unsigned>());
      emscripten::val response = result(0);
      response.set("bytes", binding_copy_output(bytes.data(), bytes.size()));
      return response;
    }
    return result(SET_ERRNO(ERROR_INVALID_PARAM, "Unknown handoff event: %s", type.c_str()));
  }

private:
  emscripten::val sample() {
    const unsigned sequence = ++sequence_;
    const auto first = encode(HANDOFF_SAMPLE_START, 0, sequence);
    const auto second = encode(HANDOFF_SAMPLE_DATA, 0, sequence);
    std::vector<uint8_t> probe(CONSENSUS_HANDOFF_HEADER + 65536);
    memcpy(probe.data(), second.data(), second.size());
    emscripten::val outputs = emscripten::val::array();
    outputs.call<void>("push", binding_copy_output(first.data(), first.size()));
    outputs.call<void>("push", binding_copy_output(probe.data(), probe.size()));
    emscripten::val response = result(0);
    response.set("outputs", outputs);
    response.set("sequence", sequence);
    return response;
  }
  emscripten::val receive(ProtocolBytes input) {
    auto bytes = binding_copy_input(input);
    consensus_handoff_message_t message{};
    const int code = consensus_handoff_decode(bytes.data(), bytes.size(), &message);
    if (code)
      return result(code);
    emscripten::val response = result(0);
    if (memcmp(message.session, session_.data(), SESSION_ID_SIZE) != 0 || message.generation < generation_) {
      response.set("handled", true);
      return response;
    }
    generation_ = message.generation;
    emscripten::val action = emscripten::val::object();
    action.set("kind", message.kind);
    action.set("participant", binding_copy_output(message.participant, SESSION_ID_SIZE));
    action.set("generation", message.generation);
    action.set("sequence", message.sequence);
    response.set("handled", true);
    response.set("action", action);
    return response;
  }
  std::vector<uint8_t> encode(uint8_t kind, unsigned generation, unsigned sequence) const {
    consensus_handoff_message_t message{};
    message.kind = kind;
    message.generation = generation;
    message.sequence = sequence;
    message.capabilities = 2;
    message.nat_tier = 255;
    memcpy(message.session, session_.data(), SESSION_ID_SIZE);
    memcpy(message.participant, participant_.data(), SESSION_ID_SIZE);
    std::vector<uint8_t> bytes(CONSENSUS_HANDOFF_HEADER);
    consensus_handoff_encode(&message, bytes.data());
    return bytes;
  }
  static emscripten::val result(int code) {
    emscripten::val output = emscripten::val::object();
    output.set("code", code);
    return output;
  }
  std::vector<uint8_t> session_;
  std::vector<uint8_t> participant_;
  unsigned generation_ = 0;
  unsigned sequence_ = 0;
};

class DiscoveryBinding {
public:
  DiscoveryBinding() : session_(new acds_protocol_t{}) {
    acds_protocol_init(session_, browser_send, this);
  }
  ~DiscoveryBinding() {
    close();
  }
  DiscoveryBinding(const DiscoveryBinding &) = delete;
  DiscoveryBinding &operator=(const DiscoveryBinding &) = delete;
  bool valid() const {
    return session_ != nullptr;
  }
  int error() const {
    return session_ ? 0 : static_cast<int>(ERROR_INVALID_STATE);
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
      return result(
          enter(event["create"].as<bool>(), event["name"].as<std::string>(), ProtocolBytes(event["password"])));
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
  emscripten::val drainOutputs() {
    emscripten::val outputs = emscripten::val::array();
    for (const auto &output : outgoing_) {
      emscripten::val item = emscripten::val::object();
      item.set("type", static_cast<unsigned>(output.first));
      item.set("payload", binding_copy_output(output.second.data(), output.second.size()));
      outputs.call<void>("push", item);
    }
    outgoing_.clear();
    return outputs;
  }
  void enqueue(packet_type_t type, const void *payload, size_t length) {
    const auto *bytes = static_cast<const uint8_t *>(payload);
    outgoing_.emplace_back(type, std::vector<uint8_t>(bytes, bytes + length));
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
    result.set("ok", !session_->terminal || session_->error[0] == '\0');
    result.set("terminal", session_->terminal);
    result.set("error", std::string(session_->error));
    result.set("name", std::string(session_->name));
    result.set("joined", member);
    return DiscoverySnapshotValue(result);
  }

  static emscripten::val result(int code) {
    emscripten::val output = emscripten::val::object();
    output.set("ok", code == 0);
    output.set("code", code);
    return output;
  }

private:
  acds_protocol_t *session_;
  std::vector<std::pair<packet_type_t, std::vector<uint8_t>>> outgoing_;
};

static asciichat_error_t browser_send(void *user_data, packet_type_t type, const void *payload, size_t length) {
  auto *binding = static_cast<DiscoveryBinding *>(user_data);
  binding->enqueue(type, payload, length);
  return ASCIICHAT_OK;
}

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
      .function("error", &DiscoveryBinding::error)
      .function("close", &DiscoveryBinding::close)
      .function("process", &DiscoveryBinding::process)
      .function("processBatch", &DiscoveryBinding::processBatch)
      .function("drainOutputs", &DiscoveryBinding::drainOutputs);
  emscripten::class_<HandoffBinding>("HandoffBinding")
      .constructor<ProtocolBytes, ProtocolBytes>()
      .function("valid", &HandoffBinding::valid)
      .function("error", &HandoffBinding::error)
      .function("close", &HandoffBinding::close)
      .function("process", &HandoffBinding::process);
}
