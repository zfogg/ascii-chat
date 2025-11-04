/**
 * @file platform/symbols.c
 * @ingroup platform
 * @brief 🔍 Symbol resolution cache: llvm-symbolizer/addr2line wrapper with hashtable-backed caching
 */

// Platform-specific binary names
#ifdef _WIN32
#define LLVM_SYMBOLIZER_BIN "llvm-symbolizer.exe"
#define ADDR2LINE_BIN "addr2line.exe"
#define popen _popen
#define pclose _pclose
#else
#define LLVM_SYMBOLIZER_BIN "llvm-symbolizer"
#define ADDR2LINE_BIN "addr2line"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>

#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#endif

#include "symbols.h"
#include "system.h"
#include "../common.h"
#include "util/uthash.h"
#include "rwlock.h"
#include "../util/path.h"
#include "../util/string.h"

// ============================================================================
// Constants
// ============================================================================

// Sentinel string for failed allocations (replaces NULL in middle of array)
#define NULL_SENTINEL "[NULL]"

// ============================================================================
// Symbolizer Type Selection
// ============================================================================

typedef enum {
  SYMBOLIZER_NONE = 0,     // No symbolizer available, use raw addresses
  SYMBOLIZER_LLVM = 1,     // llvm-symbolizer (preferred)
  SYMBOLIZER_ADDR2LINE = 2 // addr2line (fallback)
} symbolizer_type_t;

static symbolizer_type_t g_symbolizer_type = SYMBOLIZER_NONE;
static atomic_bool g_symbolizer_detected = false;

// ============================================================================
// Cache State
// ============================================================================

/**
 * @brief Symbol cache entry structure for address-to-symbol mapping
 *
 * Represents a single cached symbol resolution in the symbol cache hashtable.
 * Maps a memory address (key) to a resolved symbol name (value) to avoid
 * expensive addr2line subprocess spawns for the same addresses.
 *
 * CORE FIELDS:
 * ============
 * - addr: Memory address key (used for hashtable lookup)
 * - symbol: Resolved symbol string (allocated, owned by cache)
 * - hh: uthash handle (required for hash table operations)
 *
 * USAGE:
 * ======
 * This structure is used internally by the symbol cache hashtable:
 * - Key: Memory address (void*) from backtrace
 * - Value: Symbol name string (e.g., "main", "process_packet", etc.)
 *
 * CACHE OPERATIONS:
 * ================
 * - Lookup: Fast O(1) hashtable lookup by address
 * - Insertion: Cached after first addr2line resolution
 * - Lifetime: Owned by cache, freed on cache cleanup
 *
 * MEMORY MANAGEMENT:
 * ==================
 * - symbol string is allocated and owned by the cache
 * - Do not free symbol strings manually (cache manages them)
 * - Entries are managed by uthash
 *
 * @note This structure is used internally by the symbol cache implementation.
 *       Users should interact with the cache via symbol_cache_* functions.
 *
 * @ingroup platform
 */
typedef struct {
  /** @brief Memory address key (used for hashtable lookup) */
  void *addr;
  /** @brief Resolved symbol string (allocated, owned by cache) */
  char *symbol;
  /** @brief uthash handle (required for hash table operations) */
  UT_hash_handle hh;
} symbol_entry_t;

static symbol_entry_t *g_symbol_cache = NULL; // uthash uses structure pointer as head
static rwlock_t g_symbol_cache_lock = {0};    // External locking for thread safety
static atomic_bool g_symbol_cache_initialized = false;

// Statistics
static atomic_uint_fast64_t g_cache_hits = 0;
static atomic_uint_fast64_t g_cache_misses = 0;

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Detect which symbolizer is available in PATH
 * @return symbolizer_type_t indicating which tool to use
 */
static symbolizer_type_t detect_symbolizer(void) {
  // Try llvm-symbolizer first (preferred)
  if (platform_is_binary_in_path("llvm-symbolizer")) {
    log_debug("Found %s in PATH", LLVM_SYMBOLIZER_BIN);
    return SYMBOLIZER_LLVM;
  }

  // Fall back to addr2line
  if (platform_is_binary_in_path("addr2line")) {
    log_debug("Found %s in PATH (%s not available)", ADDR2LINE_BIN, LLVM_SYMBOLIZER_BIN);
    return SYMBOLIZER_ADDR2LINE;
  }

  log_warn("No symbolizer found in PATH (tried %s, %s) - using native backend", LLVM_SYMBOLIZER_BIN, ADDR2LINE_BIN);
  return SYMBOLIZER_NONE;
}

// ============================================================================
// Public API Implementation
// ============================================================================

asciichat_error_t symbol_cache_init(void) {
  bool expected = false;
  if (!atomic_compare_exchange_strong(&g_symbol_cache_initialized, &expected, true)) {
    return 0; // Already initialized
  }

  // Detect which symbolizer is available (once at init)
  expected = false;
  if (atomic_compare_exchange_strong(&g_symbolizer_detected, &expected, true)) {
    g_symbolizer_type = detect_symbolizer();
  }

  // Initialize rwlock for thread safety (uthash requires external locking)
  if (rwlock_init(&g_symbol_cache_lock) != 0) {
    atomic_store(&g_symbol_cache_initialized, false);
    return SET_ERRNO(ERROR_THREAD, "Failed to initialize symbol cache rwlock");
  }

  // Initialize uthash head to NULL (required)
  g_symbol_cache = NULL;

  atomic_store(&g_cache_hits, 0);
  atomic_store(&g_cache_misses, 0);

  return 0;
}

void symbol_cache_cleanup(void) {
  if (!atomic_load(&g_symbol_cache_initialized)) {
    return;
  }

  // Mark as uninitialized FIRST to prevent new inserts during cleanup
  atomic_store(&g_symbol_cache_initialized, false);

  // Acquire write lock to prevent any concurrent operations
  rwlock_wrlock(&g_symbol_cache_lock);

  // Free all symbol entries using HASH_ITER
  symbol_entry_t *entry, *tmp;
  HASH_ITER(hh, g_symbol_cache, entry, tmp) {
    HASH_DEL(g_symbol_cache, entry);
    if (entry->symbol) {
      // Use regular free() instead of SAFE_FREE() because entry->symbol was allocated
      // with strdup() (standard C library), not tracked by debug memory system
      free(entry->symbol);
    }
    SAFE_FREE(entry);
  }

  // Release lock and destroy rwlock
  rwlock_wrunlock(&g_symbol_cache_lock);
  rwlock_destroy(&g_symbol_cache_lock);

  g_symbol_cache = NULL;

  log_debug("Symbol cache cleaned up (hits=%llu, misses=%llu)", (unsigned long long)atomic_load(&g_cache_hits),
            (unsigned long long)atomic_load(&g_cache_misses));
}

const char *symbol_cache_lookup(void *addr) {
  if (!atomic_load(&g_symbol_cache_initialized) || !addr) {
    return NULL;
  }

  rwlock_rdlock(&g_symbol_cache_lock);

  symbol_entry_t *entry = NULL;
  HASH_FIND_PTR(g_symbol_cache, &addr, entry);

  if (entry) {
    const char *symbol = entry->symbol;
    atomic_fetch_add(&g_cache_hits, 1);
    rwlock_rdunlock(&g_symbol_cache_lock);
    return symbol;
  }

  atomic_fetch_add(&g_cache_misses, 1);
  rwlock_rdunlock(&g_symbol_cache_lock);
  return NULL;
}

bool symbol_cache_insert(void *addr, const char *symbol) {
  if (!atomic_load(&g_symbol_cache_initialized) || !addr || !symbol) {
    return false;
  }

  // Acquire write lock to make the entire operation atomic
  rwlock_wrlock(&g_symbol_cache_lock);

  // Double-check cache is still initialized after acquiring lock
  // (cleanup might have marked it uninitialized between our check and lock acquisition)
  if (!atomic_load(&g_symbol_cache_initialized)) {
    rwlock_wrunlock(&g_symbol_cache_lock);
    return false;
  }

  // Check if entry already exists
  symbol_entry_t *existing = NULL;
  HASH_FIND_PTR(g_symbol_cache, &addr, existing);

  if (existing) {
    // Entry exists - update symbol if different
    if (existing->symbol && strcmp(existing->symbol, symbol) != 0) {
      // Free old symbol and allocate new one
      free(existing->symbol);
      existing->symbol = platform_strdup(symbol);
      if (!existing->symbol) {
        rwlock_wrunlock(&g_symbol_cache_lock);
        return false;
      }
    }
    rwlock_wrunlock(&g_symbol_cache_lock);
    return true;
  }

  // Create new entry
  symbol_entry_t *entry = SAFE_MALLOC(sizeof(symbol_entry_t), symbol_entry_t *);
  if (!entry) {
    rwlock_wrunlock(&g_symbol_cache_lock);
    return false;
  }

  entry->addr = addr;
  entry->symbol = platform_strdup(symbol);
  if (!entry->symbol) {
    SAFE_FREE(entry);
    rwlock_wrunlock(&g_symbol_cache_lock);
    return false;
  }

  // Add to hash table
  HASH_ADD_PTR(g_symbol_cache, addr, entry);

  // Release lock
  rwlock_wrunlock(&g_symbol_cache_lock);

  return true;
}

void symbol_cache_get_stats(uint64_t *hits_out, uint64_t *misses_out, size_t *entries_out) {
  if (hits_out) {
    *hits_out = atomic_load(&g_cache_hits);
  }
  if (misses_out) {
    *misses_out = atomic_load(&g_cache_misses);
  }
  if (entries_out) {
    rwlock_rdlock(&g_symbol_cache_lock);
    *entries_out = HASH_COUNT(g_symbol_cache);
    rwlock_rdunlock(&g_symbol_cache_lock);
  }
}

void symbol_cache_print_stats(void) {
  uint64_t hits = atomic_load(&g_cache_hits);
  uint64_t misses = atomic_load(&g_cache_misses);

  rwlock_rdlock(&g_symbol_cache_lock);
  size_t entries = HASH_COUNT(g_symbol_cache);
  rwlock_rdunlock(&g_symbol_cache_lock);

  uint64_t total = hits + misses;
  double hit_rate = total > 0 ? (100.0 * hits / total) : 0.0;

  log_info("Symbol Cache Stats: %zu entries, %llu hits, %llu misses (%.1f%% hit rate)", entries,
           (unsigned long long)hits, (unsigned long long)misses, hit_rate);
}

// ============================================================================
// Batch Resolution with llvm-symbolizer and addr2line
// ============================================================================

/**
 * @brief Run llvm-symbolizer on a batch of addresses and parse results
 * @param buffer Array of addresses
 * @param size Number of addresses
 * @return Array of symbol strings (must be freed by caller)
 */
static char **run_llvm_symbolizer_batch(void *const *buffer, int size) {
  if (size <= 0 || !buffer) {
    return NULL;
  }

  // Get executable path
  char exe_path[PLATFORM_MAX_PATH_LENGTH];
  if (!platform_get_executable_path(exe_path, sizeof(exe_path))) {
    return NULL;
  }

  // SECURITY: Validate executable path to prevent command injection
  // Paths from system APIs should be safe, but validate to be thorough
  if (!validate_shell_safe(exe_path, ".-/\\:")) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid executable path - contains unsafe characters: %s", exe_path);
    return NULL;
  }

  // Conditionally escape exe_path only if it needs quoting (has spaces or special chars)
  // llvm-symbolizer handles unquoted paths correctly, so only quote when necessary
  const char *escaped_exe_path = exe_path;
  char escaped_exe_path_buf[PLATFORM_MAX_PATH_LENGTH * 2];
  bool needs_quoting = false;
  for (size_t i = 0; exe_path[i] != '\0'; i++) {
    if (exe_path[i] == ' ' || exe_path[i] == '\t' || exe_path[i] == '"' || exe_path[i] == '\'') {
      needs_quoting = true;
      break;
    }
  }

  if (needs_quoting) {
#ifdef _WIN32
    if (!escape_shell_double_quotes(exe_path, escaped_exe_path_buf, sizeof(escaped_exe_path_buf))) {
      SET_ERRNO(ERROR_STRING, "Failed to escape executable path for shell command");
      return NULL;
    }
#else
    if (!escape_shell_single_quotes(exe_path, escaped_exe_path_buf, sizeof(escaped_exe_path_buf))) {
      SET_ERRNO(ERROR_STRING, "Failed to escape executable path for shell command");
      return NULL;
    }
#endif
    escaped_exe_path = escaped_exe_path_buf;
  }

  // Build llvm-symbolizer command with --demangle, --output-style=LLVM, --relativenames, --inlining,
  // and --debug-file-directory
  char cmd[8192];
  int offset;

#ifdef BUILD_DIR
  // SECURITY: Validate BUILD_DIR (compile-time constant, but validate to be thorough)
  if (!validate_shell_safe(BUILD_DIR, ".-/\\:")) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid BUILD_DIR - contains unsafe characters: %s", BUILD_DIR);
    return NULL;
  }

  // Conditionally escape BUILD_DIR only if it needs quoting (has spaces or special chars)
  const char *escaped_build_dir = BUILD_DIR;
  char escaped_build_dir_buf[PLATFORM_MAX_PATH_LENGTH * 2];
  bool build_dir_needs_quoting = false;
  for (size_t i = 0; BUILD_DIR[i] != '\0'; i++) {
    if (BUILD_DIR[i] == ' ' || BUILD_DIR[i] == '\t' || BUILD_DIR[i] == '"' || BUILD_DIR[i] == '\'') {
      build_dir_needs_quoting = true;
      break;
    }
  }

  if (build_dir_needs_quoting) {
#ifdef _WIN32
    if (!escape_shell_double_quotes(BUILD_DIR, escaped_build_dir_buf, sizeof(escaped_build_dir_buf))) {
      SET_ERRNO(ERROR_STRING, "Failed to escape BUILD_DIR for shell command");
      return NULL;
    }
#else
    if (!escape_shell_single_quotes(BUILD_DIR, escaped_build_dir_buf, sizeof(escaped_build_dir_buf))) {
      SET_ERRNO(ERROR_STRING, "Failed to escape BUILD_DIR for shell command");
      return NULL;
    }
#endif
    escaped_build_dir = escaped_build_dir_buf;
  }

  offset = snprintf(cmd, sizeof(cmd),
                    "%s --demangle --output-style=LLVM --relativenames --inlining "
                    "--debug-file-directory=%s -e %s ",
                    LLVM_SYMBOLIZER_BIN, escaped_build_dir, escaped_exe_path);
#else
  // Note: Use platform-appropriate quotes (already escaped)
  offset = snprintf(cmd, sizeof(cmd), "%s --demangle --output-style=LLVM --relativenames --inlining -e %s ",
                    LLVM_SYMBOLIZER_BIN, escaped_exe_path);
#endif

  if (offset <= 0 || offset >= (int)sizeof(cmd)) {
    SET_ERRNO(ERROR_INVALID_STATE, "Failed to build llvm-symbolizer command");
    return NULL;
  }

  // Add all addresses to the command
  // Use explicit hex format with 0x prefix since Windows %p doesn't include it
  for (int i = 0; i < size; i++) {
    int n = snprintf(cmd + offset, sizeof(cmd) - offset, "0x%llx ", (unsigned long long)buffer[i]);
    if (n <= 0 || offset + n >= (int)sizeof(cmd)) {
      break;
    }
    offset += n;
  }

  // Execute llvm-symbolizer
  FILE *fp = popen(cmd, "r");
  if (!fp) {
    SET_ERRNO(ERROR_INVALID_STATE, "Failed to execute llvm-symbolizer command");
    return NULL;
  }

  // Allocate result array
  char **result = SAFE_CALLOC((size_t)(size + 1), sizeof(char *), char **);
  if (!result) {
    pclose(fp);
    return NULL;
  }

  // Parse output - llvm-symbolizer produces THREE lines per address:
  // Line 1: function_name (or "??")
  // Line 2: /path/to/file:line:column (or "??:0")
  // Line 3: blank line (separator)
  for (int i = 0; i < size; i++) {
    char func_name[512] = "??";
    char file_location[512] = "??:0";
    char blank_line[8];

    // Read function name line
    if (fgets(func_name, sizeof(func_name), fp) == NULL) {
      break;
    }
    func_name[strcspn(func_name, "\n")] = '\0';

    // Read file location line
    if (fgets(file_location, sizeof(file_location), fp) == NULL) {
      break;
    }
    file_location[strcspn(file_location, "\n")] = '\0';

    // Read blank separator line (and discard it)
    if (fgets(blank_line, sizeof(blank_line), fp) == NULL) {
      // End of output - not an error, just means this was the last address
    }

    // Remove column number (last :N) from file_location
    char *last_colon = strrchr(file_location, ':');
    if (last_colon) {
      *last_colon = '\0';
    }

    // Extract relative path
    const char *rel_path = extract_project_relative_path(file_location);

    // Allocate result buffer
    result[i] = SAFE_MALLOC(1024, char *);
    if (!result[i]) {
      break;
    }

    // Format symbol
    bool has_func = (strcmp(func_name, "??") != 0 && strlen(func_name) > 0);
    bool has_file =
        (strcmp(file_location, "??:0") != 0 && strcmp(file_location, "??:?") != 0 && strcmp(file_location, "??") != 0);

    if (!has_func && !has_file) {
      // Complete unknown - show raw address
      SAFE_SNPRINTF(result[i], 1024, "%p", buffer[i]);
    } else if (has_func && has_file) {
      // Best case - both function and file:line known
      // Remove () from function name if present (llvm-symbolizer includes them)
      char clean_func[512];
      SAFE_STRNCPY(clean_func, func_name, sizeof(clean_func));
      char *paren = strstr(clean_func, "()");
      if (paren) {
        *paren = '\0';
      }

      if (strstr(rel_path, ":") != NULL) {
        SAFE_SNPRINTF(result[i], 1024, "%s in %s()", rel_path, clean_func);
      } else {
        SAFE_SNPRINTF(result[i], 1024, "%s() at %s", clean_func, rel_path);
      }
    } else if (has_func) {
      // Function known but file unknown (common for library functions)
      char clean_func[512];
      SAFE_STRNCPY(clean_func, func_name, sizeof(clean_func));
      char *paren = strstr(clean_func, "()");
      if (paren) {
        *paren = '\0';
      }
      SAFE_SNPRINTF(result[i], 1024, "%s() at %p", clean_func, buffer[i]);
    } else {
      // File known but function unknown (rare)
      SAFE_SNPRINTF(result[i], 1024, "%s (unknown function)", rel_path);
    }
  }

  pclose(fp);
  return result;
}

/**
 * @brief Run addr2line on a batch of addresses and parse results
 * @param buffer Array of addresses
 * @param size Number of addresses
 * @return Array of symbol strings (must be freed by caller)
 */
static char **run_addr2line_batch(void *const *buffer, int size) {
  if (size <= 0 || !buffer) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: buffer=%p, size=%d", (void *)buffer, size);
    return NULL;
  }

  // Get executable path
  char exe_path[PLATFORM_MAX_PATH_LENGTH];
  if (!platform_get_executable_path(exe_path, sizeof(exe_path))) {
    return NULL;
  }

  // SECURITY: Validate executable path to prevent command injection
  // Paths from system APIs should be safe, but validate to be thorough
  if (!validate_shell_safe(exe_path, ".-/\\:")) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid executable path - contains unsafe characters: %s", exe_path);
    return NULL;
  }

  // Conditionally escape exe_path only if it needs quoting (has spaces or special chars)
  // addr2line handles unquoted paths correctly, so only quote when necessary
  const char *escaped_exe_path = exe_path;
  char escaped_exe_path_buf[PLATFORM_MAX_PATH_LENGTH * 2];
  bool needs_quoting = false;
  for (size_t i = 0; exe_path[i] != '\0'; i++) {
    if (exe_path[i] == ' ' || exe_path[i] == '\t' || exe_path[i] == '"' || exe_path[i] == '\'') {
      needs_quoting = true;
      break;
    }
  }

  if (needs_quoting) {
#ifdef _WIN32
    if (!escape_shell_double_quotes(exe_path, escaped_exe_path_buf, sizeof(escaped_exe_path_buf))) {
      SET_ERRNO(ERROR_STRING, "Failed to escape executable path for shell command");
      return NULL;
    }
#else
    if (!escape_shell_single_quotes(exe_path, escaped_exe_path_buf, sizeof(escaped_exe_path_buf))) {
      SET_ERRNO(ERROR_STRING, "Failed to escape executable path for shell command");
      return NULL;
    }
#endif
    escaped_exe_path = escaped_exe_path_buf;
  }

  // Build addr2line command
  char cmd[4096];
  int offset = snprintf(cmd, sizeof(cmd), "%s -e %s -f -C -i ", ADDR2LINE_BIN, escaped_exe_path);
  if (offset <= 0 || offset >= (int)sizeof(cmd)) {
    SET_ERRNO(ERROR_INVALID_STATE, "Failed to build addr2line command");
    return NULL;
  }

  // Use explicit hex format with 0x prefix since Windows %p doesn't include it
  for (int i = 0; i < size; i++) {
    int n = snprintf(cmd + offset, sizeof(cmd) - offset, "0x%llx ", (unsigned long long)buffer[i]);
    if (n <= 0 || offset + n >= (int)sizeof(cmd)) {
      SET_ERRNO(ERROR_INVALID_STATE, "Failed to build addr2line command");
      break;
    }
    offset += n;
  }

  // Execute addr2line
  FILE *fp = popen(cmd, "r");
  if (!fp) {
    SET_ERRNO(ERROR_INVALID_STATE, "Failed to execute addr2line command");
    return NULL;
  }

  // Allocate result array
  char **result = SAFE_CALLOC((size_t)(size + 1), sizeof(char *), char **);
  if (!result) {
    pclose(fp);
    return NULL;
  }

  // Parse output
  for (int i = 0; i < size; i++) {
    char func_name[256];
    char file_line[512];

    if (fgets(func_name, sizeof(func_name), fp) == NULL) {
      break;
    }
    if (fgets(file_line, sizeof(file_line), fp) == NULL) {
      break;
    }

    // Remove newlines
    func_name[strcspn(func_name, "\n")] = '\0';
    file_line[strcspn(file_line, "\n")] = '\0';

    // Extract relative path
    const char *rel_path = extract_project_relative_path(file_line);

    // Allocate result buffer
    result[i] = SAFE_MALLOC(1024, char *);

    // Format symbol
    bool has_func = (strcmp(func_name, "??") != 0);
    bool has_file = (strcmp(file_line, "??:0") != 0 && strcmp(file_line, "??:?") != 0);

    if (!has_func && !has_file) {
      // Complete unknown - show raw address
      SAFE_SNPRINTF(result[i], 1024, "%p", buffer[i]);
    } else if (has_func && has_file) {
      // Best case - both function and file:line known
      if (strstr(rel_path, ":") != NULL) {
        SAFE_SNPRINTF(result[i], 1024, "%s in %s()", rel_path, func_name);
      } else {
        SAFE_SNPRINTF(result[i], 1024, "%s() at %s", func_name, rel_path);
      }
    } else if (has_func) {
      // Function known but file unknown (common for library functions)
      SAFE_SNPRINTF(result[i], 1024, "%s() at %p", func_name, buffer[i]);
    } else {
      // File known but function unknown (rare)
      SAFE_SNPRINTF(result[i], 1024, "%s (unknown function)", rel_path);
    }
  }

  pclose(fp);
  return result;
}

char **symbol_cache_resolve_batch(void *const *buffer, int size) {
  if (size <= 0 || !buffer) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: buffer=%p, size=%d", (void *)buffer, size);
    return NULL;
  }

  // DO NOT auto-initialize here - causes circular dependency during lock_debug_init()
  // The cache must be initialized explicitly by platform_init() before use
  if (!atomic_load(&g_symbol_cache_initialized)) {
    // Cache not initialized - fall back to uncached resolution
    // This happens during early initialization before platform_init() completes
    // Try llvm-symbolizer first, then addr2line
    char **result = run_llvm_symbolizer_batch(buffer, size);
    if (!result) {
      result = run_addr2line_batch(buffer, size);
    }
    return result;
  }

  // Allocate result array (size + 1 for NULL terminator)
  // CALLOC zeros the memory, so result[size] is already NULL
  char **result = SAFE_CALLOC((size_t)(size + 1), sizeof(char *), char **);
  if (!result) {
    return NULL;
  }

  // Ensure NULL terminator is explicitly set (CALLOC already did this, but be explicit)
  result[size] = NULL;

  // First pass: check cache for all addresses
  int uncached_count = 0;
  void *uncached_addrs[size];
  int uncached_indices[size];

  for (int i = 0; i < size; i++) {
    const char *cached = symbol_cache_lookup(buffer[i]);
    if (cached) {
      // Cache hit - duplicate the string
      result[i] = platform_strdup(cached);
      // If allocation failed, use sentinel string instead of NULL
      if (!result[i]) {
        result[i] = platform_strdup(NULL_SENTINEL);
      }
    } else {
      // Cache miss - track for batch resolution
      uncached_addrs[uncached_count] = buffer[i];
      uncached_indices[uncached_count] = i;
      uncached_count++;
    }
  }

  // Second pass: resolve uncached addresses with selected symbolizer
  if (uncached_count > 0) {
    char **resolved = NULL;

    // Use the detected symbolizer type
    switch (g_symbolizer_type) {
    case SYMBOLIZER_LLVM:
      resolved = run_llvm_symbolizer_batch(uncached_addrs, uncached_count);
      break;
    case SYMBOLIZER_ADDR2LINE:
      resolved = run_addr2line_batch(uncached_addrs, uncached_count);
      break;
    case SYMBOLIZER_NONE:
    default:
      // No symbolizer available - will fall through to raw address handling
      resolved = NULL;
      break;
    }

    if (resolved) {
      for (int i = 0; i < uncached_count; i++) {
        int orig_idx = uncached_indices[i];
        if (resolved[i]) {
          result[orig_idx] = platform_strdup(resolved[i]);
          // If strdup failed, use sentinel string instead of NULL
          if (!result[orig_idx]) {
            SET_ERRNO(ERROR_MEMORY, "Failed to duplicate string for result[%d]", orig_idx);
            result[orig_idx] = platform_strdup(NULL_SENTINEL);
          }
          // Only insert into cache if strdup succeeded (and it's not the sentinel)
          if (result[orig_idx] && strcmp(result[orig_idx], NULL_SENTINEL) != 0) {
            if (!symbol_cache_insert(uncached_addrs[i], resolved[i])) {
              SET_ERRNO(ERROR_MEMORY, "Failed to insert symbol into cache for result[%d]", orig_idx);
            }
          }
          SAFE_FREE(resolved[i]);
        } else {
          // resolved[i] is NULL - use sentinel string
          if (!result[orig_idx]) {
            result[orig_idx] = platform_strdup(NULL_SENTINEL);
          }
        }
        if (!result[orig_idx]) {
          SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for result[%d]", orig_idx);
        }
      }

      SAFE_FREE(resolved);

    } else {
      // addr2line failed - fill uncached entries with raw addresses or sentinel
      for (int i = 0; i < uncached_count; i++) {
        int orig_idx = uncached_indices[i];
        if (!result[orig_idx]) {
          result[orig_idx] = SAFE_MALLOC(32, char *);
          if (result[orig_idx]) {
            SAFE_SNPRINTF(result[orig_idx], 32, "%p", uncached_addrs[i]);
          } else {
            result[orig_idx] = platform_strdup(NULL_SENTINEL);
          }
        }
        if (!result[orig_idx]) {
          SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for result[%d]", orig_idx);
        }
      }
    }
  }

  return result;
}

void symbol_cache_free_symbols(char **symbols) {
  if (!symbols) {
    return;
  }

  // The array is NULL-terminated (allocated with size+1, with result[size] = NULL)
  // The terminator is a SINGLE NULL at index 'size'
  // Failed allocations use the NULL_SENTINEL string "[NULL]" instead of NULL,
  // so there are no NULL entries in the middle - only at the terminator
  // This makes iteration safe: we can iterate until we find the first NULL (the terminator)

  // Iterate through entries, freeing all non-NULL entries until we hit the NULL terminator
  for (int i = 0; i < 64; i++) { // Reasonable limit to prevent infinite loop
    if (symbols[i] == NULL) {
      // Found NULL - this is the terminator, stop here
      break;
    }

    // Found a non-NULL entry - check if it's the sentinel string
    // Both regular strings and sentinel strings are allocated (with strdup),
    // so we free them all
    SAFE_FREE(symbols[i]);
    symbols[i] = NULL; // Clear pointer after freeing
  }

  // Free the array itself
  SAFE_FREE(symbols);
}
