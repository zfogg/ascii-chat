#include <emscripten/bind.h>
#include "../bindings/client_bytes.h"
#include <optional>
#include <string>
extern "C" {
#include "../client.h"
#include <ascii-chat/common.h>
}

static ClientSettingsResult receive_settings(ClientBytes payload) {
  auto input = binding_copy_input(payload);
  const int code = client_settings_receive(input.data(), input.size());
  char *json = code == 0 ? client_settings_values() : nullptr;
  emscripten::val result = emscripten::val::object();
  result.set("code", code);
  result.set("json", json ? emscripten::val(std::string(json)) : emscripten::val::null());
  if (json)
    SAFE_FREE(json);
  return ClientSettingsResult(result);
}
static std::optional<std::string> client_settings() {
  char *json = client_settings_values();
  if (!json)
    return std::nullopt;
  std::string result(json);
  SAFE_FREE(json);
  return result;
}

EMSCRIPTEN_BINDINGS(ascii_chat_client_options) {
  emscripten::register_type<ClientSettingsResult>("ClientSettingsResult", "{ code: number; json: string | null }");
  emscripten::function("clientSettings", &client_settings);
  emscripten::function("receiveSettings", &receive_settings);
}
