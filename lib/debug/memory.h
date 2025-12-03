// SPDX-License-Identifier: MIT
/**
 * @file debug/memory.h
 * @brief 🔍 Memory debugging helpers for tracking allocations in debug builds
 * @ingroup debug
 * @addtogroup debug
 * @{
 *
 * This module provides memory allocation wrappers that track all heap
 * allocations for debugging and leak detection. When `DEBUG_MEMORY` is
 * defined and `NDEBUG` is not set, all allocations are recorded in a
 * linked list with source location metadata.
 *
 * ## Features
 *
 * - **Allocation Tracking**: Records every malloc/calloc/realloc/free with
 *   source file and line number
 * - **Memory Statistics**: Tracks total allocated, total freed, current usage,
 *   and peak memory usage
 * - **Call Counting**: Counts malloc, calloc, realloc, and free calls
 * - **Leak Detection**: Reports unfreed allocations with source locations
 * - **Thread Safety**: Uses mutex-protected access and atomic counters
 * - **Recursion Prevention**: Thread-local guard prevents infinite recursion
 *
 * ## Usage
 *
 * Typically used via macros that inject `__FILE__` and `__LINE__`:
 * @code
 * #define TRACKED_MALLOC(size) debug_malloc(size, __FILE__, __LINE__)
 * #define TRACKED_FREE(ptr)    debug_free(ptr, __FILE__, __LINE__)
 * @endcode
 *
 * At program exit, call `debug_memory_report()` to print memory statistics
 * and any outstanding (leaked) allocations.
 *
 * ## Build Modes
 *
 * - **Full tracking** (`DEBUG_MEMORY` + debug build): Full allocation tracking
 *   with linked list, atomic statistics, and mutex protection
 * - **Passthrough** (`DEBUG_MEMORY` + `NDEBUG`): Functions become thin wrappers
 *   around standard allocators with no tracking overhead
 * - **Disabled** (no `DEBUG_MEMORY`): Header provides no declarations
 *
 * @see debug_memory_report() for generating leak reports
 */

#pragma once

#if defined(DEBUG_MEMORY)

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Enable or disable quiet mode for memory reports
 *
 * When quiet mode is enabled, `debug_memory_report()` will not print
 * anything to stderr. Useful for tests that intentionally leak or when
 * memory reports would clutter output.
 *
 * @param quiet If true, suppresses all memory report output
 *
 * @note This setting is global and not thread-safe to modify
 */
void debug_memory_set_quiet_mode(bool quiet);

/**
 * @brief Print a detailed memory usage report to stderr
 *
 * Outputs comprehensive memory statistics including:
 * - Total bytes allocated over program lifetime
 * - Total bytes freed
 * - Current memory usage
 * - Peak memory usage (high water mark)
 * - Number of malloc, calloc, and free calls
 * - List of all currently allocated blocks with source locations
 *
 * All byte values are pretty-printed (e.g., "1.5 MiB" instead of raw bytes).
 *
 * @note Call this at program exit to detect memory leaks
 * @note Output is suppressed if quiet mode is enabled
 * @note Cleans up the asciichat_errno thread-local before reporting
 *
 * Example output:
 * @code
 * === Memory Report ===
 * Total allocated: 4.2 MiB
 * Total freed: 4.1 MiB
 * Current usage: 128 KiB
 * Peak usage: 2.1 MiB
 * malloc calls: 1523
 * calloc calls: 42
 * free calls: 1501
 * (malloc calls + calloc calls) - free calls = 64
 *
 * Current allocations:
 *   - lib/network/socket.c:142 - 4 KiB
 *   - lib/ui/window.c:89 - 124 KiB
 * @endcode
 */
void debug_memory_report(void);

/**
 * @brief Allocate memory with debug tracking
 *
 * Wrapper around standard `malloc()` that records the allocation in an
 * internal linked list for leak tracking and statistics.
 *
 * @param size  Number of bytes to allocate
 * @param file  Source filename (typically `__FILE__`)
 * @param line  Source line number (typically `__LINE__`)
 *
 * @return Pointer to allocated memory, or NULL on failure
 *
 * @note Thread-safe: uses mutex for list access, atomics for statistics
 * @note Recursion-safe: nested calls fall through to standard malloc
 * @note Source paths are normalized to project-relative paths for display
 */
void *debug_malloc(size_t size, const char *file, int line);

/**
 * @brief Allocate zeroed memory with debug tracking
 *
 * Wrapper around standard `calloc()` that records the allocation in an
 * internal linked list for leak tracking and statistics.
 *
 * @param count Number of elements to allocate
 * @param size  Size of each element in bytes
 * @param file  Source filename (typically `__FILE__`)
 * @param line  Source line number (typically `__LINE__`)
 *
 * @return Pointer to zero-initialized memory, or NULL on failure
 *
 * @note Thread-safe: uses mutex for list access, atomics for statistics
 * @note Recursion-safe: nested calls fall through to standard calloc
 */
void *debug_calloc(size_t count, size_t size, const char *file, int line);

/**
 * @brief Reallocate memory with debug tracking
 *
 * Wrapper around standard `realloc()` that updates the allocation tracking
 * to reflect the new size and pointer.
 *
 * Behavior matches standard `realloc()`:
 * - If @p ptr is NULL, behaves like `debug_malloc(size, file, line)`
 * - If @p size is 0, behaves like `debug_free(ptr, file, line)` and returns NULL
 * - Otherwise, resizes the block and updates tracking metadata
 *
 * Memory statistics are updated based on whether the block grew or shrank:
 * - Growing: adds delta to total_allocated and current_usage
 * - Shrinking: adds delta to total_freed and subtracts from current_usage
 *
 * @param ptr   Pointer to existing allocation, or NULL for new allocation
 * @param size  New size in bytes, or 0 to free
 * @param file  Source filename (typically `__FILE__`)
 * @param line  Source line number (typically `__LINE__`)
 *
 * @return Pointer to reallocated memory, or NULL on failure or if size==0
 *
 * @note Thread-safe: uses mutex for list access, atomics for statistics
 * @note Recursion-safe: nested calls fall through to standard realloc
 * @note Updates peak usage if the reallocation causes a new high water mark
 */
void *debug_realloc(void *ptr, size_t size, const char *file, int line);

/**
 * @brief Free memory with debug tracking
 *
 * Wrapper around standard `free()` that removes the allocation from the
 * internal tracking list and updates statistics.
 *
 * If the pointer is not found in the tracking list (e.g., allocated before
 * tracking started or via a different allocator), a warning is logged and
 * the system attempts to determine the block size via platform-specific
 * introspection (`malloc_usable_size`, `_msize`, `malloc_size`).
 *
 * @param ptr   Pointer to free, or NULL (no-op)
 * @param file  Source filename (typically `__FILE__`)
 * @param line  Source line number (typically `__LINE__`)
 *
 * @note Thread-safe: uses mutex for list access, atomics for statistics
 * @note Recursion-safe: nested calls fall through to standard free
 * @note On Windows, handles aligned allocations via `_aligned_free()`
 * @note Logs warning with backtrace if freeing untracked pointer
 */
void debug_free(void *ptr, const char *file, int line);

/**
 * @brief Track an aligned allocation made outside the debug allocator
 *
 * Use this to register memory allocated via `aligned_alloc()`, `_aligned_malloc()`,
 * `posix_memalign()`, or similar alignment-aware allocators that cannot use
 * `debug_malloc()` directly.
 *
 * The allocation is marked as "aligned" internally, which on Windows ensures
 * it is freed via `_aligned_free()` instead of standard `free()`.
 *
 * @param ptr   Pointer to the aligned allocation (no-op if NULL)
 * @param size  Size of the allocation in bytes
 * @param file  Source filename (typically `__FILE__`)
 * @param line  Source line number (typically `__LINE__`)
 *
 * @note Thread-safe: uses mutex for list access, atomics for statistics
 * @note Recursion-safe: nested calls are ignored
 * @note The pointer must later be freed via `debug_free()` for proper tracking
 *
 * @code
 * void *aligned_buf = aligned_alloc(64, 4096);
 * debug_track_aligned(aligned_buf, 4096, __FILE__, __LINE__);
 * // ... use aligned_buf ...
 * debug_free(aligned_buf, __FILE__, __LINE__);  // Uses _aligned_free on Windows
 * @endcode
 */
void debug_track_aligned(void *ptr, size_t size, const char *file, int line);

/** @} */

#endif /* DEBUG_MEMORY */
