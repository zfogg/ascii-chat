/**
 * @file platform/windows/font.c
 * @brief Windows font resolution for render-file output
 */
#ifdef _WIN32

#include <ascii-chat/platform/font.h>
#include <ascii-chat/font.h>
#include <ascii-chat/common.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/log/log.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

// Default monospace fonts to try, in order of preference
static const char *g_default_fonts[] = {"consola.ttf", // Consolas
                                        "cour.ttf",    // Courier New
                                        "lucon.ttf",   // Lucida Console
                                        NULL};

static bool is_absolute_path(const char *path) {
  return path && ((path[0] == '/' || path[0] == '\\') || (strlen(path) > 2 && path[1] == ':'));
}

static bool file_exists(const char *path) {
  DWORD attrs = GetFileAttributesA(path);
  return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool font_display_name_matches(const char *registered_name, const char *requested_name) {
  if (_stricmp(registered_name, requested_name) == 0) {
    return true;
  }

  const char *suffix = strstr(registered_name, " (");
  size_t family_len = suffix ? (size_t)(suffix - registered_name) : strlen(registered_name);
  return strlen(requested_name) == family_len && _strnicmp(registered_name, requested_name, family_len) == 0;
}

static bool resolve_registry_font(HKEY root, const char *requested_name, const char *fonts_dir, char *out,
                                  size_t out_size) {
  HKEY key = NULL;
  if (RegOpenKeyExA(root, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", 0, KEY_QUERY_VALUE, &key) !=
      ERROR_SUCCESS) {
    return false;
  }

  bool found = false;
  for (DWORD index = 0; !found; index++) {
    char value_name[512];
    char value_data[MAX_PATH];
    DWORD value_name_size = (DWORD)sizeof(value_name);
    DWORD value_data_size = (DWORD)sizeof(value_data);
    DWORD value_type = 0;
    LONG result = RegEnumValueA(key, index, value_name, &value_name_size, NULL, &value_type, (BYTE *)value_data,
                                &value_data_size);
    if (result == ERROR_NO_MORE_ITEMS) {
      break;
    }
    if (result != ERROR_SUCCESS || (value_type != REG_SZ && value_type != REG_EXPAND_SZ) ||
        !font_display_name_matches(value_name, requested_name)) {
      continue;
    }

    value_data[sizeof(value_data) - 1] = '\0';
    if (is_absolute_path(value_data)) {
      snprintf(out, out_size, "%s", value_data);
    } else {
      snprintf(out, out_size, "%s%s", fonts_dir, value_data);
    }
    found = file_exists(out);
  }

  RegCloseKey(key);
  return found;
}

asciichat_error_t platform_font_resolve(const char *spec, char *out, size_t out_size, bool *out_is_path,
                                        const uint8_t **out_font_data, size_t *out_font_data_size) {
  if (!out || out_size == 0 || !out_is_path || !out_font_data || !out_font_data_size) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  *out_font_data = NULL;
  *out_font_data_size = 0;
  *out_is_path = true;

  const char *effective_spec = (spec && spec[0] != '\0') ? spec : "default";
  if (_stricmp(effective_spec, "matrix") == 0) {
    snprintf(out, out_size, "matrix");
    *out_is_path = false;
    *out_font_data = g_font_matrix_resurrected;
    *out_font_data_size = g_font_matrix_resurrected_size;
    return ASCIICHAT_OK;
  }
  if (_stricmp(effective_spec, "default") == 0) {
    snprintf(out, out_size, "default");
    *out_is_path = false;
    *out_font_data = g_font_default;
    *out_font_data_size = g_font_default_size;
    return ASCIICHAT_OK;
  }

  char fonts_dir[MAX_PATH];
  UINT len = GetWindowsDirectoryA(fonts_dir, sizeof(fonts_dir));
  if (len == 0 || len >= sizeof(fonts_dir) - 8) {
    return SET_ERRNO(ERROR_PLATFORM_INIT, "Failed to get Windows directory");
  }
  snprintf(fonts_dir + len, sizeof(fonts_dir) - len, "\\Fonts\\");

  // If spec is an absolute path, use it directly
  if (effective_spec[0] != '\0') {
    if (is_absolute_path(effective_spec)) {
      snprintf(out, out_size, "%s", effective_spec);
      return file_exists(out) ? ASCIICHAT_OK : SET_ERRNO(ERROR_NOT_FOUND, "render-font: not found: %s", out);
    }

    // Try spec as a font filename in Windows\Fonts
    char candidate[MAX_PATH];
    snprintf(candidate, sizeof(candidate), "%s%s", fonts_dir, effective_spec);
    if (file_exists(candidate)) {
      snprintf(out, out_size, "%s", candidate);
      return ASCIICHAT_OK;
    }

    // Try with .ttf extension
    snprintf(candidate, sizeof(candidate), "%s%s.ttf", fonts_dir, effective_spec);
    if (file_exists(candidate)) {
      snprintf(out, out_size, "%s", candidate);
      return ASCIICHAT_OK;
    }

    if (resolve_registry_font(HKEY_CURRENT_USER, effective_spec, fonts_dir, out, out_size) ||
        resolve_registry_font(HKEY_LOCAL_MACHINE, effective_spec, fonts_dir, out, out_size)) {
      log_debug("platform_font_resolve: '%s' resolved to %s", effective_spec, out);
      return ASCIICHAT_OK;
    }
  }

  // Fall back to default monospace fonts
  for (int i = 0; g_default_fonts[i] != NULL; i++) {
    char candidate[MAX_PATH];
    snprintf(candidate, sizeof(candidate), "%s%s", fonts_dir, g_default_fonts[i]);
    if (file_exists(candidate)) {
      snprintf(out, out_size, "%s", candidate);
      return ASCIICHAT_OK;
    }
  }

  return SET_ERRNO(ERROR_NOT_SUPPORTED, "No suitable monospace font found in %s", fonts_dir);
}
#endif
