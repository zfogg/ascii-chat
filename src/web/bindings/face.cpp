#include <emscripten/bind.h>
extern "C" {
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/video/face_follow.h>
}
#include <array>
#include <cstddef>

EMSCRIPTEN_DECLARE_VAL_TYPE(FacePixels);
EMSCRIPTEN_DECLARE_VAL_TYPE(FaceValues);

static face_follow_state_t state;
static std::array<unsigned char, FACE_FOLLOW_DETECT_WIDTH * FACE_FOLLOW_DETECT_HEIGHT * 4> rgba;
static std::array<unsigned char, FACE_FOLLOW_DETECT_WIDTH * FACE_FOLLOW_DETECT_HEIGHT * 3> bgr;
static std::array<unsigned char, FACE_FOLLOW_RESULT_BUFFER_SIZE> results;
static size_t face_count;

static void reset_face() {
  face_follow_init(&state);
  face_count = 0;
}

static int detect_face(FacePixels pixels, int width, int height, double now) {
  const size_t length = pixels["length"].as<size_t>();
  if (width <= 0 || height <= 0 || width > FACE_FOLLOW_DETECT_WIDTH || height > FACE_FOLLOW_DETECT_HEIGHT ||
      length != static_cast<size_t>(width) * height * 4)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid face detector image: %dx%d, %zu bytes", width, height, length);
  emscripten::val(emscripten::typed_memory_view(length, rgba.data())).call<void>("set", pixels);
  for (int i = 0; i < width * height; ++i) {
    bgr[i * 3] = rgba[i * 4 + 2];
    bgr[i * 3 + 1] = rgba[i * 4 + 1];
    bgr[i * 3 + 2] = rgba[i * 4];
  }
  face_follow_detection_t faces[FACE_FOLLOW_MAX_DETECTIONS];
  face_follow_detect(results.data(), bgr.data(), width, height, faces, FACE_FOLLOW_MAX_DETECTIONS, &face_count);
  face_follow_observe(&state, faces, face_count, now);
  return 0;
}
static FaceValues tick_face(double now, bool enabled) {
  face_follow_tick(&state, now, enabled);
  const std::array<float, 11> values = {state.crop.x,
                                        state.crop.y,
                                        state.crop.w,
                                        state.crop.h,
                                        state.face.x,
                                        state.face.y,
                                        state.face.w,
                                        state.face.h,
                                        static_cast<float>(state.detected && now - state.last_seen < 0.3),
                                        static_cast<float>(face_count),
                                        static_cast<float>(state.tracking && now - state.last_seen <= 0.7)};
  emscripten::val result = emscripten::val::global("Float32Array").new_(values.size());
  result.call<void>("set", emscripten::val(emscripten::typed_memory_view(values.size(), values.data())));
  return FaceValues(result);
}

EMSCRIPTEN_BINDINGS(ascii_chat_face_follow) {
  emscripten::register_type<FacePixels>("FacePixels", "Uint8ClampedArray");
  emscripten::register_type<FaceValues>("FaceValues", "Float32Array");
  emscripten::function("faceReset", &reset_face);
  emscripten::function("faceDetect", &detect_face);
  emscripten::function("faceTick", &tick_face);
}
