#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/common.h>
#include <catch2/catch_all.hpp>
#include <ascii-chat/common.h>
#include <libplatform/filesystem.h>

TEST_CASE("test_binary_path/absolute_build_directory_resolves_from_ctest_working_directory", "[test_binary_path]") {
  const char *build_dir = SAFE_GETENV("BUILD_DIR");
  ASCIICHAT_REQUIRE_NOT_NULL(build_dir);
  ASCIICHAT_REQUIRE_EQ(build_dir[0], '/');
  char expected[1024];
  safe_snprintf(expected, sizeof(expected), "%s/bin/ascii-chat", build_dir);
  ASCIICHAT_REQUIRE_STR_EQ(test_get_binary_path(), expected);
  ASCIICHAT_REQUIRE_EQ(platform_access(test_get_binary_path(), PLATFORM_ACCESS_READ), 0);
}
