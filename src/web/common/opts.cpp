#include <emscripten/bind.h>
#include "opts.h"
#include <optional>
#include <string>
extern "C" {
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/options/runtime.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
}

EMSCRIPTEN_DECLARE_VAL_TYPE(OptionsSnapshot);

int patch_options(OptionsPatch patch, double expected_revision) {
  emscripten::val object = patch;
  if (object.isNull() || object.isUndefined() || object.typeOf().as<std::string>() != "object" ||
      emscripten::val::global("Array").call<bool>("isArray", object))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Options patch must be an object");
  emscripten::val json = emscripten::val::global("JSON").call<emscripten::val>("stringify", object);
  if (json.isUndefined())
    return SET_ERRNO(ERROR_INVALID_PARAM, "Options patch cannot be serialized");
  std::string text = json.as<std::string>();
  return options_apply_json(text.c_str(), text.size(),
                            expected_revision < 0 ? OPTIONS_REVISION_ANY : static_cast<uint64_t>(expected_revision),
                            OPTIONS_ORIGIN_LOCAL, nullptr);
}
int initial_options(OptionsPatch patch) {
  emscripten::val object = patch;
  if (!object.isNull() && !object.isUndefined() && object.typeOf().as<std::string>() == "object" &&
      !emscripten::val::global("Array").call<bool>("isArray", object) &&
      emscripten::val::global("Object").call<emscripten::val>("keys", object)["length"].as<unsigned>() == 0)
    return ASCIICHAT_OK;
  int result = patch_options(patch, -1);
  if (result == ASCIICHAT_OK) {
    options_snapshot_t *snapshot = SAFE_MALLOC(sizeof(*snapshot), options_snapshot_t *);
    if (options_read_snapshot(snapshot) == ASCIICHAT_OK)
      log_set_level(snapshot->values.log_level);
    SAFE_FREE(snapshot);
  }
  return result;
}
static OptionsSnapshot read_options() {
  options_snapshot_t *snapshot = SAFE_MALLOC(sizeof(*snapshot), options_snapshot_t *);
  if (options_read_snapshot(snapshot) != ASCIICHAT_OK) {
    SAFE_FREE(snapshot);
    SET_ERRNO(ERROR_INVALID_STATE, "Cannot read browser options");
    return OptionsSnapshot(emscripten::val::null());
  }
  char *json = options_values_json(&snapshot->values, false);
  double revision = static_cast<double>(snapshot->revision);
  SAFE_FREE(snapshot);
  if (!json) {
    SET_ERRNO(ERROR_INVALID_STATE, "Cannot serialize browser options");
    return OptionsSnapshot(emscripten::val::null());
  }
  std::string text(json);
  SAFE_FREE(json);
  emscripten::val result = emscripten::val::object();
  result.set("revision", revision);
  result.set("values", emscripten::val::global("JSON").call<emscripten::val>("parse", text));
  return OptionsSnapshot(result);
}
static std::optional<std::string> option_help(int mode, const std::string &name) {
  const char *text = name.empty() ? nullptr : options_get_help_text(static_cast<asciichat_mode_t>(mode), name.c_str());
  return text ? std::optional<std::string>(text) : std::nullopt;
}

EMSCRIPTEN_BINDINGS(ascii_chat_options) {
  emscripten::function("browser_options_revision", +[]() { return static_cast<double>(options_revision()); });
  emscripten::register_optional<std::string>();
  emscripten::register_type<OptionsPatch>("OptionsPatch", "Record<string, number | string | boolean>");
  emscripten::register_type<OptionsSnapshot>(
      "OptionsSnapshot", "{ revision: number; values: Record<string, number | string | boolean> } | null");
  emscripten::function("patchOptions", &patch_options);
  emscripten::function("readOptions", &read_options);
  emscripten::function("optionHelp", &option_help);
}
