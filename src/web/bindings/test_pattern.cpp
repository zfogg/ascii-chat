#include <emscripten/bind.h>
extern "C" {
#include "../common/test_pattern.h"
#include <ascii-chat/asciichat_errno.h>
}

#include <cstddef>
#include <cstdint>

EMSCRIPTEN_DECLARE_VAL_TYPE(PatternPixels);

class TestPatternBinding {
public:
  TestPatternBinding(int width, int height) : pattern_(browser_test_pattern_create(width, height)) {}
  ~TestPatternBinding() {
    if (pattern_)
      browser_test_pattern_destroy(pattern_);
  }
  TestPatternBinding(const TestPatternBinding &) = delete;
  TestPatternBinding &operator=(const TestPatternBinding &) = delete;
  bool valid() const {
    return pattern_ != nullptr;
  }
  bool render(int width, int height, int index, double time, bool cadence, PatternPixels output) {
    if (!pattern_) {
      SET_ERRNO(ERROR_INVALID_STATE, "Test pattern is not initialized");
      return false;
    }
    if (width <= 0 || height <= 0 ||
        static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4 != output["length"].as<size_t>()) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Invalid test pattern image: %dx%d", width, height);
      return false;
    }
    const uint8_t *pixels = browser_test_pattern_render_rgba(pattern_, width, height, index, time, cadence);
    if (!pixels)
      return false;
    output.call<void>("set", emscripten::val(emscripten::typed_memory_view(output["length"].as<size_t>(), pixels)));
    return true;
  }

private:
  test_pattern_t *pattern_;
};

EMSCRIPTEN_BINDINGS(ascii_chat_test_pattern) {
  emscripten::register_type<PatternPixels>("PatternPixels", "Uint8ClampedArray");
  emscripten::class_<TestPatternBinding>("TestPatternBinding")
      .constructor<int, int>()
      .function("valid", &TestPatternBinding::valid)
      .function("render", &TestPatternBinding::render);
}
