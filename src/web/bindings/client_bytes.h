#pragma once
#include <emscripten/bind.h>
#include "byte_arrays.h"
#include <cstdint>
#include <vector>
EMSCRIPTEN_DECLARE_VAL_TYPE(ClientBytes);
EMSCRIPTEN_DECLARE_VAL_TYPE(ClientPacketResult);
EMSCRIPTEN_DECLARE_VAL_TYPE(ClientSettingsResult);
EMSCRIPTEN_DECLARE_VAL_TYPE(ClientFrameResult);

inline ClientPacketResult packet_result(int code, const uint8_t *data, size_t length) {
  emscripten::val result = emscripten::val::object();
  result.set("code", code);
  result.set("bytes", binding_copy_output(data, length));
  return ClientPacketResult(result);
}
