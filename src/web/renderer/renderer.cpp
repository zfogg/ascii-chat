#include <emscripten/bind.h>

// Version the standalone JavaScript-facing contract independently of the CLI.
EMSCRIPTEN_BINDINGS(ascii_chat_renderer_module) {
  emscripten::function("rendererApiVersion", +[]() { return 1; });
}
