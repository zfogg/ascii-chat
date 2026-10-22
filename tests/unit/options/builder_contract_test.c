#include <criterion/criterion.h>
#include <ascii-chat/options/builder.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/tests/logging.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  options_t base;
  bool enabled, companion;
  int count;
  double ratio;
  char label[OPTIONS_BUFF_SIZE];
  uintptr_t custom;
} sample_t;

static unsigned actions;
static void action(void) {
  actions++;
}
static bool parse_custom(const char *argument, void *destination, char **error) {
  if (!argument) {
    *(uintptr_t *)destination = 99;
    return true;
  }
  char *end;
  errno = 0;
  unsigned long value = strtoul(argument, &end, 10);
  if (errno || !*argument || *end) {
    SAFE_STRDUP(*error, "Expected an unsigned integer");
    return false;
  }
  *(uintptr_t *)destination = value;
  return true;
}

static options_builder_t *builder(void) {
  options_builder_t *b = options_builder_create(sizeof(sample_t));
  cr_assert_not_null(b);
  options_builder_add_bool(b, "enabled", 'e', offsetof(sample_t, enabled), false, "Enable", "Test", false,
                           "ASCII_TEST_BUILDER_BOOL");
  options_builder_set_mode_bitmask(b, OPTION_MODE_ALL);
  options_builder_add_bool(b, "companion", 0, offsetof(sample_t, companion), false, "Companion", "Test", false, NULL);
  options_builder_set_mode_bitmask(b, OPTION_MODE_ALL);
  options_builder_add_int(b, "count", 'c', offsetof(sample_t, count), 7, "Count", "Test", false,
                          "ASCII_TEST_BUILDER_INT", NULL);
  options_builder_set_mode_bitmask(b, OPTION_MODE_ALL);
  options_builder_add_double(b, "ratio", 'r', offsetof(sample_t, ratio), .5, "Ratio", "Test", false,
                             "ASCII_TEST_BUILDER_DOUBLE", NULL, false);
  options_builder_set_mode_bitmask(b, OPTION_MODE_ALL);
  options_builder_add_string(b, "label", 'l', offsetof(sample_t, label), "default", "Label", "Test", false,
                             "ASCII_TEST_BUILDER_STRING", NULL);
  options_builder_set_mode_bitmask(b, OPTION_MODE_ALL);
  static const uintptr_t initial = 17;
  options_builder_add_callback_optional(b, "custom", 'u', offsetof(sample_t, custom), &initial, sizeof(initial),
                                        parse_custom, "Custom", "Test", false, NULL, true);
  options_builder_set_mode_bitmask(b, OPTION_MODE_ALL);
  options_builder_add_action(b, "act", 'a', action, "Act", "Test");
  options_builder_set_mode_bitmask(b, OPTION_MODE_ALL);
  return b;
}

static options_config_t *config(void) {
  options_builder_t *b = builder();
  options_config_t *c = options_builder_build(b);
  options_builder_destroy(b);
  cr_assert_not_null(c);
  return c;
}

static sample_t defaults(void) {
  sample_t result = {.count = 7, .ratio = .5, .custom = 17};
  SAFE_STRNCPY(result.label, "default", sizeof(result.label));
  return result;
}

Test(builder_contract, typed_cli_values_short_flags_and_optional_callback) {
  options_config_t *c = config();
  sample_t sample = defaults();
  char *args[] = {"program", "--enabled=yes", "-c", "-12", "--ratio=1.25", "--label", "-", "--custom", "--act"};
  int remaining = -1;
  char **remainder = args;
  actions = 0;
  cr_assert_eq(options_config_parse(c, sizeof(args) / sizeof(args[0]), args, &sample, OPTION_MODE_CLIENT, &remaining,
                                    &remainder),
               ASCIICHAT_OK);
  cr_assert(sample.enabled);
  cr_assert_eq(sample.count, -12);
  cr_assert_float_eq(sample.ratio, 1.25, 1e-9);
  cr_assert_str_eq(sample.label, "-");
  cr_assert_eq(sample.custom, 99);
  cr_assert_eq(actions, 1);
  cr_assert_eq(remaining, 0);
  cr_assert_null(remainder);
  char *second[] = {"program", "-e", "false", "-u", "42"};
  cr_assert_eq(options_config_parse(c, 5, second, &sample, OPTION_MODE_CLIENT, NULL, NULL), ASCIICHAT_OK);
  cr_assert_not(sample.enabled);
  cr_assert_eq(sample.custom, 42);
  options_config_destroy(c);
}

Test(builder_contract, malformed_cli_values_do_not_replace_their_fields) {
  options_config_t *c = config();
  const char *bad[][3] = {{"--count", "no"},           {"--count", "2147483648"}, {"--count", "-2147483649"},
                          {"--ratio", "no"},           {"--ratio", "1.5suffix"},  {"--custom", "bad"},
                          {"--enabled=perhaps", NULL}, {"--count", NULL},         {"--unknown", NULL}};
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
    sample_t sample = defaults();
    char *args[] = {"program", (char *)bad[i][0], (char *)bad[i][1]};
    cr_assert_neq(options_config_parse(c, bad[i][1] ? 3 : 2, args, &sample, OPTION_MODE_CLIENT, NULL, NULL),
                  ASCIICHAT_OK, "%s %s", bad[i][0], bad[i][1] ? bad[i][1] : "");
    cr_assert_eq(sample.count, 7);
    cr_assert_float_eq(sample.ratio, .5, 1e-9);
    cr_assert_eq(sample.custom, 17);
    cr_assert_not(sample.enabled);
    CLEAR_ERRNO_ALL();
  }
  options_config_destroy(c);
}

Test(builder_contract, environment_then_explicit_cli_and_already_set_values) {
  options_config_t *c = config();
  platform_setenv("ASCII_TEST_BUILDER_BOOL", "yes");
  platform_setenv("ASCII_TEST_BUILDER_INT", "12");
  platform_setenv("ASCII_TEST_BUILDER_DOUBLE", "2.5");
  platform_setenv("ASCII_TEST_BUILDER_STRING", "from-env");
  sample_t sample = defaults();
  cr_assert_eq(options_config_set_defaults(c, &sample), ASCIICHAT_OK);
  cr_assert(sample.enabled);
  cr_assert_eq(sample.count, 12);
  cr_assert_float_eq(sample.ratio, 2.5, 1e-9);
  cr_assert_str_eq(sample.label, "from-env");
  char *args[] = {"program", "--count=25", "--ratio=4.5", "--label=from-cli", "--enabled=false"};
  cr_assert_eq(options_config_parse(c, 5, args, &sample, OPTION_MODE_CLIENT, NULL, NULL), ASCIICHAT_OK);
  cr_assert_eq(sample.count, 25);
  cr_assert_float_eq(sample.ratio, 4.5, 1e-9);
  cr_assert_str_eq(sample.label, "from-cli");
  cr_assert_not(sample.enabled);
  platform_unsetenv("ASCII_TEST_BUILDER_BOOL");
  platform_unsetenv("ASCII_TEST_BUILDER_INT");
  platform_unsetenv("ASCII_TEST_BUILDER_DOUBLE");
  platform_unsetenv("ASCII_TEST_BUILDER_STRING");
  cr_assert_eq(options_config_set_defaults(c, &sample), ASCIICHAT_OK);
  cr_assert_eq(sample.count, 25);
  cr_assert_float_eq(sample.ratio, 4.5, 1e-9);
  cr_assert_str_eq(sample.label, "from-cli");
  options_config_destroy(c);
}

Test(builder_contract, requires_and_conflicts_validate_the_final_option_set) {
  for (unsigned conflict = 0; conflict < 2; ++conflict) {
    options_builder_t *b = builder();
    if (conflict)
      options_builder_add_dependency_conflicts(b, "enabled", "companion", "Explicit conflict");
    else
      options_builder_add_dependency_requires(b, "enabled", "companion", NULL);
    options_config_t *c = options_builder_build(b);
    options_builder_destroy(b);
    sample_t sample = defaults();
    char *error = NULL;
    cr_assert_eq(options_config_validate(c, &sample, &error), ASCIICHAT_OK);
    sample.enabled = true;
    sample.companion = conflict;
    cr_assert_eq(options_config_validate(c, &sample, &error), ERROR_USAGE);
    cr_assert_not_null(error);
    cr_assert_not_null(strstr(error, conflict ? "Explicit conflict" : "requires"));
    SAFE_FREE(error);
    sample.companion = !conflict;
    cr_assert_eq(options_config_validate(c, &sample, NULL), ASCIICHAT_OK);
    options_config_destroy(c);
  }
}
