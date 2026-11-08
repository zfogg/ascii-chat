/**
 * @file options.c
 * @brief WASM shared option snapshots and atomic patches
 * @ingroup web_common
 *
 * Provides unified option access for all WASM modes.
 * These functions are compiled into both mirror.wasm and client.wasm.
 */

#include "options.h"
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/options/runtime.h>
#include <ascii-chat/common.h>
#include <stdio.h>
#include <string.h>

double browser_options_revision(void) {
  return (double)options_revision();
}
asciichat_error_t browser_options_patch(const char *json, double expected_revision) {
  return options_apply_json(json, json ? strlen(json) : 0,
                            expected_revision < 0 ? OPTIONS_REVISION_ANY : (uint64_t)expected_revision,
                            OPTIONS_ORIGIN_LOCAL, NULL);
}
char *browser_options_values(void) {
  options_snapshot_t *snapshot = SAFE_MALLOC(sizeof(*snapshot), options_snapshot_t *);
  char *json = NULL;
  if (options_read_snapshot(snapshot) == ASCIICHAT_OK) {
    char *values = options_values_json(&snapshot->values, false);
    if (values) {
      size_t size = strlen(values) + 96;
      json = SAFE_MALLOC(size, char *);
      snprintf(json, size, "{\"revision\":%llu,\"values\":%s}", (unsigned long long)snapshot->revision, values);
      SAFE_FREE(values);
    }
  }
  SAFE_FREE(snapshot);
  return json;
}
void browser_options_release(char *json) {
  SAFE_FREE(json);
}

// ============================================================================
// Help Text API
// ============================================================================

/**
 * Get help text for a CLI option in a specific mode
 * Exported to WASM for JavaScript access via FFI
 *
 * @param mode The mode (asciichat_mode_t as int)
 * @param option_name The long name of the option
 * @return Help text string or NULL if not applicable
 */
const char *get_help_text(int mode, const char *option_name) {
  if (!option_name || !option_name[0])
    return NULL;
  asciichat_mode_t mode_enum = (asciichat_mode_t)mode;
  const char *result = options_get_help_text(mode_enum, option_name);
  return result;
}
