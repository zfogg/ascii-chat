#include <emscripten/bind.h>
#include "protocol_bytes.h"
extern "C" {
#include <ascii-chat/common.h>
#include <ascii-chat/network/acip/acds.h>
#include <ascii-chat/network/acip/acds_client.h>
#include <ascii-chat/network/acip/transport.h>
#include "session/host.h"
}
#include <emscripten.h>
#include <array>
#include <cstdint>
#include <vector>

EM_JS(void, browser_host_send, (uint32_t client, const uint8_t *data, size_t length), {
  if (Module.hostPacketCallback)
    Module.hostPacketCallback(client, HEAPU8.slice(data, data + length));
});

struct BrowserHostState {
  session_host_t *host = nullptr;
  std::array<acip_transport_t, MAX_SESSION_PARTICIPANTS> transports{};
  std::array<uint32_t, MAX_SESSION_PARTICIPANTS> client_ids{};
  void reset() {
    session_host_destroy(host);
    host = nullptr;
    client_ids.fill(0);
    transports.fill({});
  }
  ~BrowserHostState() {
    reset();
  }
};
static BrowserHostState state;
static asciichat_error_t host_transport_send(acip_transport_t *transport, const void *data, size_t length) {
  if (!transport || !data || !length)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid browser host packet");
  browser_host_send((uint32_t)(uintptr_t)transport->impl_data, static_cast<const uint8_t *>(data), length);
  return ASCIICHAT_OK;
}
static bool connected(acip_transport_t *transport) {
  return state.host != NULL;
}
static socket_t no_socket(acip_transport_t *transport) {
  return INVALID_SOCKET_VALUE;
}
static acip_transport_type_t transport_type(acip_transport_t *transport) {
  return ACIP_TRANSPORT_WEBRTC;
}
static const acip_transport_methods_t methods = [] {
  acip_transport_methods_t result{};
  result.send = host_transport_send;
  result.is_connected = connected;
  result.get_socket = no_socket;
  result.get_type = transport_type;
  return result;
}();

static void browser_host_destroy(void) {
  state.reset();
}
static asciichat_error_t browser_host_create(void) {
  browser_host_destroy();
  session_host_config_t config{};
  config.max_clients = MAX_SESSION_PARTICIPANTS;
  state.host = session_host_create(&config);
  if (!state.host)
    return ERROR_MEMORY;
  return session_host_render_audio(state.host);
}
static uint32_t browser_host_add_client(void) {
  if (!state.host) {
    SET_ERRNO(ERROR_INVALID_STATE, "Browser host is not initialized");
    return 0;
  }
  for (size_t i = 0; i < MAX_SESSION_PARTICIPANTS; ++i) {
    if (state.client_ids[i])
      continue;
    uint32_t id = session_host_add_client(state.host, NULL, ACIP_TRANSPORT_BORROWED, "browser", 0);
    if (!id)
      return 0;
    state.client_ids[i] = id;
    state.transports[i] = {};
    state.transports[i].methods = &methods;
    state.transports[i].impl_data = reinterpret_cast<void *>(static_cast<uintptr_t>(id));
    if (session_host_set_client_transport(state.host, id, &state.transports[i], ACIP_TRANSPORT_BORROWED) !=
        ASCIICHAT_OK) {
      session_host_remove_client(state.host, id);
      state.client_ids[i] = 0;
      return 0;
    }
    return id;
  }
  return 0;
}
static void browser_host_remove_client(uint32_t id) {
  if (!state.host)
    return;
  session_host_remove_client(state.host, id);
  for (size_t i = 0; i < MAX_SESSION_PARTICIPANTS; ++i)
    if (state.client_ids[i] == id)
      state.client_ids[i] = 0;
}
static asciichat_error_t browser_host_set_local_client(uint32_t id) {
  session_host_render_target_t target{};
  target.client_id = id;
  return session_host_set_render_target(state.host, id ? &target : NULL);
}
static asciichat_error_t browser_host_receive(uint32_t id, uint32_t type, const void *data, size_t length) {
  if (!state.host || !id || !data)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid browser host packet or client");
  return session_host_receive_packet(state.host, id, static_cast<packet_type_t>(type), data, length);
}
static asciichat_error_t browser_host_render(void) {
  return session_host_render_video(state.host);
}
static asciichat_error_t browser_host_render_audio(void) {
  return session_host_render_audio(state.host);
}

// Browser host packets arrive at frame rate. Retain their backing storage.
class HostPacketBinding {
public:
  int receive(unsigned id, unsigned type, ProtocolBytes packet) {
    const size_t length = packet["length"].as<size_t>();
    input_.resize(length ? length : 1);
    if (length)
      emscripten::val(emscripten::typed_memory_view(length, input_.data())).call<void>("set", packet);
    return browser_host_receive(id, type, input_.data(), length);
  }

private:
  std::vector<uint8_t> input_;
};

EMSCRIPTEN_BINDINGS(ascii_chat_session_host) {
  emscripten::function("browser_host_create", +[]() { return static_cast<int>(browser_host_create()); });
  emscripten::function("browser_host_destroy", &browser_host_destroy);
  emscripten::function("browser_host_add_client", &browser_host_add_client);
  emscripten::function(
      "browser_host_set_local_client",
      +[](uint32_t id) { return static_cast<int>(browser_host_set_local_client(id)); });
  emscripten::function("browser_host_remove_client", &browser_host_remove_client);
  emscripten::function("browser_host_render", +[]() { return static_cast<int>(browser_host_render()); });
  emscripten::function("browser_host_render_audio", +[]() { return static_cast<int>(browser_host_render_audio()); });
  emscripten::class_<HostPacketBinding>("HostPacketBinding")
      .constructor<>()
      .function("receive", &HostPacketBinding::receive);
}
