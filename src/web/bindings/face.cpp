#include <emscripten/bind.h>
extern "C" {
#include "../face_follow.h"
#include <ascii-chat/asciichat_errno.h>
}

#include <cstddef>

EMSCRIPTEN_DECLARE_VAL_TYPE(FacePixels);
EMSCRIPTEN_DECLARE_VAL_TYPE(FaceValues);

static int detect_face(FacePixels pixels, int width, int height, double now) {
  const size_t length = pixels["length"].as<size_t>();
  if (face_web_validate_input(width, height, length))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid face detector image: %dx%d, %zu bytes", width, height, length);
  emscripten::val(emscripten::typed_memory_view(length, face_web_input())).call<void>("set", pixels);
  face_web_detect(width, height, now);
  return 0;
}
static FaceValues tick_face(double now, bool enabled) {
  float *values = face_web_tick(now, enabled);
  const size_t count = face_web_output_count();
  emscripten::val result = emscripten::val::global("Float32Array").new_(count);
  result.call<void>("set", emscripten::val(emscripten::typed_memory_view(count, values)));
  return FaceValues(result);
}

EMSCRIPTEN_BINDINGS(ascii_chat_face_follow) {
  emscripten::register_type<FacePixels>("FacePixels", "Uint8ClampedArray");
  emscripten::register_type<FaceValues>("FaceValues", "Float32Array");
  emscripten::function("faceReset", &face_web_reset);
  emscripten::function("faceDetect", &detect_face);
  emscripten::function("faceTick", &tick_face);
}
