#include <emscripten/bind.h>
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

EMSCRIPTEN_DECLARE_VAL_TYPE(RgbaBytes);
EMSCRIPTEN_DECLARE_VAL_TYPE(VisualizationSamples);

// The C session owns the frame returned by tick. Copy it before the next call.
class SessionUiBinding {
public:
  SessionUiBinding() : SessionUiBinding(true) {}
  explicit SessionUiBinding(bool capture_logs) : session_(wasm_ui_create()) {
    if (session_ && capture_logs)
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
  emscripten::function("wasm_options_revision", &wasm_options_revision);
  emscripten::function("wasm_ui_volume", &wasm_ui_volume);
  emscripten::function("wasm_ui_fps", &wasm_ui_fps);
  emscripten::function("wasm_ui_audio", +[](int value) { return static_cast<int>(wasm_ui_audio(value)); });
  emscripten::function("wasm_capture_transform_required", &wasm_capture_transform_required);
  emscripten::function("mirror_cleanup", &mirror_cleanup);
  emscripten::function("mirror_reset_stabilizer", &mirror_reset_stabilizer);
  emscripten::function("set_width", &set_width);
  emscripten::function("get_width", &get_width);
  emscripten::function("set_height", &set_height);
  emscripten::function("get_height", &get_height);
  emscripten::function("set_color_mode", &set_color_mode);
  emscripten::function("get_color_mode", &get_color_mode);
  emscripten::function("set_color_filter", &set_color_filter);
  emscripten::function("get_color_filter", &get_color_filter);
  emscripten::function("set_matrix_rain", &set_matrix_rain);
  emscripten::function("get_matrix_rain", &get_matrix_rain);
  emscripten::function("set_waveform", &set_waveform);
  emscripten::function("get_waveform", &get_waveform);
  emscripten::function("set_fft", &set_fft);
  emscripten::function("get_fft", &get_fft);
  emscripten::function("set_flip_x", &set_flip_x);
  emscripten::function("get_flip_x", &get_flip_x);
  emscripten::function("set_flip_y", &set_flip_y);
  emscripten::function("get_flip_y", &get_flip_y);
  emscripten::function("set_render_mode", &set_render_mode);
  emscripten::function("get_render_mode", &get_render_mode);
  emscripten::function("set_target_fps", &set_target_fps);
  emscripten::function("get_target_fps", &get_target_fps);
  emscripten::function("get_palette", &get_palette);

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
