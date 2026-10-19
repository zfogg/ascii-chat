#pragma once

#include <ascii-chat/options/options.h>
#include <stdint.h>

/** Runtime edits are applied to the latest snapshot under the writer mutex. */
typedef enum { OPTIONS_VALUE_INT, OPTIONS_VALUE_BOOL, OPTIONS_VALUE_DOUBLE, OPTIONS_VALUE_STRING } options_value_type_t;
typedef enum { OPTIONS_ASSIGN, OPTIONS_TOGGLE, OPTIONS_ADD, OPTIONS_CYCLE } options_operation_t;
typedef enum { OPTIONS_ORIGIN_LOCAL, OPTIONS_ORIGIN_REMOTE, OPTIONS_ORIGIN_CONFIG } options_origin_t;

typedef struct {
  const char *name;
  options_value_type_t type;
  options_operation_t operation;
  union {
    int integer;
    bool boolean;
    double number;
    const char *string;
  } value;
} options_edit_t;

#define OPTIONS_REVISION_ANY UINT64_MAX
#define OPTIONS_PATCH_MAX_EDITS 32

typedef struct {
  const options_edit_t *edits;
  size_t count;
  uint64_t expected_revision;
  options_origin_t origin;
} options_patch_t;

typedef struct {
  uint64_t revision;
  bool changed;
} options_patch_result_t;
typedef struct {
  options_t values;
  uint64_t revision;
  options_origin_t origin;
} options_snapshot_t;

/** No callbacks or I/O run under the writer mutex. A rejected patch publishes nothing. */
asciichat_error_t options_apply_patch(const options_patch_t *patch, options_patch_result_t *result);
/** Prepare caller-owned state. The owner serializes access; remote edits use an allowlist. */
asciichat_error_t options_prepare_patch(const options_t *current, const options_patch_t *patch, options_t *candidate,
                                        bool remote_only);
/** Capture values and revision together. Callers own the copied snapshot. */
asciichat_error_t options_read_snapshot(options_snapshot_t *snapshot);
/** Observers coalesce changes by reading a new snapshot when this revision changes. */
uint64_t options_revision(void);
asciichat_error_t options_toggle(const char *name);
asciichat_error_t options_adjust(const char *name, double amount);
asciichat_error_t options_cycle(const char *name, int count);

/** Only explicitly exposed fields may enter a remote rendering patch. */
bool options_remote_render_field(const char *name);
asciichat_error_t options_prepare_json(const options_t *current, const char *json, size_t length, options_t *candidate,
                                       bool remote_only);
asciichat_error_t options_apply_json(const char *json, size_t length, uint64_t expected_revision,
                                     options_origin_t origin, options_patch_result_t *result);
/** Caller releases JSON with SAFE_FREE. Only public runtime fields are serialized. */
char *options_values_json(const options_t *values, bool remote_only);
