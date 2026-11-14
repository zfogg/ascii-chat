#include <ascii-chat/util/symbols.h>

#include <ctype.h>
#include <stddef.h>
#include <string.h>

static bool ascii_case_equal(const char *left, const char *right) {
  while (*left && *right) {
    if (tolower((unsigned char)*left) != tolower((unsigned char)*right))
      return false;
    ++left;
    ++right;
  }
  return *left == '\0' && *right == '\0';
}

bool asciichat_symbol_module_is_project(const char *module_name) {
  if (!module_name || !*module_name)
    return false;

  const char *basename = module_name;
  for (const char *cursor = module_name; *cursor; ++cursor) {
    if (*cursor == '/' || *cursor == '\\')
      basename = cursor + 1;
  }

  static const char *const exact_names[] = {
      "ascii-chat", "ascii-chat.exe", "asciichat.dll", "libasciichat.a",
      "libasciichat.so", "libasciichat.dll", "libasciichat.dylib"};
  for (size_t i = 0; i < sizeof(exact_names) / sizeof(exact_names[0]); ++i) {
    if (ascii_case_equal(basename, exact_names[i]))
      return true;
  }

  return strncmp(basename, "libasciichat.", strlen("libasciichat.")) == 0 &&
         (strstr(basename, ".so") != NULL || strstr(basename, ".dylib") != NULL);
}
