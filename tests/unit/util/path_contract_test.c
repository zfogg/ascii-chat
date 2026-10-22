#include <criterion/criterion.h>
#include <ascii-chat/util/path.h>
#include <ascii-chat/util/pcre2.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/source_root.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>
#include <stdlib.h>

static char directory[PLATFORM_MAX_PATH_LENGTH];
static const char *names[] = {"HOME", "TMPDIR", "XDG_CONFIG_HOME", "XDG_DATA_HOME"};
static char *saved[4];
static void setup(void) {
  log_init(NULL, LOG_FATAL, false, false);
  cr_assert_eq(platform_mkdtemp(directory, sizeof(directory), "path-contract"), ASCIICHAT_OK);
  for (size_t i = 0; i < 4; ++i) {
    const char *value = getenv(names[i]);
    if (value)
      SAFE_STRDUP(saved[i], value);
    cr_assert_eq(platform_setenv(names[i], directory), 0);
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
  asciichat_pcre2_cleanup_all();
}
TestSuite(path_contract, .init = setup, .fini = teardown);

Test(path_contract, xdg_paths_override_home_and_empty_environment_falls_back_without_allocating_garbage) {
  char expected[PLATFORM_MAX_PATH_LENGTH];
  safe_snprintf(expected, sizeof(expected), "%s/ascii-chat/", directory);
  char *config = get_config_dir(), *data = get_data_dir();
  cr_assert_not_null(config);
  cr_assert_not_null(data);
  cr_assert_str_eq(config, expected);
  cr_assert_str_eq(data, expected);
  SAFE_FREE(config);
  SAFE_FREE(data);
  platform_setenv("XDG_CONFIG_HOME", "");
  platform_unsetenv("XDG_DATA_HOME");
  config = get_config_dir();
  data = get_data_dir();
  safe_snprintf(expected, sizeof(expected), "%s/.config/ascii-chat/", directory);
  cr_assert_str_eq(config, expected);
  safe_snprintf(expected, sizeof(expected), "%s/.local/share/ascii-chat/", directory);
  cr_assert_str_eq(data, expected);
  SAFE_FREE(config);
  SAFE_FREE(data);
  char *expanded = expand_path("~/keys/server.pub");
  safe_snprintf(expected, sizeof(expected), "%s/keys/server.pub", directory);
  cr_assert_str_eq(expanded, expected);
  SAFE_FREE(expanded);
  platform_unsetenv("HOME");
  cr_assert_null(get_config_dir());
  cr_assert_null(get_data_dir());
  cr_assert_null(expand_path("~/key"));
  expanded = expand_path("relative-key.pub");
  cr_assert_str_eq(expanded, "relative-key.pub");
  SAFE_FREE(expanded);
}

Test(path_contract, role_specific_rejections_clear_output_for_invalid_protected_and_outside_paths) {
  const path_role_t roles[] = {PATH_ROLE_CONFIG_FILE, PATH_ROLE_LOG_FILE, PATH_ROLE_KEY_PRIVATE,
                               PATH_ROLE_KEY_PUBLIC, PATH_ROLE_CLIENT_KEYS};
  const asciichat_error_t errors[] = {ERROR_CONFIG, ERROR_LOGGING_INIT, ERROR_CRYPTO_KEY,
                                     ERROR_CRYPTO_KEY, ERROR_CRYPTO_KEY};
  for (size_t i = 0; i < 5; ++i) {
    const char *invalid[] = {NULL, "", "/etc", "/etc/passwd", "/etc/../etc/shadow",
                             "/var/lib/ascii-chat-policy-test", "/ascii-chat-outside-policy/file"};
    for (size_t j = 0; j < sizeof(invalid) / sizeof(invalid[0]); ++j) {
      char *output = (char *)"must be cleared";
      cr_assert_eq(path_validate_user_path(invalid[j], roles[i], &output), errors[i]);
      cr_assert_null(output);
      CLEAR_ERRNO_ALL();
    }
    cr_assert_eq(path_validate_user_path("~/key", roles[i], NULL), errors[i]);
    CLEAR_ERRNO_ALL();
  }
}

Test(path_contract, existing_colored_application_log_is_accepted_without_changing_its_bytes) {
  char path[PLATFORM_MAX_PATH_LENGTH];
  safe_snprintf(path, sizeof(path), "%s/existing.log", directory);
  const char contents[] = "\033[32m[12:34:56.123456] [INFO] prior application log\033[0m\n";
  FILE *file = platform_fopen("path-fixture", path, "wb");
  cr_assert_not_null(file);
  cr_assert_eq(fwrite(contents, 1, sizeof(contents) - 1, file), sizeof(contents) - 1);
  cr_assert_eq(fclose(file), 0);
  char *output = NULL;
  cr_assert_eq(path_validate_user_path(path, PATH_ROLE_LOG_FILE, &output), ASCIICHAT_OK);
  cr_assert_str_eq(output, path);
  SAFE_FREE(output);
  file = platform_fopen("path-readback", path, "rb");
  cr_assert_not_null(file);
  char readback[sizeof(contents)] = {0};
  cr_assert_eq(fread(readback, 1, sizeof(contents) - 1, file), sizeof(contents) - 1);
  cr_assert_eq(fclose(file), 0);
  cr_assert_arr_eq(readback, contents, sizeof(contents));
}

Test(path_contract, log_directory_uses_the_platform_temp_directory_in_release_and_source_root_in_debug) {
  char *logs = get_log_dir();
  cr_assert_not_null(logs);
#ifdef NDEBUG
  char temp[PLATFORM_MAX_PATH_LENGTH], expected[PLATFORM_MAX_PATH_LENGTH];
  cr_assert(platform_get_temp_dir(temp, sizeof(temp)));
  safe_snprintf(expected, sizeof(expected), "%s/ascii-chat", temp);
  cr_assert_str_eq(logs, expected);
  cr_assert(platform_is_directory(logs));
#else
  cr_assert_str_eq(logs, ASCIICHAT_SOURCE_ROOT);
#endif
  SAFE_FREE(logs);
}
Test(path_contract, root_containment_and_component_boundaries_do_not_confuse_sibling_paths) {
  cr_assert(path_is_within_base("/tmp/child", "/"));
  cr_assert(path_is_within_base("/", "/"));
  cr_assert_not(path_is_within_base("relative", "/"));
  cr_assert(path_is_within_base("/base/inside/../file", "/base/"));
  cr_assert_not(path_is_within_base("/base-sibling/file", "/base/"));
  cr_assert_not(path_is_within_base("/base/../escape", "/base"));
  const char *bases[] = {NULL, "", "relative", "/base"};
  cr_assert(path_is_within_any_base("/base/file", bases, 4));
  cr_assert_not(path_is_within_any_base("/elsewhere/file", bases, 4));
  cr_assert_not(path_is_within_any_base(NULL, bases, 4));
  cr_assert_not(path_is_within_any_base("/base/file", NULL, 0));
  cr_assert_str_eq(path_last_components("/a//b/file", 2), "b/file");
  cr_assert_str_eq(path_last_components("C:\\a\\b\\file", 2), "b\\file");
  cr_assert_null(path_last_components(NULL, 1));
}