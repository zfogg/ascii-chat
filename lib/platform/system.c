/**
 * @file platform/system.c
 * @ingroup platform
 * @brief 🔧 Shared cross-platform system utilities (included by posix/system.c and windows/system.c)
 */

// NOTE: This file is #included by windows/system.c and posix/system.c
// All necessary headers are already included by the parent files

#include <stdatomic.h>
#include "../common.h"
#include "../util/fnv1a.h"

// UBSan-safe hash wrapper for uthash (fnv1a uses 64-bit arithmetic, no overflow)
// Note: uthash expects HASH_FUNCTION(keyptr, keylen, hashv) where hashv is an output parameter
#undef HASH_FUNCTION
#define HASH_FUNCTION(keyptr, keylen, hashv)                                                                           \
  do {                                                                                                                 \
    if (!(keyptr) || (keylen) == 0) {                                                                                  \
      (hashv) = 1; /* Non-zero constant for safety */                                                                  \
    } else {                                                                                                           \
      (hashv) = fnv1a_hash_bytes((keyptr), (keylen));                                                                  \
    }                                                                                                                  \
  } while (0)

#include "../util/uthash.h"
#include "logging.h"

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
static atomic_bool g_cache_initialized = false;

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
 * @brief Check if binary is in PATH (no caching)
 */
static bool check_binary_in_path_uncached(const char *bin_name) {
  char bin_with_suffix[512];
  char full_path[PLATFORM_MAX_PATH_LENGTH];

  // On Windows, add .exe suffix if not present
#ifdef _WIN32
  if (strstr(bin_name, ".exe") == NULL) {
    safe_snprintf(bin_with_suffix, sizeof(bin_with_suffix), "%s%s", bin_name, BIN_SUFFIX);
  } else {
    SAFE_STRNCPY(bin_with_suffix, bin_name, sizeof(bin_with_suffix));
  }
#else
  SAFE_STRNCPY(bin_with_suffix, bin_name, sizeof(bin_with_suffix));
#endif

  // Get PATH environment variable
  const char *path_env = SAFE_GETENV("PATH");
  if (!path_env) {
    return false;
  }

  // Make a copy we can modify
  size_t path_len = strlen(path_env);
  char *path_copy = SAFE_MALLOC(path_len + 1, char *);
  if (!path_copy) {
    return false;
  }
  SAFE_STRNCPY(path_copy, path_env, path_len + 1);

  // Search each directory in PATH
  bool found = false;
  char *saveptr = NULL;
  char *dir = platform_strtok_r(path_copy, PATH_ENV_SEPARATOR, &saveptr);

  while (dir != NULL) {
    // Skip empty directory entries
    if (dir[0] == '\0') {
      dir = platform_strtok_r(NULL, PATH_ENV_SEPARATOR, &saveptr);
      continue;
    }

    // Build full path to binary
    safe_snprintf(full_path, sizeof(full_path), "%s%c%s", dir, PATH_DELIM, bin_with_suffix);

    // Check if file exists and is executable
    if (is_executable_file(full_path)) {
      found = true;
      break;
    }

    dir = platform_strtok_r(NULL, PATH_ENV_SEPARATOR, &saveptr);
  }

  SAFE_FREE(path_copy);
  return found;
}

/**
 * @brief Initialize the binary PATH cache
 */
static void init_cache_once(void) {
  bool expected = false;
  if (!atomic_compare_exchange_strong(&g_cache_initialized, &expected, true)) {
    return; // Already initialized
  }

  // Initialize uthash head pointer (NULL = empty)
  g_bin_path_cache = NULL;

  // Initialize rwlock for thread-safe access
  if (rwlock_init(&g_cache_rwlock) != 0) {
    log_error("Failed to initialize binary PATH cache rwlock");
    atomic_store(&g_cache_initialized, false);
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
  if (!atomic_load(&g_cache_initialized)) {
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

  atomic_store(&g_cache_initialized, false);
}

// ============================================================================
// Public API
// ============================================================================

bool platform_is_binary_in_path(const char *bin_name) {
  if (!bin_name || bin_name[0] == '\0') {
    return false;
  }

  // Initialize cache if needed
  init_cache_once();
  if (!atomic_load(&g_cache_initialized)) {
    // Cache initialization failed, check directly (this should never happen)
    SET_ERRNO(ERROR_INVALID_STATE, "Binary PATH cache not initialized, checking directly (this should never happen)");
    return check_binary_in_path_uncached(bin_name);
  }

  // Check cache first
  rwlock_rdlock(&g_cache_rwlock);
  bin_cache_entry_t *entry = NULL;
  HASH_FIND_STR(g_bin_path_cache, bin_name, entry);
  rwlock_rdunlock(&g_cache_rwlock);

  const char **colors = log_get_color_array();

  if (entry) {
    // Cache hit
    log_debug("Binary '%s' %sfound%s in PATH (%scached%s)", bin_name, colors[LOGGING_COLOR_INFO],
              colors[LOGGING_COLOR_RESET], colors[LOGGING_COLOR_WARN], colors[LOGGING_COLOR_RESET]);
    return entry->in_path;
  }

  // Cache miss - check PATH and cache result
  bool found = check_binary_in_path_uncached(bin_name);

  // Create new cache entry
  entry = SAFE_MALLOC(sizeof(bin_cache_entry_t), bin_cache_entry_t *);
  if (!entry) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate cache entry");
    return found; // Return result without caching
  }

  entry->bin_name = platform_strdup(bin_name);
  if (!entry->bin_name) {
    SET_ERRNO(ERROR_MEMORY, "Failed to duplicate binary name");
    SAFE_FREE(entry);
    return found;
  }

  entry->in_path = found;

  // Add to cache using uthash
  rwlock_wrlock(&g_cache_rwlock);
  HASH_ADD_KEYPTR(hh, g_bin_path_cache, entry->bin_name, strlen(entry->bin_name), entry);
  rwlock_wrunlock(&g_cache_rwlock);

  log_debug("Binary '%s' %s%s%s in PATH", bin_name, colors[found ? LOGGING_COLOR_INFO : LOGGING_COLOR_ERROR],
            found ? "found" : "NOT found", colors[LOGGING_COLOR_RESET]);
  return found;
}

/**
 * @brief Get the path to the current executable
 * @param exe_path Buffer to store the executable path
 * @param path_size Size of the buffer
 * @return true on success, false on failure
 */
bool platform_get_executable_path(char *exe_path, size_t path_size) {
  if (!exe_path || path_size == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: exe_path=%p, path_size=%zu", (void *)exe_path, path_size);
    return false;
  }

#ifdef _WIN32
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

#elif defined(__linux__)
  ssize_t len = readlink("/proc/self/exe", exe_path, path_size - 1);
  if (len < 0) {
    SET_ERRNO_SYS(ERROR_INVALID_STATE, "readlink(\"/proc/self/exe\") failed: %s", SAFE_STRERROR(errno));
    return false;
  }
  if ((size_t)len >= path_size - 1) {
    SET_ERRNO(ERROR_BUFFER_OVERFLOW,
              "Executable path exceeds buffer size (path length >= %zu bytes, buffer size = %zu bytes)", (size_t)len,
              path_size);
    return false;
  }
  exe_path[len] = '\0';
  return true;

#elif defined(__APPLE__)
  uint32_t bufsize = (uint32_t)path_size;
  int result = _NSGetExecutablePath(exe_path, &bufsize);
  if (result != 0) {
    SET_ERRNO(ERROR_BUFFER_OVERFLOW, "_NSGetExecutablePath failed: path requires %u bytes, buffer size = %zu bytes",
              bufsize, path_size);
    return false;
  }
  return true;

#else
  SET_ERRNO(ERROR_GENERAL, "Unsupported platform - cannot get executable path");
  return false;
#endif
}
