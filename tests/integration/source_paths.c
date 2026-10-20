/** Regressions for source paths in log headers and memory reports. */
#include <ascii-chat/common.h>
#include <ascii-chat/debug/memory.h>
#include <ascii-chat/source_root.h>
#include <ascii-chat/util/path.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expr)                                                                                                    \
  do {                                                                                                                 \
    if (!(expr)) {                                                                                                     \
      log_error("Regression failed at line %d: %s", __LINE__, #expr);                                                  \
      return 1;                                                                                                        \
    }                                                                                                                  \
  } while (0)

int main(int argc, char **argv) {
  CHECK(argc == 2);
  log_init(argv[1], LOG_DEBUG, false, false);

  const char *paths[] = {"src/main.c", "lib/platform/windows/keyboard.c", "include/ascii-chat/util/path.h",
                         "tests/integration/source_paths.c"};
  for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
    char absolute[4096];
    safe_snprintf(absolute, sizeof(absolute), "%s/%s", ASCIICHAT_SOURCE_ROOT, paths[i]);
    CHECK(strcmp(extract_project_relative_path(absolute), paths[i]) == 0);
    CHECK(extract_project_relative_path(paths[i]) == paths[i]);

    // CMake uses forward slashes; __FILE__ on Windows can use backslashes.
    for (char *p = absolute; *p; p++) {
      if (*p == '/')
        *p = '\\';
    }
    CHECK(extract_project_relative_path(absolute) == absolute + strlen(ASCIICHAT_SOURCE_ROOT) + 1);
#ifdef _WIN32
    for (size_t j = 0; j < strlen(ASCIICHAT_SOURCE_ROOT); j++)
      absolute[j] = (char)toupper((unsigned char)absolute[j]);
    CHECK(extract_project_relative_path(absolute) == absolute + strlen(ASCIICHAT_SOURCE_ROOT) + 1);
#endif
  }

#ifdef _WIN32
  CHECK(strcmp(extract_project_relative_path("C:\\src\\client\\main.c"), "src\\client\\main.c") == 0);
  CHECK(strcmp(extract_project_relative_path("C:\\lib\\debug\\backtrace.c"), "lib\\debug\\backtrace.c") == 0);
  CHECK(strcmp(extract_project_relative_path("C:\\tests\\integration\\source_paths.c"),
               "tests\\integration\\source_paths.c") == 0);
  CHECK(strcmp(extract_project_relative_path("C:/include/ascii-chat/util/path.h"), "include/ascii-chat/util/path.h") ==
        0);
  CHECK(strcmp(extract_project_relative_path("C:\\library\\external.c"), "C:\\library\\external.c") == 0);
#endif

  char sibling[4096];
  safe_snprintf(sibling, sizeof(sibling), "%s-other/src/main.c", ASCIICHAT_SOURCE_ROOT);
  CHECK(extract_project_relative_path(sibling) == sibling);
  CHECK(strcmp(extract_project_relative_path(NULL), "unknown") == 0);
  CHECK(strcmp(extract_project_relative_path(""), "") == 0);
  CHECK(strcmp(extract_project_relative_path(ASCIICHAT_SOURCE_ROOT), ASCIICHAT_SOURCE_ROOT) == 0);
  CHECK(strcmp(extract_project_relative_path("Z:\\external\\file.c"), "Z:\\external\\file.c") == 0);

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
  CHECK(src && lib && windows && external);
  debug_memory_set_quiet_mode(false);
  debug_memory_report();
  debug_free(src, __FILE__, __LINE__);
  debug_free(lib, __FILE__, __LINE__);
  debug_free(windows, __FILE__, __LINE__);
  debug_free(external, __FILE__, __LINE__);
#endif
  log_destroy();

  FILE *report = fopen(argv[1], "rb");
  CHECK(report != NULL);
  static char contents[256 * 1024];
  size_t length = fread(contents, 1, sizeof(contents) - 1, report);
  CHECK(!ferror(report));
  fclose(report);
  contents[length] = '\0';
#ifndef NDEBUG
  // Debug log headers include source locations; optimized builds use the
  // compact log format but still exercise the path utility checks above.
  CHECK(strstr(contents, "src/path_fixture.c:123") != NULL);
  CHECK(strstr(contents, "lib/path_fixture.c:456") != NULL);
  CHECK(strstr(contents, "lib\\windows_fixture.c:321") != NULL);
  CHECK(strstr(contents, ASCIICHAT_SOURCE_ROOT "/src/path_fixture.c") == NULL);
  CHECK(strstr(contents, ASCIICHAT_SOURCE_ROOT "/lib/path_fixture.c") == NULL);
#endif
  CHECK(strstr(contents, "<key:") == NULL);
#ifdef DEBUG_MEMORY
  CHECK(strstr(contents, "src/path_fixture.c:123 [") != NULL);
  CHECK(strstr(contents, "lib/path_fixture.c:456 [") != NULL);
  CHECK(strstr(contents, "src\\windows_fixture.c:654 [") != NULL);
  CHECK(strstr(contents, "Z:\\external\\path_fixture.c:789 [") != NULL);
#endif
  return 0;
}
