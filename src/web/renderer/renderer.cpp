#include <emscripten/bind.h>

extern "C" {
#include "../mirror.h"
#include <ascii-chat/asciichat_errno.h>
}

// The options registry and rendering history are module-global. One owning
// session per module ensures no Embind object can destroy another session.
class RendererSessionBinding {
public:
  RendererSessionBinding() {
    if (active_) {
      result_ = SET_ERRNO(ERROR_INVALID_STATE, "Renderer session is already initialized");
      return;
    }
    result_ = mirror_init();
    if (result_ != 0) {
      result_ = SET_ERRNO(ERROR_INIT, "Failed to initialize renderer session");
      return;
    }
    active_ = true;
    owns_session_ = true;
  }
  ~RendererSessionBinding() {
    if (owns_session_) {
      mirror_cleanup();
      active_ = false;
    }
  }
  RendererSessionBinding(const RendererSessionBinding &) = delete;
  RendererSessionBinding &operator=(const RendererSessionBinding &) = delete;

  bool valid() const {
    return owns_session_;
  }
  int error() const {
    return result_;
  }

private:
  static bool active_;
  bool owns_session_ = false;
  int result_ = 0;
};

bool RendererSessionBinding::active_ = false;

EMSCRIPTEN_BINDINGS(ascii_chat_renderer_module) {
  emscripten::function("rendererApiVersion", +[]() { return 1; });
  emscripten::class_<RendererSessionBinding>("RendererSessionBinding")
      .constructor<>()
      .function("valid", &RendererSessionBinding::valid)
      .function("error", &RendererSessionBinding::error);
}
