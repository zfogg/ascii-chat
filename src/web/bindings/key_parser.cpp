#include <emscripten/bind.h>
extern "C" {
#include "../client.h"
#include "../common/errors.h"
}
#include <sodium.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

EMSCRIPTEN_DECLARE_VAL_TYPE(KeyInput);
EMSCRIPTEN_DECLARE_VAL_TYPE(KeyParseResult);
EMSCRIPTEN_DECLARE_VAL_TYPE(ErrorFrameResult);

static KeyParseResult parse_key(int kind, bool binary, KeyInput input) {
  const size_t length = input["length"].as<size_t>();
  std::vector<uint8_t> bytes(length + 1, 0);
  if (length) {
    emscripten::val view = emscripten::val(emscripten::typed_memory_view(length, bytes.data()));
    view.call<void>("set", input);
  }
  uint8_t output[64] = {0};
  int code = -1;
  switch (kind) {
  case 0:
    code = client_parse_ssh_private_key(reinterpret_cast<const char *>(bytes.data()), output);
    break;
  case 1:
    code = client_parse_ssh_public_key(reinterpret_cast<const char *>(bytes.data()), output);
    break;
  case 2:
    code = binary ? client_parse_gpg_private_key_binary(bytes.data(), length, output)
                  : client_parse_gpg_private_key(reinterpret_cast<const char *>(bytes.data()), output);
    break;
  case 3:
    code = binary ? client_parse_gpg_public_key_binary(bytes.data(), length, output)
                  : client_parse_gpg_public_key(reinterpret_cast<const char *>(bytes.data()), output);
    break;
  default:
    code = SET_ERRNO(ERROR_INVALID_PARAM, "Invalid key parser kind: %d", kind);
    break;
  }
  if (code != 0 && kind >= 0 && kind <= 3) {
    asciichat_error_context_t context;
    if (!HAS_ERRNO(&context))
      SET_ERRNO(ERROR_CRYPTO_KEY, "Cannot parse browser key");
  }
  const size_t output_length = code == 0 ? (kind == 0 || kind == 2 ? 64 : 32) : 0;
  emscripten::val result_bytes = emscripten::val::global("Uint8Array").new_(output_length);
  if (output_length)
    result_bytes.call<void>("set", emscripten::val(emscripten::typed_memory_view(output_length, output)));
  sodium_memzero(output, sizeof(output));
  sodium_memzero(bytes.data(), bytes.size());
  emscripten::val result = emscripten::val::object();
  result.set("code", code);
  result.set("bytes", result_bytes);
  return KeyParseResult(result);
}

static std::string parser_error() {
  char buffer[1024] = {0};
  const int status = client_get_crypto_error_message(buffer, sizeof(buffer));
  std::string result = status == 0 ? buffer : "Invalid SSH key";
  sodium_memzero(buffer, sizeof(buffer));
  return result;
}

static ErrorFrameResult error_frame(size_t index) {
  emscripten::val result = emscripten::val::object();
  result.set("code", browser_error_code(index));
  result.set("message", std::string(browser_error_message(index)));
  result.set("file", std::string(browser_error_file(index)));
  result.set("fullFile", std::string(browser_error_full_file(index)));
  result.set("function", std::string(browser_error_function(index)));
  result.set("line", browser_error_line(index));
  return ErrorFrameResult(result);
}

EMSCRIPTEN_BINDINGS(ascii_chat_key_parser) {
  emscripten::function("browser_error_dismiss", &browser_error_dismiss);
  emscripten::register_type<KeyInput>("KeyInput", "Uint8Array");
  emscripten::register_type<KeyParseResult>("KeyParseResult", "{ code: number; bytes: Uint8Array }");
  emscripten::register_type<ErrorFrameResult>(
      "ErrorFrameResult",
      "{ code: number; message: string; file: string; fullFile: string; function: string; line: number }");
  emscripten::function("parseKey", &parse_key);
  emscripten::function("keyParserError", &parser_error);
  emscripten::function("errorSnapshot", &browser_error_snapshot);
  emscripten::function("errorFrame", &error_frame);
}
