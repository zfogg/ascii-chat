/**
 * @file windows/filesystem.c
 * @brief Windows implementation of filesystem utilities
 */

#ifdef _WIN32

#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
#include <windows.h>
#include <shlobj.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
#include <share.h>
#include <sys/stat.h>
#include <direct.h>
#include <errno.h>

// ============================================================================
// Basic File Checks
// ============================================================================

bool file_is_readable(const char *path) {
  if (!path) {
    return false;
  }

  DWORD attrs = GetFileAttributesA(path);
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return false;
  }

  HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, NULL);

  if (hFile == INVALID_HANDLE_VALUE) {
    return false;
  }

  CloseHandle(hFile);
  return true;
}

bool file_is_writable(const char *path) {
  if (!path) {
    return false;
  }

  DWORD attrs = GetFileAttributesA(path);
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return true; // Optimistically assume writable if doesn't exist yet
  }

  if (attrs & FILE_ATTRIBUTE_READONLY) {
    return false;
  }

  HANDLE hFile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, NULL);

  if (hFile == INVALID_HANDLE_VALUE) {
    return false;
  }

  CloseHandle(hFile);
  return true;
}

// ============================================================================
// Directory Management
// ============================================================================

asciichat_error_t platform_mkdir(const char *path, int mode) {
  (void)mode; // Windows doesn't use Unix-style permissions

  if (!path) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid path to platform_mkdir");
  }

  if (CreateDirectoryA(path, NULL)) {
    return ASCIICHAT_OK;
  }

  DWORD error = GetLastError();
  if (error == ERROR_ALREADY_EXISTS) {
    // Verify it's actually a directory
    DWORD attrs = GetFileAttributesA(path);
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
      return ASCIICHAT_OK;
    }
    return SET_ERRNO(ERROR_FILE_OPERATION, "Path exists but is not a directory: %s", path);
  }

  return SET_ERRNO(ERROR_FILE_OPERATION, "Failed to create directory: %s (error %lu)", path, error);
}

asciichat_error_t platform_mkdir_recursive(const char *path, int mode) {
  (void)mode; // Windows ignores POSIX permission modes
  if (!path || path[0] == '\0') {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid path to platform_mkdir_recursive");
  }

  char tmp[512];
  size_t len = strlen(path);
  if (len >= sizeof(tmp)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Path too long for platform_mkdir_recursive: %zu", len);
  }

  SAFE_STRNCPY(tmp, path, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';

  // Create each directory in the path
  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/' || *p == '\\') {
      char orig = *p;
      *p = '\0';

      // Skip empty components and drive letters (e.g., "C:")
      if (tmp[0] != '\0' && strcmp(tmp, ".") != 0 && !(strlen(tmp) == 2 && tmp[1] == ':')) {
        DWORD attrs = GetFileAttributesA(tmp);
        if (attrs == INVALID_FILE_ATTRIBUTES) {
          if (!CreateDirectoryA(tmp, NULL)) {
            DWORD error = GetLastError();
            if (error != ERROR_ALREADY_EXISTS) {
              return SET_ERRNO(ERROR_FILE_OPERATION, "Failed to create directory: %s (error %lu)", tmp, error);
            }
          }
        }
      }

      *p = orig;
    }
  }

  // Create the final directory
  DWORD attrs = GetFileAttributesA(tmp);
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    if (!CreateDirectoryA(tmp, NULL)) {
      DWORD error = GetLastError();
      if (error != ERROR_ALREADY_EXISTS) {
        return SET_ERRNO(ERROR_FILE_OPERATION, "Failed to create directory: %s (error %lu)", tmp, error);
      }
    }
  }

  return ASCIICHAT_OK;
}

// ============================================================================
// File Statistics
// ============================================================================

asciichat_error_t platform_stat(const char *path, platform_stat_t *stat_out) {
  if (!path || !stat_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters to platform_stat");
  }

  WIN32_FILE_ATTRIBUTE_DATA fad;
  if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) {
    log_dev("Failed to stat file: %s", path);
    return ERROR_FILE_NOT_FOUND;
  }

  LARGE_INTEGER size;
  size.HighPart = fad.nFileSizeHigh;
  size.LowPart = fad.nFileSizeLow;
  stat_out->size = (size_t)size.QuadPart;
  stat_out->mode = 0;
  stat_out->is_regular_file = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 0 : 1;
  stat_out->is_directory = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
  stat_out->is_symlink = (fad.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? 1 : 0;

  return ASCIICHAT_OK;
}

int platform_is_regular_file(const char *path) {
  if (!path) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: path=%p", path);
    return 0;
  }

  platform_stat_t stat_info;
  if (platform_stat(path, &stat_info) != ASCIICHAT_OK) {
    return 0;
  }

  return stat_info.is_regular_file;
}

int platform_is_directory(const char *path) {
  if (!path) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: path=%p", path);
    return 0;
  }

  platform_stat_t stat_info;
  if (platform_stat(path, &stat_info) != ASCIICHAT_OK) {
    return 0;
  }

  return stat_info.is_directory;
}

// ============================================================================
// Temporary Files and Directories
// ============================================================================

int platform_create_temp_file(char *path_out, size_t path_size, const char *prefix, int *fd) {
  char temp_dir[MAX_PATH];
  if (!GetTempPathA(MAX_PATH, temp_dir)) {
    return -1;
  }

  char temp_file[MAX_PATH];
  if (!GetTempFileNameA(temp_dir, prefix, 0, temp_file)) {
    return -1;
  }

  if (strlen(temp_file) >= path_size) {
    DeleteFileA(temp_file);
    return -1;
  }

  SAFE_STRNCPY(path_out, temp_file, path_size);

  // Open the file and get the file descriptor
  int temp_fd = -1;
  errno_t err = _sopen_s(&temp_fd, temp_file, _O_RDWR | _O_BINARY, _SH_DENYNO, _S_IREAD | _S_IWRITE);
  if (err != 0 || temp_fd < 0) {
    DeleteFileA(temp_file);
    return -1;
  }

  *fd = temp_fd;
  return 0;
}

int platform_delete_temp_file(const char *path) {
  return DeleteFileA(path) ? 0 : -1;
}

asciichat_error_t platform_mkdtemp(char *path_out, size_t path_size, const char *prefix) {
  if (!path_out || path_size < 32 || !prefix) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for platform_mkdtemp");
  }

  char temp_dir[MAX_PATH];
  if (!GetTempPathA(MAX_PATH, temp_dir)) {
    return SET_ERRNO(ERROR_FILE_OPERATION, "Failed to get temp directory");
  }

  // Generate a unique directory name
  for (int attempt = 0; attempt < 100; attempt++) {
    char unique[32];
    snprintf(unique, sizeof(unique), "%s%lu%d", prefix, GetTickCount(), attempt);

    int needed = snprintf(path_out, path_size, "%s%s", temp_dir, unique);
    if (needed < 0 || (size_t)needed >= path_size) {
      return SET_ERRNO(ERROR_INVALID_PARAM, "Path buffer too small for temporary directory");
    }

    if (CreateDirectoryA(path_out, NULL)) {
      return ASCIICHAT_OK;
    }

    if (GetLastError() != ERROR_ALREADY_EXISTS) {
      break;
    }
  }

  return SET_ERRNO(ERROR_FILE_OPERATION, "Failed to create temporary directory");
}

asciichat_error_t platform_rmdir_recursive(const char *path) {
  if (!path) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "path is NULL");
  }

  // Check if path exists
  DWORD attrs = GetFileAttributesA(path);
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return ASCIICHAT_OK; // Path doesn't exist - treat as success
  }

  if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
    // It's a file, just delete it
    if (!DeleteFileA(path)) {
      return SET_ERRNO(ERROR_FILE_OPERATION, "Failed to delete file: %s", path);
    }
    return ASCIICHAT_OK;
  }

  // Build search pattern
  char search_path[MAX_PATH];
  snprintf(search_path, sizeof(search_path), "%s\\*", path);

  WIN32_FIND_DATAA ffd;
  HANDLE hFind = FindFirstFileA(search_path, &ffd);
  if (hFind == INVALID_HANDLE_VALUE) {
    // Empty or inaccessible directory
    if (!RemoveDirectoryA(path)) {
      return SET_ERRNO(ERROR_FILE_OPERATION, "Failed to delete directory: %s", path);
    }
    return ASCIICHAT_OK;
  }

  asciichat_error_t result = ASCIICHAT_OK;

  do {
    // Skip . and ..
    if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0) {
      continue;
    }

    char full_path[MAX_PATH];
    snprintf(full_path, sizeof(full_path), "%s\\%s", path, ffd.cFileName);

    if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      // Recursively delete subdirectory
      asciichat_error_t subdir_result = platform_rmdir_recursive(full_path);
      if (subdir_result != ASCIICHAT_OK) {
        result = subdir_result;
      }
    } else {
      // Delete file
      if (!DeleteFileA(full_path)) {
        log_warn("Failed to delete file during cleanup: %s", full_path);
        result = ERROR_FILE_OPERATION;
      }
    }
  } while (FindNextFileA(hFind, &ffd));

  FindClose(hFind);

  // Delete the directory itself
  if (!RemoveDirectoryA(path)) {
    return SET_ERRNO(ERROR_FILE_OPERATION, "Failed to delete directory: %s", path);
  }

  return result;
}

// ============================================================================
// Key File Security
// ============================================================================

asciichat_error_t platform_validate_key_file_permissions(const char *key_path) {
  if (!key_path) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: key_path=%p", key_path);
  }

  // On Windows, file permission checking is more complex (ACLs)
  // For now, just verify the file exists and is readable
  DWORD attrs = GetFileAttributesA(key_path);
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Cannot access key file: %s", key_path);
  }

  // TODO: Could check ACLs for more thorough security validation
  return ASCIICHAT_OK;
}

// ============================================================================
// Config File Search
// ============================================================================

asciichat_error_t platform_find_config_file(const char *filename, config_file_list_t *list_out) {
  if (!filename || !list_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters to platform_find_config_file");
  }

  // Initialize output list
  list_out->files = NULL;
  list_out->count = 0;
  list_out->capacity = 0;

  // Pre-allocate capacity for possible results
  const size_t total_dirs = 4;
  list_out->capacity = total_dirs;
  list_out->files = SAFE_MALLOC(sizeof(config_file_result_t) * total_dirs, config_file_result_t *);
  if (!list_out->files) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate config file list");
  }

  uint8_t priority = 0;
  char full_path[MAX_PATH];

  // Priority 0: %APPDATA%\ascii-chat (user config)
  char appdata[MAX_PATH];
  if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata))) {
    int ret = snprintf(full_path, sizeof(full_path), "%s\\ascii-chat\\%s", appdata, filename);
    if (ret >= 0 && (size_t)ret < sizeof(full_path)) {
      if (platform_is_regular_file(full_path)) {
        config_file_result_t *result = &list_out->files[list_out->count];
        result->path = platform_strdup(full_path);
        if (result->path) {
          result->priority = priority++;
          result->exists = true;
          result->is_system_config = false;
          list_out->count++;
        }
      }
    }
  }

  // Priority 1: %LOCALAPPDATA%\ascii-chat
  char localappdata[MAX_PATH];
  if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localappdata))) {
    int ret = snprintf(full_path, sizeof(full_path), "%s\\ascii-chat\\%s", localappdata, filename);
    if (ret >= 0 && (size_t)ret < sizeof(full_path)) {
      if (platform_is_regular_file(full_path)) {
        config_file_result_t *result = &list_out->files[list_out->count];
        result->path = platform_strdup(full_path);
        if (result->path) {
          result->priority = priority++;
          result->exists = true;
          result->is_system_config = false;
          list_out->count++;
        }
      }
    }
  }

  // Priority 2: %PROGRAMDATA%\ascii-chat (system config)
  char programdata[MAX_PATH];
  if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_COMMON_APPDATA, NULL, 0, programdata))) {
    int ret = snprintf(full_path, sizeof(full_path), "%s\\ascii-chat\\%s", programdata, filename);
    if (ret >= 0 && (size_t)ret < sizeof(full_path)) {
      if (platform_is_regular_file(full_path)) {
        config_file_result_t *result = &list_out->files[list_out->count];
        result->path = platform_strdup(full_path);
        if (result->path) {
          result->priority = priority++;
          result->exists = true;
          result->is_system_config = true;
          list_out->count++;
        }
      }
    }
  }

  return ASCIICHAT_OK;
}

void config_file_list_destroy(config_file_list_t *list) {
  if (!list) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: list=%p", list);
    return;
  }

  if (list->files) {
    for (size_t i = 0; i < list->count; i++) {
      SAFE_FREE(list->files[i].path);
    }
    SAFE_FREE(list->files);
  }

  list->count = 0;
  list->capacity = 0;
}

// ============================================================================
// Home and Config Directory Discovery
// ============================================================================

const char *platform_get_home_dir(void) {
  // Try USERPROFILE first
  const char *userprofile = platform_getenv("USERPROFILE");
  if (userprofile && userprofile[0] != '\0') {
    return userprofile;
  }

  // Fallback to HOMEDRIVE + HOMEPATH
  static char home_buffer[MAX_PATH];
  const char *homedrive = platform_getenv("HOMEDRIVE");
  const char *homepath = platform_getenv("HOMEPATH");
  if (homedrive && homepath) {
    snprintf(home_buffer, sizeof(home_buffer), "%s%s", homedrive, homepath);
    return home_buffer;
  }

  return NULL;
}

char *platform_get_config_dir(void) {
  char appdata[MAX_PATH];
  if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata))) {
    size_t len = strlen(appdata) + strlen("\\ascii-chat\\") + 1;
    char *dir = SAFE_MALLOC(len, char *);
    if (!dir) {
      return NULL;
    }
    snprintf(dir, len, "%s\\ascii-chat\\", appdata);
    return dir;
  }

  return NULL;
}

char *platform_get_data_dir(void) {
  char localappdata[MAX_PATH];
  if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localappdata))) {
    size_t len = strlen(localappdata) + strlen("\\ascii-chat\\") + 1;
    char *dir = SAFE_MALLOC(len, char *);
    if (!dir) {
      return NULL;
    }
    snprintf(dir, len, "%s\\ascii-chat\\", localappdata);
    return dir;
  }

  return NULL;
}

// ============================================================================
// Platform Path Utilities
// ============================================================================

asciichat_error_t platform_temp_file_open(const char *name, const char *path, int *fd_out) {
  if (!name || !path || !fd_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  int temp_fd = -1;
  errno_t err = _sopen_s(&temp_fd, path, _O_RDWR | _O_BINARY, _SH_DENYNO, _S_IREAD | _S_IWRITE);
  if (err != 0 || temp_fd < 0) {
    return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to open temp file: %s (file descriptor: %s)", path, name);
  }

  *fd_out = temp_fd;
  return ASCIICHAT_OK;
}

const char *platform_path_skip_absolute_prefix(const char *path) {
  if (!path) {
    return path;
  }

  // Skip drive letter (e.g., "C:")
  if (path[0] != '\0' && path[1] == ':') {
    return path + 2;
  }

  // Handle UNC paths (\\server\share)
  if (path[0] == '\\' && path[1] == '\\') {
    // Skip \\server\share
    const char *p = path + 2;
    int slashes = 0;
    while (*p && slashes < 2) {
      if (*p == '\\') {
        slashes++;
      }
      p++;
    }
    return p;
  }

  return path;
}

void platform_normalize_path_separators(char *path) {
  if (!path) {
    return;
  }

  // Convert forward slashes to backslashes on Windows
  for (char *p = path; *p; p++) {
    if (*p == '/') {
      *p = '\\';
    }
  }
}

int platform_path_strcasecmp(const char *a, const char *b, size_t n) {
  // Windows: Case-insensitive comparison for paths
  return _strnicmp(a, b, n);
}

asciichat_error_t platform_truncate_file(const char *path, size_t size) {
  if (!path) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "path cannot be NULL");
  }

  HANDLE hFile = CreateFileA(path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE) {
    return SET_ERRNO(ERROR_FILE_OPERATION, "Failed to open file for truncation: %s", path);
  }

  LARGE_INTEGER li;
  li.QuadPart = (LONGLONG)size;

  if (!SetFilePointerEx(hFile, li, NULL, FILE_BEGIN)) {
    CloseHandle(hFile);
    return SET_ERRNO(ERROR_FILE_OPERATION, "Failed to set file pointer: %s", path);
  }

  if (!SetEndOfFile(hFile)) {
    CloseHandle(hFile);
    return SET_ERRNO(ERROR_FILE_OPERATION, "Failed to truncate file: %s", path);
  }

  CloseHandle(hFile);
  return ASCIICHAT_OK;
}

bool platform_path_is_absolute(const char *path) {
  if (!path || path[0] == '\0') {
    return false;
  }

  // Windows: Absolute if starts with drive letter (C:) or UNC path (\\)
  if (path[0] != '\0' && path[1] == ':') {
    return true;
  }

  if (path[0] == '\\' && path[1] == '\\') {
    return true;
  }

  return false;
}

char platform_path_get_separator(void) {
  return '\\';
}

asciichat_error_t platform_path_normalize(const char *input, char *output, size_t output_size) {
  if (!input || !output || output_size == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid arguments");
  }

  size_t input_len = strlen(input);
  if (input_len >= output_size) {
    return SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Output buffer too small");
  }

  // Copy and normalize path
  size_t output_pos = 0;
  bool last_was_sep = false;

  for (size_t i = 0; i < input_len; i++) {
    char c = input[i];

    // Convert forward slashes to backslashes
    if (c == '/') {
      c = '\\';
    }

    if (c == '\\') {
      if (!last_was_sep) {
        output[output_pos++] = c;
        last_was_sep = true;
      }
    } else {
      output[output_pos++] = c;
      last_was_sep = false;
    }
  }

  // Remove trailing separator unless it's root (C:\ or \\)
  if (output_pos > 1 && output[output_pos - 1] == '\\') {
    // Check if it's a root path
    if (!(output_pos == 3 && output[1] == ':') && !(output_pos >= 3 && output[0] == '\\' && output[1] == '\\')) {
      output_pos--;
    }
  }

  output[output_pos] = '\0';
  return ASCIICHAT_OK;
}

// ============================================================================
// Get Executable Path - Windows Implementation
// ============================================================================

bool platform_get_executable_path(char *exe_path, size_t path_size) {
  if (!exe_path || path_size == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: exe_path=%p, path_size=%zu", (void *)exe_path, path_size);
    return false;
  }

  DWORD len = GetModuleFileNameA(NULL, exe_path, (DWORD)path_size);
  if (len == 0) {
    SET_ERRNO_SYS(ERROR_INVALID_STATE, "GetModuleFileNameA failed: error code %lu", GetLastError());
    return false;
  }
  if (len >= path_size) {
    SET_ERRNO(ERROR_BUFFER_OVERFLOW,
              "Executable path exceeds buffer size (path length >= %zu bytes, buffer size = %zu bytes)", (size_t)len,
              path_size);
    return false;
  }
  return true;
}

// ============================================================================
// Binary PATH Cache - Windows Implementation
// ============================================================================

#define BIN_SUFFIX ".exe"

/**
 * @brief Check if a file exists and is executable (Windows)
 */
static bool is_executable_file(const char *path) {
  DWORD attrs = GetFileAttributesA(path);
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return false;
  }
  if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
    return false;
  }
  HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    return false;
  }
  CloseHandle(h);
  return true;
}

/**
 * @brief Convert Unix-style path to Windows path (for Git Bash compatibility)
 *
 * Converts paths like "/c/foo" to "C:\foo" for compatibility with Git Bash on Windows.
 */
static bool convert_unix_path_to_windows(const char *unix_path, char *win_path, size_t win_path_size) {
  if (!unix_path || !win_path || win_path_size < 4) {
    return false;
  }

  if (unix_path[0] == '/' && unix_path[1] != '\0' && unix_path[2] == '/') {
    char drive_letter = (char)toupper((unsigned char)unix_path[1]);
    safe_snprintf(win_path, win_path_size, "%c:%s", drive_letter, unix_path + 2);
    for (char *p = win_path; *p; p++) {
      if (*p == '/')
        *p = '\\';
    }
    return true;
  }

  SAFE_STRNCPY(win_path, unix_path, win_path_size);
  return false;
}

/**
 * @brief Check if binary is in PATH (no caching) - Windows implementation
 */
bool check_binary_in_path_uncached(const char *bin_name) {
  char bin_with_suffix[512];
  char full_path[PLATFORM_MAX_PATH_LENGTH];

  if (strstr(bin_name, ".exe") == NULL) {
    safe_snprintf(bin_with_suffix, sizeof(bin_with_suffix), "%s%s", bin_name, BIN_SUFFIX);
  } else {
    SAFE_STRNCPY(bin_with_suffix, bin_name, sizeof(bin_with_suffix));
  }

  const char *path_env = SAFE_GETENV("PATH");
  if (!path_env) {
    return false;
  }

  size_t path_len = strlen(path_env);
  char *path_copy = SAFE_MALLOC(path_len + 1, char *);
  if (!path_copy) {
    return false;
  }
  SAFE_STRNCPY(path_copy, path_env, path_len + 1);

  bool found = false;
  char *saveptr = NULL;
  const char *separator = (strchr(path_copy, ';') != NULL) ? ";" : ":";

  char *dir = platform_strtok_r(path_copy, separator, &saveptr);

  while (dir != NULL) {
    if (dir[0] == '\0') {
      dir = platform_strtok_r(NULL, separator, &saveptr);
      continue;
    }

    char win_dir[PLATFORM_MAX_PATH_LENGTH];
    convert_unix_path_to_windows(dir, win_dir, sizeof(win_dir));
    safe_snprintf(full_path, sizeof(full_path), "%s\\%s", win_dir, bin_with_suffix);

    if (is_executable_file(full_path)) {
      found = true;
      break;
    }

    dir = platform_strtok_r(NULL, separator, &saveptr);
  }

  SAFE_FREE(path_copy);
  return found;
}

/**
 * @brief Synchronize file descriptor to disk
 * @param fd File descriptor to sync
 * @return 0 on success, -1 on failure
 */
int platform_fsync(int fd) {
  // Windows uses _commit for file sync
  return _commit(fd);
}

#endif // _WIN32
