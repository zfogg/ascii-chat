/**
 * @file tests/unit/platform/filesystem_test.c
 * @brief Tests for platform filesystem directory discovery
 * @ingroup tests
 */

#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <string.h>

#include <ascii-chat/common.h>
#include <ascii-chat/paths.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/util/string.h>
TEST_CASE("platform_filesystem/data_directories_are_platform_specific", "[platform_filesystem]") {
  platform_directory_list_t list = {0};
  ASCIICHAT_REQUIRE_EQ(platform_get_data_dir_candidates(ASCIICHAT_INSTALL_PREFIX, &list), ASCIICHAT_OK);

#ifdef _WIN32
  const char *program_data = platform_getenv("PROGRAMDATA");
  if (program_data && program_data[0] != '\0') {
    ASCIICHAT_REQUIRE_EQ(list.count, 1);
    size_t expected_len = strlen(program_data) + strlen("\\ascii-chat\\") + 1;
    char *expected = SAFE_MALLOC(expected_len, char *);
    safe_snprintf(expected, expected_len, "%s\\ascii-chat\\", program_data);
    ASCIICHAT_REQUIRE_STR_EQ(list.candidates[0].path, expected);
    ASCIICHAT_REQUIRE_EQ(list.candidates[0].mode, 0755);
    SAFE_FREE(expected);
  } else {
    ASCIICHAT_REQUIRE_EQ(list.count, 0);
  }
#elif defined(__APPLE__)
  const size_t expected_count = strcmp(ASCIICHAT_INSTALL_PREFIX, "/usr/local") == 0 ? 3 : 4;
  ASCIICHAT_REQUIRE_EQ(list.count, expected_count);
  ASCIICHAT_REQUIRE(strstr(list.candidates[0].path, "/Library/Application Support/ascii-chat/") != NULL);
  ASCIICHAT_REQUIRE_EQ(list.candidates[0].mode, DIR_PERM_PRIVATE);
  ASCIICHAT_REQUIRE_STR_EQ(list.candidates[1].path, "/Library/Application Support/ascii-chat/");
  ASCIICHAT_REQUIRE_EQ(list.candidates[1].mode, 0755);
  ASCIICHAT_REQUIRE_STR_EQ(list.candidates[2].path, ASCIICHAT_INSTALL_PREFIX "/var/ascii-chat/");
  ASCIICHAT_REQUIRE_EQ(list.candidates[2].mode, 0755);
  if (list.count == 4) {
    ASCIICHAT_REQUIRE_STR_EQ(list.candidates[3].path, "/usr/local/var/ascii-chat/");
    ASCIICHAT_REQUIRE_EQ(list.candidates[3].mode, 0755);
  }
#else
  ASCIICHAT_REQUIRE_EQ(list.count, 1);
  ASCIICHAT_REQUIRE_STR_EQ(list.candidates[0].path, ASCIICHAT_INSTALL_PREFIX "/var/ascii-chat/");
  ASCIICHAT_REQUIRE_EQ(list.candidates[0].mode, 0755);
#endif

  platform_directory_list_destroy(&list);
  ASCIICHAT_REQUIRE_NULL(list.candidates);
  ASCIICHAT_REQUIRE_EQ(list.count, 0);
}
