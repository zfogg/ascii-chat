/**
 * @file platform/system.c
 * @ingroup platform
 * @brief 🔧 Shared cross-platform system utilities (safe string functions, etc.)
 */

#include <ascii-chat/atomic.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <ascii-chat/common.h>
#include <ascii-chat/uthash.h> // UBSan-safe uthash wrapper
#include <ascii-chat/log/log.h>
#include <ascii-chat/log/format.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/lifecycle.h>
#include <ascii-chat/platform/filesystem.h>
#include <time.h>

// Forward declaration for filesystem functions
extern bool check_binary_in_path_uncached(const char *bin_name);

// Platform-specific binary suffix
#ifdef _WIN32
#define BIN_SUFFIX ".exe"
#else
#define BIN_SUFFIX ""
#endif
// PATH_DELIM and PATH_ENV_SEPARATOR are now defined in system.h

// ============================================================================
// Maximum Path Length
// ============================================================================

/**
 * Maximum path length supported by the operating system
 *
 * Platform-specific values:
 * - Windows: 32767 characters (extended-length path with \\?\ prefix)
 * - Linux: 4096 bytes (PATH_MAX from limits.h)
 * - macOS: 1024 bytes (PATH_MAX from sys/syslimits.h)
 *
 * Note: Windows legacy MAX_PATH (260) is too restrictive for modern use.
 * We use the extended-length limit instead.
 */
#ifdef _WIN32
// Windows extended-length path maximum
// Reference: https://docs.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation
#define PLATFORM_MAX_PATH_LENGTH 32767
#elif defined(__linux__)
// Linux PATH_MAX (typically 4096)
#ifndef PATH_MAX
#define PLATFORM_MAX_PATH_LENGTH 4096
#else
#define PLATFORM_MAX_PATH_LENGTH PATH_MAX
#endif
#elif defined(__APPLE__)
// macOS PATH_MAX (typically 1024)
#ifndef PATH_MAX
#define PLATFORM_MAX_PATH_LENGTH 1024
#else
#define PLATFORM_MAX_PATH_LENGTH PATH_MAX
#endif
#else
// Fallback for unknown platforms
#define PLATFORM_MAX_PATH_LENGTH 4096
#endif

// ============================================================================
// Binary PATH Detection Cache
// ============================================================================

/**
 * @brief Binary PATH cache entry structure for binary detection caching
 *
 * Represents a single cached binary PATH detection result. Maps a binary name
 * to a boolean indicating whether the binary was found in PATH, avoiding
 * expensive PATH searches for frequently checked binaries.
 *
 * CORE FIELDS:
 * ============
 * - bin_name: Binary name (e.g., "llvm-symbolizer", "addr2line")
 * - in_path: Whether binary was found in PATH during last check
 *
 * USAGE:
 * ======
 * This structure is used internally by the binary PATH detection cache:
 * - Key: Binary name (hashed for hashtable lookup)
 * - Value: Boolean indicating PATH presence
 *
 * CACHE OPERATIONS:
 * ================
 * - Lookup: Fast O(1) hashtable lookup by binary name
 * - Insertion: Cached after first PATH search
 * - Lifetime: Owned by cache, freed on cache cleanup
 *
 * PERFORMANCE BENEFITS:
 * =====================
 * PATH searching without caching requires:
 * - Tokenizing PATH environment variable (expensive)
 * - Stat/access system calls for each PATH directory (slow)
 * - File system operations for each directory check (disk-bound)
 *
 * With caching:
 * - First check: Same cost as uncached (cache miss)
 * - Subsequent checks: O(1) hashtable lookup (fast)
 * - Eliminates redundant PATH searches
 *
 * MEMORY MANAGEMENT:
 * ==================
 * - bin_name string is allocated and owned by the cache
 * - Do not free bin_name manually (cache manages it)
 * - Entries are stored in hashtable (pre-allocated pool)
 *
 * @note This structure is used internally by the binary PATH detection system.
 *       Users should interact via system_find_binary_in_path() function.
 *
 * @ingroup platform
 */
typedef struct {
  /** @brief Binary name string (allocated, owned by cache) - also used as uthash key */
  char *bin_name;
  /** @brief Whether binary was found in PATH (true = found, false = not found) */
  bool in_path;
  /** @brief uthash handle */
  UT_hash_handle hh;
} bin_cache_entry_t;

static bin_cache_entry_t *g_bin_path_cache = NULL; // uthash head pointer
static rwlock_t g_cache_rwlock;
static lifecycle_t g_cache_lc = LIFECYCLE_INIT;

/**
 * @brief Check if a file exists and is executable
 */
static bool is_executable_file(const char *path) {
#ifdef _WIN32
  // On Windows, check if file exists and is readable
  // Windows doesn't have execute permission bits like Unix
  DWORD attrs = GetFileAttributesA(path);
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return false;
  }
  // Must be a regular file (not a directory)
  if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
    return false;
  }
  // Try to open for reading to verify access
  HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    return false;
  }
  CloseHandle(h);
  return true;
#else
  // On Unix, use access() to check if file exists and is executable
  return (access(path, X_OK) == 0);
#endif
}

/**
 * @brief Convert Unix-style path to Windows path (e.g., /c/foo -> C:\foo)
 * @param unix_path Input path (potentially Unix-style)
 * @param win_path Output buffer for Windows path
 * @param win_path_size Size of output buffer
 * @return true if conversion happened, false if path was already Windows-style
 */
#ifdef _WIN32
static bool convert_unix_path_to_windows(const char *unix_path, char *win_path, size_t win_path_size) {
  if (!unix_path || !win_path || win_path_size < 4) {
    return false;
  }

  // Check for Unix-style path like /c/foo or /d/bar (Git Bash format)
  if (unix_path[0] == '/' && unix_path[1] != '\0' && unix_path[2] == '/') {
    // Convert /x/... to X:\...
    char drive_letter = (char)toupper((unsigned char)unix_path[1]);
    safe_snprintf(win_path, win_path_size, "%c:%s", drive_letter, unix_path + 2);
    // Convert remaining forward slashes to backslashes
    for (char *p = win_path; *p; p++) {
      if (*p == '/')
        *p = '\\';
    }
    return true;
  }

  // Already Windows-style or relative path
  SAFE_STRNCPY(win_path, unix_path, win_path_size);
  return false;
}
#endif

/**
 * @brief Initialize the binary PATH cache
 */
static void init_cache_once(void) {
  if (!lifecycle_init(&g_cache_lc, "cache")) {
    return; // Already initialized
  }

  // Initialize uthash head pointer (NULL = empty)
  g_bin_path_cache = NULL;

  // Initialize rwlock for thread-safe access
  if (rwlock_init(&g_cache_rwlock, "binary_cache") != 0) {
    log_error("Failed to initialize binary PATH cache rwlock");
    lifecycle_init_abort(&g_cache_lc);
  }
}

/**
 * @brief Free a cache entry (for cleanup iteration)
 */
static void free_cache_entry(bin_cache_entry_t *entry) {
  if (entry) {
    if (entry->bin_name) {
      SAFE_FREE(entry->bin_name);
    }
    SAFE_FREE(entry);
  }
}

/**
 * @brief Cleanup the binary PATH cache
 */
void platform_cleanup_binary_path_cache(void) {
  if (!lifecycle_shutdown(&g_cache_lc)) {
    return;
  }

  if (g_bin_path_cache) {
    rwlock_wrlock(&g_cache_rwlock);

    // Free all cached entries using uthash iteration
    bin_cache_entry_t *entry, *tmp;
    HASH_ITER(hh, g_bin_path_cache, entry, tmp) {
      HASH_DELETE(hh, g_bin_path_cache, entry);
      free_cache_entry(entry);
    }

    rwlock_wrunlock(&g_cache_rwlock);
    rwlock_destroy(&g_cache_rwlock);
    g_bin_path_cache = NULL;
  }
}

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Safe formatted string printing to buffer
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @param format Printf-style format string
 * @return Number of characters written, or -1 on error
 *
 * Safely formats a string into a buffer with bounds checking.
 * Uses platform_snprintf for cross-platform implementation.
 * Returns the number of characters written (not including null terminator).
 * Returns -1 if buffer is too small.
 */
int safe_snprintf(char *buffer, size_t buffer_size, const char *format, ...) {
  if (!buffer || !format || buffer_size == 0) {
    return -1;
  }

  va_list args;
  va_start(args, format);

  /* Delegate to platform_snprintf for actual formatting */
  int ret = platform_vsnprintf(buffer, buffer_size, format, args);

  va_end(args);
  return ret;
}

/**
 * @brief Safe formatted output to file stream
 * @param stream Output file stream
 * @param format Printf-style format string
 * @return Number of characters written, or -1 on error
 *
 * Safely formats and prints output to a file stream.
 * Returns the number of characters written, or -1 on error.
 */
int safe_fprintf(FILE *stream, const char *format, ...) {
  if (!stream || !format) {
    return -1;
  }

  va_list args;
  va_start(args, format);
  int ret = vfprintf(stream, format, args);
  va_end(args);

  return ret;
}

/**
 * @brief Safe formatted string printing with va_list
 * @param buffer Output buffer (can be NULL if buffer_size is 0 for size calculation)
 * @param buffer_size Size of output buffer
 * @param format Printf-style format string
 * @param ap Variable argument list
 * @return Number of characters written, or -1 on error
 *
 * Safely formats a string into a buffer with bounds checking using va_list.
 * Uses platform_vsnprintf for cross-platform implementation.
 * Returns the number of characters written (not including null terminator).
 * Special case: NULL buffer with size 0 returns required buffer size for size calculation.
 * Returns -1 if buffer is too small, format is invalid, or buffer is NULL with size > 0.
 */
int safe_vsnprintf(char *buffer, size_t buffer_size, const char *format, va_list ap) {
  if (!format) {
    return -1;
  }

  // Allow NULL buffer with size 0 for size calculation (standard vsnprintf behavior)
  if (!buffer && buffer_size > 0) {
    return -1; // Non-NULL buffer required when buffer_size > 0
  }

  /* Delegate to platform_vsnprintf for actual formatting */
  return platform_vsnprintf(buffer, buffer_size, format, ap);
}
