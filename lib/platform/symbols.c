#include <ascii-chat/platform/symbols.h>

#include <ctype.h>
#include <string.h>

#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/util/path.h>

static bool ascii_case_equal(const char *left, const char *right) {
  while (*left && *right) {
    if (tolower((unsigned char)*left) != tolower((unsigned char)*right))
      return false;
    ++left;
    ++right;
  }
  return *left == '\0' && *right == '\0';
}

bool symbol_module_is_project(const char *module_name) {
  if (!module_name || !*module_name)
    return false;
  const char *basename = module_name;
  for (const char *cursor = module_name; *cursor; ++cursor)
    if (*cursor == '/' || *cursor == '\\')
      basename = cursor + 1;
  static const char *const project_modules[] = {"ascii-chat", "ascii-chat.exe", "asciichat.dll", "libasciichat.a",
                                                 "libasciichat.so", "libasciichat.dll", "libasciichat.dylib"};
  for (size_t i = 0; i < sizeof(project_modules) / sizeof(project_modules[0]); ++i)
    if (ascii_case_equal(basename, project_modules[i]))
      return true;
  return strncmp(basename, "libasciichat.", strlen("libasciichat.")) == 0 &&
         (strstr(basename, ".so") || strstr(basename, ".dylib"));
}

static bool source_path_is_absolute(const char *path) {
  return path && ((isalpha((unsigned char)path[0]) && path[1] == ':' && (path[2] == '/' || path[2] == '\\')) || path[0] == '/' || path[0] == '\\');
}

asciichat_error_t symbol_format_source_path(const char *module_name, const char *source_path, char *output, size_t output_size, bool *external,
                                            const char *const *owned_dirs) {
  if (!output || !output_size || !external)
    return ERROR_INVALID_PARAM;
  const char *path = source_path ? source_path : "unknown";
  const char *relative = extract_project_relative_path(path, owned_dirs);
  *external = relative == path && (!symbol_module_is_project(module_name) || source_path_is_absolute(path));
  const char *display = *external ? path_last_components(path, 5) : relative;
  bool truncated = *external && display != path;
  size_t prefix = truncated ? 4 : 0;
  size_t required = prefix + strlen(display) + 1;
  if (required > output_size)
    return ERROR_BUFFER_OVERFLOW;
  if (truncated) {
    memcpy(output, terminal_supports_utf8() ? "\xE2\x80\xA6" : "...", 3);
    output[3] = display[-1];
    memcpy(output + prefix, display, strlen(display) + 1);
  } else {
    memcpy(output, display, required);
  }
  return ASCIICHAT_OK;
}
