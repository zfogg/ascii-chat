#include <emscripten/bind.h>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
extern "C" {
#include "../common/options.h"
#include "../common/capture.h"
#include "../common/ui.h"
#include "../mirror.h"
}

static int patch_options(const std::string &json, double expected_revision) {
  return browser_options_patch(json.c_str(), expected_revision);
}
static int set_palette_binding(const std::string &value) {
  const int result = set_palette(value.c_str());
  if (result)
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid palette: %s", value.c_str());
  return result;
}
static int set_palette_chars_binding(const std::string &value) {
  const int result = set_palette_chars(value.c_str());
  if (result)
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid palette characters");
  return result;
}
static std::string get_palette_chars_binding() {
  const char *value = get_palette_chars();
  return value ? value : "";
}
static std::optional<std::string> read_options() {
  char *json = browser_options_values();
  if (!json) {
    SET_ERRNO(ERROR_INVALID_STATE, "Cannot read browser options");
    return std::nullopt;
  }
  std::string result(json);
  browser_options_release(json);
  return result;
}
static std::optional<std::string> option_help(int mode, const std::string &name) {
  const char *text = get_help_text(mode, name.c_str());
  return text ? std::optional<std::string>(text) : std::nullopt;
}

EMSCRIPTEN_BINDINGS(ascii_chat_options) {
  emscripten::function("browser_options_revision", &browser_options_revision);
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
  emscripten::function("patchOptions", &patch_options);
  emscripten::function("readOptions", &read_options);
  emscripten::function("setPalette", &set_palette_binding);
  emscripten::function("setPaletteChars", &set_palette_chars_binding);
  emscripten::function("getPaletteChars", &get_palette_chars_binding);
  emscripten::function("optionHelp", &option_help);
}
