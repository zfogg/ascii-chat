#include <catch2/catch_test_macros.hpp>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/common.h>
#include <catch2/catch_all.hpp>
#include <ascii-chat/common.h>
#include <libplatform/filesystem.h>

TEST_CASE("test_binary_path/absolute_build_directory_resolves_from_ctest_working_directory", "[test_binary_path]") {
  const char *build_dir = SAFE_GETENV("BUILD_DIR");
  REQUIRE((build_dir) != nullptr);
  REQUIRE((build_dir[0]) == ('/'));
  char expected[1024];
  safe_snprintf(expected, sizeof(expected), "%s/bin/ascii-chat", build_dir);
  REQUIRE(std::strcmp((test_get_binary_path()), (expected)) == 0);
  REQUIRE((platform_access(test_get_binary_path(), PLATFORM_ACCESS_READ)) == (0));
}
