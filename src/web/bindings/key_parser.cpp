#include <emscripten/bind.h>
extern "C" {
#include "../client.h"
#include "../common/errors.h"
#include <ascii-chat/util/path.h>
}
#include <sodium.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

EMSCRIPTEN_DECLARE_VAL_TYPE(KeyInput);
EMSCRIPTEN_DECLARE_VAL_TYPE(KeyParseResult);
EMSCRIPTEN_DECLARE_VAL_TYPE(ErrorFrameResult);

static emscripten::val frame_value(const asciichat_error_context_t &frame) {
  emscripten::val result = emscripten::val::object();
  result.set("code", static_cast<int>(frame.code));
  result.set("message", std::string(frame.context_message));
  result.set("file", std::string(frame.file ? extract_project_relative_path(frame.file) : ""));
#ifdef NDEBUG
  result.set("fullFile", std::string());
#else
  result.set("fullFile", std::string(frame.file ? frame.file : ""));
#endif
  result.set("function", std::string(frame.function ? frame.function : ""));
  result.set("line", frame.line);
  return result;
}

static KeyParseResult parse_key(int kind, bool binary, KeyInput input) {
  const asciichat_errno_scope_t checkpoint = errno_checkpoint();
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
    if (!errno_peek_since(checkpoint, &context))
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
  emscripten::val frames = emscripten::val::array();
  if (code != 0) {
    asciichat_error_context_t contexts[ASCIICHAT_ERRNO_MAX_DEPTH];
    const asciichat_errno_snapshot_t snapshot =
        errno_snapshot(checkpoint.thread_id, contexts, ASCIICHAT_ERRNO_MAX_DEPTH);
    unsigned index = 0;
    for (size_t i = 0; i < snapshot.copied; ++i) {
      if (contexts[i].generation == checkpoint.generation && contexts[i].error_id > checkpoint.after_id)
        frames.set(index++, frame_value(contexts[i]));
    }
  }
  result.set("frames", frames);
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
  emscripten::register_type<KeyParseResult>("KeyParseResult",
                                            "{ code: number; bytes: Uint8Array; frames: ErrorFrameResult[] }");
  emscripten::register_type<ErrorFrameResult>(
      "ErrorFrameResult",
      "{ code: number; message: string; file: string; fullFile: string; function: string; line: number }");
  emscripten::function("parseKey", &parse_key);
  emscripten::function("keyParserError", &parser_error);
  emscripten::function("errorSnapshot", &browser_error_snapshot);
  emscripten::function("errorFrame", &error_frame);
}
