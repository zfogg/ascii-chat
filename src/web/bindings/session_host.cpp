#include <emscripten/bind.h>
#include "protocol_bytes.h"
extern "C" {
#include "../session_host.h"
}
#include <cstdint>
#include <vector>

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
