#include <emscripten/bind.h>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
extern "C" {
#include <ascii-chat/asciichat_errno.h>
#include "../common/capture.h"
#include "../mirror.h"
}

EMSCRIPTEN_DECLARE_VAL_TYPE(RgbaBytes);
EMSCRIPTEN_DECLARE_VAL_TYPE(VisualizationSamples);

// Retain the input allocation between frames; JS arrays and returned strings
// remain owned by JavaScript. No heap view escapes this synchronous call.
class MirrorFrameBinding {
public:
  MirrorFrameBinding() : context_(mirror_render_context_create()) {}
  ~MirrorFrameBinding() { close(); }
  MirrorFrameBinding(const MirrorFrameBinding &) = delete;
  MirrorFrameBinding &operator=(const MirrorFrameBinding &) = delete;
  bool valid() const { return context_ != nullptr; }
  int error() const { return valid() ? 0 : static_cast<int>(ERROR_INVALID_STATE); }
  void close() {
    mirror_render_context_destroy(context_);
    context_ = nullptr;
  }
  static int transformRequired() { return browser_capture_transform_required(); }
  void reset() {
    if (!context_) return;
    mirror_render_context_reset(context_);
  }
  std::optional<std::string> convert(RgbaBytes rgba, int width, int height, bool contain) {
    if (!context_) return std::nullopt;
    const size_t length = rgba["byteLength"].as<size_t>();
    if (width <= 0 || height <= 0 || static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4 != length) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Invalid mirror image: %dx%d, %zu bytes", width, height, length);
      return std::nullopt;
    }
    input_.resize(length);
    emscripten::val view = emscripten::val(emscripten::typed_memory_view(length, input_.data()));
    view.call<void>("set", rgba);
    char *frame = mirror_convert_frame(context_, input_.data(), width, height, contain);
    if (!frame) {
      SET_ERRNO(ERROR_DISPLAY, "Mirror frame conversion failed");
      return std::nullopt;
    }
    std::string result(frame);
    mirror_free_string(frame);
    return result;
  }
  emscripten::val processFrame(RgbaBytes rgba, int width, int height, bool contain) {
    emscripten::val result = emscripten::val::object();
    const auto text = convert(rgba, width, height, contain);
    result.set("text", text ? emscripten::val(*text) : emscripten::val::null());
    result.set("width", width);
    result.set("height", height);
    result.set("changed", text.has_value());
    return result;
  }
  int transform(RgbaBytes rgba, int width, int height) {
    if (!context_) return error();
    const size_t length = rgba["byteLength"].as<size_t>();
    if (width <= 0 || height <= 0 || static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4 != length)
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid capture image: %dx%d, %zu bytes", width, height, length);
    input_.resize(length);
    emscripten::val view = emscripten::val(emscripten::typed_memory_view(length, input_.data()));
    view.call<void>("set", rgba);
    const int result = browser_capture_transform_rgba(input_.data(), width, height);
    if (!result) {
      emscripten::val result_view = emscripten::val(emscripten::typed_memory_view(length, input_.data()));
      rgba.call<void>("set", result_view);
    }
    return result;
  }
  void submitAudio(VisualizationSamples samples, int source) {
    if (!context_) {
      SET_ERRNO(ERROR_INVALID_STATE, "Mirror frame binding is closed");
      return;
    }
    if (source != 0 && source != 1) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Invalid audio visualization source: %d", source);
      return;
    }
    const size_t length = samples["length"].as<size_t>();
    if (!length)
      return;
    const size_t chunk_size = std::min(length, size_t{8192});
    audio_.resize(chunk_size);
    for (size_t offset = 0; offset < length;) {
      const size_t count = std::min(chunk_size, length - offset);
      emscripten::val part = samples.call<emscripten::val>("subarray", offset, offset + count);
      emscripten::val view = emscripten::val(emscripten::typed_memory_view(count, audio_.data()));
      view.call<void>("set", part);
      mirror_submit_audio_samples(context_, audio_.data(), static_cast<int>(count), source);
      offset += count;
    }
  }
  std::optional<std::string> renderAudio(unsigned width, unsigned height, int source) {
    if (!context_) return std::nullopt;
    if (!width || !height || (source != 0 && source != 1)) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Invalid audio visualization dimensions or source");
      return std::nullopt;
    }
    char *frame = mirror_render_audio_visualization(context_, width, height, source);
    if (!frame) {
      SET_ERRNO(ERROR_DISPLAY, "Audio visualization rendering failed");
      return std::nullopt;
    }
    std::string result(frame);
    mirror_free_string(frame);
    return result;
  }

private:
  mirror_render_context_t *context_;
  std::vector<uint8_t> input_;
  std::vector<float> audio_;
};

EMSCRIPTEN_BINDINGS(ascii_chat_frame) {
  emscripten::register_type<RgbaBytes>("RgbaBytes", "Uint8Array | Uint8ClampedArray");
  emscripten::register_type<VisualizationSamples>("VisualizationSamples", "Float32Array");
  emscripten::class_<MirrorFrameBinding>("MirrorFrameBinding")
      .constructor<>()
      .function("valid", &MirrorFrameBinding::valid)
      .function("error", &MirrorFrameBinding::error)
      .function("close", &MirrorFrameBinding::close)
      .function("reset", &MirrorFrameBinding::reset)
      .function("convert", &MirrorFrameBinding::convert)
      .function("processFrame", &MirrorFrameBinding::processFrame)
      .function("transform", &MirrorFrameBinding::transform)
      .function("submitAudio", &MirrorFrameBinding::submitAudio)
      .function("renderAudio", &MirrorFrameBinding::renderAudio)
      .class_function("transformRequired", &MirrorFrameBinding::transformRequired);
}
