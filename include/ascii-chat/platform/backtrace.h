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
 * @brief Get a backtrace of the current call stack
 * @param buffer Array of pointers to store return addresses
 * @param size Maximum number of frames to capture
 * @return Number of frames captured
 *
 * Captures the current call stack into the provided buffer.
 * Returns the number of frames actually captured.
 *
 * @ingroup platform
 */
int platform_backtrace(void **buffer, int size);

/**
 * @brief Convert backtrace addresses to symbol names
 * @param buffer Array of return addresses from platform_backtrace()
 * @param size Number of frames in buffer
 * @return Array of symbol name strings, or NULL on error
 *
 * Converts the return addresses from platform_backtrace() into
 * human-readable symbol names (function names, file names, line numbers).
 *
 * @note The returned array must be freed with platform_backtrace_symbols_destroy().
 *
 * @ingroup platform
 */
char **platform_backtrace_symbols(void *const *buffer, int size);

/**
 * @brief Free symbol array returned by platform_backtrace_symbols()
 * @param strings Array of symbol strings to free
 *
 * Frees the memory allocated by platform_backtrace_symbols().
 *
 * @ingroup platform
 */
void platform_backtrace_symbols_destroy(char **strings);

/**
 * @brief Callback type for filtering backtrace frames
 * @param frame The frame string to check
 * @return true if the frame should be skipped, false to include it
 *
 * @ingroup platform
 */
typedef bool (*backtrace_frame_filter_t)(const char *frame);

/**
 * @brief Print pre-resolved backtrace symbols with consistent formatting
 *
 * Uses colored format for all backtraces:
 *   [0] crypto_handshake_server_complete() (lib/crypto/handshake.c:1471)
 *   [1] server_crypto_handshake() (src/server/crypto.c:511)
 *
 * @param label Header label (e.g., "Backtrace", "Call stack")
 * @param symbols Array of pre-resolved symbol strings
 * @param count Number of symbols in the array
 * @param skip_frames Number of frames to skip from the start
 * @param max_frames Maximum frames to print (0 = unlimited)
 * @param filter Optional filter callback to skip specific frames (NULL = no filtering)
 *
 * @ingroup platform
 */
void platform_print_backtrace_symbols(const char *label, char **symbols, int count, int skip_frames, int max_frames,
                                      backtrace_frame_filter_t filter);

/**
 * @brief Format pre-resolved backtrace symbols to a buffer
 *
 * Same format as platform_print_backtrace_symbols() but writes to a buffer.
 *
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @param label Header label (e.g., "Call stack")
 * @param symbols Array of pre-resolved symbol strings
 * @param count Number of symbols in the array
 * @param skip_frames Number of frames to skip from the start
 * @param max_frames Maximum frames to print (0 = unlimited)
 * @param filter Optional filter callback to skip specific frames (NULL = no filtering)
 * @return Number of bytes written (excluding null terminator)
 *
 * @ingroup platform
 */
int platform_format_backtrace_symbols(char *buffer, size_t buffer_size, const char *label, char **symbols, int count,
                                      int skip_frames, int max_frames, backtrace_frame_filter_t filter);

/**
 * @brief Print a backtrace of the current call stack
 * @param skip_frames Number of frames to skip from the top
 *
 * Captures a backtrace and prints it using platform_print_backtrace_symbols().
 * Useful for debugging crashes or errors.
 *
 * @ingroup platform
 */
void platform_print_backtrace(int skip_frames);

/**
 * @brief Log N backtrace frames with function name, line number, and file
 *
 * Captures and logs the backtrace at debug level, showing the function name,
 * file path, and line number for each frame.
 *
 * @param num_frames Number of frames to capture and log (0 = all available)
 * @param skip_frames Number of frames to skip from the top (e.g., skip this function itself)
 *
 * Example output:
 *   [0] my_function() at src/main.c:42
 *   [1] caller_function() at src/caller.c:123
 *   [2] main() at src/main.c:999
 *
 * @note Uses log_debug() for output
 * @note Call with skip_frames=1 to skip the platform_log_backtrace_frames() call itself
 *
 * @ingroup platform
 */
void platform_log_backtrace_frames(int num_frames, int skip_frames);

/** @} */
