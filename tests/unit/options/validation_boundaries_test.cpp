#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/options/validation.h>
#include <ascii-chat/options/parsers.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <limits.h>
#include <string.h>
TEST_CASE("option_validation_boundaries/finite_float_and_volume_inputs", "[option_validation_boundaries]") {
  char error[256];
  float (*validators[])(const char *, char *, size_t) = {validate_opt_float_non_negative, validate_opt_volume};
  const char *invalid[] = {NULL,           "",    "garbage", "0.5junk",  "-0.01", "nan", "NAN",
                           "nan(payload)", "inf", "-inf",    "Infinity", "1e1000"};
  for (size_t v = 0; v < sizeof(validators) / sizeof(*validators); ++v) {
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
      error[0] = '\0';
      ASCIICHAT_REQUIRE_EQ(validators[v](invalid[i], error, sizeof(error)), -1.0f, "validator %zu accepted %s", v,
                   invalid[i] ? invalid[i] : "NULL");
      ASCIICHAT_REQUIRE_NE(error[0], '\0');
      ASCIICHAT_REQUIRE_EQ(validators[v](invalid[i], NULL, 0), -1.0f);
    }
    ASCIICHAT_REQUIRE_EQ(validators[v]("0", error, sizeof(error)), 0.0f);
    ASCIICHAT_REQUIRE_FLOAT_EQ(validators[v]("0.125", error, sizeof(error)), 0.125f, 0.000001f);
    ASCIICHAT_REQUIRE_EQ(validators[v]("1", error, sizeof(error)), 1.0f);
    ASCIICHAT_REQUIRE_EQ(validators[v]("-0", error, sizeof(error)), 0.0f);
  }
  ASCIICHAT_REQUIRE_EQ(validate_opt_float_non_negative("1024.5", error, sizeof(error)), 1024.5f);
  ASCIICHAT_REQUIRE_EQ(validate_opt_volume("1.001", error, sizeof(error)), -1.0f);
  ASCIICHAT_REQUIRE_EQ(validate_opt_volume("1.001", NULL, 0), -1.0f);
}

TEST_CASE("option_validation_boundaries/address_literals_and_local_hostname", "[option_validation_boundaries]") {
  const char *inputs[] = {"127.0.0.1",     "0.0.0.0",   "192.0.2.123",       "::1", "[::1]", "2001:db8::1",
                          "[2001:db8::1]", "localhost", "ｌｏｃａｌｈｏｓｔ"};
  const char *expected[] = {"127.0.0.1",   "0.0.0.0",     "192.0.2.123", "::1",      "::1",
                            "2001:db8::1", "2001:db8::1", "127.0.0.1",   "127.0.0.1"};
  char parsed[256], error[256];
  for (size_t i = 0; i < sizeof(inputs) / sizeof(*inputs); ++i)
    for (int client = 0; client <= 1; ++client) {
      ASCIICHAT_REQUIRE_EQ(validate_opt_ip_address(inputs[i], parsed, sizeof(parsed), client, error, sizeof(error)), 0);
      ASCIICHAT_REQUIRE_STR_EQ(parsed, expected[i]);
    }
  const char *invalid[] = {NULL, "", "999.1.2.3", "1.2.3", "127.0.0.1:80"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
    strcpy(parsed, "unchanged");
    error[0] = '\0';
    ASCIICHAT_REQUIRE_EQ(validate_opt_ip_address(invalid[i], parsed, sizeof(parsed), true, error, sizeof(error)), -1);
    ASCIICHAT_REQUIRE_STR_EQ(parsed, "unchanged");
    ASCIICHAT_REQUIRE_NE(error[0], '\0');
    ASCIICHAT_REQUIRE_EQ(validate_opt_ip_address(invalid[i], parsed, sizeof(parsed), false, NULL, 0), -1);
  }
}

TEST_CASE("option_validation_boundaries/numeric_ranges_and_error_messages", "[option_validation_boundaries]") {
  char error[256];
  const char *invalid[] = {NULL, "", "invalid", "1junk", "999999999999999999999"};
  int (*validators[])(const char *, char *,
                      size_t) = {validate_opt_positive_int, validate_opt_non_negative_int,  validate_opt_fps,
                                 validate_opt_max_clients,  validate_opt_compression_level, validate_opt_port};
  for (size_t v = 0; v < sizeof(validators) / sizeof(*validators); ++v)
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
      error[0] = '\0';
      ASCIICHAT_REQUIRE_LT(validators[v](invalid[i], error, sizeof(error)), 0);
      ASCIICHAT_REQUIRE_GT(strlen(error), 0);
      ASCIICHAT_REQUIRE_LT(validators[v](invalid[i], NULL, 0), 0);
    }
  ASCIICHAT_REQUIRE_EQ(validate_opt_positive_int("1", error, sizeof(error)), 1);
  ASCIICHAT_REQUIRE_EQ(validate_opt_non_negative_int("0", error, sizeof(error)), 0);
  ASCIICHAT_REQUIRE_EQ(validate_opt_positive_int("0", error, sizeof(error)), -1);
  ASCIICHAT_REQUIRE_EQ(validate_opt_fps("0", error, sizeof(error)), 0);
  ASCIICHAT_REQUIRE_EQ(validate_opt_fps("144", error, sizeof(error)), 144);
  ASCIICHAT_REQUIRE_EQ(validate_opt_fps("145", error, sizeof(error)), -1);
  ASCIICHAT_REQUIRE_EQ(validate_opt_compression_level("9", error, sizeof(error)), 9);
  ASCIICHAT_REQUIRE_EQ(validate_opt_compression_level("10", error, sizeof(error)), -1);
  ASCIICHAT_REQUIRE_EQ(validate_opt_port("65535", error, sizeof(error)), 0);
  ASCIICHAT_REQUIRE_EQ(validate_opt_port("65536", error, sizeof(error)), -1);
  ASCIICHAT_REQUIRE_EQ(validate_opt_reconnect("off", error, sizeof(error)), 0);
  ASCIICHAT_REQUIRE_EQ(validate_opt_reconnect("AUTO", error, sizeof(error)), -1);
  ASCIICHAT_REQUIRE_EQ(validate_opt_reconnect("999", error, sizeof(error)), 999);
  ASCIICHAT_REQUIRE_EQ(validate_opt_reconnect("1000", error, sizeof(error)), INT_MIN);
  ASCIICHAT_REQUIRE_EQ(validate_opt_reconnect("-2", error, sizeof(error)), INT_MIN);
  ASCIICHAT_REQUIRE_EQ(validate_opt_reconnect(NULL, error, sizeof(error)), INT_MIN);
  ASCIICHAT_REQUIRE_EQ(validate_opt_device_index("-1", error, sizeof(error)), -1);
  ASCIICHAT_REQUIRE_EQ(validate_opt_device_index("0", error, sizeof(error)), 0);
  ASCIICHAT_REQUIRE_EQ(validate_opt_device_index("-2", error, sizeof(error)), INT_MIN);
  ASCIICHAT_REQUIRE_EQ(validate_opt_device_index("bad", error, sizeof(error)), INT_MIN);
}

TEST_CASE("option_validation_boundaries/passwords_and_remote_identity_key_schemes", "[option_validation_boundaries]") {
  char error[256], password[258];
  memset(password, 'a', sizeof(password));
  password[257] = '\0';
  ASCIICHAT_REQUIRE_EQ(validate_opt_password(NULL, error, sizeof(error)), -1);
  ASCIICHAT_REQUIRE_EQ(validate_opt_password("short", error, sizeof(error)), -1);
  ASCIICHAT_REQUIRE_EQ(validate_opt_password("long-enough-password", error, sizeof(error)), 0);
  ASCIICHAT_REQUIRE_EQ(validate_opt_password(password, error, sizeof(error)), -1);
  const char *remote[] = {"github:zfogg", "gitlab:user", "gpg:ABCDEF", "http://example.test/key",
                          "https://example.test/key"};
  for (size_t i = 0; i < sizeof(remote) / sizeof(*remote); ++i)
    ASCIICHAT_REQUIRE(is_remote_key_path(remote[i]));
  ASCIICHAT_REQUIRE_NOT(is_remote_key_path(NULL));
  ASCIICHAT_REQUIRE_NOT(is_remote_key_path(""));
  ASCIICHAT_REQUIRE_NOT(is_remote_key_path("/tmp/local-key"));
  options_t *options = SAFE_CALLOC(1, sizeof(*options), options_t *);
  char *argv[] = {
      "ascii-chat", "server", "--key", "github:zfogg", "-K", "gpg:ABCDEF", "--key=https://example.test/key"};
  ASCIICHAT_REQUIRE_EQ(options_collect_identity_keys(options, 7, argv), 3);
  ASCIICHAT_REQUIRE_EQ(options->num_identity_keys, 3);
  ASCIICHAT_REQUIRE_STR_EQ(options->encrypt_key, "github:zfogg");
  ASCIICHAT_REQUIRE_STR_EQ(options->identity_keys[1], "gpg:ABCDEF");
  ASCIICHAT_REQUIRE_STR_EQ(options->identity_keys[2], "https://example.test/key");
  SAFE_FREE(options);
}

TEST_CASE("option_validation_boundaries/enum_parsers_reject_unknown_values_without_overwriting_destination", "[option_validation_boundaries]") {
  bool (*parsers[])(const char *, void *, char **) = {parse_color_mode,   parse_color_filter, parse_render_mode,
                                                      parse_render_theme, parse_palette_type, parse_log_level,
                                                      parse_port_option};
  for (size_t i = 0; i < sizeof(parsers) / sizeof(*parsers); ++i) {
    int value = 12345;
    char *error = NULL;
    ASCIICHAT_REQUIRE_NOT(parsers[i]("definitely-invalid", &value, &error));
    ASCIICHAT_REQUIRE_NOT_NULL(error);
    ASCIICHAT_REQUIRE_EQ(value, 12345);
    SAFE_FREE(error);
    ASCIICHAT_REQUIRE_NOT(parsers[i]("value", NULL, &error));
    SAFE_FREE(error);
  }
  int value = 0;
  char *error = NULL;
  ASCIICHAT_REQUIRE(parse_color_mode("truecolor", &value, &error));
  ASCIICHAT_REQUIRE_EQ(value, COLOR_MODE_TRUECOLOR);
  ASCIICHAT_REQUIRE(parse_render_mode("half-block", &value, &error));
  ASCIICHAT_REQUIRE_EQ(value, RENDER_MODE_HALF_BLOCK);
  ASCIICHAT_REQUIRE(parse_log_level("WARN", &value, &error));
  ASCIICHAT_REQUIRE_EQ(value, LOG_WARN);
  ASCIICHAT_REQUIRE(parse_port_option("65535", &value, &error));
  ASCIICHAT_REQUIRE_EQ(value, 65535);
  options_t *options = SAFE_CALLOC(1, sizeof(*options), options_t *);
  ASCIICHAT_REQUIRE(parse_palette_chars(" .:#", options->palette_custom, &error));
  ASCIICHAT_REQUIRE_STR_EQ(options->palette_custom, " .:#");
  ASCIICHAT_REQUIRE_EQ(options->palette_type, PALETTE_CUSTOM);
  ASCIICHAT_REQUIRE_NOT(parse_palette_chars("", options->palette_custom, &error));
  SAFE_FREE(error);
  SAFE_FREE(options);
}

TEST_CASE("option_validation_boundaries/session_key_array_rejects_excess_and_truncated_paths", "[option_validation_boundaries]") {
  options_t opts{};
  char *args[MAX_SESSION_KEYS + 2];
  args[0] = "ascii-chat";
  for (size_t i = 1; i < MAX_SESSION_KEYS + 2; ++i)
    args[i] = "--key=github:zfogg";
  ASCIICHAT_REQUIRE_EQ(options_collect_identity_keys(&opts, MAX_SESSION_KEYS + 1, args), MAX_SESSION_KEYS);
  ASCIICHAT_REQUIRE_EQ(opts.num_identity_keys, MAX_SESSION_KEYS);
  ASCIICHAT_REQUIRE_EQ(options_collect_identity_keys(&opts, MAX_SESSION_KEYS + 2, args), -1);
  char path[OPTIONS_BUFF_SIZE + 7];
  memcpy(path, "--key=", 6);
  memset(path + 6, 'a', OPTIONS_BUFF_SIZE);
  memcpy(path + 6, "github:", 7);
  path[sizeof(path) - 1] = '\0';
  args[1] = path;
  ASCIICHAT_REQUIRE_EQ(options_collect_identity_keys(&opts, 2, args), -1);
}
