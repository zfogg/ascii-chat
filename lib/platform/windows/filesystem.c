/**
 * @file platform/windows/filesystem.c
 * @brief Windows filesystem operations with ACL checking
 * @ingroup platform
 */

#ifdef _WIN32

#include "../filesystem.h"
#include "../util.h"
#include "../../common.h"
#include "../../log/logging.h"
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <io.h>
#include <fcntl.h>

#pragma comment(lib, "advapi32.lib")

// ============================================================================
// Directory Management
// ============================================================================

/**
 * @brief Create a directory (Windows implementation)
 */
asciichat_error_t platform_mkdir(const char *path, int mode) {
  UNUSED(mode); // Windows doesn't use Unix-style permissions

  if (!path) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid path to platform_mkdir");
    return ERROR_INVALID_PARAM;
  }

  if (CreateDirectoryA(path, NULL)) {
    return ASCIICHAT_OK;
  }

  DWORD error = GetLastError();
  if (error == ERROR_ALREADY_EXISTS) {
    // Verify it's a directory
    DWORD attrib = GetFileAttributesA(path);
    if (attrib != INVALID_FILE_ATTRIBUTES && (attrib & FILE_ATTRIBUTE_DIRECTORY)) {
      return ASCIICHAT_OK;
    }
    return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Path exists but is not a directory: %s", path);
  }

  return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to create directory: %s", path);
}

/**
 * @brief Create directories recursively (Windows implementation)
 */
asciichat_error_t platform_mkdir_recursive(const char *path, int mode) {
  UNUSED(mode); // Windows doesn't use Unix-style permissions

  if (!path || path[0] == '\0') {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid path to platform_mkdir_recursive");
    return ERROR_INVALID_PARAM;
  }

  char tmp[512];
  size_t len = strlen(path);
  if (len >= sizeof(tmp)) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Path too long for platform_mkdir_recursive: %zu", len);
    return ERROR_INVALID_PARAM;
  }

  strncpy(tmp, path, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';

  // Create each directory in the path
  // Skip drive letter on Windows (e.g., C:\ or C:/)
  char *start = tmp;
  if (len >= 2 && tmp[1] == ':') {
    start = tmp + 2;
  }

  for (char *p = start + 1; *p; p++) {
    if (*p == '/' || *p == '\\') {
      char orig = *p;
      *p = '\0';

      // Skip empty components
      if (tmp[0] != '\0' && strcmp(tmp, ".") != 0) {
        if (!CreateDirectoryA(tmp, NULL)) {
          DWORD error = GetLastError();
          if (error != ERROR_ALREADY_EXISTS) {
            *p = orig; // Restore before returning error
            return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to create directory: %s", tmp);
          }
        }
      }

      *p = orig;
    }
  }

  // Create the final directory
  if (!CreateDirectoryA(tmp, NULL)) {
    DWORD error = GetLastError();
    if (error != ERROR_ALREADY_EXISTS) {
      return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to create directory: %s", tmp);
    }
  }

  return ASCIICHAT_OK;
}

// ============================================================================
// File Statistics
// ============================================================================

/**
 * @brief Get file statistics (Windows implementation)
 */
asciichat_error_t platform_stat(const char *path, platform_stat_t *stat_out) {
  if (!path || !stat_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters to platform_stat");
    return ERROR_INVALID_PARAM;
  }

  WIN32_FILE_ATTRIBUTE_DATA attr;
  if (!GetFileAttributesExA(path, GetFileExInfoStandard, &attr)) {
    return SET_ERRNO_SYS(ERROR_FILE_NOT_FOUND, "Failed to stat file: %s", path);
  }

  // Combine high and low parts to get full file size
  ULARGE_INTEGER size;
  size.LowPart = attr.nFileSizeLow;
  size.HighPart = attr.nFileSizeHigh;

  stat_out->size = (size_t)size.QuadPart;
  stat_out->mode = 0; // Windows doesn't have Unix-style modes

  // Check file type
  stat_out->is_directory = (attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
  stat_out->is_symlink = (attr.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? 1 : 0;
  // Regular file excludes directories and symlinks (matches POSIX S_ISREG behavior)
  stat_out->is_regular_file = (stat_out->is_directory || stat_out->is_symlink) ? 0 : 1;

  return ASCIICHAT_OK;
}

/**
 * @brief Check if a path is a regular file (Windows implementation)
 */
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

/**
 * @brief Check if a path is a directory (Windows implementation)
 */
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
  /* Windows temp file creation with process ID for concurrent process safety */
  char temp_dir[PLATFORM_MAX_PATH_LENGTH];
  DWORD temp_dir_len = GetTempPathA(sizeof(temp_dir), temp_dir);
  if (temp_dir_len == 0 || temp_dir_len >= sizeof(temp_dir)) {
    return -1;
  }

  /* Create process-specific temp file prefix (e.g., "asc_sig_12345_") */
  char temp_prefix[32];
  safe_snprintf(temp_prefix, sizeof(temp_prefix), "%s_%lu_", prefix, GetCurrentProcessId());

  /* Create temp file with GetTempFileName */
  if (GetTempFileNameA(temp_dir, temp_prefix, 0, path_out) == 0 || (int)strlen(path_out) >= (int)path_size) {
    return -1;
  }

  /* Windows: Return -1 for fd since the file is already created and closed by GetTempFileName */
  *fd = -1;
  return 0;
}

int platform_delete_temp_file(const char *path) {
  if (DeleteFileA(path)) {
    return 0;
  }
  return -1;
}

asciichat_error_t platform_mkdtemp(char *path_out, size_t path_size, const char *prefix) {
  if (!path_out || path_size < 32 || !prefix) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for platform_mkdtemp");
  }

  /* Get Windows temp directory */
  char temp_dir[PLATFORM_MAX_PATH_LENGTH];
  DWORD temp_dir_len = GetTempPathA(sizeof(temp_dir), temp_dir);
  if (temp_dir_len == 0 || temp_dir_len >= sizeof(temp_dir)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Failed to get temp directory path");
  }

  /* Create process-specific temp directory prefix (e.g., "ascii-chat-gpg_12345_") */
  char temp_prefix[32];
  safe_snprintf(temp_prefix, sizeof(temp_prefix), "%s_%lu_", prefix, GetCurrentProcessId());

  /* Create unique temp directory name */
  char unique_name[32];
  static unsigned int counter = 0;
  safe_snprintf(unique_name, sizeof(unique_name), "%s%u", temp_prefix, InterlockedIncrement((LONG *)&counter));

  /* Build full path */
  int needed = safe_snprintf(path_out, path_size, "%s%s", temp_dir, unique_name);
  if (needed < 0 || (size_t)needed >= path_size) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Path buffer too small for temporary directory");
  }

  /* Create the directory */
  if (CreateDirectoryA(path_out, NULL)) {
    return ASCIICHAT_OK;
  }

  return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to create temporary directory");
}

asciichat_error_t platform_rmdir_recursive(const char *path) {
  if (!path) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "path is NULL");
  }

  /* Check if path exists and is a directory */
  WIN32_FILE_ATTRIBUTE_DATA attrs;
  if (!GetFileAttributesExA(path, GetFileExInfoStandard, &attrs)) {
    /* Path doesn't exist - treat as success (no-op) */
    return ASCIICHAT_OK;
  }

  if (!(attrs.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
    /* Not a directory - treat as error */
    return SET_ERRNO(ERROR_INVALID_PARAM, "path is not a directory");
  }

  /* Find all files in the directory */
  WIN32_FIND_DATAA find_data;
  HANDLE find_handle;
  asciichat_error_t result = ASCIICHAT_OK;

  char search_path[PLATFORM_MAX_PATH_LENGTH];
  safe_snprintf(search_path, sizeof(search_path), "%s\\*", path);

  find_handle = FindFirstFileA(search_path, &find_data);
  if (find_handle == INVALID_HANDLE_VALUE) {
    /* Can't read directory - might be empty, try removing it directly */
    if (RemoveDirectoryA(path)) {
      return ASCIICHAT_OK;
    }
    return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to delete directory: %s", path);
  }

  do {
    /* Skip . and .. */
    if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) {
      continue;
    }

    /* Build full path to entry */
    char full_path[PLATFORM_MAX_PATH_LENGTH];
    safe_snprintf(full_path, sizeof(full_path), "%s\\%s", path, find_data.cFileName);

    if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      /* Recursively delete subdirectory */
      asciichat_error_t subdir_result = platform_rmdir_recursive(full_path);
      if (subdir_result != ASCIICHAT_OK) {
        result = subdir_result;
      }
    } else {
      /* Delete file */
      if (!DeleteFileA(full_path)) {
        result = ERROR_FILE_OPERATION;
      }
    }
  } while (FindNextFileA(find_handle, &find_data));

  FindClose(find_handle);

  /* Delete the directory itself */
  if (RemoveDirectoryA(path)) {
    return result;
  }

  return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to delete directory: %s", path);
}

// ============================================================================
// Key File Security
// ============================================================================

asciichat_error_t platform_validate_key_file_permissions(const char *key_path) {
  if (!key_path) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: key_path=%p", key_path);
    return ERROR_INVALID_PARAM;
  }

  // Get the file's security descriptor
  PSECURITY_DESCRIPTOR pSecurityDescriptor = NULL;
  PACL pDacl = NULL;
  BOOL daclPresent = FALSE;
  BOOL daclDefaulted = FALSE;

  DWORD result = GetNamedSecurityInfoA((LPSTR)key_path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                       NULL,   // pOwner
                                       NULL,   // pGroup
                                       &pDacl, // pDacl
                                       NULL,   // pSacl
                                       &pSecurityDescriptor);

  if (result != ERROR_SUCCESS) {
    DWORD err = GetLastError();
    log_error("Failed to get file security info: %lu", err);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Cannot read ACL for key file: %s (error %lu)", key_path, err);
  }

  bool is_valid = true;

  // Check if DACL exists and has appropriate entries
  if (pDacl != NULL) {
    ACL_SIZE_INFORMATION aclInfo;
    if (!GetAclInformation(pDacl, &aclInfo, sizeof(aclInfo), AclSizeInformation)) {
      log_error("Failed to get ACL information");
      LocalFree(pSecurityDescriptor);
      return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to validate key file ACL: %s", key_path);
    }

    // Get current user's SID for comparison
    HANDLE processToken = NULL;
    DWORD tokenUserSize = 0;
    PTOKEN_USER pTokenUser = NULL;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &processToken)) {
      log_error("Failed to open process token");
      LocalFree(pSecurityDescriptor);
      return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to validate key file ownership: %s", key_path);
    }

    // Get the size needed for token user info
    GetTokenInformation(processToken, TokenUser, NULL, 0, &tokenUserSize);

    pTokenUser = (PTOKEN_USER)LocalAlloc(LMEM_FIXED, tokenUserSize);
    if (!pTokenUser) {
      log_error("Failed to allocate token user buffer");
      CloseHandle(processToken);
      LocalFree(pSecurityDescriptor);
      return SET_ERRNO(ERROR_INVALID_STATE, "Memory allocation failed for ACL validation");
    }

    if (!GetTokenInformation(processToken, TokenUser, pTokenUser, tokenUserSize, &tokenUserSize)) {
      log_error("Failed to get token user information");
      LocalFree(pTokenUser);
      CloseHandle(processToken);
      LocalFree(pSecurityDescriptor);
      return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to get current user info for ACL validation: %s", key_path);
    }

    PSID pCurrentUserSid = pTokenUser->User.Sid;

    // Check each ACE (Access Control Entry) in the DACL
    for (DWORD i = 0; i < aclInfo.AceCount; i++) {
      PACE_HEADER pAceHeader = NULL;
      if (!GetAce(pDacl, i, (LPVOID *)&pAceHeader)) {
        log_error("Failed to get ACE at index %lu", i);
        is_valid = false;
        break;
      }

      // Check ACE type
      if (pAceHeader->AceType == ACCESS_ALLOWED_ACE_TYPE) {
        PACCESS_ALLOWED_ACE pAllowAce = (PACCESS_ALLOWED_ACE)pAceHeader;
        PSID pAceSid = (PSID)&pAllowAce->SidStart;

        // Only the owner (current user) should have access
        if (!EqualSid(pCurrentUserSid, pAceSid)) {
          // Non-owner has access - this is too permissive
          log_error("Key file allows access to non-owner users");
          is_valid = false;
          break;
        }

        // Check that it's only read access, not write/delete
        // For SSH keys, we typically want just READ access (0x00120089)
        // But we allow reasonable key access permissions
        DWORD allowedMask = pAllowAce->Mask;

        // Warn if owner has write permission (which is sometimes OK but not ideal)
        if (allowedMask & (FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES | FILE_DELETE_CHILD)) {
          log_warn("Key file allows owner to modify/delete the file (consider restricting to read-only)");
        }
      } else if (pAceHeader->AceType == ACCESS_DENIED_ACE_TYPE) {
        // Deny ACEs are fine (they restrict access)
        continue;
      } else if (pAceHeader->AceType == SYSTEM_AUDIT_ACE_TYPE) {
        // Audit ACEs don't affect permissions
        continue;
      } else {
        // Unknown ACE type - be conservative
        log_warn("Unknown ACE type in key file ACL: %d", pAceHeader->AceType);
      }
    }

    LocalFree(pTokenUser);
    CloseHandle(processToken);
  } else {
    // No DACL on the file - this is unusual
    // On Windows, files typically inherit DACL from parent directory
    // This might indicate the file is accessible to everyone
    log_warn("Key file has no DACL (might be accessible to all users)");
    is_valid = false;
  }

  LocalFree(pSecurityDescriptor);

  if (!is_valid) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Key file has overly permissive ACL - ensure only owner can read: %s", key_path);
  }

  return ASCIICHAT_OK;
}

// ============================================================================
// Config File Search
// ============================================================================

/**
 * @brief Get APPDATA directory path (Windows helper)
 */
static const char *get_appdata_dir(void) {
  static char appdata_path[MAX_PATH] = {0};
  static bool initialized = false;

  if (initialized) {
    return appdata_path;
  }

  const char *appdata = getenv("APPDATA");
  if (appdata) {
    strncpy(appdata_path, appdata, sizeof(appdata_path) - 1);
    appdata_path[sizeof(appdata_path) - 1] = '\0';
  } else {
    // Fallback to %USERPROFILE%\AppData\Roaming
    const char *userprofile = getenv("USERPROFILE");
    if (userprofile) {
      safe_snprintf(appdata_path, sizeof(appdata_path), "%s\\AppData\\Roaming", userprofile);
    } else {
      return NULL;
    }
  }

  initialized = true;
  return appdata_path;
}

/**
 * @brief Get PROGRAMDATA directory path (Windows helper)
 */
static const char *get_programdata_dir(void) {
  static char programdata_path[MAX_PATH] = {0};
  static bool initialized = false;

  if (initialized) {
    return programdata_path;
  }

  const char *programdata = getenv("PROGRAMDATA");
  if (programdata) {
    strncpy(programdata_path, programdata, sizeof(programdata_path) - 1);
    programdata_path[sizeof(programdata_path) - 1] = '\0';
  } else {
    // Fallback to C:\ProgramData
    strncpy(programdata_path, "C:\\ProgramData", sizeof(programdata_path) - 1);
    programdata_path[sizeof(programdata_path) - 1] = '\0';
  }

  initialized = true;
  return programdata_path;
}

/**
 * @brief Find config file across multiple standard locations (Windows implementation)
 */
asciichat_error_t platform_find_config_file(const char *filename, config_file_list_t *list_out) {
  if (!filename || !list_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters to platform_find_config_file");
  }

  // Initialize output list
  list_out->files = NULL;
  list_out->count = 0;
  list_out->capacity = 0;

  // Pre-allocate capacity for all possible results (2 on Windows)
  list_out->capacity = 2;
  list_out->files = SAFE_MALLOC(sizeof(config_file_result_t) * 2, config_file_result_t *);
  if (!list_out->files) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate config file list");
  }

  // Priority 0: User config (%APPDATA%\ascii-chat\) - highest priority
  const char *appdata_dir = get_appdata_dir();
  if (appdata_dir) {
    char full_path[MAX_PATH];
    safe_snprintf(full_path, sizeof(full_path), "%s\\ascii-chat\\%s", appdata_dir, filename);

    if (platform_is_regular_file(full_path)) {
      config_file_result_t *result = &list_out->files[list_out->count];
      result->path = platform_strdup(full_path);
      if (!result->path) {
        return SET_ERRNO(ERROR_MEMORY, "Failed to allocate path string");
      }
      result->priority = 0;
      result->exists = true;
      result->is_system_config = false;
      list_out->count++;
    }
  }

  // Priority 1: System config (%PROGRAMDATA%\ascii-chat\) - lowest priority
  const char *programdata_dir = get_programdata_dir();
  if (programdata_dir) {
    char full_path[MAX_PATH];
    safe_snprintf(full_path, sizeof(full_path), "%s\\ascii-chat\\%s", programdata_dir, filename);

    if (platform_is_regular_file(full_path)) {
      config_file_result_t *result = &list_out->files[list_out->count];
      result->path = platform_strdup(full_path);
      if (!result->path) {
        return SET_ERRNO(ERROR_MEMORY, "Failed to allocate path string");
      }
      result->priority = 1;
      result->exists = true;
      result->is_system_config = true;
      list_out->count++;
    }
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Free config file list resources (Windows implementation)
 */
void config_file_list_free(config_file_list_t *list) {
  if (!list) {
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
// Home and Config Directory Discovery (Windows Implementation)
// ============================================================================

const char *platform_get_home_dir(void) {
  /* Windows: Try USERPROFILE first, fallback to HOME */
  const char *home = platform_getenv("USERPROFILE");
  if (!home) {
    home = platform_getenv("HOME");
  }
  return home;
}

char *platform_get_config_dir(void) {
  /* Windows: Use %APPDATA%/ascii-chat/ */
  const char *appdata = platform_getenv("APPDATA");
  if (appdata && appdata[0] != '\0') {
    size_t len = strlen(appdata) + strlen("\\ascii-chat\\") + 1;
    char *dir = SAFE_MALLOC(len, char *);
    if (!dir) {
      return NULL;
    }
    safe_snprintf(dir, len, "%s\\ascii-chat\\", appdata);
    return dir;
  }

  /* Fallback to %USERPROFILE%/.ascii-chat/ */
  const char *userprofile = platform_getenv("USERPROFILE");
  if (userprofile && userprofile[0] != '\0') {
    size_t len = strlen(userprofile) + strlen("\\.ascii-chat\\") + 1;
    char *dir = SAFE_MALLOC(len, char *);
    if (!dir) {
      return NULL;
    }
    safe_snprintf(dir, len, "%s\\.ascii-chat\\", userprofile);
    return dir;
  }

  return NULL;
}

// ============================================================================
// Platform Path Utilities
// ============================================================================

/**
 * @brief Open temporary file (Windows)
 *
 * On Windows, platform_create_temp_file returns fd=-1, so we need to open the file.
 */
asciichat_error_t platform_temp_file_open(const char *path, int *fd_out) {
  if (!path || !fd_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "path and fd_out cannot be NULL");
  }

  /* Windows: Open the temp file for writing */
  int fd = open(path, O_WRONLY | O_BINARY);
  if (fd < 0) {
    return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to open temporary file: %s", path);
  }

  *fd_out = fd;
  return ASCIICHAT_OK;
}

/**
 * @brief Skip absolute path prefix (Windows)
 *
 * On Windows, skip the drive letter prefix (e.g., "C:" in "C:\path")
 */
const char *platform_path_skip_absolute_prefix(const char *path) {
  if (!path || path[0] == '\0') {
    return path;
  }
  /* Windows: Skip drive letter (e.g., "C:") */
  if (path[1] == ':') {
    return path + 2;
  }
  return path;
}

/**
 * @brief Normalize path separators for the current platform (Windows)
 *
 * On Windows, convert forward slashes to backslashes.
 */
void platform_normalize_path_separators(char *path) {
  /* Windows: Convert forward slashes to backslashes */
  if (!path)
    return;
  for (char *p = path; *p; p++) {
    if (*p == '/') {
      *p = '\\';
    }
  }
}

/**
 * @brief Platform-aware path string comparison (Windows)
 *
 * On Windows, paths are case-insensitive, so use _strnicmp.
 */
int platform_path_strcasecmp(const char *a, const char *b, size_t n) {
  /* Windows: Case-insensitive comparison */
  return _strnicmp(a, b, n);
}

/**
 * @brief Truncate file (Windows)
 */
asciichat_error_t platform_truncate_file(const char *path, size_t size) {
  if (!path) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "path cannot be NULL");
  }

  /* Windows: Use CreateFileA, SetFilePointerEx, SetEndOfFile */
  HANDLE file_handle = CreateFileA(path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (file_handle == INVALID_HANDLE_VALUE) {
    return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to open file for truncation: %s", path);
  }

  /* Set file position to desired size */
  LARGE_INTEGER file_size;
  file_size.QuadPart = (LONGLONG)size;

  if (!SetFilePointerEx(file_handle, file_size, NULL, FILE_BEGIN)) {
    CloseHandle(file_handle);
    return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to seek in file: %s", path);
  }

  /* Truncate at current position */
  if (!SetEndOfFile(file_handle)) {
    CloseHandle(file_handle);
    return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to truncate file: %s", path);
  }

  CloseHandle(file_handle);
  return ASCIICHAT_OK;
}

/**
 * @brief Check if path is absolute (Windows)
 */
bool platform_path_is_absolute(const char *path) {
  if (!path || path[0] == '\0') {
    return false;
  }

  /* Check for drive letter: C: */
  if (path[1] == ':' && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'))) {
    return true;
  }

  /* Check for UNC path: \\server */
  if (path[0] == '\\' && path[1] == '\\') {
    return true;
  }

  return false;
}

/**
 * @brief Get path separator (Windows)
 */
char platform_path_get_separator(void) {
  return '\\';
}

/**
 * @brief Normalize path (Windows)
 */
asciichat_error_t platform_path_normalize(const char *input, char *output, size_t output_size) {
  if (!input || !output || output_size == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid arguments");
  }

  size_t input_len = strlen(input);
  if (input_len >= output_size) {
    return SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Output buffer too small");
  }

  size_t output_pos = 0;
  bool last_was_slash = false;

  /* Convert to Windows style and remove redundant separators */
  for (size_t i = 0; i < input_len; i++) {
    char c = input[i];

    /* Convert forward slash to backslash */
    if (c == '/') {
      c = '\\';
    }

    if (c == '\\') {
      if (!last_was_slash) {
        output[output_pos++] = c;
        last_was_slash = true;
      }
    } else {
      output[output_pos++] = c;
      last_was_slash = false;
    }
  }

  /* Remove trailing backslash unless after drive letter or UNC */
  if (output_pos > 1) {
    if (output[output_pos - 1] == '\\') {
      /* Keep backslash after drive letter (C:\) */
      if (!(output_pos == 3 && output[1] == ':')) {
        output_pos--;
      }
    }
  }

  output[output_pos] = '\0';
  return ASCIICHAT_OK;
}

#endif // _WIN32
