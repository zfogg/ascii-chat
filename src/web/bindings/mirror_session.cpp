#include <emscripten/bind.h>
#include <string>
extern "C" {
#include "../mirror.h"
#include <ascii-chat/asciichat_errno.h>
}

static int init_mirror(const std::string &args) {
  int result = mirror_init_with_args(args.c_str());
  if (result)
    SET_ERRNO(ERROR_INIT, "Browser mirror initialization failed");
  return result;
}

EMSCRIPTEN_BINDINGS(ascii_chat_mirror_session) {
  emscripten::function("initMirror", &init_mirror);
  emscripten::function("sessionRenderCleanup", &session_render_cleanup);
  emscripten::function("mirror_cleanup", &mirror_cleanup);
}
