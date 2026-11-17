#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <ascii-chat/options/validation.h>
#include <ascii-chat/options/parsers.h>
#include <ascii-chat/tests/logging.h>
#include <limits.h>
#include <string.h>
TEST_CASE("option_validation_boundaries/finite_float_and_volume_inputs", "[option_validation_boundaries] [unit]") {
  char error[256];
  float (*validators[])(const char *, char *, size_t) = {validate_opt_float_non_negative, validate_opt_volume};
  const char *invalid[] = {NULL, "", "garbage", "0.5junk", "-0.01", "nan", "NAN", "nan(payload)", "inf", "-inf", "Infinity", "1e1000"};
  for (size_t v = 0; v < sizeof(validators) / sizeof(*validators); ++v) {
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
      error[0] = '\0';
      REQUIRE((validators[v](invalid[i], error, sizeof(error))) == (-1.0f));
      REQUIRE((error[0]) != ('\0'));
      REQUIRE((validators[v](invalid[i], NULL, 0)) == (-1.0f));
    }
    REQUIRE((validators[v]("0", error, sizeof(error))) == (0.0f));
    REQUIRE((validators[v]("0.125", error, sizeof(error))) == Catch::Approx(0.125f));
    REQUIRE((validators[v]("1", error, sizeof(error))) == (1.0f));
    REQUIRE((validators[v]("-0", error, sizeof(error))) == (0.0f));
  }
  REQUIRE((validate_opt_float_non_negative("1024.5", error, sizeof(error))) == (1024.5f));
  REQUIRE((validate_opt_volume("1.001", error, sizeof(error))) == (-1.0f));
  REQUIRE((validate_opt_volume("1.001", NULL, 0)) == (-1.0f));
}

TEST_CASE("option_validation_boundaries/address_literals_and_local_hostname", "[option_validation_boundaries] [unit]") {
  const char *inputs[] = {"127.0.0.1", "0.0.0.0", "192.0.2.123", "::1", "[::1]", "2001:db8::1", "[2001:db8::1]", "localhost", "ｌｏｃａｌｈｏｓｔ"};
  const char *expected[] = {"127.0.0.1", "0.0.0.0", "192.0.2.123", "::1", "::1", "2001:db8::1", "2001:db8::1", "127.0.0.1", "127.0.0.1"};
  char parsed[256], error[256];
  for (size_t i = 0; i < sizeof(inputs) / sizeof(*inputs); ++i)
    for (int client = 0; client <= 1; ++client) {
      REQUIRE((validate_opt_ip_address(inputs[i], parsed, sizeof(parsed), client, error, sizeof(error))) == (0));
      REQUIRE(std::strcmp((parsed), (expected[i])) == 0);
    }
  const char *invalid[] = {NULL, "", "999.1.2.3", "1.2.3", "127.0.0.1:80"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
    strcpy(parsed, "unchanged");
    error[0] = '\0';
    REQUIRE((validate_opt_ip_address(invalid[i], parsed, sizeof(parsed), true, error, sizeof(error))) == (-1));
    REQUIRE(std::strcmp((parsed), ("unchanged")) == 0);
    REQUIRE((error[0]) != ('\0'));
    REQUIRE((validate_opt_ip_address(invalid[i], parsed, sizeof(parsed), false, NULL, 0)) == (-1));
  }
}

TEST_CASE("option_validation_boundaries/numeric_ranges_and_error_messages", "[option_validation_boundaries] [unit]") {
  char error[256];
  const char *invalid[] = {NULL, "", "invalid", "1junk", "999999999999999999999"};
  int (*validators[])(const char *, char *, size_t) = {validate_opt_positive_int, validate_opt_non_negative_int,  validate_opt_fps,
                                                       validate_opt_max_clients,  validate_opt_compression_level, validate_opt_port};
  for (size_t v = 0; v < sizeof(validators) / sizeof(*validators); ++v)
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
      error[0] = '\0';
      REQUIRE((validators[v](invalid[i], error, sizeof(error))) < (0));
      REQUIRE((strlen(error)) > (0));
      REQUIRE((validators[v](invalid[i], NULL, 0)) < (0));
    }
  REQUIRE((validate_opt_positive_int("1", error, sizeof(error))) == (1));
  REQUIRE((validate_opt_non_negative_int("0", error, sizeof(error))) == (0));
  REQUIRE((validate_opt_positive_int("0", error, sizeof(error))) == (-1));
  REQUIRE((validate_opt_fps("0", error, sizeof(error))) == (0));
  REQUIRE((validate_opt_fps("144", error, sizeof(error))) == (144));
  REQUIRE((validate_opt_fps("145", error, sizeof(error))) == (-1));
  REQUIRE((validate_opt_compression_level("9", error, sizeof(error))) == (9));
  REQUIRE((validate_opt_compression_level("10", error, sizeof(error))) == (-1));
  REQUIRE((validate_opt_port("65535", error, sizeof(error))) == (0));
  REQUIRE((validate_opt_port("65536", error, sizeof(error))) == (-1));
  REQUIRE((validate_opt_reconnect("off", error, sizeof(error))) == (0));
  REQUIRE((validate_opt_reconnect("AUTO", error, sizeof(error))) == (-1));
  REQUIRE((validate_opt_reconnect("999", error, sizeof(error))) == (999));
  REQUIRE((validate_opt_reconnect("1000", error, sizeof(error))) == (INT_MIN));
  REQUIRE((validate_opt_reconnect("-2", error, sizeof(error))) == (INT_MIN));
  REQUIRE((validate_opt_reconnect(NULL, error, sizeof(error))) == (INT_MIN));
  REQUIRE((validate_opt_device_index("-1", error, sizeof(error))) == (-1));
  REQUIRE((validate_opt_device_index("0", error, sizeof(error))) == (0));
  REQUIRE((validate_opt_device_index("-2", error, sizeof(error))) == (INT_MIN));
  REQUIRE((validate_opt_device_index("bad", error, sizeof(error))) == (INT_MIN));
}

TEST_CASE("option_validation_boundaries/passwords_and_remote_identity_key_schemes", "[option_validation_boundaries] [unit]") {
  char error[256], password[258];
  memset(password, 'a', sizeof(password));
  password[257] = '\0';
  REQUIRE((validate_opt_password(NULL, error, sizeof(error))) == (-1));
  REQUIRE((validate_opt_password("short", error, sizeof(error))) == (-1));
  REQUIRE((validate_opt_password("long-enough-password", error, sizeof(error))) == (0));
  REQUIRE((validate_opt_password(password, error, sizeof(error))) == (-1));
  const char *remote[] = {"github:zfogg", "gitlab:user", "gpg:ABCDEF", "http://example.test/key", "https://example.test/key"};
  for (size_t i = 0; i < sizeof(remote) / sizeof(*remote); ++i)
    REQUIRE(is_remote_key_path(remote[i]));
  REQUIRE(!(is_remote_key_path(NULL)));
  REQUIRE(!(is_remote_key_path("")));
  REQUIRE(!(is_remote_key_path("/tmp/local-key")));
  options_t *options = SAFE_CALLOC(1, sizeof(*options), options_t *);
  char *argv[] = {"ascii-chat", "server", "--key", "github:zfogg", "-K", "gpg:ABCDEF", "--key=https://example.test/key"};
  REQUIRE((options_collect_identity_keys(options, 7, argv)) == (3));
  REQUIRE((options->num_identity_keys) == (3));
  REQUIRE(std::strcmp((options->encrypt_key), ("github:zfogg")) == 0);
  REQUIRE(std::strcmp((options->identity_keys[1]), ("gpg:ABCDEF")) == 0);
  REQUIRE(std::strcmp((options->identity_keys[2]), ("https://example.test/key")) == 0);
  SAFE_FREE(options);
}

TEST_CASE("option_validation_boundaries/enum_parsers_reject_unknown_values_without_overwriting_destination", "[option_validation_boundaries] [unit]") {
  bool (*parsers[])(const char *, void *, char **) = {parse_color_mode, parse_color_filter, parse_render_mode, parse_render_theme, parse_palette_type, parse_log_level, parse_port_option};
  for (size_t i = 0; i < sizeof(parsers) / sizeof(*parsers); ++i) {
    int value = 12345;
    char *error = NULL;
    REQUIRE(!(parsers[i]("definitely-invalid", &value, &error)));
    REQUIRE((error) != nullptr);
    REQUIRE((value) == (12345));
    SAFE_FREE(error);
    REQUIRE(!(parsers[i]("value", NULL, &error)));
    SAFE_FREE(error);
  }
  int value = 0;
  char *error = NULL;
  REQUIRE(parse_color_mode("truecolor", &value, &error));
  REQUIRE((value) == (COLOR_MODE_TRUECOLOR));
  REQUIRE(parse_render_mode("half-block", &value, &error));
  REQUIRE((value) == (RENDER_MODE_HALF_BLOCK));
  REQUIRE(parse_log_level("WARN", &value, &error));
  REQUIRE((value) == (LOG_WARN));
  REQUIRE(parse_port_option("65535", &value, &error));
  REQUIRE((value) == (65535));
  options_t *options = SAFE_CALLOC(1, sizeof(*options), options_t *);
  REQUIRE(parse_palette_chars(" .:#", options->palette_custom, &error));
  REQUIRE(std::strcmp((options->palette_custom), (" .:#")) == 0);
  REQUIRE((options->palette_type) == (PALETTE_CUSTOM));
  REQUIRE(!(parse_palette_chars("", options->palette_custom, &error)));
  SAFE_FREE(error);
  SAFE_FREE(options);
}

TEST_CASE("option_validation_boundaries/session_key_array_rejects_excess_and_truncated_paths", "[option_validation_boundaries] [unit]") {
  options_t opts{};
  char *args[MAX_SESSION_KEYS + 2];
  args[0] = "ascii-chat";
  for (size_t i = 1; i < MAX_SESSION_KEYS + 2; ++i)
    args[i] = "--key=github:zfogg";
  REQUIRE((options_collect_identity_keys(&opts, MAX_SESSION_KEYS + 1, args)) == (MAX_SESSION_KEYS));
  REQUIRE((opts.num_identity_keys) == (MAX_SESSION_KEYS));
  REQUIRE((options_collect_identity_keys(&opts, MAX_SESSION_KEYS + 2, args)) == (-1));
  char path[OPTIONS_BUFF_SIZE + 7];
  memcpy(path, "--key=", 6);
  memset(path + 6, 'a', OPTIONS_BUFF_SIZE);
  memcpy(path + 6, "github:", 7);
  path[sizeof(path) - 1] = '\0';
  args[1] = path;
  REQUIRE((options_collect_identity_keys(&opts, 2, args)) == (-1));
}
