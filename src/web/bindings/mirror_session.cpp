#include <emscripten/bind.h>
#include "../common/opts.h"
#include <string>
extern "C" {
#include "../mirror.h"
#include <ascii-chat/asciichat_errno.h>
}

static int init_mirror(OptionsPatch options) {
  int result = mirror_init();
  if (result)
    SET_ERRNO(ERROR_INIT, "Browser mirror initialization failed");
  if (result)
    return result;
  result = initial_options(options);
  if (result) {
    asciichat_error_context_t context = {};
    bool has_context = HAS_ERRNO(&context);
    mirror_cleanup();
    return SET_ERRNO(static_cast<asciichat_error_t>(result), "%s",
                     has_context ? context.context_message : "Invalid initial mirror options");
  }
  return 0;
}

EMSCRIPTEN_BINDINGS(ascii_chat_mirror_session) {
  emscripten::function("initMirror", &init_mirror);
  emscripten::function("sessionRenderCleanup", &session_render_cleanup);
  emscripten::function("mirror_cleanup", &mirror_cleanup);
}
