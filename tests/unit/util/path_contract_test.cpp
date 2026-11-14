#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/util/path.h>
#include <ascii-chat/util/pcre2.h>
#include <ascii-chat/platform/filesystem.h>
#include <libplatform/system.h>
#include <ascii-chat/source_root.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <direct.h>
#define test_chdir _chdir
#else
#include <unistd.h>
#define test_chdir chdir
#endif

static char directory[PLATFORM_MAX_PATH_LENGTH];
static const char *names[] = {"HOME", "TMPDIR", "XDG_CONFIG_HOME", "XDG_DATA_HOME"};
static char *saved[4];
static void setup(void) {
  log_init(NULL, LOG_FATAL, false, false);
  ASCIICHAT_REQUIRE_EQ(platform_mkdtemp(directory, sizeof(directory), "path-contract"), ASCIICHAT_OK);
  for (size_t i = 0; i < 4; ++i) {
    const char *value = getenv(names[i]);
    if (value)
      SAFE_STRDUP(saved[i], value);
    ASCIICHAT_REQUIRE_EQ(platform_setenv(names[i], directory), 0);
  }
}
static void teardown(void) {
  for (size_t i = 0; i < 4; ++i) {
    if (saved[i]) {
      platform_setenv(names[i], saved[i]);
      SAFE_FREE(saved[i]);
    } else {
      platform_unsetenv(names[i]);
    }
  }
  platform_rmdir_recursive(directory);
  CLEAR_ERRNO_ALL();
  log_destroy();
}
#ifndef NDEBUG
TEST_CASE("path_contract/debug_default_log_directory_is_allowed_from_another_working_directory", "[path_contract]") {
  char previous[PLATFORM_MAX_PATH_LENGTH];
  char path[PLATFORM_MAX_PATH_LENGTH];
  char *normalized = NULL;
  ASCIICHAT_REQUIRE(platform_get_cwd(previous, sizeof(previous)));
  ASCIICHAT_REQUIRE_EQ(test_chdir(directory), 0);
  safe_snprintf(path, sizeof(path), "%s/ascii-chat-path-contract-test.log", ASCIICHAT_SOURCE_ROOT);
  asciichat_error_t result = path_validate_user_path(path, PATH_ROLE_LOG_FILE, &normalized);
  ASCIICHAT_REQUIRE_EQ(test_chdir(previous), 0);
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NOT_NULL(normalized);
  SAFE_FREE(normalized);
}
#endif

TEST_CASE("path_contract/xdg_paths_override_home_and_empty_environment_falls_back_without_allocating_garbage", "[path_contract]") {
  char expected[PLATFORM_MAX_PATH_LENGTH];
  safe_snprintf(expected, sizeof(expected), "%s/ascii-chat/", directory);
  char *config = get_config_dir(), *data = get_data_dir();
  ASCIICHAT_REQUIRE_NOT_NULL(config);
  ASCIICHAT_REQUIRE_NOT_NULL(data);
  ASCIICHAT_REQUIRE_STR_EQ(config, expected);
  ASCIICHAT_REQUIRE_STR_EQ(data, expected);
  SAFE_FREE(config);
  SAFE_FREE(data);
  platform_setenv("XDG_CONFIG_HOME", "");
  platform_unsetenv("XDG_DATA_HOME");
  config = get_config_dir();
  data = get_data_dir();
  safe_snprintf(expected, sizeof(expected), "%s/.config/ascii-chat/", directory);
  ASCIICHAT_REQUIRE_STR_EQ(config, expected);
  safe_snprintf(expected, sizeof(expected), "%s/.local/share/ascii-chat/", directory);
  ASCIICHAT_REQUIRE_STR_EQ(data, expected);
  SAFE_FREE(config);
  SAFE_FREE(data);
  char *expanded = expand_path("~/keys/server.pub");
  safe_snprintf(expected, sizeof(expected), "%s/keys/server.pub", directory);
  ASCIICHAT_REQUIRE_STR_EQ(expanded, expected);
  SAFE_FREE(expanded);
  platform_unsetenv("HOME");
  ASCIICHAT_REQUIRE_NULL(get_config_dir());
  ASCIICHAT_REQUIRE_NULL(get_data_dir());
  ASCIICHAT_REQUIRE_NULL(expand_path("~/key"));
  expanded = expand_path("relative-key.pub");
  ASCIICHAT_REQUIRE_STR_EQ(expanded, "relative-key.pub");
  SAFE_FREE(expanded);
}

TEST_CASE("path_contract/role_specific_rejections_clear_output_for_invalid_protected_and_outside_paths", "[path_contract]") {
  const path_role_t roles[] = {PATH_ROLE_CONFIG_FILE, PATH_ROLE_LOG_FILE, PATH_ROLE_KEY_PRIVATE, PATH_ROLE_KEY_PUBLIC,
                               PATH_ROLE_CLIENT_KEYS};
  const asciichat_error_t errors[] = {ERROR_CONFIG, ERROR_LOGGING_INIT, ERROR_CRYPTO_KEY, ERROR_CRYPTO_KEY,
                                      ERROR_CRYPTO_KEY};
  for (size_t i = 0; i < 5; ++i) {
    const char *invalid[] = {NULL,
                             "",
                             "/etc",
                             "/etc/passwd",
                             "/etc/../etc/shadow",
                             "/var/lib/ascii-chat-policy-test",
                             "/ascii-chat-outside-policy/file"};
    for (size_t j = 0; j < sizeof(invalid) / sizeof(invalid[0]); ++j) {
      char *output = (char *)"must be cleared";
      ASCIICHAT_REQUIRE_EQ(path_validate_user_path(invalid[j], roles[i], &output), errors[i]);
      ASCIICHAT_REQUIRE_NULL(output);
      CLEAR_ERRNO_ALL();
    }
    ASCIICHAT_REQUIRE_EQ(path_validate_user_path("~/key", roles[i], NULL), errors[i]);
    CLEAR_ERRNO_ALL();
  }
}

TEST_CASE("path_contract/existing_colored_application_log_is_accepted_without_changing_its_bytes", "[path_contract]") {
  char path[PLATFORM_MAX_PATH_LENGTH];
  safe_snprintf(path, sizeof(path), "%s/existing.log", directory);
  const char contents[] = "\033[32m[12:34:56.123456] [INFO] prior application log\033[0m\n";
  FILE *file = platform_fopen("path-fixture", path, "wb");
  ASCIICHAT_REQUIRE_NOT_NULL(file);
  ASCIICHAT_REQUIRE_EQ(fwrite(contents, 1, sizeof(contents) - 1, file), sizeof(contents) - 1);
  ASCIICHAT_REQUIRE_EQ(fclose(file), 0);
  char *output = NULL;
  ASCIICHAT_REQUIRE_EQ(path_validate_user_path(path, PATH_ROLE_LOG_FILE, &output), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_STR_EQ(output, path);
  SAFE_FREE(output);
  file = platform_fopen("path-readback", path, "rb");
  ASCIICHAT_REQUIRE_NOT_NULL(file);
  char readback[sizeof(contents)] = {0};
  ASCIICHAT_REQUIRE_EQ(fread(readback, 1, sizeof(contents) - 1, file), sizeof(contents) - 1);
  ASCIICHAT_REQUIRE_EQ(fclose(file), 0);
  ASCIICHAT_REQUIRE_ARRAY_EQ(readback, contents, sizeof(contents));
}

TEST_CASE("path_contract/log_directory_uses_the_platform_temp_directory_in_release_and_source_root_in_debug", "[path_contract]") {
  char *logs = get_log_dir();
  ASCIICHAT_REQUIRE_NOT_NULL(logs);
#ifdef NDEBUG
  char temp[PLATFORM_MAX_PATH_LENGTH], expected[PLATFORM_MAX_PATH_LENGTH];
  ASCIICHAT_REQUIRE(platform_get_temp_dir(temp, sizeof(temp)));
  safe_snprintf(expected, sizeof(expected), "%s/ascii-chat", temp);
  ASCIICHAT_REQUIRE_STR_EQ(logs, expected);
  ASCIICHAT_REQUIRE(platform_is_directory(logs));
#else
  ASCIICHAT_REQUIRE_STR_EQ(logs, ASCIICHAT_SOURCE_ROOT);
#endif
  SAFE_FREE(logs);
}
TEST_CASE("path_contract/root_containment_and_component_boundaries_do_not_confuse_sibling_paths", "[path_contract]") {
  ASCIICHAT_REQUIRE(path_is_within_base("/tmp/child", "/"));
  ASCIICHAT_REQUIRE(path_is_within_base("/", "/"));
  ASCIICHAT_REQUIRE_NOT(path_is_within_base("relative", "/"));
  ASCIICHAT_REQUIRE(path_is_within_base("/base/inside/../file", "/base/"));
  ASCIICHAT_REQUIRE_NOT(path_is_within_base("/base-sibling/file", "/base/"));
  ASCIICHAT_REQUIRE_NOT(path_is_within_base("/base/../escape", "/base"));
  const char *bases[] = {NULL, "", "relative", "/base"};
  ASCIICHAT_REQUIRE(path_is_within_any_base("/base/file", bases, 4));
  ASCIICHAT_REQUIRE_NOT(path_is_within_any_base("/elsewhere/file", bases, 4));
  ASCIICHAT_REQUIRE_NOT(path_is_within_any_base(NULL, bases, 4));
  ASCIICHAT_REQUIRE_NOT(path_is_within_any_base("/base/file", NULL, 0));
  ASCIICHAT_REQUIRE_STR_EQ(path_last_components("/a//b/file", 2), "b/file");
  ASCIICHAT_REQUIRE_STR_EQ(path_last_components("C:\\a\\b\\file", 2), "b\\file");
  ASCIICHAT_REQUIRE_NULL(path_last_components(NULL, 1));
}
