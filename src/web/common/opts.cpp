#include <emscripten/bind.h>
#ifdef BUILD_WASM_CLIENT
#include "../bindings/client_bytes.h"
extern "C" {
#include "../client.h"
}
#endif
#include <optional>
#include <string>
extern "C" {
#include "options.h"
}

EMSCRIPTEN_DECLARE_VAL_TYPE(OptionsPatch);
EMSCRIPTEN_DECLARE_VAL_TYPE(OptionsSnapshot);

static int patch_options(OptionsPatch patch, double expected_revision) {
  emscripten::val object = patch;
  if (object.isNull() || object.isUndefined() || object.typeOf().as<std::string>() != "object" ||
      emscripten::val::global("Array").call<bool>("isArray", object))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Options patch must be an object");
  emscripten::val json = emscripten::val::global("JSON").call<emscripten::val>("stringify", object);
  if (json.isUndefined())
    return SET_ERRNO(ERROR_INVALID_PARAM, "Options patch cannot be serialized");
  std::string text = json.as<std::string>();
  return browser_options_patch(text.c_str(), expected_revision);
}
static OptionsSnapshot read_options() {
  char *json = browser_options_values();
  if (!json) {
    SET_ERRNO(ERROR_INVALID_STATE, "Cannot read browser options");
    return OptionsSnapshot(emscripten::val::null());
  }
  std::string text(json);
  browser_options_release(json);
  return OptionsSnapshot(emscripten::val::global("JSON").call<emscripten::val>("parse", text));
}
static std::optional<std::string> option_help(int mode, const std::string &name) {
  const char *text = get_help_text(mode, name.c_str());
  return text ? std::optional<std::string>(text) : std::nullopt;
}

#ifdef BUILD_WASM_CLIENT
static ClientSettingsResult receive_settings(ClientBytes payload) {
  auto input = binding_copy_input(payload);
  const int code = client_settings_receive(input.data(), input.size());
  char *json = code == 0 ? client_settings_values() : nullptr;
  emscripten::val result = emscripten::val::object();
  result.set("code", code);
  result.set("json", json ? emscripten::val(std::string(json)) : emscripten::val::null());
  if (json)
    browser_options_release(json);
  return ClientSettingsResult(result);
}
static std::optional<std::string> client_settings() {
  char *json = client_settings_values();
  if (!json)
    return std::nullopt;
  std::string result(json);
  browser_options_release(json);
  return result;
}
#endif

EMSCRIPTEN_BINDINGS(ascii_chat_options) {
#ifdef BUILD_WASM_CLIENT
  emscripten::register_type<ClientSettingsResult>("ClientSettingsResult", "{ code: number; json: string | null }");
  emscripten::function("clientSettings", &client_settings);
  emscripten::function("receiveSettings", &receive_settings);
#endif
  emscripten::function("browser_options_revision", &browser_options_revision);
  emscripten::register_optional<std::string>();
  emscripten::register_type<OptionsPatch>("OptionsPatch", "Record<string, number | string | boolean>");
  emscripten::register_type<OptionsSnapshot>(
      "OptionsSnapshot", "{ revision: number; values: Record<string, number | string | boolean> } | null");
  emscripten::function("patchOptions", &patch_options);
  emscripten::function("readOptions", &read_options);
  emscripten::function("optionHelp", &option_help);
}
