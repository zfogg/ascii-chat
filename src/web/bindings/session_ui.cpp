#include <emscripten/bind.h>
#include <algorithm>
extern "C" {
#include "../common/ui.h"
#include "../common/options.h"
#include "../common/capture.h"
#include "../mirror.h"
}

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The C session owns the frame returned by tick. Copy it before the next call.
class SessionUiBinding {
public:
  SessionUiBinding() : SessionUiBinding(true) {}
  explicit SessionUiBinding(bool capture_logs) : session_(browser_ui_create()) {
    if (session_ && capture_logs)
      browser_ui_capture_logs(session_);
  }
  ~SessionUiBinding() {
    if (session_)
      browser_ui_destroy(session_);
  }

  SessionUiBinding(const SessionUiBinding &) = delete;
  SessionUiBinding &operator=(const SessionUiBinding &) = delete;

  bool valid() const {
    return session_ != nullptr;
  }
  void resize(int cols, int rows) {
    browser_ui_resize(session_, cols, rows);
  }
  void loading(bool loading, const std::string &message) {
    browser_ui_loading(session_, loading, message.c_str());
  }
  void log(const std::string &message) {
    browser_ui_log(session_, message.c_str());
  }
  int media(const std::string &text) {
    return browser_ui_media(session_, text.data(), text.size());
  }
  int key(int key) {
    return browser_ui_key(session_, key);
  }
  std::optional<std::string> tick(double milliseconds) {
    const char *frame = browser_ui_tick(session_, milliseconds);
    return frame ? std::optional<std::string>(frame) : std::nullopt;
  }
  void clear() {
    browser_ui_clear(session_);
  }
  void redraw() {
    browser_ui_redraw(session_);
  }

private:
  ui_session *session_;
};

EMSCRIPTEN_BINDINGS(ascii_chat_session_ui) {
  emscripten::function("browser_ui_audio", +[](int value) { return static_cast<int>(browser_ui_audio(value)); });
  emscripten::function("browser_ui_fps", &browser_ui_fps);
  emscripten::function("browser_ui_volume", &browser_ui_volume);
  emscripten::class_<SessionUiBinding>("SessionUiBinding")
      .constructor<>()
      .constructor<bool>()
      .function("valid", &SessionUiBinding::valid)
      .function("resize", &SessionUiBinding::resize)
      .function("loading", &SessionUiBinding::loading)
      .function("log", &SessionUiBinding::log)
      .function("media", &SessionUiBinding::media)
      .function("key", &SessionUiBinding::key)
      .function("tick", &SessionUiBinding::tick)
      .function("clear", &SessionUiBinding::clear)
      .function("redraw", &SessionUiBinding::redraw);
}
