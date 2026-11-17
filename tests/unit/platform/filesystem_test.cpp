#include <catch2/catch_test_macros.hpp>
/**
 * @file tests/unit/platform/filesystem_test.c
 * @brief Tests for platform filesystem directory discovery
 * @ingroup tests
 */

#include <string.h>

#include <ascii-chat/common.h>
#include <ascii-chat/paths.h>
#include <libplatform/filesystem.h>
#include <ascii-chat/util/string.h>
TEST_CASE("platform_filesystem/data_directories_are_platform_specific", "[platform_filesystem] [unit]") {
  platform_directory_list_t list = {0};
  REQUIRE((platform_get_data_dir_candidates(ASCIICHAT_INSTALL_PREFIX, &list)) == (ASCIICHAT_OK));

#ifdef _WIN32
  const char *program_data = platform_getenv("PROGRAMDATA");
  if (program_data && program_data[0] != '\0') {
    REQUIRE((list.count) == (1));
    size_t expected_len = strlen(program_data) + strlen("\\ascii-chat\\") + 1;
    char *expected = SAFE_MALLOC(expected_len, char *);
    safe_snprintf(expected, expected_len, "%s\\ascii-chat\\", program_data);
    REQUIRE(std::strcmp((list.candidates[0].path), (expected)) == 0);
    REQUIRE((list.candidates[0].mode) == (0755));
    SAFE_FREE(expected);
  } else {
    REQUIRE((list.count) == (0));
  }
#elif defined(__APPLE__)
  const size_t expected_count = strcmp(ASCIICHAT_INSTALL_PREFIX, "/usr/local") == 0 ? 3 : 4;
  REQUIRE((list.count) == (expected_count));
  REQUIRE(strstr(list.candidates[0].path, "/Library/Application Support/ascii-chat/") != NULL);
  REQUIRE((list.candidates[0].mode) == (DIR_PERM_PRIVATE));
  REQUIRE(std::strcmp((list.candidates[1].path), ("/Library/Application Support/ascii-chat/")) == 0);
  REQUIRE((list.candidates[1].mode) == (0755));
  REQUIRE(std::strcmp((list.candidates[2].path), (ASCIICHAT_INSTALL_PREFIX "/var/ascii-chat/")) == 0);
  REQUIRE((list.candidates[2].mode) == (0755));
  if (list.count == 4) {
    REQUIRE(std::strcmp((list.candidates[3].path), ("/usr/local/var/ascii-chat/")) == 0);
    REQUIRE((list.candidates[3].mode) == (0755));
  }
#else
  REQUIRE((list.count) == (1));
  REQUIRE(std::strcmp((list.candidates[0].path), (ASCIICHAT_INSTALL_PREFIX "/var/ascii-chat/")) == 0);
  REQUIRE((list.candidates[0].mode) == (0755));
#endif

  platform_directory_list_destroy(&list);
  REQUIRE((list.candidates) == nullptr);
  REQUIRE((list.count) == (0));
}
