#include <catch2/catch_test_macros.hpp>
#include <ascii-chat/util/path.h>
#include <ascii-chat/util/pcre2.h>
#include <libplatform/filesystem.h>
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
  REQUIRE((platform_mkdtemp(directory, sizeof(directory), "path-contract")) == (ASCIICHAT_OK));
  for (size_t i = 0; i < 4; ++i) {
    const char *value = getenv(names[i]);
    if (value)
      SAFE_STRDUP(saved[i], value);
    REQUIRE((platform_setenv(names[i], directory)) == (0));
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
TEST_CASE("path_contract/debug_default_log_directory_is_allowed_from_another_working_directory", "[path_contract] [unit]") {
  char previous[PLATFORM_MAX_PATH_LENGTH];
  char path[PLATFORM_MAX_PATH_LENGTH];
  char *normalized = NULL;
  REQUIRE(platform_get_cwd(previous, sizeof(previous)));
  REQUIRE((test_chdir(directory)) == (0));
  safe_snprintf(path, sizeof(path), "%s/ascii-chat-path-contract-test.log", ASCIICHAT_SOURCE_ROOT);
  asciichat_error_t result = path_validate_user_path(path, PATH_ROLE_LOG_FILE, &normalized);
  REQUIRE((test_chdir(previous)) == (0));
  REQUIRE((result) == (ASCIICHAT_OK));
  REQUIRE((normalized) != nullptr);
  SAFE_FREE(normalized);
}
#endif

TEST_CASE("path_contract/xdg_paths_override_home_and_empty_environment_falls_back_without_allocating_garbage", "[path_contract] [unit]") {
  char expected[PLATFORM_MAX_PATH_LENGTH];
  safe_snprintf(expected, sizeof(expected), "%s/ascii-chat/", directory);
  char *config = get_config_dir(), *data = get_data_dir();
  REQUIRE((config) != nullptr);
  REQUIRE((data) != nullptr);
  REQUIRE(std::strcmp((config), (expected)) == 0);
  REQUIRE(std::strcmp((data), (expected)) == 0);
  SAFE_FREE(config);
  SAFE_FREE(data);
  platform_setenv("XDG_CONFIG_HOME", "");
  platform_unsetenv("XDG_DATA_HOME");
  config = get_config_dir();
  data = get_data_dir();
  safe_snprintf(expected, sizeof(expected), "%s/.config/ascii-chat/", directory);
  REQUIRE(std::strcmp((config), (expected)) == 0);
  safe_snprintf(expected, sizeof(expected), "%s/.local/share/ascii-chat/", directory);
  REQUIRE(std::strcmp((data), (expected)) == 0);
  SAFE_FREE(config);
  SAFE_FREE(data);
  char *expanded = expand_path("~/keys/server.pub");
  safe_snprintf(expected, sizeof(expected), "%s/keys/server.pub", directory);
  REQUIRE(std::strcmp((expanded), (expected)) == 0);
  SAFE_FREE(expanded);
  platform_unsetenv("HOME");
  REQUIRE((get_config_dir()) == nullptr);
  REQUIRE((get_data_dir()) == nullptr);
  REQUIRE((expand_path("~/key")) == nullptr);
  expanded = expand_path("relative-key.pub");
  REQUIRE(std::strcmp((expanded), ("relative-key.pub")) == 0);
  SAFE_FREE(expanded);
}

TEST_CASE("path_contract/role_specific_rejections_clear_output_for_invalid_protected_and_outside_paths", "[path_contract] [unit]") {
  const path_role_t roles[] = {PATH_ROLE_CONFIG_FILE, PATH_ROLE_LOG_FILE, PATH_ROLE_KEY_PRIVATE, PATH_ROLE_KEY_PUBLIC, PATH_ROLE_CLIENT_KEYS};
  const asciichat_error_t errors[] = {ERROR_CONFIG, ERROR_LOGGING_INIT, ERROR_CRYPTO_KEY, ERROR_CRYPTO_KEY, ERROR_CRYPTO_KEY};
  for (size_t i = 0; i < 5; ++i) {
    const char *invalid[] = {NULL, "", "/etc", "/etc/passwd", "/etc/../etc/shadow", "/var/lib/ascii-chat-policy-test", "/ascii-chat-outside-policy/file"};
    for (size_t j = 0; j < sizeof(invalid) / sizeof(invalid[0]); ++j) {
      char *output = (char *)"must be cleared";
      REQUIRE((path_validate_user_path(invalid[j], roles[i], &output)) == (errors[i]));
      REQUIRE((output) == nullptr);
      CLEAR_ERRNO_ALL();
    }
    REQUIRE((path_validate_user_path("~/key", roles[i], NULL)) == (errors[i]));
    CLEAR_ERRNO_ALL();
  }
}

TEST_CASE("path_contract/existing_colored_application_log_is_accepted_without_changing_its_bytes", "[path_contract] [unit]") {
  char path[PLATFORM_MAX_PATH_LENGTH];
  safe_snprintf(path, sizeof(path), "%s/existing.log", directory);
  const char contents[] = "\033[32m[12:34:56.123456] [INFO] prior application log\033[0m\n";
  FILE *file = platform_fopen("path-fixture", path, "wb");
  REQUIRE((file) != nullptr);
  REQUIRE((fwrite(contents, 1, sizeof(contents) - 1, file)) == (sizeof(contents) - 1));
  REQUIRE((fclose(file)) == (0));
  char *output = NULL;
  REQUIRE((path_validate_user_path(path, PATH_ROLE_LOG_FILE, &output)) == (ASCIICHAT_OK));
  REQUIRE(std::strcmp((output), (path)) == 0);
  SAFE_FREE(output);
  file = platform_fopen("path-readback", path, "rb");
  REQUIRE((file) != nullptr);
  char readback[sizeof(contents)] = {0};
  REQUIRE((fread(readback, 1, sizeof(contents) - 1, file)) == (sizeof(contents) - 1));
  REQUIRE((fclose(file)) == (0));
  REQUIRE(std::memcmp((readback), (contents), (sizeof(contents))) == 0);
}

TEST_CASE("path_contract/log_directory_uses_the_platform_temp_directory_in_release_and_source_root_in_debug", "[path_contract] [unit]") {
  char *logs = get_log_dir();
  REQUIRE((logs) != nullptr);
#ifdef NDEBUG
  char temp[PLATFORM_MAX_PATH_LENGTH], expected[PLATFORM_MAX_PATH_LENGTH];
  REQUIRE(platform_get_temp_dir(temp, sizeof(temp)));
  safe_snprintf(expected, sizeof(expected), "%s/ascii-chat", temp);
  REQUIRE(std::strcmp((logs), (expected)) == 0);
  REQUIRE(platform_is_directory(logs));
#else
  REQUIRE(std::strcmp((logs), (ASCIICHAT_SOURCE_ROOT)) == 0);
#endif
  SAFE_FREE(logs);
}
TEST_CASE("path_contract/root_containment_and_component_boundaries_do_not_confuse_sibling_paths", "[path_contract] [unit]") {
  REQUIRE(path_is_within_base("/tmp/child", "/"));
  REQUIRE(path_is_within_base("/", "/"));
  REQUIRE(!(path_is_within_base("relative", "/")));
  REQUIRE(path_is_within_base("/base/inside/../file", "/base/"));
  REQUIRE(!(path_is_within_base("/base-sibling/file", "/base/")));
  REQUIRE(!(path_is_within_base("/base/../escape", "/base")));
  const char *bases[] = {NULL, "", "relative", "/base"};
  REQUIRE(path_is_within_any_base("/base/file", bases, 4));
  REQUIRE(!(path_is_within_any_base("/elsewhere/file", bases, 4)));
  REQUIRE(!(path_is_within_any_base(NULL, bases, 4)));
  REQUIRE(!(path_is_within_any_base("/base/file", NULL, 0)));
  REQUIRE(std::strcmp((path_last_components("/a//b/file", 2)), ("b/file")) == 0);
  REQUIRE(std::strcmp((path_last_components("C:\\a\\b\\file", 2)), ("b\\file")) == 0);
  REQUIRE((path_last_components(NULL, 1)) == nullptr);
}
