#pragma once

#include <stddef.h>

/**
 * @file platform/backtrace.h
 * @brief Cross-platform backtrace and stack trace capture
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * Platform abstraction for capturing and symbolizing stack traces.
 *
 * **Platform-specific implementations:**
 * - **POSIX (Linux/macOS)**: Uses libexecinfo backtrace() with fallback to manual frame walking
 * - **Windows**: Uses CaptureStackBackTrace or StackWalk64 API
 * - **WASM**: No-op stubs (web environment doesn't support stack traces)
 *
 * **Thread safety:**
 * - platform_backtrace() is thread-safe for concurrent calls
 * - platform_backtrace_symbols() may serialize addr2line calls to prevent concurrency issues
 * - platform_backtrace_symbols_destroy() is thread-safe
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date March 2026
 */

/**
 * @brief Get a backtrace of the current call stack
 *
 * Captures return addresses from the current call stack into a provided buffer.
 * Uses platform-specific mechanisms to walk the stack:
 * - POSIX: libexecinfo backtrace() or manual frame pointer walking
 * - Windows: CaptureStackBackTrace or StackWalk64
 *
 * @param buffer Array of void* pointers to store return addresses
 * @param size Maximum number of frames to capture
 * @return Number of frames captured (0 on error or empty stack)
 *
 * @note Return addresses are raw pointers; use platform_backtrace_symbols() to convert to names
 * @note This is a low-level function; prefer backtrace_capture() from debug/backtrace.h
 */
int platform_backtrace(void **buffer, int size);

/**
 * @brief Convert backtrace addresses to symbol names
 *
 * Converts raw return addresses from platform_backtrace() into human-readable
 * symbol information (function names, file paths, line numbers).
 *
 * Platform-specific resolution strategy:
 * - POSIX: Uses addr2line/llvm-symbolizer via symbol_cache_resolve_batch()
 * - Windows: Uses DbgHelp (SymFromAddr) with fallback to addr2line cache
 *
 * @param buffer Array of return addresses from platform_backtrace()
 * @param size Number of addresses in buffer
 * @return Null-terminated array of symbol strings, or NULL on error
 *
 * @note The returned array must be freed with platform_backtrace_symbols_destroy()
 * @note Array is guaranteed to have 'size' valid entries plus a NULL terminator
 * @note This is a low-level function; prefer backtrace_symbolize() from debug/backtrace.h
 */
char **platform_backtrace_symbols(void *const *buffer, int size);

/**
 * @brief Free symbol array from platform_backtrace_symbols()
 *
 * Releases all memory allocated by platform_backtrace_symbols(),
 * including individual symbol strings and the array itself.
 *
 * @param strings Array returned by platform_backtrace_symbols(), or NULL
 *
 * @note Safe to call with NULL pointer
 * @note Must not be called multiple times on same pointer
 */
void platform_backtrace_symbols_destroy(char **strings);

/**
 * @brief Print backtrace of the current call stack
 *
 * Captures the current call stack and prints it to stderr using platform_backtrace_symbols().
 * Useful for debugging crashes and errors.
 *
 * @param skip_frames Number of frames to skip from the top (0 = include all)
 */
void backtrace_print_simple(int skip_frames);

/** @} */
