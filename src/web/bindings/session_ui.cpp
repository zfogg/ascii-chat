#include <emscripten/bind.h>
#include <algorithm>
extern "C" {
#include <ascii-chat/tui/session.h>
#include <ascii-chat/tui/controller.h>
#include <ascii-chat/options/options.h>
#include "../common/capture.h"
#include "../mirror.h"
}

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Controller callbacks operate on the session currently handling browser input.
static thread_local ui_session_t *active_session = nullptr;
extern "C" terminal_size_t ui_controller_size(void) {
  return ui_session_size(active_session);
}
extern "C" void ui_controller_redraw(void) {
  ui_session_redraw(active_session);
}

// The C session owns the frame returned by tick. Copy it before the next call.
class SessionUiBinding {
public:
  SessionUiBinding() : SessionUiBinding(true) {}
  explicit SessionUiBinding(bool capture_logs) : session_(ui_session_create()) {
    if (session_ && capture_logs)
      ui_session_capture_logs(session_);
  }
  ~SessionUiBinding() { close(); }

  SessionUiBinding(const SessionUiBinding &) = delete;
  SessionUiBinding &operator=(const SessionUiBinding &) = delete;

  bool valid() const {
    return session_ != nullptr;
  }
  int error() const { return valid() ? 0 : static_cast<int>(ERROR_INVALID_STATE); }
  void close() {
    if (!session_)
      return;
    ui_session_destroy(session_);
    session_ = nullptr;
  }
  void resize(int cols, int rows) {
    ui_session_resize(session_, cols, rows);
  }
  void loading(bool loading, const std::string &message) {
    ui_session_loading(session_, loading, message.c_str());
  }
  void log(const std::string &message) {
    ui_session_log(session_, message.c_str());
  }
  int media(const std::string &text) {
    return ui_session_media(session_, text.data(), text.size());
  }
  int key(int key) {
    ui_session_t *previous = active_session;
    active_session = session_;
    int result = ui_session_input(session_, static_cast<keyboard_key_t>(key));
    active_session = previous;
    return result;
  }
  std::optional<std::string> tick(double milliseconds) {
    ui_session_t *previous = active_session;
    active_session = session_;
    const char *frame = ui_session_tick(session_, static_cast<uint64_t>(milliseconds * 1000000));
    active_session = previous;
    return frame ? std::optional<std::string>(frame) : std::nullopt;
  }
  void clear() {
    ui_session_clear(session_);
  }
  void redraw() {
    ui_session_redraw(session_);
  }

private:
  ui_session *session_;
};

EMSCRIPTEN_BINDINGS(ascii_chat_session_ui) {
  emscripten::function(
      "browser_ui_audio", +[](int value) { return static_cast<int>(options_set_bool("audio_enabled", value != 0)); });
  emscripten::function("browser_ui_fps", +[]() { return static_cast<int>(GET_OPTION(fps_counter)); });
  emscripten::function("browser_ui_volume", +[]() { return static_cast<double>(GET_OPTION(speakers_volume)); });
  emscripten::class_<SessionUiBinding>("SessionUiBinding")
      .constructor<>()
      .constructor<bool>()
      .function("valid", &SessionUiBinding::valid)
      .function("error", &SessionUiBinding::error)
      .function("close", &SessionUiBinding::close)
      .function("resize", &SessionUiBinding::resize)
      .function("loading", &SessionUiBinding::loading)
      .function("log", &SessionUiBinding::log)
      .function("media", &SessionUiBinding::media)
      .function("key", &SessionUiBinding::key)
      .function("tick", &SessionUiBinding::tick)
      .function("clear", &SessionUiBinding::clear)
      .function("redraw", &SessionUiBinding::redraw);
}
