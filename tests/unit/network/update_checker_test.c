#include <criterion/criterion.h>

#include <ascii-chat/network/update_checker.h>
#include <ascii-chat/version.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static char *test_config_root;
static char *saved_xdg_config_home;

static void set_test_config_root(void) {
  char template_path[] = "/tmp/ascii-chat-update-check-XXXXXX";
  char *directory = mkdtemp(template_path);
  cr_assert_not_null(directory, "mkdtemp failed");
  test_config_root = strdup(directory);
  cr_assert_not_null(test_config_root, "strdup failed");

  const char *current = getenv("XDG_CONFIG_HOME");
  if (current) {
    saved_xdg_config_home = strdup(current);
  }
  cr_assert_eq(setenv("XDG_CONFIG_HOME", test_config_root, 1), 0);
}

static void restore_test_config_root(void) {
  if (saved_xdg_config_home) {
    setenv("XDG_CONFIG_HOME", saved_xdg_config_home, 1);
    free(saved_xdg_config_home);
    saved_xdg_config_home = NULL;
  } else {
    unsetenv("XDG_CONFIG_HOME");
  }

  char cache_dir[1024];
  snprintf(cache_dir, sizeof(cache_dir), "%s/ascii-chat", test_config_root);
  char cache_path[1024];
  snprintf(cache_path, sizeof(cache_path), "%s/last_update_check", cache_dir);
  (void)remove(cache_path);
  (void)rmdir(cache_dir);
  (void)rmdir(test_config_root);
  test_config_root = NULL;
}

Test(update_checker, cache_round_trip_preserves_release_link) {
  set_test_config_root();

  update_check_result_t expected = {0};
  snprintf(expected.latest_version, sizeof(expected.latest_version), "v99.0.0");
  snprintf(expected.latest_sha, sizeof(expected.latest_sha), "0123456789012345678901234567890123456789");
  snprintf(expected.release_url, sizeof(expected.release_url),
           "https://github.com/zfogg/ascii-chat/releases/tag/v99.0.0");
  expected.last_check_time = time(NULL);

  cr_assert_eq(update_check_save_cache(&expected), ASCIICHAT_OK);

  update_check_result_t loaded = {0};
  cr_assert_eq(update_check_load_cache(&loaded), ASCIICHAT_OK);
  cr_assert_str_eq(loaded.latest_version, expected.latest_version);
  cr_assert_str_eq(loaded.latest_sha, expected.latest_sha);
  cr_assert_str_eq(loaded.release_url, expected.release_url);
  cr_assert(loaded.update_available, "a newer cached release must be reported");
  cr_assert(loaded.check_succeeded, "a valid cached release must be marked successful");

  restore_test_config_root();
}

Test(update_checker, cached_current_release_does_not_report_update) {
  set_test_config_root();

  update_check_result_t expected = {0};
  snprintf(expected.latest_version, sizeof(expected.latest_version), "%s", ASCII_CHAT_VERSION_STRING);
  snprintf(expected.release_url, sizeof(expected.release_url), "https://github.com/zfogg/ascii-chat/releases/tag/%s",
           ASCII_CHAT_VERSION_STRING);
  expected.last_check_time = time(NULL);

  cr_assert_eq(update_check_save_cache(&expected), ASCIICHAT_OK);

  update_check_result_t loaded = {0};
  cr_assert_eq(update_check_load_cache(&loaded), ASCIICHAT_OK);
  cr_assert_not(loaded.update_available, "the current cached release must not trigger an update notice");
  cr_assert(loaded.check_succeeded, "a valid current-release cache must be marked successful");

  restore_test_config_root();
}

Test(update_checker, cache_without_release_link_is_rejected) {
  set_test_config_root();

  char cache_dir[1024];
  snprintf(cache_dir, sizeof(cache_dir), "%s/ascii-chat", test_config_root);
  cr_assert_eq(mkdir(cache_dir, 0700), 0);
  char cache_path[1024];
  snprintf(cache_path, sizeof(cache_path), "%s/last_update_check", cache_dir);
  FILE *cache = fopen(cache_path, "w");
  cr_assert_not_null(cache);
  fprintf(cache, "%lld\nv99.0.0\n\n", (long long)time(NULL));
  fclose(cache);

  update_check_result_t loaded = {0};
  cr_assert_neq(update_check_load_cache(&loaded), ASCIICHAT_OK);
  cr_assert(access(cache_path, F_OK) != 0, "legacy cache should be deleted and treated as missing");

  restore_test_config_root();
}

Test(update_checker, cache_freshness_obeys_one_week_window) {
  update_check_result_t result = {0};
  result.last_check_time = time(NULL);
  cr_assert(update_check_is_cache_fresh(&result));

  result.last_check_time = time(NULL) - (7 * 24 * 60 * 60);
  cr_assert_not(update_check_is_cache_fresh(&result));

  result.last_check_time = 0;
  cr_assert_not(update_check_is_cache_fresh(&result));
}

Test(update_checker, notification_contains_action_and_exact_release_source) {
  update_check_result_t result = {0};
  snprintf(result.current_version, sizeof(result.current_version), "v1.0.0");
  snprintf(result.latest_version, sizeof(result.latest_version), "v2.0.0");
  snprintf(result.release_url, sizeof(result.release_url), "https://github.com/zfogg/ascii-chat/releases/tag/v2.0.0");

  char notification[2048];
  update_check_format_notification(&result, notification, sizeof(notification));

  cr_assert(strstr(notification, "v1.0.0") != NULL);
  cr_assert(strstr(notification, "v2.0.0") != NULL);
  cr_assert(strstr(notification, "https://github.com/zfogg/ascii-chat/releases/tag/v2.0.0") != NULL);
  cr_assert(strstr(notification, "Download:") != NULL || strstr(notification, "Run:") != NULL);
}
