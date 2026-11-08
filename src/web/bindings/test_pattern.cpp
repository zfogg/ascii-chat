#include <emscripten/bind.h>
#include <cstddef>
#include <cstdint>

EMSCRIPTEN_DECLARE_VAL_TYPE(PatternPixels);

extern "C" {
void *wasm_test_pattern_create(int width, int height);
const uint8_t *wasm_test_pattern_render_rgba(void *, int, int, int, double, bool);
void wasm_test_pattern_destroy(void *);
}

class TestPatternBinding {
public:
  TestPatternBinding(int width, int height) : pattern_(wasm_test_pattern_create(width, height)) {}
  ~TestPatternBinding() {
    if (pattern_)
      wasm_test_pattern_destroy(pattern_);
  }
  TestPatternBinding(const TestPatternBinding &) = delete;
  TestPatternBinding &operator=(const TestPatternBinding &) = delete;
  bool valid() const {
    return pattern_ != nullptr;
  }
  bool render(int width, int height, int index, double time, bool cadence, PatternPixels output) {
    if (!pattern_ || width <= 0 || height <= 0 ||
        static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4 != output["length"].as<size_t>())
      return false;
    const uint8_t *pixels = wasm_test_pattern_render_rgba(pattern_, width, height, index, time, cadence);
    if (!pixels)
      return false;
    output.call<void>("set", emscripten::val(emscripten::typed_memory_view(output["length"].as<size_t>(), pixels)));
    return true;
  }

private:
  void *pattern_;
};

EMSCRIPTEN_BINDINGS(ascii_chat_test_pattern) {
  emscripten::register_type<PatternPixels>("PatternPixels", "Uint8ClampedArray");
  emscripten::class_<TestPatternBinding>("TestPatternBinding")
      .constructor<int, int>()
      .function("valid", &TestPatternBinding::valid)
      .function("render", &TestPatternBinding::render);
}
