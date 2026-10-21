#include <criterion/criterion.h>
#include <ascii-chat/platform/filesystem.h>
/** Regressions for source paths in log headers and memory reports. */
#include <ascii-chat/common.h>
#include <ascii-chat/debug/memory.h>
#include <ascii-chat/platform/symbols.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/source_root.h>
#include <ascii-chat/util/path.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static char report_path[512];
static void setup(void) {
  int fd = -1;
  cr_assert_eq(platform_create_temp_file(report_path, sizeof(report_path), "source-paths", &fd), 0);
  if (fd >= 0)
    platform_close(fd);
  log_init(report_path, LOG_DEBUG, false, false);
}
static void teardown(void) {
  log_destroy();
  cr_assert_eq(platform_delete_temp_file(report_path), 0);
}
TestSuite(source_paths, .init = setup, .fini = teardown);

Test(source_paths, project_relative_paths_and_symbol_display) {

  const char *paths[] = {"src/main.c", "lib/platform/windows/keyboard.c", "include/ascii-chat/util/path.h",
                         "tests/integration/source_paths.c"};
  for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
    char absolute[4096];
    safe_snprintf(absolute, sizeof(absolute), "%s/%s", ASCIICHAT_SOURCE_ROOT, paths[i]);
    if (ASCIICHAT_SOURCE_ROOT[0]) {
      cr_assert_eq(strcmp(extract_project_relative_path(absolute), paths[i]), 0);
    } else {
      cr_assert_eq(extract_project_relative_path(absolute), absolute);
    }
    cr_assert_eq(extract_project_relative_path(paths[i]), paths[i]);

    // CMake uses forward slashes; __FILE__ on Windows can use backslashes.
    for (char *p = absolute; *p; p++) {
      if (*p == '/')
        *p = '\\';
    }
    if (ASCIICHAT_SOURCE_ROOT[0]) {
      cr_assert_eq(extract_project_relative_path(absolute), absolute + strlen(ASCIICHAT_SOURCE_ROOT) + 1);
    }
#ifdef _WIN32
    for (size_t j = 0; j < strlen(ASCIICHAT_SOURCE_ROOT); j++)
      absolute[j] = (char)toupper((unsigned char)absolute[j]);
    if (ASCIICHAT_SOURCE_ROOT[0]) {
      cr_assert_eq(extract_project_relative_path(absolute), absolute + strlen(ASCIICHAT_SOURCE_ROOT) + 1);
    }
#endif
  }

#ifdef _WIN32
  cr_assert_eq(strcmp(extract_project_relative_path("C:\\src\\client\\main.c"), "src\\client\\main.c"), 0);
  cr_assert_eq(strcmp(extract_project_relative_path("C:\\lib\\debug\\backtrace.c"), "lib\\debug\\backtrace.c"), 0);
  cr_assert_eq(strcmp(extract_project_relative_path("C:\\tests\\integration\\source_paths.c"),
                      "tests\\integration\\source_paths.c"),
               0);
  cr_assert_eq(
      strcmp(extract_project_relative_path("C:/include/ascii-chat/util/path.h"), "include/ascii-chat/util/path.h"), 0);
  cr_assert_eq(strcmp(extract_project_relative_path("C:\\library\\external.c"), "C:\\library\\external.c"), 0);
#endif

  char sibling[4096];
  safe_snprintf(sibling, sizeof(sibling), "%s-other/src/main.c", ASCIICHAT_SOURCE_ROOT);
  cr_assert_eq(extract_project_relative_path(sibling), sibling);
  cr_assert_eq(strcmp(extract_project_relative_path(NULL), "unknown"), 0);
  cr_assert_eq(strcmp(extract_project_relative_path(""), ""), 0);
  cr_assert_eq(strcmp(extract_project_relative_path(ASCIICHAT_SOURCE_ROOT), ASCIICHAT_SOURCE_ROOT), 0);
  cr_assert_eq(strcmp(extract_project_relative_path("Z:\\external\\file.c"), "Z:\\external\\file.c"), 0);
  cr_assert_eq(
      strcmp(path_last_components("D:\\a\\_work\\1\\s\\src\\vctools\\crt\\vcstartup\\src\\startup\\exe_common.inl", 5),
             "crt\\vcstartup\\src\\startup\\exe_common.inl"),
      0);
  cr_assert_eq(strcmp(path_last_components("vendor/runtime/file.c", 5), "vendor/runtime/file.c"), 0);

  cr_assert(symbol_module_is_project("ascii-chat.exe"));
  cr_assert(symbol_module_is_project("libasciichat.0.3.0.dylib"));
  cr_assert(symbol_module_is_project("asciichat.dll"));
  cr_assert(!symbol_module_is_project("ucrtbase.dll"));

  char display_path[512];
  bool external_source = false;
  cr_assert_eq(symbol_format_source_path(
                   "ucrtbase.dll", "D:\\a\\_work\\1\\s\\src\\vctools\\crt\\vcstartup\\src\\startup\\exe_common.inl",
                   display_path, sizeof(display_path), &external_source),
               ASCIICHAT_OK);
  cr_assert(external_source);
  const char *truncation_prefix = terminal_supports_utf8() ? "\xE2\x80\xA6\\" : "...\\";
  char expected_external_path[512];
  safe_snprintf(expected_external_path, sizeof(expected_external_path),
                "%scrt\\vcstartup\\src\\startup\\exe_common.inl", truncation_prefix);
  cr_assert_eq(strcmp(display_path, expected_external_path), 0);
  cr_assert_eq(symbol_format_source_path(
                   "ascii-chat.exe", "D:\\a\\_work\\1\\s\\src\\vctools\\crt\\vcstartup\\src\\startup\\exe_common.inl",
                   display_path, sizeof(display_path), &external_source),
               ASCIICHAT_OK);
  cr_assert(external_source);
  cr_assert_eq(strcmp(display_path, expected_external_path), 0);

  cr_assert_eq(symbol_format_source_path("ucrtbase.dll", "vendor/runtime/file.c", display_path, sizeof(display_path),
                                         &external_source),
               ASCIICHAT_OK);
  cr_assert(external_source);
  cr_assert_eq(strcmp(display_path, "vendor/runtime/file.c"), 0);

  char too_small[4];
  cr_assert_eq(symbol_format_source_path("ucrtbase.dll", "vendor/runtime/file.c", too_small, sizeof(too_small),
                                         &external_source),
               ERROR_BUFFER_OVERFLOW);
  CLEAR_ERRNO();
}

Test(source_paths, log_headers_and_memory_reports_preserve_locations) {
  // Exercise the actual log header renderer with absolute src/ and lib/ paths.
  log_msg(LOG_DEBUG, ASCIICHAT_SOURCE_ROOT "/src/path_fixture.c", 123, "source_fixture", "src path fixture");
  log_msg(LOG_DEBUG, ASCIICHAT_SOURCE_ROOT "/lib/path_fixture.c", 456, "source_fixture", "lib path fixture");
  log_msg(LOG_DEBUG, ASCIICHAT_SOURCE_ROOT "\\lib\\windows_fixture.c", 321, "source_fixture", "Windows path fixture");

#ifdef DEBUG_MEMORY
  void *src = debug_malloc(17, ASCIICHAT_SOURCE_ROOT "/src/path_fixture.c", 123);
  void *lib = debug_malloc(19, ASCIICHAT_SOURCE_ROOT "/lib/path_fixture.c", 456);
  void *windows = debug_malloc(29, ASCIICHAT_SOURCE_ROOT "\\src\\windows_fixture.c", 654);
  // Outside the root, the drive colon must survive parsing of :line:tid.
  void *external = debug_malloc(23, "Z:\\external\\path_fixture.c", 789);
  cr_assert(src && lib && windows && external);
  debug_memory_set_quiet_mode(false);
  debug_memory_report();
  debug_free(src, __FILE__, __LINE__);
  debug_free(lib, __FILE__, __LINE__);
  debug_free(windows, __FILE__, __LINE__);
  debug_free(external, __FILE__, __LINE__);
#endif
  log_destroy();

  FILE *report = fopen(report_path, "rb");
  cr_assert_neq(report, NULL);
  static char contents[256 * 1024];
  size_t length = fread(contents, 1, sizeof(contents) - 1, report);
  cr_assert(!ferror(report));
  fclose(report);
  contents[length] = '\0';
#ifndef NDEBUG
  // Debug log headers include source locations; optimized builds use the
  // compact log format but still exercise the path utility checks above.
  cr_assert_neq(strstr(contents, "src/path_fixture.c:123"), NULL);
  cr_assert_neq(strstr(contents, "lib/path_fixture.c:456"), NULL);
  cr_assert_neq(strstr(contents, "lib\\windows_fixture.c:321"), NULL);
  cr_assert_eq(strstr(contents, ASCIICHAT_SOURCE_ROOT "/src/path_fixture.c"), NULL);
  cr_assert_eq(strstr(contents, ASCIICHAT_SOURCE_ROOT "/lib/path_fixture.c"), NULL);
#endif
  cr_assert_eq(strstr(contents, "<key:"), NULL);
#ifdef DEBUG_MEMORY
  cr_assert_neq(strstr(contents, "src/path_fixture.c:123 ["), NULL);
  cr_assert_neq(strstr(contents, "lib/path_fixture.c:456 ["), NULL);
  cr_assert_neq(strstr(contents, "src\\windows_fixture.c:654 ["), NULL);
  cr_assert_neq(strstr(contents, "Z:\\external\\path_fixture.c:789 ["), NULL);
#endif
}
