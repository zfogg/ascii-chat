#include "path.h"
#include <string.h>
#include <stdbool.h>

/* Helper function for slash-agnostic path matching (handles Windows/Unix path separators) */
static bool path_char_match(char a, char b) {
  /* Treat both / and \ as equivalent */
  if ((a == '/' || a == '\\') && (b == '/' || b == '\\'))
    return true;
  return a == b;
}

/* Helper function to find source root in path (slash-agnostic for Windows compatibility) */
static const char *find_source_root(const char *file, const char *source_root) {
  if (!file || !source_root)
    return NULL;

  size_t root_len = strlen(source_root);
  const char *file_pos = file;

  /* Search for source_root in file path, treating / and \ as equivalent */
  while (*file_pos) {
    size_t i;
    for (i = 0; i < root_len && file_pos[i]; i++) {
      if (!path_char_match(file_pos[i], source_root[i]))
        break;
    }

    /* Found a match if we compared all characters of source_root */
    if (i == root_len) {
      /* Verify it's followed by a path separator or end of string */
      char next = file_pos[i];
      if (next == '\0' || next == '/' || next == '\\')
        return file_pos;
    }

    file_pos++;
  }

  return NULL;
}

const char *extract_project_relative_path(const char *file) {
  if (!file)
    return "unknown";

#ifdef PROJECT_SOURCE_ROOT
  /* Use the dynamically defined source root */
  const char *source_root = PROJECT_SOURCE_ROOT;
  const char *root_pos = find_source_root(file, source_root);

  if (root_pos) {
    /* Move past the source root */
    const char *after_root = root_pos + strlen(source_root);

    /* Skip the path separator if present */
    if (*after_root == '/' || *after_root == '\\') {
      after_root++;
    }

    /* Return the path relative to repo root */
    if (*after_root != '\0') {
      return after_root;
    }
  }
#endif

  /* Fallback: try to find just the filename */
  const char *last_slash = strrchr(file, '/');
  const char *last_backslash = strrchr(file, '\\');
  const char *last_sep = (last_slash > last_backslash) ? last_slash : last_backslash;

  if (last_sep) {
    return last_sep + 1;
  }

  /* Last resort: return the original path */
  return file;
}
