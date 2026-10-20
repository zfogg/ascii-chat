/**
 * @file tests/unit/platform/filesystem_test.c
 * @brief Tests for platform filesystem directory discovery
 * @ingroup tests
 */

#include <criterion/criterion.h>
#include <string.h>

#include <ascii-chat/common.h>
#include <ascii-chat/paths.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/util/string.h>

TestSuite(platform_filesystem);

Test(platform_filesystem, data_directories_are_platform_specific) {
  platform_directory_list_t list = {0};
  cr_assert_eq(platform_get_data_dir_candidates(&list), ASCIICHAT_OK);

#ifdef _WIN32
  const char *program_data = platform_getenv("PROGRAMDATA");
  if (program_data && program_data[0] != '\0') {
    cr_assert_eq(list.count, 1);
    size_t expected_len = strlen(program_data) + strlen("\\ascii-chat\\") + 1;
    char *expected = SAFE_MALLOC(expected_len, char *);
    safe_snprintf(expected, expected_len, "%s\\ascii-chat\\", program_data);
    cr_assert_str_eq(list.candidates[0].path, expected);
    cr_assert_eq(list.candidates[0].mode, 0755);
    SAFE_FREE(expected);
  } else {
    cr_assert_eq(list.count, 0);
  }
#elif defined(__APPLE__)
  const size_t expected_count = strcmp(ASCIICHAT_INSTALL_PREFIX, "/usr/local") == 0 ? 3 : 4;
  cr_assert_eq(list.count, expected_count);
  cr_assert(strstr(list.candidates[0].path, "/Library/Application Support/ascii-chat/") != NULL);
  cr_assert_eq(list.candidates[0].mode, DIR_PERM_PRIVATE);
  cr_assert_str_eq(list.candidates[1].path, "/Library/Application Support/ascii-chat/");
  cr_assert_eq(list.candidates[1].mode, 0755);
  cr_assert_str_eq(list.candidates[2].path, ASCIICHAT_INSTALL_PREFIX "/var/ascii-chat/");
  cr_assert_eq(list.candidates[2].mode, 0755);
  if (list.count == 4) {
    cr_assert_str_eq(list.candidates[3].path, "/usr/local/var/ascii-chat/");
    cr_assert_eq(list.candidates[3].mode, 0755);
  }
#else
  cr_assert_eq(list.count, 1);
  cr_assert_str_eq(list.candidates[0].path, ASCIICHAT_INSTALL_PREFIX "/var/ascii-chat/");
  cr_assert_eq(list.candidates[0].mode, 0755);
#endif

  platform_directory_list_destroy(&list);
  cr_assert_null(list.candidates);
  cr_assert_eq(list.count, 0);
}
