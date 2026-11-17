#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <ascii-chat/options/builder.h>
#include <libplatform/system.h>
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
  REQUIRE((b) != nullptr);
  options_builder_add_bool(b, "enabled", 'e', offsetof(sample_t, enabled), false, "Enable", "Test", false, "ASCII_TEST_BUILDER_BOOL");
  options_builder_set_mode_bitmask(b, OPTION_MODE_ALL);
  options_builder_add_bool(b, "companion", 0, offsetof(sample_t, companion), false, "Companion", "Test", false, NULL);
  options_builder_set_mode_bitmask(b, OPTION_MODE_ALL);
  options_builder_add_int(b, "count", 'c', offsetof(sample_t, count), 7, "Count", "Test", false, "ASCII_TEST_BUILDER_INT", NULL);
  options_builder_set_mode_bitmask(b, OPTION_MODE_ALL);
  options_builder_add_double(b, "ratio", 'r', offsetof(sample_t, ratio), .5, "Ratio", "Test", false, "ASCII_TEST_BUILDER_DOUBLE", NULL, false);
  options_builder_set_mode_bitmask(b, OPTION_MODE_ALL);
  options_builder_add_string(b, "label", 'l', offsetof(sample_t, label), "default", "Label", "Test", false, "ASCII_TEST_BUILDER_STRING", NULL);
  options_builder_set_mode_bitmask(b, OPTION_MODE_ALL);
  static const uintptr_t initial = 17;
  options_builder_add_callback_optional(b, "custom", 'u', offsetof(sample_t, custom), &initial, sizeof(initial), parse_custom, "Custom", "Test", false, NULL, true);
  options_builder_set_mode_bitmask(b, OPTION_MODE_ALL);
  options_builder_add_action(b, "act", 'a', action, "Act", "Test");
  options_builder_set_mode_bitmask(b, OPTION_MODE_ALL);
  return b;
}

static options_config_t *config(void) {
  options_builder_t *b = builder();
  options_config_t *c = options_builder_build(b);
  options_builder_destroy(b);
  REQUIRE((c) != nullptr);
  return c;
}

static sample_t defaults(void) {
  sample_t result = {.count = 7, .ratio = .5, .custom = 17};
  SAFE_STRNCPY(result.label, "default", sizeof(result.label));
  return result;
}

TEST_CASE("builder_contract/typed_cli_values_short_flags_and_optional_callback", "[builder_contract] [unit]") {
  options_config_t *c = config();
  sample_t sample = defaults();
  char *args[] = {"program", "--enabled=yes", "-c", "-12", "--ratio=1.25", "--label", "-", "--custom", "--act"};
  int remaining = -1;
  char **remainder = args;
  actions = 0;
  REQUIRE((options_config_parse(c, sizeof(args) / sizeof(args[0]), args, &sample, OPTION_MODE_CLIENT, &remaining, &remainder)) == (ASCIICHAT_OK));
  REQUIRE(sample.enabled);
  REQUIRE((sample.count) == (-12));
  REQUIRE((sample.ratio) == Catch::Approx(1.25));
  REQUIRE(std::strcmp((sample.label), ("-")) == 0);
  REQUIRE((sample.custom) == (99));
  REQUIRE((actions) == (1));
  REQUIRE((remaining) == (0));
  REQUIRE((remainder) == nullptr);
  char *second[] = {"program", "-e", "false", "-u", "42"};
  REQUIRE((options_config_parse(c, 5, second, &sample, OPTION_MODE_CLIENT, NULL, NULL)) == (ASCIICHAT_OK));
  REQUIRE(!(sample.enabled));
  REQUIRE((sample.custom) == (42));
  options_config_destroy(c);
}

TEST_CASE("builder_contract/malformed_cli_values_do_not_replace_their_fields", "[builder_contract] [unit]") {
  options_config_t *c = config();
  const char *bad[][3] = {{"--count", "no"},   {"--count", "2147483648"},   {"--count", "-2147483649"}, {"--ratio", "no"},  {"--ratio", "1.5suffix"},
                          {"--custom", "bad"}, {"--enabled=perhaps", NULL}, {"--count", NULL},          {"--unknown", NULL}};
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
    sample_t sample = defaults();
    char *args[] = {"program", (char *)bad[i][0], (char *)bad[i][1]};
    REQUIRE((options_config_parse(c, bad[i][1] ? 3 : 2, args, &sample, OPTION_MODE_CLIENT, NULL, NULL)) != (ASCIICHAT_OK));
    REQUIRE((sample.count) == (7));
    REQUIRE((sample.ratio) == Catch::Approx(.5));
    REQUIRE((sample.custom) == (17));
    REQUIRE(!(sample.enabled));
    CLEAR_ERRNO_ALL();
  }
  options_config_destroy(c);
}

TEST_CASE("builder_contract/environment_then_explicit_cli_and_already_set_values", "[builder_contract] [unit]") {
  options_config_t *c = config();
  platform_setenv("ASCII_TEST_BUILDER_BOOL", "yes");
  platform_setenv("ASCII_TEST_BUILDER_INT", "12");
  platform_setenv("ASCII_TEST_BUILDER_DOUBLE", "2.5");
  platform_setenv("ASCII_TEST_BUILDER_STRING", "from-env");
  sample_t sample = defaults();
  sample.enabled = true;
  sample.ratio = 7.0;
  SAFE_STRNCPY(sample.label, "from-config", sizeof(sample.label));
  platform_setenv("ASCII_TEST_BUILDER_BOOL", "false");
  REQUIRE((options_config_set_defaults(c, &sample)) == (ASCIICHAT_OK));
  REQUIRE(!(sample.enabled));
  platform_setenv("ASCII_TEST_BUILDER_BOOL", "yes");
  REQUIRE((options_config_set_defaults(c, &sample)) == (ASCIICHAT_OK));
  REQUIRE(sample.enabled);
  REQUIRE((sample.count) == (12));
  REQUIRE((sample.ratio) == Catch::Approx(2.5));
  REQUIRE(std::strcmp((sample.label), ("from-env")) == 0);
  char *args[] = {"program", "--count=25", "--ratio=4.5", "--label=from-cli", "--enabled=false"};
  REQUIRE((options_config_parse(c, 5, args, &sample, OPTION_MODE_CLIENT, NULL, NULL)) == (ASCIICHAT_OK));
  REQUIRE((sample.count) == (25));
  REQUIRE((sample.ratio) == Catch::Approx(4.5));
  REQUIRE(std::strcmp((sample.label), ("from-cli")) == 0);
  REQUIRE(!(sample.enabled));
  platform_unsetenv("ASCII_TEST_BUILDER_BOOL");
  platform_unsetenv("ASCII_TEST_BUILDER_INT");
  platform_unsetenv("ASCII_TEST_BUILDER_DOUBLE");
  platform_unsetenv("ASCII_TEST_BUILDER_STRING");
  REQUIRE((options_config_set_defaults(c, &sample)) == (ASCIICHAT_OK));
  REQUIRE((sample.count) == (25));
  REQUIRE((sample.ratio) == Catch::Approx(4.5));
  REQUIRE(std::strcmp((sample.label), ("from-cli")) == 0);
  options_config_destroy(c);
}

TEST_CASE("builder_contract/invalid_double_environment_preserves_value_and_returns_error", "[builder_contract] [unit]") {
  options_config_t *c = config();
  const char *invalid[] = {"not-a-number", "2.5suffix", "1e9999", "nan"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    sample_t sample = defaults();
    sample.ratio = 7.0;
    platform_setenv("ASCII_TEST_BUILDER_DOUBLE", invalid[i]);
    REQUIRE((options_config_set_defaults(c, &sample)) == (ERROR_USAGE));
    REQUIRE((sample.ratio) == Catch::Approx(7.0));
    CLEAR_ERRNO_ALL();
  }
  platform_unsetenv("ASCII_TEST_BUILDER_DOUBLE");
  options_config_destroy(c);
}

TEST_CASE("builder_contract/requires_and_conflicts_validate_the_final_option_set", "[builder_contract] [unit]") {
  for (unsigned variant = 0; variant < 3; ++variant) {
    bool conflict = variant != 0;
    options_builder_t *b = builder();
    if (conflict)
      options_builder_add_dependency_conflicts(b, "enabled", "companion", variant == 1 ? "Explicit conflict" : NULL);
    else
      options_builder_add_dependency_requires(b, "enabled", "companion", NULL);
    options_config_t *c = options_builder_build(b);
    options_builder_destroy(b);
    sample_t sample = defaults();
    char *error = NULL;
    REQUIRE((options_config_validate(c, &sample, &error)) == (ASCIICHAT_OK));
    sample.enabled = true;
    sample.companion = conflict;
    REQUIRE((options_config_validate(c, &sample, &error)) == (ERROR_USAGE));
    REQUIRE((error) != nullptr);
    REQUIRE((strstr(error, variant == 1 ? "Explicit conflict" : conflict ? "conflicts" : "requires")) != nullptr);
    SAFE_FREE(error);
    sample.companion = !conflict;
    REQUIRE((options_config_validate(c, &sample, NULL)) == (ASCIICHAT_OK));
    options_config_destroy(c);
  }
}

static int parse_pair(const char *argument, void *destination, char **remaining, int count, char **error) {
  sample_t *sample = static_cast<sample_t *>(destination);
  if (strcmp(argument, "reject") == 0)
    return -1;
  if (count < 1 || strcmp(remaining[0], "value") != 0) {
    SAFE_STRDUP(*error, "Expected pair NAME value");
    return -1;
  }
  SAFE_STRNCPY(sample->label, argument, sizeof(sample->label));
  sample->count = count;
  return 2;
}
TEST_CASE("builder_contract/positional_parser_consumes_pairs_and_rejects_missing_or_extra_arguments", "[builder_contract] [unit]") {
  options_builder_t *b = builder();
  options_builder_add_positional(b, "pair", "A two-word pair", true, NULL, NULL, 0, OPTION_MODE_ALL, parse_pair);
  options_config_t *c = options_builder_build(b);
  options_builder_destroy(b);
  REQUIRE((c) != nullptr);
  sample_t sample = defaults();
  char *valid[] = {"name", "value"};
  REQUIRE((options_config_parse_positional(c, 2, valid, &sample)) == (ASCIICHAT_OK));
  REQUIRE(std::strcmp((sample.label), ("name")) == 0);
  REQUIRE((sample.count) == (1));
  REQUIRE((options_config_parse_positional(c, 0, NULL, &sample)) == (ERROR_USAGE));
  REQUIRE((options_config_parse_positional(c, 1, valid, &sample)) == (ERROR_USAGE));
  char *rejected[] = {"reject", "value"};
  REQUIRE((options_config_parse_positional(c, 2, rejected, &sample)) == (ERROR_USAGE));
  char *extra[] = {"name", "value", "unexpected"};
  REQUIRE((options_config_parse_positional(c, 3, extra, &sample)) == (ERROR_USAGE));
  REQUIRE((options_config_parse_positional(c, -1, valid, &sample)) == (ERROR_INVALID_PARAM));
  REQUIRE((options_config_parse_positional(c, 1, NULL, &sample)) == (ERROR_INVALID_PARAM));
  REQUIRE((options_config_parse_positional(NULL, 2, valid, &sample)) == (ERROR_INVALID_PARAM));
  REQUIRE((options_config_parse_positional(c, 2, valid, NULL)) == (ERROR_INVALID_PARAM));
  options_config_destroy(c);
}
static bool validate_count(const void *destination) {
  const sample_t *sample = static_cast<const sample_t *>(destination);
  if (sample->count < 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Count must not be negative");
    return false;
  }
  return sample->count < 10;
}
TEST_CASE("builder_contract/custom_validator_propagates_fresh_errors_and_ignores_stale_ones", "[builder_contract] [unit]") {
  options_builder_t *b = options_builder_create(sizeof(sample_t));
  REQUIRE((b) != nullptr);
  options_builder_add_int(b, "count", 'c', offsetof(sample_t, count), 7, "Count", "Test", false, NULL, validate_count);
  options_config_t *c = options_builder_build(b);
  options_builder_destroy(b);
  REQUIRE((c) != nullptr);
  sample_t sample = defaults();
  REQUIRE((options_config_validate(c, &sample, NULL)) == (ASCIICHAT_OK));
  sample.count = -1;
  REQUIRE((options_config_validate(c, &sample, NULL)) == (ERROR_INVALID_PARAM));
  sample.count = 12;
  REQUIRE((options_config_validate(c, &sample, NULL)) == (ERROR_USAGE));
  sample.count = 9;
  REQUIRE((options_config_validate(c, &sample, NULL)) == (ASCIICHAT_OK));
  options_config_destroy(c);
}
TEST_CASE("builder_contract/required_fields_report_the_cli_and_environment_routes", "[builder_contract] [unit]") {
  for (unsigned with_environment = 0; with_environment < 2; ++with_environment) {
    options_builder_t *b = options_builder_create(sizeof(sample_t));
    REQUIRE((b) != nullptr);
    options_builder_add_string(b, "label", 'l', offsetof(sample_t, label), NULL, "Label", "Test", true, with_environment ? "ASCII_TEST_REQUIRED_LABEL" : NULL, NULL);
    options_config_t *c = options_builder_build(b);
    options_builder_destroy(b);
    REQUIRE((c) != nullptr);
    sample_t sample{};
    char *error = NULL;
    REQUIRE((options_config_validate(c, &sample, &error)) == (ERROR_USAGE));
    REQUIRE((error) != nullptr);
    REQUIRE((strstr(error, "--label")) != nullptr);
    if (with_environment)
      REQUIRE((strstr(error, "ASCII_TEST_REQUIRED_LABEL")) != nullptr);
    SAFE_FREE(error);
    REQUIRE((options_config_validate(c, &sample, NULL)) == (ERROR_USAGE));
    SAFE_STRNCPY(sample.label, "supplied", sizeof(sample.label));
    REQUIRE((options_config_validate(c, &sample, &error)) == (ASCIICHAT_OK));
    REQUIRE((error) == nullptr);
    options_config_destroy(c);
  }
}

static int parse_literals(const char *argument, void *destination, char **remaining, int count, char **error) {
  (void)error;
  REQUIRE(std::strcmp((argument), ("-literal")) == 0);
  REQUIRE((count) == (1));
  REQUIRE(std::strcmp((remaining[0]), ("client")) == 0);
  SAFE_STRNCPY(((sample_t *)destination)->label, argument, sizeof(((sample_t *)destination)->label));
  return 2;
}
TEST_CASE("builder_contract/boolean_spellings_and_end_of_options_preserve_positional_arguments", "[builder_contract] [unit]") {
  options_builder_t *b = builder();
  options_builder_add_positional(b, "literals", "Literal words", false, NULL, NULL, 0, OPTION_MODE_CLIENT, parse_literals);
  options_config_t *c = options_builder_build(b);
  options_builder_destroy(b);
  REQUIRE((c) != nullptr);
  const char *values[] = {"true", "YES", "1", "On", "false", "NO", "0", "off"};
  for (size_t i = 0; i < 8; ++i) {
    sample_t sample = defaults();
    char *args[] = {"program", "--enabled", (char *)values[i], "--", "-literal", "client"};
    int remaining = 0;
    char **rest = NULL;
    REQUIRE((options_config_parse(c, 6, args, &sample, OPTION_MODE_CLIENT, &remaining, &rest)) == (ASCIICHAT_OK));
    REQUIRE((sample.enabled) == (i < 4));
    REQUIRE((remaining) == (0));
    REQUIRE((rest) == nullptr);
    REQUIRE(std::strcmp((sample.label), ("-literal")) == 0);
  }
  sample_t sample = defaults();
  char *toggles[] = {"program", "--enabled", "--enabled"};
  REQUIRE((options_config_parse(c, 3, toggles, &sample, OPTION_MODE_CLIENT, NULL, NULL)) == (ASCIICHAT_OK));
  REQUIRE(!(sample.enabled));
  options_config_destroy(c);
}
TEST_CASE("builder_contract/primary_port_environment_overrides_short_aliases", "[builder_contract] [unit]") {
  const char *primary[] = {"ASCII_CHAT_PORT", "ASCII_CHAT_WEBSOCKET_PORT"};
  const char *alias[] = {"PORT", "WS_PORT"};
  for (size_t i = 0; i < 2; ++i) {
    char *old_primary = NULL, *old_alias = NULL;
    if (getenv(primary[i]))
      SAFE_STRDUP(old_primary, getenv(primary[i]));
    if (getenv(alias[i]))
      SAFE_STRDUP(old_alias, getenv(alias[i]));
    platform_unsetenv(primary[i]);
    platform_setenv(alias[i], "1234");
    options_builder_t *b = options_builder_create(sizeof(sample_t));
    REQUIRE((b) != nullptr);
    options_builder_add_int(b, "port", 'p', offsetof(sample_t, count), 7, "Port", "Test", false, primary[i], NULL);
    options_config_t *c = options_builder_build(b);
    options_builder_destroy(b);
    sample_t sample = defaults();
    REQUIRE((options_config_set_defaults(c, &sample)) == (ASCIICHAT_OK));
    REQUIRE((sample.count) == (1234));
    platform_setenv(primary[i], "4321");
    REQUIRE((options_config_set_defaults(c, &sample)) == (ASCIICHAT_OK));
    REQUIRE((sample.count) == (4321));
    const char *invalid[] = {"invalid", "12suffix", "2147483648", "-2147483649"};
    for (size_t j = 0; j < 4; ++j) {
      platform_setenv(primary[i], invalid[j]);
      REQUIRE((options_config_set_defaults(c, &sample)) == (ASCIICHAT_OK));
      REQUIRE((sample.count) == (4321));
    }
    options_config_destroy(c);
    if (old_primary)
      platform_setenv(primary[i], old_primary);
    else
      platform_unsetenv(primary[i]);
    if (old_alias)
      platform_setenv(alias[i], old_alias);
    else
      platform_unsetenv(alias[i]);
    SAFE_FREE(old_primary);
    SAFE_FREE(old_alias);
  }
}

TEST_CASE("builder_contract/conflicting_color_options_return_an_owned_error_message", "[builder_contract] [unit]") {
  options_config_t *c = config();
  sample_t sample = defaults();
  sample.base.color = true;
  sample.base.color_mode = TERM_COLOR_NONE;
  char *error = NULL;
  REQUIRE((options_config_validate(c, &sample, &error)) == (ERROR_USAGE));
  REQUIRE((error) != nullptr);
  REQUIRE((strstr(error, "--color-mode=none")) != nullptr);
  SAFE_FREE(error);
  REQUIRE((options_config_validate(c, &sample, NULL)) == (ERROR_USAGE));
  sample.base.color = false;
  REQUIRE((options_config_validate(c, &sample, NULL)) == (ASCIICHAT_OK));
  options_config_destroy(c);
}

TEST_CASE("builder_contract/string_defaults_belong_to_each_builder_and_survive_transfer", "[builder_contract] [unit]") {
  for (unsigned i = 0; i < 1100; ++i) {
    options_builder_t *b = options_builder_create(sizeof(sample_t));
    char initial[] = "owned default";
    options_builder_add_string(b, "label", 0, offsetof(sample_t, label), initial, "Label", "Test", false, NULL, NULL);
    memset(initial, 'x', sizeof(initial) - 1);
    REQUIRE((b->num_descriptors) == (1));
    if (i % 2) {
      options_builder_destroy(b);
      continue;
    }
    options_config_t *c = options_builder_build(b);
    options_builder_destroy(b);
    sample_t sample{};
    REQUIRE((c) != nullptr);
    REQUIRE((options_config_set_defaults(c, &sample)) == (ASCIICHAT_OK));
    REQUIRE(std::strcmp((sample.label), ("owned default")) == 0);
    options_config_destroy(c);
  }
}
