#include <emscripten/bind.h>
#include <cstddef>
#include <cstdint>
#include <string>

EMSCRIPTEN_DECLARE_VAL_TYPE(RasterPixels);

extern "C" {
struct terminal_renderer_s;
int wasm_renderer_create(int cols, int rows, double font_size, int matrix, int selection, terminal_renderer_s **output);
int term_renderer_set_zalgo(terminal_renderer_s *renderer, int level);
int term_renderer_feed(terminal_renderer_s *renderer, const char *text, size_t length);
const uint8_t *term_renderer_pixels(terminal_renderer_s *renderer);
int term_renderer_width_px(terminal_renderer_s *renderer);
int term_renderer_height_px(terminal_renderer_s *renderer);
int term_renderer_pitch(terminal_renderer_s *renderer);
int term_renderer_get_cols(terminal_renderer_s *renderer);
int term_renderer_get_rows(terminal_renderer_s *renderer);
void term_renderer_destroy(terminal_renderer_s *renderer);
}

class TerminalRendererBinding {
public:
  TerminalRendererBinding(int cols, int rows, double font_size, bool matrix, int selection)
      : result_(wasm_renderer_create(cols, rows, font_size, matrix, selection, &renderer_)) {}
  ~TerminalRendererBinding() {
    if (renderer_)
      term_renderer_destroy(renderer_);
  }
  TerminalRendererBinding(const TerminalRendererBinding &) = delete;
  TerminalRendererBinding &operator=(const TerminalRendererBinding &) = delete;

  bool valid() const {
    return renderer_ && !result_;
  }
  int error() const {
    return result_;
  }
  int cols() {
    return term_renderer_get_cols(renderer_);
  }
  int rows() {
    return term_renderer_get_rows(renderer_);
  }
  int width() {
    return term_renderer_width_px(renderer_);
  }
  int height() {
    return term_renderer_height_px(renderer_);
  }
  int setZalgo(int level) {
    return term_renderer_set_zalgo(renderer_, level);
  }
  int feed(const std::string &text) {
    return term_renderer_feed(renderer_, text.data(), text.size());
  }
  bool copyPixels(RasterPixels output) {
    const int w = width(), h = height(), pitch = term_renderer_pitch(renderer_);
    const uint8_t *pixels = term_renderer_pixels(renderer_);
    if (!pixels || w <= 0 || h <= 0 || pitch <= 0 || static_cast<uint64_t>(pitch) < static_cast<uint64_t>(w) * 4 ||
        static_cast<uint64_t>(w) * h * 4 != output["length"].as<size_t>())
      return false;
    const size_t row_bytes = static_cast<size_t>(w) * 4;
    if (static_cast<size_t>(pitch) == row_bytes) {
      output.call<void>("set", emscripten::val(emscripten::typed_memory_view(row_bytes * h, pixels)));
    } else {
      for (int row = 0; row < h; ++row) {
        output.call<void>(
            "set", emscripten::val(emscripten::typed_memory_view(row_bytes, pixels + static_cast<size_t>(row) * pitch)),
            row * row_bytes);
      }
    }
    return true;
  }

private:
  terminal_renderer_s *renderer_ = nullptr;
  int result_;
};

EMSCRIPTEN_BINDINGS(ascii_chat_terminal_renderer) {
  emscripten::register_type<RasterPixels>("RasterPixels", "Uint8ClampedArray");
  emscripten::class_<TerminalRendererBinding>("TerminalRendererBinding")
      .constructor<int, int, double, bool, int>()
      .function("valid", &TerminalRendererBinding::valid)
      .function("error", &TerminalRendererBinding::error)
      .function("cols", &TerminalRendererBinding::cols)
      .function("rows", &TerminalRendererBinding::rows)
      .function("width", &TerminalRendererBinding::width)
      .function("height", &TerminalRendererBinding::height)
      .function("setZalgo", &TerminalRendererBinding::setZalgo)
      .function("feed", &TerminalRendererBinding::feed)
      .function("copyPixels", &TerminalRendererBinding::copyPixels);
}
