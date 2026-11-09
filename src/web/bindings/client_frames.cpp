#include <emscripten/bind.h>
#include "client_bytes.h"
extern "C" {
#include "../client.h"
}
#include <cstdint>
#include <vector>

class ClientFrameBinding {
public:
  static ClientFrameResult validate(ClientBytes encoded, unsigned width, unsigned height, unsigned checksum,
                                    unsigned flags, ClientBytes contract, unsigned original_size) {
  const size_t input_size = encoded["length"].as<size_t>();
  const size_t metadata_size = contract["length"].as<size_t>();
  const bool compressed = (flags & 2) != 0;
  if (!input_size || input_size > 16 * 1024 * 1024 || !original_size || original_size > 16 * 1024 * 1024 ||
      metadata_size > 275 || (!compressed && input_size != original_size)) {
    emscripten::val result = emscripten::val::object();
    result.set("code", static_cast<int>(SET_ERRNO(ERROR_INVALID_PARAM, "Invalid encoded frame or metadata size")));
    result.set("decodeFailed", false);
    result.set("bytes", binding_copy_output(nullptr, 0));
    return ClientFrameResult(result);
  }
  auto input = binding_copy_input(encoded);
  auto metadata = binding_copy_input(contract);
  std::vector<uint8_t> decoded(compressed ? original_size : 0);
  const uint8_t *text = compressed ? decoded.data() : input.data();
  int code = compressed ? client_decode_frame_data(input.data(), input.size(), decoded.data(), decoded.size()) : 0;
  const bool decode_failed = code != 0;
  if (!code)
    code = client_validate_frame(reinterpret_cast<const char *>(text), original_size, width, height, checksum, flags,
                                 metadata.empty() ? nullptr : metadata.data(), metadata.size());
  emscripten::val result = emscripten::val::object();
  result.set("code", code);
  result.set("decodeFailed", decode_failed);
  result.set("bytes", binding_copy_output(text, code == 0 ? original_size : 0));
  return ClientFrameResult(result);
  }
};

EMSCRIPTEN_BINDINGS(ascii_chat_client_frames) {
  emscripten::register_type<ClientBytes>("ClientBytes", "Uint8Array");
  emscripten::register_type<ClientFrameResult>("ClientFrameResult",
                                               "{ code: number; decodeFailed: boolean; bytes: Uint8Array }");
  emscripten::class_<ClientFrameBinding>("ClientFrameBinding")
      .class_function("validate", &ClientFrameBinding::validate);
}
