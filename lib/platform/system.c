/**
 * @file platform/system.c
 * @ingroup platform
 * @brief 🔧 Shared cross-platform system utilities (included by posix/system.c and windows/system.c)
 */

// NOTE: This file is #included by windows/system.c and posix/system.c
// All necessary headers are already included by the parent files

#include <stdatomic.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <ascii-chat/common.h>
#include <ascii-chat/uthash.h> // UBSan-safe uthash wrapper
#include <ascii-chat/log/log.h>
#include <ascii-chat/log/format.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/lifecycle.h>
#include <time.h>

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
  // On Windows, try both ';' (native) and ':' (Git Bash) as separators
  bool found = false;
  char *saveptr = NULL;

#ifdef _WIN32
  // Detect separator: Git Bash uses ':' with Unix-style paths (/c/foo)
  // Native Windows uses ';' with Windows paths (C:\foo)
  const char *separator = (strchr(path_copy, ';') != NULL) ? ";" : ":";
#else
  const char *separator = PATH_ENV_SEPARATOR;
#endif

  char *dir = platform_strtok_r(path_copy, separator, &saveptr);

  while (dir != NULL) {
    // Skip empty directory entries
    if (dir[0] == '\0') {
      dir = platform_strtok_r(NULL, separator, &saveptr);
      continue;
    }

#ifdef _WIN32
    // Convert Unix-style paths from Git Bash to Windows paths
    char win_dir[PLATFORM_MAX_PATH_LENGTH];
    convert_unix_path_to_windows(dir, win_dir, sizeof(win_dir));

    // Build full path to binary (use backslash for Windows)
    safe_snprintf(full_path, sizeof(full_path), "%s\\%s", win_dir, bin_with_suffix);
#else
    // Build full path to binary
    safe_snprintf(full_path, sizeof(full_path), "%s%c%s", dir, PATH_DELIM, bin_with_suffix);
#endif

    // Check if file exists and is executable
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

bool platform_is_binary_in_path(const char *bin_name) {
  if (!bin_name || bin_name[0] == '\0') {
    return false;
  }

  // Initialize cache if needed
  init_cache_once();
  if (!lifecycle_is_initialized(&g_cache_lc)) {
    // Cache initialization failed, check directly (this should never happen)
    SET_ERRNO(ERROR_INVALID_STATE, "Binary PATH cache not initialized, checking directly (this should never happen)");
    return check_binary_in_path_uncached(bin_name);
  }

  // Check cache first
  rwlock_rdlock(&g_cache_rwlock);
  bin_cache_entry_t *entry = NULL;
  HASH_FIND_STR(g_bin_path_cache, bin_name, entry);
  rwlock_rdunlock(&g_cache_rwlock);

  if (entry) {
    // Cache hit
    log_dev("Binary '%s' %s in PATH (%s)", bin_name, colored_string(LOG_COLOR_INFO, "found"),
            colored_string(LOG_COLOR_WARN, "cached"));
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

  log_dev("Binary '%s' %s in PATH", bin_name,
          colored_string(found ? LOG_COLOR_INFO : LOG_COLOR_ERROR, found ? "found" : "NOT found"));

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

/**
 * @brief Print pre-resolved backtrace symbols with colored terminal output and plain log file output
 *
 * This is a cross-platform function that formats backtrace symbols with:
 * - Semantic coloring: grey numbers, blue functions, red unknowns, magenta addresses
 * - Two output streams: colored to stderr (terminal), plain to log file
 *
 * @param label Header label (e.g., "Backtrace")
 * @param symbols Array of pre-resolved symbol strings
 * @param count Number of symbols in the array
 * @param skip_frames Number of frames to skip from the start
 * @param max_frames Maximum frames to print (0 = unlimited)
 * @param filter Optional filter callback to skip specific frames (NULL = no filtering)
 */
// Note: This function is declared weak so platform-specific stubs (like WASM) can override it
__attribute__((weak)) void platform_print_backtrace_symbols(const char *label, char **symbols, int count,
                                                            int skip_frames, int max_frames,
                                                            backtrace_frame_filter_t filter) {
  if (!symbols || count <= 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: symbols=%p, count=%d", symbols, count);
    return;
  }

  // Calculate frame limits
  int start = skip_frames;
  int end = count;
  if (max_frames > 0 && (start + max_frames) < end) {
    end = start + max_frames;
  }

  // Build backtrace in two versions: colored for terminal, plain for log file
  char colored_buffer[16384] = {0};
  char plain_buffer[16384] = {0};
  int colored_offset = 0;
  int plain_offset = 0;

  // Format log header using logging system's template
  char log_header_buf[512] = {0};
  time_t now = time(NULL);
  struct tm *tm_info = localtime(&now);
  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "%H:%M:%S", tm_info);

  thread_id_t tid = asciichat_thread_self();
  uint64_t tid_val = (uintptr_t)tid;

  // Get current time in nanoseconds for template formatting
  uint64_t time_ns = platform_get_monotonic_time_us() * 1000ULL;

  // Try to format header using the logging system's template
  log_template_t *format = log_get_template();
  if (format) {
    // Use label directly - logging system will handle color output
    int len = log_template_apply(format, log_header_buf, sizeof(log_header_buf), LOG_WARN, timestamp, __FILE__,
                                 __LINE__, __func__, tid_val, label, true, time_ns);
    if (len > 0) {
      // Successfully formatted with logging template
      colored_offset += safe_snprintf(colored_buffer + colored_offset, sizeof(colored_buffer) - (size_t)colored_offset,
                                      "%s\n", log_header_buf);
    } else {
      // Fallback: manual formatting if template fails
      safe_snprintf(log_header_buf, sizeof(log_header_buf), "[%s] [WARN] [tid:%llu] %s: %s", timestamp, tid_val,
                    __func__, label);
      colored_offset += safe_snprintf(colored_buffer + colored_offset, sizeof(colored_buffer) - (size_t)colored_offset,
                                      "%s\n", log_header_buf);
    }
  } else {
    // Fallback: manual formatting if no template available
    safe_snprintf(log_header_buf, sizeof(log_header_buf), "[%s] [WARN] [tid:%llu] %s: %s", timestamp, tid_val, __func__,
                  label);
    colored_offset += safe_snprintf(colored_buffer + colored_offset, sizeof(colored_buffer) - (size_t)colored_offset,
                                    "%s\n", log_header_buf);
  }

  // Add plain label header for log file
  plain_offset +=
      safe_snprintf(plain_buffer + plain_offset, sizeof(plain_buffer) - (size_t)plain_offset, "%s\n", label);

  // Build backtrace frames with colored output for terminal, plain for log
  int frame_num = 0;
  for (int i = start; i < end && colored_offset < (int)sizeof(colored_buffer) - 512; i++) {
    const char *symbol = symbols[i] ? symbols[i] : "???";

    // Skip frame if filter says to
    if (filter && filter(symbol)) {
      continue;
    }

    // Build frame number string
    char frame_num_str[16];
    safe_snprintf(frame_num_str, sizeof(frame_num_str), "%d", frame_num);

    // Use frame number directly - logging system will handle color output

    // Parse symbol to extract parts for selective coloring
    // Format: "[binary_name] function_name() (file:line)"
    char colored_symbol[2048] = {0};
    const char *s = symbol;
    int colored_sym_offset = 0;

    // Color binary name between brackets
    if (*s == '[') {
      colored_sym_offset +=
          safe_snprintf(colored_symbol + colored_sym_offset, sizeof(colored_symbol) - colored_sym_offset, "[");
      s++;
      // Find closing bracket
      const char *bracket_end = strchr(s, ']');
      if (bracket_end) {
        int bin_len = bracket_end - s;
        char bin_name[512];
        strncpy(bin_name, s, bin_len);
        bin_name[bin_len] = '\0';
        colored_sym_offset += safe_snprintf(colored_symbol + colored_sym_offset,
                                            sizeof(colored_symbol) - colored_sym_offset, "%s", bin_name);
        colored_sym_offset +=
            safe_snprintf(colored_symbol + colored_sym_offset, sizeof(colored_symbol) - colored_sym_offset, "] ");
        s = bracket_end + 1;
      }
    }

    // Skip leading spaces after bracket
    while (*s && *s == ' ')
      s++;

    // Parse: could be "function() (file:line)" or "file:line (unresolved)"
    const char *paren_start = strchr(s, '(');

    // Detect format: if there's a colon before the first paren, it's "file:line (description)"
    const char *colon_pos = strchr(s, ':');
    if (colon_pos && paren_start && colon_pos < paren_start) {
      // Format: "file:line (unresolved function)" - rearrange to "(unresolved) (file:line)"
      // Extract file:line part (trim trailing spaces)
      int file_len = paren_start - s;
      while (file_len > 0 && s[file_len - 1] == ' ')
        file_len--;
      char file_part[512];
      strncpy(file_part, s, file_len);
      file_part[file_len] = '\0';

      // Extract description content (without parens)
      const char *paren_end = strchr(paren_start, ')');
      int desc_content_len = paren_end - paren_start - 1; // -1 to skip opening paren
      char desc_content[512];
      strncpy(desc_content, paren_start + 1, desc_content_len); // +1 to skip opening paren
      desc_content[desc_content_len] = '\0';

      colored_sym_offset += safe_snprintf(colored_symbol + colored_sym_offset,
                                          sizeof(colored_symbol) - colored_sym_offset, "(%s)", desc_content);

      // Now color file:line in parens
      // Skip leading spaces in file_part
      const char *file_start = file_part;
      while (*file_start && *file_start == ' ')
        file_start++;

      char *file_colon = strchr(file_start, ':');
      if (file_colon) {
        int filename_len = file_colon - file_start;
        char filename[512];
        strncpy(filename, file_start, filename_len);
        filename[filename_len] = '\0';

        const char *line_num = file_colon + 1;
        colored_sym_offset +=
            safe_snprintf(colored_symbol + colored_sym_offset, sizeof(colored_symbol) - colored_sym_offset, " (%s:%s)",
                          filename, line_num);
      }
    } else if (paren_start) {
      // Format: "function() (file:line)"
      int func_len = paren_start - s;
      char func_name[512];
      strncpy(func_name, s, func_len);
      func_name[func_len] = '\0';

      const char *colored_func_ptr = colored_string(LOG_COLOR_DEV, func_name);
      char colored_func_buf[512];
      strncpy(colored_func_buf, colored_func_ptr, sizeof(colored_func_buf) - 1);
      colored_func_buf[sizeof(colored_func_buf) - 1] = '\0';
      colored_sym_offset += safe_snprintf(colored_symbol + colored_sym_offset,
                                          sizeof(colored_symbol) - colored_sym_offset, "%s()", colored_func_buf);

      // Find file:line in second set of parens
      s = paren_start + 1;
      while (*s && *s != '(')
        s++;

      if (*s == '(') {
        const char *file_paren_end = strchr(s, ')');
        if (file_paren_end) {
          colored_sym_offset +=
              safe_snprintf(colored_symbol + colored_sym_offset, sizeof(colored_symbol) - colored_sym_offset, " (");
          int file_len = file_paren_end - s - 1;
          char file_part[512];
          strncpy(file_part, s + 1, file_len);
          file_part[file_len] = '\0';

          // Skip leading spaces in file_part
          const char *file_start = file_part;
          while (*file_start && *file_start == ' ')
            file_start++;

          char *colon_pos = strchr(file_start, ':');
          if (colon_pos) {
            int filename_len = colon_pos - file_start;
            char filename[512];
            strncpy(filename, file_start, filename_len);
            filename[filename_len] = '\0';

            const char *colored_file_ptr = colored_string(LOG_COLOR_DEBUG, filename);
            char colored_file_buf[512];
            strncpy(colored_file_buf, colored_file_ptr, sizeof(colored_file_buf) - 1);
            colored_file_buf[sizeof(colored_file_buf) - 1] = '\0';

            const char *line_num = colon_pos + 1;
            const char *colored_line_ptr = colored_string(LOG_COLOR_GREY, line_num);
            char colored_line_buf[512];
            strncpy(colored_line_buf, colored_line_ptr, sizeof(colored_line_buf) - 1);
            colored_line_buf[sizeof(colored_line_buf) - 1] = '\0';

            colored_sym_offset +=
                safe_snprintf(colored_symbol + colored_sym_offset, sizeof(colored_symbol) - colored_sym_offset, "%s:%s",
                              colored_file_buf, colored_line_buf);
          }
          colored_sym_offset +=
              safe_snprintf(colored_symbol + colored_sym_offset, sizeof(colored_symbol) - colored_sym_offset, ")");
        }
      }
    } else {
      // No parens, likely a hex address
      colored_sym_offset +=
          safe_snprintf(colored_symbol + colored_sym_offset, sizeof(colored_symbol) - colored_sym_offset, "%s", s);
    }

    // Format colored buffer: "  [num] symbol\n"
    colored_offset += safe_snprintf(colored_buffer + colored_offset, sizeof(colored_buffer) - (size_t)colored_offset,
                                    "  [%d] %s\n", frame_num, colored_symbol);

    // Format plain buffer: "  [num] symbol\n"
    plain_offset += safe_snprintf(plain_buffer + plain_offset, sizeof(plain_buffer) - (size_t)plain_offset,
                                  "  [%d] %s\n", frame_num, symbol);
    frame_num++;
  }

  // TODO: Investigate why log_warn() can't be used here. Currently we bypass the logging
  // system to preserve ANSI color codes, but this means we lose the normal log formatting
  // (timestamps, log level, etc). Ideally log_warn() should accept a flag to preserve codes.
  fprintf(stderr, "%s", colored_buffer);

  // Write plain version to log file only (skip stderr since we already printed colored version)
  log_file_msg("%s", plain_buffer);
}
