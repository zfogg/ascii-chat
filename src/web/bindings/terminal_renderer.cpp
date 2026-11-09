#include <emscripten/bind.h>
extern "C" {
#include "../common/font.h"
#include <ascii-chat/media/render/renderer.h>
}

#include <cstddef>
#include <cstdint>
#include <string>

class AsciiRendererBinding {
public:
  AsciiRendererBinding(int cols, int rows, double font_size, bool matrix, int selection)
      : output_(emscripten::val::undefined()) {
    term_renderer_config_t config{};
    config.cols = cols;
    config.rows = rows;
    config.font_size_pt = font_size;
    config.theme = static_cast<term_renderer_theme_t>(0);
    config.font_data = get_font_default_ptr(selection);
    config.font_data_size = get_font_default_size(selection);
    if (matrix)
      SAFE_STRNCPY(config.font_spec, "Matrix", sizeof(config.font_spec));
    result_ = term_renderer_create(&config, &renderer_);
  }
  ~AsciiRendererBinding() {
    close();
  }
  AsciiRendererBinding(const AsciiRendererBinding &) = delete;
  AsciiRendererBinding &operator=(const AsciiRendererBinding &) = delete;

  bool valid() const {
    return renderer_ && !result_;
  }
  int error() const {
    return render_error_ ? render_error_ : result_;
  }
  void close() {
    if (!renderer_) return;
    term_renderer_destroy(renderer_);
    renderer_ = nullptr;
    output_ = emscripten::val::undefined();
  }
  emscripten::val dimensions() const {
    emscripten::val result = emscripten::val::object();
    result.set("cols", term_renderer_get_cols(renderer_));
    result.set("rows", term_renderer_get_rows(renderer_));
    result.set("width", term_renderer_width_px(renderer_));
    result.set("height", term_renderer_height_px(renderer_));
    return result;
  }
  emscripten::val render(const std::string &text, int zalgo) {
    if (!renderer_) {
      render_error_ = SET_ERRNO(ERROR_INVALID_STATE, "Terminal renderer is not initialized");
      return emscripten::val::null();
    }
    const int zalgo_result = term_renderer_set_zalgo(renderer_, zalgo);
    if (zalgo_result) {
      render_error_ = SET_ERRNO(ERROR_INVALID_STATE, "Cannot set renderer Zalgo level");
      return emscripten::val::null();
    }
    const int feed_result = term_renderer_feed(renderer_, text.data(), text.size());
    if (feed_result) {
      render_error_ = SET_ERRNO(ERROR_INVALID_STATE, "Cannot feed ANSI text to renderer");
      return emscripten::val::null();
    }
    const int w = term_renderer_width_px(renderer_), h = term_renderer_height_px(renderer_),
              pitch = term_renderer_pitch(renderer_);
    const uint8_t *pixels = term_renderer_pixels(renderer_);
    if (!pixels || w <= 0 || h <= 0 || pitch <= 0 || static_cast<uint64_t>(pitch) < static_cast<uint64_t>(w) * 4) {
      render_error_ = SET_ERRNO(ERROR_INVALID_STATE, "Terminal renderer has invalid pixel storage");
      return emscripten::val::null();
    }
    if (output_.isUndefined() || output_["length"].as<size_t>() != static_cast<size_t>(w) * h * 4)
      output_ = emscripten::val::global("Uint8ClampedArray").new_(static_cast<size_t>(w) * h * 4);
    const size_t row_bytes = static_cast<size_t>(w) * 4;
    if (static_cast<size_t>(pitch) == row_bytes) {
      output_.call<void>("set", emscripten::val(emscripten::typed_memory_view(row_bytes * h, pixels)));
    } else {
      for (int row = 0; row < h; ++row) {
        output_.call<void>(
            "set", emscripten::val(emscripten::typed_memory_view(row_bytes, pixels + static_cast<size_t>(row) * pitch)),
            row * row_bytes);
      }
    }
    render_error_ = 0;
    return output_.call<emscripten::val>("slice");
  }

private:
  terminal_renderer_s *renderer_ = nullptr;
  int result_;
  int render_error_ = 0;
  emscripten::val output_;
};

EMSCRIPTEN_BINDINGS(ascii_chat_terminal_renderer) {
  emscripten::class_<AsciiRendererBinding>("AsciiRendererBinding")
      .constructor<int, int, double, bool, int>()
      .function("valid", &AsciiRendererBinding::valid)
      .function("error", &AsciiRendererBinding::error)
      .function("close", &AsciiRendererBinding::close)
      .function("dimensions", &AsciiRendererBinding::dimensions)
      .function("render", &AsciiRendererBinding::render);
}
