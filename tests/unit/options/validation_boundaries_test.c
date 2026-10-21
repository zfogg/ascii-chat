#include <criterion/criterion.h>
#include <ascii-chat/options/validation.h>
#include <ascii-chat/options/parsers.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <limits.h>
#include <string.h>

TestSuite(option_validation_boundaries);

Test(option_validation_boundaries, numeric_ranges_and_error_messages) {
  char error[256];
  const char *invalid[] = {NULL, "", "invalid", "1junk", "999999999999999999999"};
  int (*validators[])(const char *, char *,
                      size_t) = {validate_opt_positive_int, validate_opt_non_negative_int,  validate_opt_fps,
                                 validate_opt_max_clients,  validate_opt_compression_level, validate_opt_port};
  for (size_t v = 0; v < sizeof(validators) / sizeof(*validators); ++v)
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
      error[0] = '\0';
      cr_assert_lt(validators[v](invalid[i], error, sizeof(error)), 0);
      cr_assert_gt(strlen(error), 0);
      cr_assert_lt(validators[v](invalid[i], NULL, 0), 0);
    }
  cr_assert_eq(validate_opt_positive_int("1", error, sizeof(error)), 1);
  cr_assert_eq(validate_opt_non_negative_int("0", error, sizeof(error)), 0);
  cr_assert_eq(validate_opt_positive_int("0", error, sizeof(error)), -1);
  cr_assert_eq(validate_opt_fps("144", error, sizeof(error)), 144);
  cr_assert_eq(validate_opt_fps("145", error, sizeof(error)), -1);
  cr_assert_eq(validate_opt_compression_level("9", error, sizeof(error)), 9);
  cr_assert_eq(validate_opt_compression_level("10", error, sizeof(error)), -1);
  cr_assert_eq(validate_opt_port("65535", error, sizeof(error)), 0);
  cr_assert_eq(validate_opt_port("65536", error, sizeof(error)), -1);
  cr_assert_eq(validate_opt_reconnect("off", error, sizeof(error)), 0);
  cr_assert_eq(validate_opt_reconnect("AUTO", error, sizeof(error)), -1);
  cr_assert_eq(validate_opt_reconnect("999", error, sizeof(error)), 999);
  cr_assert_eq(validate_opt_reconnect("1000", error, sizeof(error)), INT_MIN);
  cr_assert_eq(validate_opt_reconnect("-2", error, sizeof(error)), INT_MIN);
  cr_assert_eq(validate_opt_reconnect(NULL, error, sizeof(error)), INT_MIN);
  cr_assert_eq(validate_opt_device_index("-1", error, sizeof(error)), -1);
  cr_assert_eq(validate_opt_device_index("0", error, sizeof(error)), 0);
  cr_assert_eq(validate_opt_device_index("-2", error, sizeof(error)), INT_MIN);
  cr_assert_eq(validate_opt_device_index("bad", error, sizeof(error)), INT_MIN);
}

Test(option_validation_boundaries, passwords_and_remote_identity_key_schemes) {
  char error[256], password[258];
  memset(password, 'a', sizeof(password));
  password[257] = '\0';
  cr_assert_eq(validate_opt_password(NULL, error, sizeof(error)), -1);
  cr_assert_eq(validate_opt_password("short", error, sizeof(error)), -1);
  cr_assert_eq(validate_opt_password("long-enough-password", error, sizeof(error)), 0);
  cr_assert_eq(validate_opt_password(password, error, sizeof(error)), -1);
  const char *remote[] = {"github:zfogg", "gitlab:user", "gpg:ABCDEF", "http://example.test/key",
                          "https://example.test/key"};
  for (size_t i = 0; i < sizeof(remote) / sizeof(*remote); ++i)
    cr_assert(is_remote_key_path(remote[i]));
  cr_assert_not(is_remote_key_path(NULL));
  cr_assert_not(is_remote_key_path(""));
  cr_assert_not(is_remote_key_path("/tmp/local-key"));
  options_t *options = SAFE_CALLOC(1, sizeof(*options), options_t *);
  char *argv[] = {
      "ascii-chat", "server", "--key", "github:zfogg", "-K", "gpg:ABCDEF", "--key=https://example.test/key"};
  cr_assert_eq(options_collect_identity_keys(options, 7, argv), 3);
  cr_assert_eq(options->num_identity_keys, 3);
  cr_assert_str_eq(options->encrypt_key, "github:zfogg");
  cr_assert_str_eq(options->identity_keys[1], "gpg:ABCDEF");
  cr_assert_str_eq(options->identity_keys[2], "https://example.test/key");
  SAFE_FREE(options);
}

Test(option_validation_boundaries, enum_parsers_reject_unknown_values_without_overwriting_destination) {
  bool (*parsers[])(const char *, void *, char **) = {parse_color_mode,   parse_color_filter, parse_render_mode,
                                                      parse_render_theme, parse_palette_type, parse_log_level,
                                                      parse_port_option};
  for (size_t i = 0; i < sizeof(parsers) / sizeof(*parsers); ++i) {
    int value = 12345;
    char *error = NULL;
    cr_assert_not(parsers[i]("definitely-invalid", &value, &error));
    cr_assert_not_null(error);
    cr_assert_eq(value, 12345);
    SAFE_FREE(error);
    cr_assert_not(parsers[i]("value", NULL, &error));
    SAFE_FREE(error);
  }
  int value = 0;
  char *error = NULL;
  cr_assert(parse_color_mode("truecolor", &value, &error));
  cr_assert_eq(value, COLOR_MODE_TRUECOLOR);
  cr_assert(parse_render_mode("half-block", &value, &error));
  cr_assert_eq(value, RENDER_MODE_HALF_BLOCK);
  cr_assert(parse_log_level("WARN", &value, &error));
  cr_assert_eq(value, LOG_WARN);
  cr_assert(parse_port_option("65535", &value, &error));
  cr_assert_eq(value, 65535);
  options_t *options = SAFE_CALLOC(1, sizeof(*options), options_t *);
  cr_assert(parse_palette_chars(" .:#", options->palette_custom, &error));
  cr_assert_str_eq(options->palette_custom, " .:#");
  cr_assert_eq(options->palette_type, PALETTE_CUSTOM);
  cr_assert_not(parse_palette_chars("", options->palette_custom, &error));
  SAFE_FREE(error);
  SAFE_FREE(options);
}
