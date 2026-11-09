#include <emscripten/bind.h>
#include <emscripten.h>
#include "protocol_bytes.h"
extern "C" {
#include <ascii-chat/common.h>
#include <ascii-chat/network/acip/acds.h>
#include <ascii-chat/network/acip/acds_client.h>
#include <ascii-chat/network/acip/transport.h>
#include "session/host.h"
}
#include <array>
#include <cstdint>
#include <vector>

EM_JS(void, browser_host_send, (uint32_t client, const uint8_t *data, size_t length), {
  if (Module.hostPacketCallback)
    Module.hostPacketCallback(client, HEAPU8.slice(data, data + length));
});

class BrowserHostBinding;
struct HostTransportContext {
  BrowserHostBinding *owner = nullptr;
  uint32_t id = 0;
  acip_transport_t transport{};
};

class BrowserHostBinding {
public:
  BrowserHostBinding() { create(); }
  ~BrowserHostBinding() { close(); }
  BrowserHostBinding(const BrowserHostBinding &) = delete;
  BrowserHostBinding &operator=(const BrowserHostBinding &) = delete;

  bool valid() const { return host_ != nullptr; }
  int error() const { return host_ ? 0 : static_cast<int>(ERROR_INVALID_STATE); }
  void close() {
    session_host_destroy(host_);
    host_ = nullptr;
    for (auto &client : clients_) {
      client.owner = nullptr;
      client.id = 0;
      client.transport = {};
    }
  }
  uint32_t addClient();
  void removeClient(uint32_t id);
  int setLocalClient(uint32_t id);
  int render() { return host_ ? static_cast<int>(session_host_render_video(host_)) : invalidState(); }
  int renderAudio() { return host_ ? static_cast<int>(session_host_render_audio(host_)) : invalidState(); }
  int receive(uint32_t id, uint32_t type, ProtocolBytes packet);

private:
  session_host_t *host_ = nullptr;
  std::array<HostTransportContext, MAX_SESSION_PARTICIPANTS> clients_{};
  static int invalidState() { return static_cast<int>(ERROR_INVALID_STATE); }
  void create() {
    session_host_config_t config{};
    config.max_clients = MAX_SESSION_PARTICIPANTS;
    host_ = session_host_create(&config);
    if (host_) session_host_render_audio(host_);
  }
  void send(uint32_t id, const void *data, size_t length) {
    browser_host_send(id, static_cast<const uint8_t *>(data), length);
  }
  static asciichat_error_t sendPacket(acip_transport_t *transport, const void *data, size_t length);
  static bool connected(acip_transport_t *transport);
  static socket_t noSocket(acip_transport_t *transport);
  static acip_transport_type_t type(acip_transport_t *transport);
  static const acip_transport_methods_t methods;
};

asciichat_error_t BrowserHostBinding::sendPacket(acip_transport_t *transport, const void *data, size_t length) {
  auto *context = transport ? static_cast<HostTransportContext *>(transport->impl_data) : nullptr;
  if (!context || !context->owner || !data || !length)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid browser host packet");
  context->owner->send(context->id, data, length);
  return ASCIICHAT_OK;
}
bool BrowserHostBinding::connected(acip_transport_t *transport) {
  auto *context = transport ? static_cast<HostTransportContext *>(transport->impl_data) : nullptr;
  return context && context->owner && context->owner->valid();
}
socket_t BrowserHostBinding::noSocket(acip_transport_t *) { return INVALID_SOCKET_VALUE; }
acip_transport_type_t BrowserHostBinding::type(acip_transport_t *) { return ACIP_TRANSPORT_WEBRTC; }
const acip_transport_methods_t BrowserHostBinding::methods = {
    .send = &BrowserHostBinding::sendPacket,
    .get_type = &BrowserHostBinding::type,
    .get_socket = &BrowserHostBinding::noSocket,
    .is_connected = &BrowserHostBinding::connected};

uint32_t BrowserHostBinding::addClient() {
  if (!host_) return 0;
  for (auto &client : clients_) {
    if (client.id) continue;
    const uint32_t id = session_host_add_client(host_, nullptr, ACIP_TRANSPORT_BORROWED, "browser", 0);
    if (!id) return 0;
    client.owner = this;
    client.id = id;
    client.transport = {};
    client.transport.methods = &methods;
    client.transport.impl_data = &client;
    if (session_host_set_client_transport(host_, id, &client.transport, ACIP_TRANSPORT_BORROWED) != ASCIICHAT_OK) {
      session_host_remove_client(host_, id);
      client = {};
      return 0;
    }
    return id;
  }
  SET_ERRNO(ERROR_INVALID_STATE, "Browser session host has no available client slots");
  return 0;
}
void BrowserHostBinding::removeClient(uint32_t id) {
  if (!host_) return;
  session_host_remove_client(host_, id);
  for (auto &client : clients_) if (client.id == id) client = {};
}
int BrowserHostBinding::setLocalClient(uint32_t id) {
  if (!host_) return invalidState();
  session_host_render_target_t target{};
  target.client_id = id;
  return static_cast<int>(session_host_set_render_target(host_, id ? &target : nullptr));
}
int BrowserHostBinding::receive(uint32_t id, uint32_t typeValue, ProtocolBytes packet) {
  if (!host_ || !id) return static_cast<int>(SET_ERRNO(ERROR_INVALID_PARAM, "Invalid browser host client"));
  const size_t length = packet["length"].as<size_t>();
  std::vector<uint8_t> input(length ? length : 1);
  if (length) emscripten::val(emscripten::typed_memory_view(length, input.data())).call<void>("set", packet);
  return static_cast<int>(session_host_receive_packet(host_, id, static_cast<packet_type_t>(typeValue), input.data(), length));
}

EMSCRIPTEN_BINDINGS(ascii_chat_session_host) {
  emscripten::class_<BrowserHostBinding>("BrowserHostBinding")
      .constructor<>()
      .function("valid", &BrowserHostBinding::valid)
      .function("error", &BrowserHostBinding::error)
      .function("close", &BrowserHostBinding::close)
      .function("addClient", &BrowserHostBinding::addClient)
      .function("removeClient", &BrowserHostBinding::removeClient)
      .function("setLocalClient", &BrowserHostBinding::setLocalClient)
      .function("render", &BrowserHostBinding::render)
      .function("renderAudio", &BrowserHostBinding::renderAudio)
      .function("receive", &BrowserHostBinding::receive);
}
