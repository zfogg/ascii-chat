#include <emscripten/bind.h>
#include "../common/opts.h"
extern "C" {
#include "../mirror.h"
#include <ascii-chat/asciichat_errno.h>
}

class MirrorSessionBinding {
public:
  explicit MirrorSessionBinding(OptionsPatch options) {
    result_ = mirror_init();
    if (result_) {
      SET_ERRNO(ERROR_INIT, "Browser mirror initialization failed");
      return;
    }
    result_ = initial_options(options);
    if (result_) {
      asciichat_error_context_t context{};
      const bool has_context = HAS_ERRNO(&context);
      close();
      SET_ERRNO(static_cast<asciichat_error_t>(result_), "%s",
                has_context ? context.context_message : "Invalid initial mirror options");
    } else {
      owned_ = true;
    }
  }
  ~MirrorSessionBinding() { close(); }
  MirrorSessionBinding(const MirrorSessionBinding &) = delete;
  MirrorSessionBinding &operator=(const MirrorSessionBinding &) = delete;
  bool valid() const { return owned_; }
  int error() const { return result_; }
  void resetRender() {
    if (owned_)
      session_render_cleanup();
  }
  void close() {
    if (!owned_)
      return;
    mirror_cleanup();
    owned_ = false;
  }

private:
  bool owned_ = false;
  int result_ = 0;
};

EMSCRIPTEN_BINDINGS(ascii_chat_mirror_session) {
  emscripten::class_<MirrorSessionBinding>("MirrorSessionBinding")
      .constructor<OptionsPatch>()
      .function("valid", &MirrorSessionBinding::valid)
      .function("error", &MirrorSessionBinding::error)
      .function("resetRender", &MirrorSessionBinding::resetRender)
      .function("close", &MirrorSessionBinding::close);
}
