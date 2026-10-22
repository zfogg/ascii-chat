#include <criterion/criterion.h>
#include <ascii-chat/options/config.h>
#include <ascii-chat/options/schema.h>
#include <ascii-chat/options/builder.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/log/log.h>
#include <string.h>

static char path[PLATFORM_MAX_PATH_LENGTH];
static options_config_t *schema;
static void setup(void) {
  log_init(NULL, LOG_FATAL, false, false);
  log_set_terminal_output(false);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  options_builder_t *b = options_builder_create(sizeof(options_t));
  cr_assert_not_null(b);
  const struct {
    const char *name;
    size_t offset;
  } fields[] = {{"color-mode", offsetof(options_t, color_mode)},
                {"render-mode", offsetof(options_t, render_mode)},
                {"palette-type", offsetof(options_t, palette_type)},
                {"color-filter", offsetof(options_t, color_filter)},
                {"fps", offsetof(options_t, fps)}};
  for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); ++i) {
    options_builder_add_int(b, fields[i].name, 0, fields[i].offset, 0, "Typed field", "Contract", false, NULL, NULL);
    options_builder_set_mode_bitmask(b, OPTION_MODE_ALL);
  }
  options_builder_add_bool(b, "stretch", 0, offsetof(options_t, stretch), false, "Stretch", "Contract", false, NULL);
  options_builder_set_mode_bitmask(b, OPTION_MODE_ALL);
  options_builder_add_double(b, "snapshot-delay", 0, offsetof(options_t, snapshot_delay), 0, "Delay", "Contract", false,
                             NULL, NULL, false);
  options_builder_set_mode_bitmask(b, OPTION_MODE_ALL);
  options_builder_add_string(b, "log-file", 0, offsetof(options_t, log_file), NULL, "Log", "Contract", false, NULL,
                             NULL);
  options_builder_set_mode_bitmask(b, OPTION_MODE_ALL);
  schema = options_builder_build(b);
  options_builder_destroy(b);
  cr_assert_not_null(schema);
  const options_config_t *configs[] = {schema};
  cr_assert_eq(config_schema_build_from_configs(configs, 1), ASCIICHAT_OK);
  int fd = -1;
  cr_assert_eq(platform_create_temp_file(path, sizeof(path), "typed-config", &fd), 0);
  cr_assert_eq(platform_close(fd), 0);
}
static void teardown(void) {
  platform_delete_temp_file(path);
  config_schema_destroy();
  options_config_destroy(schema);
  options_state_destroy();
  log_destroy();
}
TestSuite(config_typed_contract, .init = setup, .fini = teardown);
static asciichat_error_t load(const char *toml, bool strict, options_t *options) {
  FILE *file = fopen(path, "w");
  cr_assert_not_null(file);
  cr_assert_eq(fwrite(toml, 1, strlen(toml), file), strlen(toml));
  cr_assert_eq(fclose(file), 0);
  return config_load_and_apply(MODE_MIRROR, path, strict, options);
}
Test(config_typed_contract, typed_toml_loads_enum_names_scalars_and_escaped_strings) {
  options_t options = options_t_new();
  const char *toml =
      "[contract]\ncolor_mode='truecolor'\nrender_mode='background'\npalette_type='blocks'\ncolor_filter='none'\n"
      "fps=24\nstretch=true\nsnapshot_delay=1.25\nlog_file=\"folder\\\\trace\\\"name.log\"\n";
  cr_assert_eq(load(toml, true, &options), ASCIICHAT_OK);
  cr_assert_eq(options.color_mode, TERM_COLOR_TRUECOLOR);
  cr_assert_eq(options.render_mode, RENDER_MODE_BACKGROUND);
  cr_assert_eq(options.palette_type, PALETTE_BLOCKS);
  cr_assert_eq(options.color_filter, COLOR_FILTER_NONE);
  cr_assert_eq(options.fps, 24);
  cr_assert(options.stretch);
  cr_assert_float_eq(options.snapshot_delay, 1.25, 1e-9);
  cr_assert_str_eq(options.log_file, "folder\\trace\"name.log");
  cr_assert_eq(options_get()->fps, 24);
}
Test(config_typed_contract, numeric_enum_compatibility_and_invalid_values_preserve_fields) {
  const struct {
    const char *key;
    size_t offset;
    int valid;
  } fields[] = {{"color_mode", offsetof(options_t, color_mode), TERM_COLOR_256},
                {"color_mode", offsetof(options_t, color_mode), TERM_COLOR_AUTO},
                {"color_mode", offsetof(options_t, color_mode), TERM_COLOR_NONE},
                {"color_mode", offsetof(options_t, color_mode), TERM_COLOR_16},
                {"color_mode", offsetof(options_t, color_mode), TERM_COLOR_TRUECOLOR},
                {"render_mode", offsetof(options_t, render_mode), RENDER_MODE_HALF_BLOCK},
                {"render_mode", offsetof(options_t, render_mode), RENDER_MODE_FOREGROUND},
                {"render_mode", offsetof(options_t, render_mode), RENDER_MODE_BACKGROUND},
                {"palette_type", offsetof(options_t, palette_type), PALETTE_DIGITAL},
                {"color_filter", offsetof(options_t, color_filter), COLOR_FILTER_NONE}};
  for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); ++i) {
    options_t options = options_t_new();
    char toml[256];
    int actual;
    safe_snprintf(toml, sizeof(toml), "[contract]\n%s=%d\n", fields[i].key, fields[i].valid);
    cr_assert_eq(load(toml, true, &options), ASCIICHAT_OK, "%s", fields[i].key);
    memcpy(&actual, (char *)&options + fields[i].offset, sizeof(actual));
    cr_assert_eq(actual, fields[i].valid);
    const char *invalid[] = {"9999", "-9999", "'invalid'", "'99999999999999999999999999'", "'12suffix'"};
    for (size_t j = 0; j < sizeof(invalid) / sizeof(*invalid); ++j) {
      safe_snprintf(toml, sizeof(toml), "[contract]\n%s=%s\n", fields[i].key, invalid[j]);
      cr_assert_eq(load(toml, true, &options), ERROR_CONFIG, "%s=%s", fields[i].key, invalid[j]);
      memcpy(&actual, (char *)&options + fields[i].offset, sizeof(actual));
      cr_assert_eq(actual, fields[i].valid);
      cr_assert_eq(load(toml, false, &options), ASCIICHAT_OK);
      memcpy(&actual, (char *)&options + fields[i].offset, sizeof(actual));
      cr_assert_eq(actual, fields[i].valid);
    }
  }
}
Test(config_typed_contract, malformed_scalars_and_wrong_toml_types_do_not_replace_existing_values) {
  options_t options = options_t_new();
  options.fps = 30;
  options.snapshot_delay = 2;
  const char *invalid[] = {"fps='junk'", "fps=2147483648", "fps=-2147483649", "snapshot_delay='bad'"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
    char toml[256];
    safe_snprintf(toml, sizeof(toml), "[contract]\n%s\n", invalid[i]);
    cr_assert_eq(load(toml, true, &options), ERROR_CONFIG, "%s", invalid[i]);
    cr_assert_eq(options.fps, 30);
    cr_assert_float_eq(options.snapshot_delay, 2, 1e-9);
  }
  cr_assert_eq(load("[contract]\nfps=[1,2]\nstretch={x=1}\nlog_file=[1]\n", false, &options), ASCIICHAT_OK);
  cr_assert_eq(options.fps, 30);
  cr_assert_not(options.stretch);
}
