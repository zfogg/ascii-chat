#include <ascii-chat/util/symbols.h>

#include <ctype.h>
#include <string.h>

bool symbol_module_is_project(const char *module_name) {
  if (!module_name || !*module_name)
    return false;

  const char *basename = module_name;
  for (const char *cursor = module_name; *cursor; ++cursor) {
    if (*cursor == '/' || *cursor == '\\')
      basename = cursor + 1;
  }

  static const char *const project_modules[] = {"ascii-chat", "ascii-chat.exe", "asciichat.dll", "libasciichat.a", "libasciichat.so", "libasciichat.dll", "libasciichat.dylib"};
  for (size_t i = 0; i < sizeof(project_modules) / sizeof(project_modules[0]); ++i) {
    const char *left = basename;
    const char *right = project_modules[i];
    while (*left && *right && tolower((unsigned char)*left) == tolower((unsigned char)*right)) {
      ++left;
      ++right;
    }
    if (!*left && !*right)
      return true;
  }

  return strncmp(basename, "libasciichat.", strlen("libasciichat.")) == 0 && (strstr(basename, ".so") || strstr(basename, ".dylib"));
}
