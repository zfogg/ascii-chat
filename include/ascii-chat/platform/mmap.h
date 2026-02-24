#pragma once

/**
 * @file platform/mmap.h
 * @brief Cross-platform memory-mapped file interface
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * This header provides a unified interface for memory-mapped files across
 * platforms. Memory-mapped files allow treating file contents as memory,
 * enabling efficient shared state and crash-safe logging.
 *
 * The interface provides:
 * - Memory-mapping files for read/write access
 * - Automatic file creation and sizing
 * - Explicit sync to flush changes to disk
 * - Clean unmapping and resource cleanup
 *
 * Platform implementations:
 * - POSIX (Linux/macOS): mmap(), munmap(), msync()
 * - Windows: CreateFileMapping(), MapViewOfFile(), FlushViewOfFile()
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#include <stddef.h>
#include <stdbool.h>
#include "../common.h"

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Memory-mapped file handle
 *
 * Contains platform-specific handles and mapping information.
 * Do not access members directly; use the platform_mmap_* functions.
 */
typedef struct platform_mmap {
  void *addr;  /**< Mapped memory address (NULL if not mapped) */
  size_t size; /**< Size of the mapping in bytes */
#ifdef _WIN32
  HANDLE file_handle;    /**< Windows file handle */
  HANDLE mapping_handle; /**< Windows mapping handle */
#else
  int fd; /**< POSIX file descriptor */
#endif
} platform_mmap_t;

/**
 * @brief Initialize a platform_mmap_t structure
 *
 * Sets all fields to safe initial values. Call before first use.
 *
 * @param mapping Pointer to mapping structure to initialize
 */
void platform_mmap_init(platform_mmap_t *mapping);

/**
 * @brief Memory-map a file for read/write access
 *
 * Opens or creates a file and maps it into memory. The file is created
 * if it doesn't exist, and resized to the specified size.
 *
 * The mapping uses shared mode (MAP_SHARED on POSIX, FILE_MAP_ALL_ACCESS
 * on Windows) so changes are visible to other processes and persist to
 * the file.
 *
 * @param path File path to map (created if doesn't exist)
 * @param size Desired mapping size in bytes
 * @param[out] out Output mapping handle (must be initialized with platform_mmap_init)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note On success, out->addr contains the mapped memory address
 * @note Call platform_mmap_close() to unmap and close
 *
 * Example:
 * @code
 * platform_mmap_t mapping;
 * platform_mmap_init(&mapping);
 * if (platform_mmap_open("/tmp/log.mmap", 1024 * 1024, &mapping) == ASCIICHAT_OK) {
 *     // Use mapping.addr as normal memory
 *     memset(mapping.addr, 0, mapping.size);
 *     platform_mmap_close(&mapping);
 * }
 * @endcode
 */
asciichat_error_t platform_mmap_open(const char *name, const char *path, size_t size, platform_mmap_t *out);

/**
 * @brief Unmap and close a memory-mapped file
 *
 * Unmaps the memory region and closes the underlying file handle.
 * Safe to call on an already-closed or uninitialized mapping.
 *
 * @param mapping Mapping handle to close
 *
 * @note Does not explicitly sync before closing; kernel will flush
 *       dirty pages eventually. Call platform_mmap_sync() first if
 *       immediate persistence is required.
 */
void platform_mmap_close(platform_mmap_t *mapping);

/**
 * @brief Flush memory-mapped changes to disk
 *
 * Requests the kernel to flush any modified pages to the underlying file.
 * This is typically not needed as the kernel flushes automatically, but
 * can be used to ensure data persistence at specific points.
 *
 * @param mapping Mapping handle to sync
 * @param async If true, return immediately (async flush). If false, block
 *              until flush completes (sync flush).
 *
 * @note On crash, unflushed data may be lost. For crash-critical data,
 *       call platform_mmap_sync(mapping, false) after important writes.
 */
void platform_mmap_sync(platform_mmap_t *mapping, bool async);

/**
 * @brief Check if a mapping is currently valid
 *
 * @param mapping Mapping handle to check
 * @return true if the mapping is open and usable, false otherwise
 */
bool platform_mmap_is_valid(const platform_mmap_t *mapping);

#ifdef __cplusplus
}
#endif

/** @} */ /* platform */
