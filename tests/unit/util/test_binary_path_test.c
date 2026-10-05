#include <criterion/criterion.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/common.h>
#include <ascii-chat/platform/filesystem.h>

Test(test_binary_path, absolute_build_directory_resolves_from_ctest_working_directory) {
  const char *build_dir = SAFE_GETENV("BUILD_DIR");
  cr_assert_not_null(build_dir);
  cr_assert_eq(build_dir[0], '/');
  char expected[1024];
  safe_snprintf(expected, sizeof(expected), "%s/bin/ascii-chat", build_dir);
  cr_assert_str_eq(test_get_binary_path(), expected);
  cr_assert_eq(platform_access(test_get_binary_path(), PLATFORM_ACCESS_READ), 0);
}
