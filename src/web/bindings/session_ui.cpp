#include <emscripten/bind.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

EMSCRIPTEN_DECLARE_VAL_TYPE(RgbaBytes);
EMSCRIPTEN_DECLARE_VAL_TYPE(VisualizationSamples);

extern "C" {
struct ui_session;
ui_session *wasm_ui_create(void);
void wasm_ui_destroy(ui_session *session);
void wasm_ui_resize(ui_session *session, int cols, int rows);
void wasm_ui_loading(ui_session *session, int loading, const char *message);
void wasm_ui_log(ui_session *session, const char *message);
int wasm_ui_media(ui_session *session, const char *text, size_t length);
int wasm_ui_key(ui_session *session, int key);
const char *wasm_ui_tick(ui_session *session, double milliseconds);
void wasm_ui_clear(ui_session *session);
void wasm_ui_redraw(ui_session *session);
void wasm_ui_capture_logs(ui_session *session);
int mirror_init_with_args(const char *args);
char *mirror_convert_frame(uint8_t *rgba, int width, int height, int contain);
void mirror_free_string(char *text);
void mirror_submit_audio_samples(const float *samples, int count, int source);
char *mirror_render_audio_visualization(unsigned width, unsigned height, int source);
int wasm_capture_transform_rgba(uint8_t *pixels, int width, int height);
const char *get_help_text(int mode, const char *option_name);
char *wasm_options_values(void);
void wasm_options_release(char *json);
int wasm_options_patch(const char *json, double expected_revision);
int set_palette(const char *value);
int set_palette_chars(const char *value);
const char *get_palette_chars(void);
}

// The C session owns the frame returned by tick. Copy it before the next call.
class SessionUiBinding {
public:
  SessionUiBinding() : session_(wasm_ui_create()) {
    if (session_)
      wasm_ui_capture_logs(session_);
  }
  ~SessionUiBinding() {
    if (session_)
      wasm_ui_destroy(session_);
  }

  SessionUiBinding(const SessionUiBinding &) = delete;
  SessionUiBinding &operator=(const SessionUiBinding &) = delete;

  bool valid() const {
    return session_ != nullptr;
  }
  void resize(int cols, int rows) {
    wasm_ui_resize(session_, cols, rows);
  }
  void loading(bool loading, const std::string &message) {
    wasm_ui_loading(session_, loading, message.c_str());
  }
  void log(const std::string &message) {
    wasm_ui_log(session_, message.c_str());
  }
  int media(const std::string &text) {
    return wasm_ui_media(session_, text.data(), text.size());
  }
  int key(int key) {
    return wasm_ui_key(session_, key);
  }
  std::optional<std::string> tick(double milliseconds) {
    const char *frame = wasm_ui_tick(session_, milliseconds);
    return frame ? std::optional<std::string>(frame) : std::nullopt;
  }
  void clear() {
    wasm_ui_clear(session_);
  }
  void redraw() {
    wasm_ui_redraw(session_);
  }

private:
  ui_session *session_;
};

// Retain the input allocation between frames; JS arrays and returned strings
// remain owned by JavaScript. No heap view escapes this synchronous call.
class MirrorFrameBinding {
public:
  std::optional<std::string> convert(RgbaBytes rgba, int width, int height, bool contain) {
    const size_t length = rgba["byteLength"].as<size_t>();
    if (width <= 0 || height <= 0 || static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4 != length)
      return std::nullopt;
    input_.resize(length);
    emscripten::val view = emscripten::val(emscripten::typed_memory_view(length, input_.data()));
    view.call<void>("set", rgba);
    char *frame = mirror_convert_frame(input_.data(), width, height, contain);
    if (!frame)
      return std::nullopt;
    std::string result(frame);
    mirror_free_string(frame);
    return result;
  }
  int transform(RgbaBytes rgba, int width, int height) {
    const size_t length = rgba["byteLength"].as<size_t>();
    if (width <= 0 || height <= 0 || static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4 != length)
      return -1;
    input_.resize(length);
    emscripten::val view = emscripten::val(emscripten::typed_memory_view(length, input_.data()));
    view.call<void>("set", rgba);
    const int result = wasm_capture_transform_rgba(input_.data(), width, height);
    if (!result) {
      emscripten::val result_view = emscripten::val(emscripten::typed_memory_view(length, input_.data()));
      rgba.call<void>("set", result_view);
    }
    return result;
  }
  void submitAudio(VisualizationSamples samples, int source) {
    const size_t length = samples["length"].as<size_t>();
    if (!length || length > 8192)
      return;
    audio_.resize(length);
    emscripten::val view = emscripten::val(emscripten::typed_memory_view(length, audio_.data()));
    view.call<void>("set", samples);
    mirror_submit_audio_samples(audio_.data(), static_cast<int>(length), source);
  }
  std::optional<std::string> renderAudio(unsigned width, unsigned height, int source) {
    char *frame = mirror_render_audio_visualization(width, height, source);
    if (!frame)
      return std::nullopt;
    std::string result(frame);
    mirror_free_string(frame);
    return result;
  }

private:
  std::vector<uint8_t> input_;
  std::vector<float> audio_;
};

static int init_mirror(const std::string &args) {
  return mirror_init_with_args(args.c_str());
}
static int patch_options(const std::string &json, double expected_revision) {
  return wasm_options_patch(json.c_str(), expected_revision);
}
static int set_palette_binding(const std::string &value) {
  return set_palette(value.c_str());
}
static int set_palette_chars_binding(const std::string &value) {
  return set_palette_chars(value.c_str());
}
static std::string get_palette_chars_binding() {
  const char *value = get_palette_chars();
  return value ? value : "";
}
static std::optional<std::string> read_options() {
  char *json = wasm_options_values();
  if (!json)
    return std::nullopt;
  std::string result(json);
  wasm_options_release(json);
  return result;
}
static std::optional<std::string> option_help(int mode, const std::string &name) {
  const char *text = get_help_text(mode, name.c_str());
  return text ? std::optional<std::string>(text) : std::nullopt;
}

EMSCRIPTEN_BINDINGS(ascii_chat_session_ui) {
  emscripten::register_optional<std::string>();
  emscripten::register_type<RgbaBytes>("RgbaBytes", "Uint8Array | Uint8ClampedArray");
  emscripten::register_type<VisualizationSamples>("VisualizationSamples", "Float32Array");
  emscripten::function("initMirror", &init_mirror);
  emscripten::function("patchOptions", &patch_options);
  emscripten::function("readOptions", &read_options);
  emscripten::function("setPalette", &set_palette_binding);
  emscripten::function("setPaletteChars", &set_palette_chars_binding);
  emscripten::function("getPaletteChars", &get_palette_chars_binding);
  emscripten::function("optionHelp", &option_help);
  emscripten::class_<MirrorFrameBinding>("MirrorFrameBinding")
      .constructor<>()
      .function("convert", &MirrorFrameBinding::convert)
      .function("transform", &MirrorFrameBinding::transform)
      .function("submitAudio", &MirrorFrameBinding::submitAudio)
      .function("renderAudio", &MirrorFrameBinding::renderAudio);
  emscripten::class_<SessionUiBinding>("SessionUiBinding")
      .constructor<>()
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
