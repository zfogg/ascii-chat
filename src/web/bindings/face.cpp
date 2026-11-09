#include <emscripten/bind.h>
extern "C" {
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/video/face_follow.h>
}
#include <array>
#include <cstddef>

EMSCRIPTEN_DECLARE_VAL_TYPE(FacePixels);
EMSCRIPTEN_DECLARE_VAL_TYPE(FaceValues);

class FaceFollowBinding {
public:
  FaceFollowBinding() { reset(); }
  bool valid() const { return !closed_; }
  int error() const { return valid() ? 0 : ERROR_INVALID_STATE; }
  void close() { closed_ = true; }
  void reset() {
    closed_ = false;
    face_follow_init(&state_);
    face_count_ = 0;
  }
  int detect(FacePixels pixels, int width, int height, double now) {
    if (!valid()) return SET_ERRNO(ERROR_INVALID_STATE, "Face tracker is closed");
    const size_t length = pixels["length"].as<size_t>();
    if (width <= 0 || height <= 0 || width > FACE_FOLLOW_DETECT_WIDTH || height > FACE_FOLLOW_DETECT_HEIGHT ||
        length != static_cast<size_t>(width) * height * 4)
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid face detector image: %dx%d, %zu bytes", width, height, length);
    emscripten::val(emscripten::typed_memory_view(length, rgba_.data())).call<void>("set", pixels);
    for (int i = 0; i < width * height; ++i) {
      bgr_[i * 3] = rgba_[i * 4 + 2];
      bgr_[i * 3 + 1] = rgba_[i * 4 + 1];
      bgr_[i * 3 + 2] = rgba_[i * 4];
    }
    face_follow_detection_t faces[FACE_FOLLOW_MAX_DETECTIONS];
    face_follow_detect(results_.data(), bgr_.data(), width, height, faces, FACE_FOLLOW_MAX_DETECTIONS, &face_count_);
    face_follow_observe(&state_, faces, face_count_, now);
    return 0;
  }
  FaceValues tick(double now, bool enabled) {
    if (!valid()) return FaceValues(emscripten::val::global("Float32Array").new_(11));
    face_follow_tick(&state_, now, enabled);
    const std::array<float, 11> values = {
        state_.crop.x, state_.crop.y, state_.crop.w, state_.crop.h,
        state_.face.x, state_.face.y, state_.face.w, state_.face.h,
        static_cast<float>(state_.detected && now - state_.last_seen < 0.3),
        static_cast<float>(face_count_),
        static_cast<float>(state_.tracking && now - state_.last_seen <= 0.7)};
    emscripten::val result = emscripten::val::global("Float32Array").new_(values.size());
    result.call<void>("set", emscripten::val(emscripten::typed_memory_view(values.size(), values.data())));
    return FaceValues(result);
  }

private:
  bool closed_ = false;
  face_follow_state_t state_{};
  std::array<unsigned char, FACE_FOLLOW_DETECT_WIDTH * FACE_FOLLOW_DETECT_HEIGHT * 4> rgba_{};
  std::array<unsigned char, FACE_FOLLOW_DETECT_WIDTH * FACE_FOLLOW_DETECT_HEIGHT * 3> bgr_{};
  std::array<unsigned char, FACE_FOLLOW_RESULT_BUFFER_SIZE> results_{};
  size_t face_count_ = 0;
};

EMSCRIPTEN_BINDINGS(ascii_chat_face_follow) {
  emscripten::register_type<FacePixels>("FacePixels", "Uint8ClampedArray");
  emscripten::register_type<FaceValues>("FaceValues", "Float32Array");
  emscripten::class_<FaceFollowBinding>("FaceFollowBinding")
      .constructor<>()
      .function("valid", &FaceFollowBinding::valid)
      .function("error", &FaceFollowBinding::error)
      .function("close", &FaceFollowBinding::close)
      .function("reset", &FaceFollowBinding::reset)
      .function("detect", &FaceFollowBinding::detect)
      .function("tick", &FaceFollowBinding::tick);
}
