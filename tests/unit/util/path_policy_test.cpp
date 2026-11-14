#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/util/path.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>
TEST_CASE("path_policy/normalized_containment_checks_component_boundaries_and_traversal", "[path_policy]") {
#ifndef _WIN32
  char output[256];
  ASCIICHAT_REQUIRE(path_normalize_copy("/tmp/base/./inner/../file", output, sizeof(output)));
  ASCIICHAT_REQUIRE_STR_EQ(output, "/tmp/base/file");
  ASCIICHAT_REQUIRE(path_is_within_base("/tmp/base/inner/../file", "/tmp/base"));
  ASCIICHAT_REQUIRE(path_is_within_base("/tmp/base", "/tmp/base/"));
  ASCIICHAT_REQUIRE_NOT(path_is_within_base("/tmp/base-other/file", "/tmp/base"));
  ASCIICHAT_REQUIRE_NOT(path_is_within_base("/tmp/base/../outside", "/tmp/base"));
  ASCIICHAT_REQUIRE_NOT(path_is_within_base("relative/file", "/tmp/base"));
  const char *bases[] = {"/opt/example", "/tmp/base"};
  ASCIICHAT_REQUIRE(path_is_within_any_base("/tmp/base/file", bases, 2));
  ASCIICHAT_REQUIRE_NOT(path_is_within_any_base("/tmp/other", bases, 2));
  ASCIICHAT_REQUIRE_NOT(path_normalize_copy("/too-long", output, 2));
#endif
  ASCIICHAT_REQUIRE(path_looks_like_path("./key.pub"));
  ASCIICHAT_REQUIRE(path_looks_like_path("~/key.pub"));
  ASCIICHAT_REQUIRE_NOT(path_looks_like_path("github:zfogg"));
  ASCIICHAT_REQUIRE_NOT(path_looks_like_path("0123456789abcdef"));
  ASCIICHAT_REQUIRE_NOT(path_looks_like_path(NULL));
}

TEST_CASE("path_policy/log_validation_protects_existing_unrelated_files_without_modifying_them", "[path_policy]") {
  char path[256];
  int fd;
  ASCIICHAT_REQUIRE_EQ(platform_create_temp_file(path, sizeof(path), "ascii-path-policy", &fd), 0);
  char *normalized = NULL;
  ASCIICHAT_REQUIRE_EQ(path_validate_user_path(path, PATH_ROLE_LOG_FILE, &normalized), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NOT_NULL(normalized);
  SAFE_FREE(normalized);
  const char contents[] = "unrelated data that must survive validation\n";
  ASCIICHAT_REQUIRE_EQ(platform_write(fd, contents, sizeof(contents) - 1), (ssize_t)sizeof(contents) - 1);
  platform_close(fd);
  ASCIICHAT_REQUIRE_EQ(path_validate_user_path(path, PATH_ROLE_LOG_FILE, &normalized), ERROR_LOGGING_INIT);
  ASCIICHAT_REQUIRE_NULL(normalized);
  FILE *file = platform_fopen("verify-path-fixture", path, "rb");
  ASCIICHAT_REQUIRE_NOT_NULL(file);
  char actual[sizeof(contents)] = {0};
  ASCIICHAT_REQUIRE_EQ(fread(actual, 1, sizeof(contents) - 1, file), sizeof(contents) - 1);
  fclose(file);
  ASCIICHAT_REQUIRE_STR_EQ(actual, contents);
  platform_delete_temp_file(path);
}

TEST_CASE("path_policy/relative_key_paths_are_canonical_and_protected_system_paths_are_rejected", "[path_policy]") {
  char *normalized = NULL;
  ASCIICHAT_REQUIRE_EQ(path_validate_user_path("./keys/../key.pub", PATH_ROLE_KEY_PUBLIC, &normalized), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NOT_NULL(normalized);
  ASCIICHAT_REQUIRE(path_is_absolute(normalized));
  ASCIICHAT_REQUIRE_NULL(strstr(normalized, "/../"));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(normalized, "key.pub"));
  SAFE_FREE(normalized);
#ifndef _WIN32
  ASCIICHAT_REQUIRE_NE(path_validate_user_path("/etc/passwd", PATH_ROLE_KEY_PUBLIC, &normalized), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NULL(normalized);
  ASCIICHAT_REQUIRE_NE(path_validate_user_path("/etc/shadow", PATH_ROLE_LOG_FILE, &normalized), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NULL(normalized);
#endif
  ASCIICHAT_REQUIRE_NE(path_validate_user_path("", PATH_ROLE_KEY_PRIVATE, &normalized), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NULL(normalized);
}
