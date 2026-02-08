/**
 * @file windows/filesystem.c
 * @brief Windows implementation of filesystem utilities
 */

#include <ascii-chat/platform/filesystem.h>
#include <windows.h>
#include <stdbool.h>

bool file_is_readable(const char *path) {
  if (!path) {
    return false;
  }

  DWORD attrs = GetFileAttributesA(path);
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return false; // File does not exist or cannot access
  }

  // Try to open for reading
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
    // File might not exist - check parent directory
    return true; // Optimistically assume writable if doesn't exist yet
  }

  // Check if read-only attribute is set
  if (attrs & FILE_ATTRIBUTE_READONLY) {
    return false;
  }

  // Try to open for writing
  HANDLE hFile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, NULL);

  if (hFile == INVALID_HANDLE_VALUE) {
    return false;
  }

  CloseHandle(hFile);
  return true;
}
