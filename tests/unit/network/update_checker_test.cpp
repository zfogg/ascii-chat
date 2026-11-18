#include <catch2/catch_test_macros.hpp>

#include <ascii-chat/common.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/network/update_checker.h>
#include <ascii-chat/version.h>
#include <libplatform/filesystem.h>
#include <libplatform/system.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char *test_config_root;
static char *saved_xdg_config_home;

static void set_test_config_root(void) {
  char directory[256];
  REQUIRE((platform_mkdtemp(directory, sizeof(directory), "ascii-chat-update-check")) == (ASCIICHAT_OK));
  test_config_root = platform_strdup(directory);
  REQUIRE((test_config_root) != nullptr);

  const char *current = getenv("XDG_CONFIG_HOME");
  if (current) {
    saved_xdg_config_home = strdup(current);
  }
  REQUIRE((platform_setenv("XDG_CONFIG_HOME", test_config_root)) == (0));
}

static void restore_test_config_root(void) {
  if (saved_xdg_config_home) {
    platform_setenv("XDG_CONFIG_HOME", saved_xdg_config_home);
    free(saved_xdg_config_home);
    saved_xdg_config_home = NULL;
  } else {
    platform_unsetenv("XDG_CONFIG_HOME");
  }

  char cache_dir[1024];
  snprintf(cache_dir, sizeof(cache_dir), "%s/ascii-chat", test_config_root);
  char cache_path[1024];
  snprintf(cache_path, sizeof(cache_path), "%s/last_update_check", cache_dir);
  (void)platform_unlink(cache_path);
  (void)platform_rmdir_recursive(cache_dir);
  (void)platform_rmdir_recursive(test_config_root);
  free(test_config_root);
  test_config_root = NULL;
}

TEST_CASE("update_checker/cache_round_trip_preserves_release_link", "[update_checker] [unit]") {
  set_test_config_root();

  update_check_result_t expected = {0};
  snprintf(expected.latest_version, sizeof(expected.latest_version), "v99.0.0");
  snprintf(expected.latest_sha, sizeof(expected.latest_sha), "0123456789012345678901234567890123456789");
  snprintf(expected.release_url, sizeof(expected.release_url), "https://github.com/zfogg/ascii-chat/releases/tag/v99.0.0");
  expected.last_check_time = time(NULL);

  REQUIRE((update_check_save_cache(&expected)) == (ASCIICHAT_OK));

  update_check_result_t loaded = {0};
  REQUIRE((update_check_load_cache(&loaded)) == (ASCIICHAT_OK));
  REQUIRE(std::strcmp((loaded.latest_version), (expected.latest_version)) == 0);
  REQUIRE(std::strcmp((loaded.latest_sha), (expected.latest_sha)) == 0);
  REQUIRE(std::strcmp((loaded.release_url), (expected.release_url)) == 0);
  REQUIRE(loaded.update_available);
  REQUIRE(loaded.check_succeeded);

  restore_test_config_root();
}

TEST_CASE("update_checker/cached_current_release_does_not_report_update", "[update_checker] [unit]") {
  set_test_config_root();

  update_check_result_t expected = {0};
  snprintf(expected.latest_version, sizeof(expected.latest_version), "%s", ASCII_CHAT_VERSION_STRING);
  snprintf(expected.release_url, sizeof(expected.release_url), "https://github.com/zfogg/ascii-chat/releases/tag/%s", ASCII_CHAT_VERSION_STRING);
  expected.last_check_time = time(NULL);

  REQUIRE((update_check_save_cache(&expected)) == (ASCIICHAT_OK));

  update_check_result_t loaded = {0};
  REQUIRE((update_check_load_cache(&loaded)) == (ASCIICHAT_OK));
  REQUIRE(!(loaded.update_available));
  REQUIRE(loaded.check_succeeded);

  restore_test_config_root();
}

TEST_CASE("update_checker/cache_without_release_link_is_rejected", "[update_checker] [unit]") {
  set_test_config_root();

  char cache_dir[1024];
  snprintf(cache_dir, sizeof(cache_dir), "%s/ascii-chat", test_config_root);
  REQUIRE((platform_mkdir(cache_dir, 0700)) == (0));
  char cache_path[1024];
  snprintf(cache_path, sizeof(cache_path), "%s/last_update_check", cache_dir);
  FILE *cache = fopen(cache_path, "w");
  REQUIRE((cache) != nullptr);
  fprintf(cache, "%lld\nv99.0.0\n\n", (long long)time(NULL));
  fclose(cache);

  update_check_result_t loaded = {0};
  REQUIRE((update_check_load_cache(&loaded)) != (ASCIICHAT_OK));
  REQUIRE(platform_access(cache_path, PLATFORM_ACCESS_EXISTS) != 0);

  restore_test_config_root();
}

TEST_CASE("update_checker/cache_freshness_obeys_one_week_window", "[update_checker] [unit]") {
  update_check_result_t result = {0};
  result.last_check_time = time(NULL);
  REQUIRE(update_check_is_cache_fresh(&result));

  result.last_check_time = time(NULL) - (7 * 24 * 60 * 60);
  REQUIRE(!(update_check_is_cache_fresh(&result)));

  result.last_check_time = 0;
  REQUIRE(!(update_check_is_cache_fresh(&result)));
}

TEST_CASE("update_checker/notification_contains_action_and_exact_release_source", "[update_checker] [unit]") {
  update_check_result_t result = {0};
  snprintf(result.current_version, sizeof(result.current_version), "v1.0.0");
  snprintf(result.latest_version, sizeof(result.latest_version), "v2.0.0");
  snprintf(result.release_url, sizeof(result.release_url), "https://github.com/zfogg/ascii-chat/releases/tag/v2.0.0");

  char notification[2048];
  update_check_format_notification(&result, notification, sizeof(notification));

  REQUIRE(strstr(notification, "v1.0.0") != NULL);
  REQUIRE(strstr(notification, "v2.0.0") != NULL);
  REQUIRE(strstr(notification, "https://github.com/zfogg/ascii-chat/releases/tag/v2.0.0") != NULL);
  REQUIRE((strstr(notification, "Download:") != NULL || strstr(notification, "Run:") != NULL));
}

TEST_CASE("update_checker/override_cache_directory_accepts_a_missing_or_existing_separator", "[update_checker] [unit]") {
  set_test_config_root();
  const char *existing = getenv("ASCII_CHAT_UPDATE_CACHE_DIR");
  char *saved = existing ? platform_strdup(existing) : NULL;
  char directory[1024], override[1024], cache_path[1024];
  snprintf(directory, sizeof(directory), "%s/override", test_config_root);
  snprintf(cache_path, sizeof(cache_path), "%s/last_update_check", directory);
  update_check_result_t expected = {0};
  snprintf(expected.latest_version, sizeof(expected.latest_version), "v99.0.0");
  snprintf(expected.release_url, sizeof(expected.release_url), "https://example.com/release");
  expected.last_check_time = time(NULL);
  for (unsigned trailing = 0; trailing < 2; ++trailing) {
    snprintf(override, sizeof(override), "%s%s", directory, trailing ? "/" : "");
    REQUIRE((platform_setenv("ASCII_CHAT_UPDATE_CACHE_DIR", override)) == (0));
    REQUIRE((update_check_save_cache(&expected)) == (ASCIICHAT_OK));
    platform_stat_t status;
    REQUIRE((platform_stat(cache_path, &status)) == (0));
    update_check_result_t loaded;
    REQUIRE((update_check_load_cache(&loaded)) == (ASCIICHAT_OK));
    REQUIRE(std::strcmp((loaded.latest_version), (expected.latest_version)) == 0);
  }
  if (saved)
    REQUIRE((platform_setenv("ASCII_CHAT_UPDATE_CACHE_DIR", saved)) == (0));
  else
    REQUIRE((platform_unsetenv("ASCII_CHAT_UPDATE_CACHE_DIR")) == (0));
  SAFE_FREE(saved);
  (void)platform_unlink(cache_path);
  (void)platform_rmdir_recursive(directory);
  restore_test_config_root();
}
