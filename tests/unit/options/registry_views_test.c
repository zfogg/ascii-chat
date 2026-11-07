#include <criterion/criterion.h>
#include <ascii-chat/options/registry.h>
#include <ascii-chat/options/registry/core.h>
#include <ascii-chat/common.h>

static void setup(void) {
  log_init(NULL, LOG_ERROR, false, false);
}
static void cleanup(void) {
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TestSuite(registry_views, .init = setup, .fini = cleanup);

Test(registry_views, mode_views_have_unique_names_and_expose_only_matching_options) {
  const asciichat_mode_t modes[] = {MODE_SERVER,    MODE_CLIENT,    MODE_MIRROR, MODE_DISCOVERY_SERVICE,
                                    MODE_DISCOVERY, MODE_MCP_SERVER};
  const option_mode_bitmask_t masks[] = {OPTION_MODE_SERVER,        OPTION_MODE_CLIENT,    OPTION_MODE_MIRROR,
                                         OPTION_MODE_DISCOVERY_SVC, OPTION_MODE_DISCOVERY, OPTION_MODE_MCP_SERVER};
  registry_init_size();
  const registry_entry_t *raw = g_options_registry;
  size_t total = g_registry_size;
  cr_assert_not_null(raw);
  cr_assert_gt(total, 0);
  cr_assert_null(raw[total].long_name);
  for (size_t mode = 0; mode < ARRAY_SIZE(modes); ++mode) {
    size_t count;
    const option_descriptor_t *view = options_registry_get_for_mode(modes[mode], &count);
    cr_assert_not_null(view);
    cr_assert_gt(count, 0);
    size_t expected = 0;
    for (size_t i = 0; i < total; ++i)
      if (raw[i].mode_bitmask & masks[mode])
        ++expected;
    cr_assert_eq(count, expected);
    for (size_t i = 0; i < count; ++i) {
      cr_assert_not_null(view[i].long_name);
      cr_assert(view[i].mode_bitmask & masks[mode]);
      for (size_t j = i + 1; j < count; ++j)
        cr_assert_str_neq(view[i].long_name, view[j].long_name);
      const option_descriptor_t *named = options_registry_find_by_name(view[i].long_name);
      cr_assert_not_null(named);
      cr_assert_eq(named->type, view[i].type);
      if (view[i].short_name)
        cr_assert_not_null(options_registry_find_by_short(view[i].short_name));
    }
    SAFE_FREE(view);
  }
  size_t count;
  const option_descriptor_t *binary = options_registry_get_binary_options(&count);
  cr_assert_not_null(binary);
  cr_assert_gt(count, 0);
  for (size_t i = 0; i < count; ++i)
    cr_assert(binary[i].mode_bitmask & OPTION_MODE_BINARY);
  SAFE_FREE(binary);
}

Test(registry_views, metadata_queries_distinguish_enum_numeric_and_absent_options) {
  registry_init_size();
  const registry_entry_t *raw = g_options_registry;
  for (size_t i = 0; i < g_registry_size; ++i) {
    const option_metadata_t *meta = options_registry_get_metadata(raw[i].long_name);
    cr_assert_not_null(meta);
    cr_assert_eq(options_registry_get_input_type(raw[i].long_name), meta->input_type);
    int min = -1, max = -1, step = -1;
    bool numeric = options_registry_get_numeric_range(raw[i].long_name, &min, &max, &step);
    cr_assert_eq(numeric, meta->input_type == OPTION_INPUT_NUMERIC);
    if (numeric) {
      cr_assert_eq(min, meta->numeric_range.min);
      cr_assert_eq(max, meta->numeric_range.max);
      cr_assert_eq(step, meta->numeric_range.step);
    } else {
      cr_assert_eq(min, 0);
      cr_assert_eq(max, 0);
      cr_assert_eq(step, 0);
    }
    size_t count;
    const char **examples = options_registry_get_examples(raw[i].long_name, &count);
    if (examples) {
      cr_assert_gt(count, 0);
      cr_assert_null(examples[count]);
    } else
      cr_assert_eq(count, 0);
    const char **descriptions = NULL;
    const char **values = options_registry_get_enum_values(raw[i].long_name, &descriptions, &count);
    if (values) {
      cr_assert_eq(meta->input_type, OPTION_INPUT_ENUM);
      cr_assert_gt(count, 0);
      cr_assert_null(values[count]);
    } else
      cr_assert_eq(count, 0);
    CLEAR_ERRNO_ALL();
  }
}

Test(registry_views, rejected_queries_return_empty_results_and_do_not_dereference_outputs) {
  size_t count = 99;
  cr_assert_null(options_registry_get_for_mode((asciichat_mode_t)-1, &count));
  cr_assert_eq(count, 0);
  cr_assert_null(options_registry_get_for_mode(MODE_CLIENT, NULL));
  cr_assert_null(options_registry_get_binary_options(NULL));
  cr_assert_null(options_registry_get_for_display(MODE_CLIENT, false, NULL));
  cr_assert_null(options_registry_find_by_name(NULL));
  cr_assert_null(options_registry_find_by_name("not-an-option"));
  cr_assert_not_null(options_registry_find_by_name("config"));
  cr_assert_null(options_registry_find_by_short(0));
  cr_assert_null(options_registry_find_by_short('\1'));
  cr_assert_null(options_registry_get_metadata(NULL));
  cr_assert_eq(options_registry_get_input_type(NULL), OPTION_INPUT_NONE);
  cr_assert_null(options_registry_get_enum_values(NULL, NULL, &count));
  cr_assert_eq(count, 0);
  cr_assert_null(options_registry_get_enum_values("color-mode", NULL, NULL));
  cr_assert_null(options_registry_get_examples(NULL, &count));
  cr_assert_eq(count, 0);
  cr_assert_null(options_registry_get_examples("color-mode", NULL));
  int min, max, step;
  cr_assert_not(options_registry_get_numeric_range(NULL, &min, &max, &step));
  cr_assert_not(options_registry_get_numeric_range("width", NULL, &max, &step));
  cr_assert_not(options_registry_get_numeric_range("width", &min, NULL, &step));
  cr_assert_not(options_registry_get_numeric_range("width", &min, &max, NULL));
}
