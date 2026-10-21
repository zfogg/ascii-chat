#include <criterion/criterion.h>
#include <ascii-chat/util/path.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>

TestSuite(path_policy);

Test(path_policy, normalized_containment_checks_component_boundaries_and_traversal) {
#ifndef _WIN32
  char output[256];
  cr_assert(path_normalize_copy("/tmp/base/./inner/../file", output, sizeof(output)));
  cr_assert_str_eq(output, "/tmp/base/file");
  cr_assert(path_is_within_base("/tmp/base/inner/../file", "/tmp/base"));
  cr_assert(path_is_within_base("/tmp/base", "/tmp/base/"));
  cr_assert_not(path_is_within_base("/tmp/base-other/file", "/tmp/base"));
  cr_assert_not(path_is_within_base("/tmp/base/../outside", "/tmp/base"));
  cr_assert_not(path_is_within_base("relative/file", "/tmp/base"));
  const char *bases[] = {"/opt/example", "/tmp/base"};
  cr_assert(path_is_within_any_base("/tmp/base/file", bases, 2));
  cr_assert_not(path_is_within_any_base("/tmp/other", bases, 2));
  cr_assert_not(path_normalize_copy("/too-long", output, 2));
#endif
  cr_assert(path_looks_like_path("./key.pub"));
  cr_assert(path_looks_like_path("~/key.pub"));
  cr_assert_not(path_looks_like_path("github:zfogg"));
  cr_assert_not(path_looks_like_path("0123456789abcdef"));
  cr_assert_not(path_looks_like_path(NULL));
}

Test(path_policy, log_validation_protects_existing_unrelated_files_without_modifying_them) {
  char path[256];
  int fd;
  cr_assert_eq(platform_create_temp_file(path, sizeof(path), "ascii-path-policy", &fd), 0);
  char *normalized = NULL;
  cr_assert_eq(path_validate_user_path(path, PATH_ROLE_LOG_FILE, &normalized), ASCIICHAT_OK);
  cr_assert_not_null(normalized);
  SAFE_FREE(normalized);
  const char contents[] = "unrelated data that must survive validation\n";
  cr_assert_eq(platform_write(fd, contents, sizeof(contents) - 1), (ssize_t)sizeof(contents) - 1);
  platform_close(fd);
  cr_assert_eq(path_validate_user_path(path, PATH_ROLE_LOG_FILE, &normalized), ERROR_LOGGING_INIT);
  cr_assert_null(normalized);
  FILE *file = platform_fopen("verify-path-fixture", path, "rb");
  cr_assert_not_null(file);
  char actual[sizeof(contents)] = {0};
  cr_assert_eq(fread(actual, 1, sizeof(contents) - 1, file), sizeof(contents) - 1);
  fclose(file);
  cr_assert_str_eq(actual, contents);
  platform_delete_temp_file(path);
}

Test(path_policy, relative_key_paths_are_canonical_and_protected_system_paths_are_rejected) {
  char *normalized = NULL;
  cr_assert_eq(path_validate_user_path("./keys/../key.pub", PATH_ROLE_KEY_PUBLIC, &normalized), ASCIICHAT_OK);
  cr_assert_not_null(normalized);
  cr_assert(path_is_absolute(normalized));
  cr_assert_null(strstr(normalized, "/../"));
  cr_assert_not_null(strstr(normalized, "key.pub"));
  SAFE_FREE(normalized);
#ifndef _WIN32
  cr_assert_neq(path_validate_user_path("/etc/passwd", PATH_ROLE_KEY_PUBLIC, &normalized), ASCIICHAT_OK);
  cr_assert_null(normalized);
  cr_assert_neq(path_validate_user_path("/etc/shadow", PATH_ROLE_LOG_FILE, &normalized), ASCIICHAT_OK);
  cr_assert_null(normalized);
#endif
  cr_assert_neq(path_validate_user_path("", PATH_ROLE_KEY_PRIVATE, &normalized), ASCIICHAT_OK);
  cr_assert_null(normalized);
}
