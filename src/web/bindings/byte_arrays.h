#pragma once
#include <emscripten/val.h>
#include <cstdint>
#include <vector>

inline std::vector<uint8_t> binding_copy_input(emscripten::val bytes) {
  const size_t length = bytes["length"].as<size_t>();
  std::vector<uint8_t> copy;
  copy.reserve(length + 1);
  copy.resize(length);
  if (length) {
    emscripten::val view = emscripten::val(emscripten::typed_memory_view(length, copy.data()));
    view.call<void>("set", bytes);
  }
  return copy;
}

inline emscripten::val binding_copy_output(const void *data, size_t length) {
  emscripten::val output = emscripten::val::global("Uint8Array").new_(length);
  if (length) {
    emscripten::val view = emscripten::val(emscripten::typed_memory_view(length, static_cast<const uint8_t *>(data)));
    output.call<void>("set", view);
  }
  return output;
}
