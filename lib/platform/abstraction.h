#pragma once

/**
 * @defgroup platform Platform Abstractions
 * @brief 🔌 Cross-platform abstractions for threading, sockets, and system calls, and hardware access
 *
 * @file platform/abstraction.h
 * @ingroup platform
 * @brief 🔌 Cross-platform abstraction layer umbrella header for ascii-chat
 *
 * This header provides the main entry point for the platform abstraction layer,
 * enabling ascii-chat to run seamlessly on Windows, Linux, and macOS with a
 * unified API. It serves as the umbrella header that includes all platform
 * abstraction components.
 *
 * **Purpose**:
 *
 * The abstraction layer eliminates platform-specific code (`#ifdef _WIN32` blocks)
 * from application code by providing a unified API. All platform differences are
 * hidden behind this abstraction layer, making the application code completely
 * platform-independent.
 *
 * **Usage**:
 *
 * @code{.c}
 * // Include the main platform abstraction header
 * #include "platform/abstraction.h"
 *
 * // Use unified API - works identically on all platforms
 * socket_t sock = socket_create(AF_INET, SOCK_STREAM, 0);
 * asciithread_t thread;
 * ascii_thread_create(&thread, worker_func, arg);
 * @endcode
 *
 * **Organization**:
 *
 * The abstraction layer is organized into modular components, each with its own
 * header file. This header includes them all:
 *
 * - **Threading** (`thread.h`): Thread creation, joining, detaching, thread IDs
 * - **Synchronization** (`mutex.h`, `rwlock.h`, `cond.h`): Mutexes, read-write locks, condition variables
 * - **Networking** (`socket.h`): Socket operations, bind, listen, accept, send, recv
 * - **Terminal I/O** (`terminal.h`): Terminal size, raw mode, cursor control, ANSI support
 * - **System Functions** (`system.h`): Process info, environment variables, signals, backtraces
 * - **String Operations** (`string.h`): Safe string formatting and manipulation
 * - **File I/O** (`file.h`): Cross-platform file operations
 *
 * **Platform Detection**:
 *
 * This header automatically detects the target platform using `_WIN32` macro:
 * - **Windows**: Uses Win32 API, Winsock2, Critical Sections, SRW Locks
 * - **POSIX**: Uses pthreads, BSD sockets, termios (Linux/macOS)
 *
 * **Compatibility Layer**:
 *
 * This header also provides platform compatibility macros and definitions:
 * - POSIX-style constants for Windows (file permissions, signal constants, etc.)
 * - Type definitions (socket types, nfds_t, useconds_t)
 * - Function aliases (Windows POSIX-like function names)
 * - Missing POSIX functions (aligned_alloc, clock_gettime, gmtime_r)
 *
 * **Utility Macros**:
 *
 * - `PLATFORM_WINDOWS`: Defined as 1 on Windows, 0 on POSIX
 * - `PLATFORM_POSIX`: Defined as 1 on POSIX, 0 on Windows
 * - `PACKED_STRUCT_BEGIN/END`: Cross-platform packed struct support
 * - `ALIGNED_ATTR(x)`: Memory alignment attributes
 * - `THREAD_LOCAL`: Thread-local storage keyword
 * - `ALIGNED_32/ALIGNED_16`: Specific alignment macros
 * - `UNUSED(x)`: Suppress unused parameter warnings
 *
 * **Thread-Local Storage**:
 *
 * Provides unified thread-local storage support:
 * - **Windows MSVC**: `__declspec(thread)`
 * - **Windows Clang/GCC**: `__thread`
 * - **POSIX**: `__thread`
 *
 * **Initialization**:
 *
 * Before using platform functions, call `platform_init()` (required on Windows for
 * Winsock initialization). See `platform/system.h` for details.
 *
 * @par Example:
 * @code{.c}
 * #include "platform/abstraction.h"
 *
 * int main() {
 *     // Initialize platform (required on Windows)
 *     if (platform_init() != ASCIICHAT_OK) {
 *         return 1;
 *     }
 *
 *     // Use platform functions - no #ifdefs needed!
 *     socket_t sock = socket_create(AF_INET, SOCK_STREAM, 0);
 *     asciithread_t thread;
 *     ascii_thread_create(&thread, worker, NULL);
 *
 *     // Cleanup
 *     platform_cleanup();
 *     return 0;
 * }
 * @endcode
 *
 * @note This header includes all platform abstraction components. For fine-grained
 *       control, you can include individual component headers directly (e.g.,
 *       `platform/thread.h`, `platform/socket.h`).
 *
 * @note All platform-specific code (`#ifdef _WIN32`) is contained in this header and
 *       implementation files. Application code never needs platform conditionals.
 *
 * @see @ref topic_platform "Platform Abstraction Layer" for comprehensive documentation
 * @see platform/thread.h for threading primitives
 * @see platform/mutex.h for mutex operations
 * @see platform/socket.h for socket operations
 * @see platform/terminal.h for terminal I/O
 * @see platform/system.h for system functions
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

// ============================================================================
// Platform Detection
// ============================================================================

/**
 * @name Platform Detection Macros
 * @{
 */

#ifdef _WIN32
// Suppress Microsoft deprecation warnings for POSIX functions
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _CRT_NONSTDC_NO_WARNINGS
#define _CRT_NONSTDC_NO_WARNINGS
#endif

/** @brief Platform detection: 1 on Windows, 0 on POSIX
 *
 * Use this macro to conditionally compile Windows-specific code in the
 * platform implementation files. Application code should NOT use this
 * macro - use the unified API instead.
 *
 * @ingroup platform
 */
#define PLATFORM_WINDOWS 1

/** @brief Platform detection: 1 on POSIX, 0 on Windows
 *
 * Use this macro to conditionally compile POSIX-specific code in the
 * platform implementation files. Application code should NOT use this
 * macro - use the unified API instead.
 *
 * @ingroup platform
 */
#define PLATFORM_POSIX 0
#else
/** @brief Platform detection: 1 on Windows, 0 on POSIX */
#define PLATFORM_WINDOWS 0
/** @brief Platform detection: 1 on POSIX, 0 on Windows */
#define PLATFORM_POSIX 1
#endif

/** @} */

// ============================================================================
// Compiler Attributes
// ============================================================================

/**
 * @name Compiler Attribute Macros
 * @{
 */

#ifdef _WIN32
// MSVC doesn't support __attribute__((packed)) - use #pragma pack instead

/** @brief Begin a packed structure (Windows: #pragma pack(push, 1))
 *
 * Use this macro before a structure definition to pack it to 1-byte alignment.
 * Must be paired with PACKED_STRUCT_END after the structure.
 *
 * @par Example:
 * @code{.c}
 * PACKED_STRUCT_BEGIN
 * typedef struct {
 *     uint8_t  field1;
 *     uint16_t field2;
 * } packed_struct_t;
 * PACKED_STRUCT_END
 * // sizeof(packed_struct_t) == 3 (not 4 with padding)
 * @endcode
 *
 * @ingroup platform
 */
#define PACKED_STRUCT_BEGIN __pragma(pack(push, 1))

/** @brief End a packed structure (Windows: #pragma pack(pop))
 *
 * Use this macro after a structure definition to restore default packing.
 * Must be paired with PACKED_STRUCT_BEGIN before the structure.
 *
 * @ingroup platform
 */
#define PACKED_STRUCT_END __pragma(pack(pop))

/** @brief Packed structure attribute (Windows: empty, handled by pragma)
 *
 * On Windows, packing is handled by PACKED_STRUCT_BEGIN/END macros.
 * This macro is provided for compatibility but is a no-op.
 *
 * @ingroup platform
 */
#define PACKED_ATTR

/** @brief Memory alignment attribute (Windows: __declspec(align))
 * @param x Alignment in bytes (must be power of 2)
 *
 * Aligns a variable or structure member to the specified byte boundary.
 *
 * @par Example:
 * @code{.c}
 * ALIGNED_ATTR(32) uint8_t buffer[1024];  // 32-byte aligned
 * @endcode
 *
 * @ingroup platform
 */
#define ALIGNED_ATTR(x) __declspec(align(x))
#else
// GCC/Clang packed struct support

/** @brief Begin a packed structure (POSIX: no-op, uses attribute) */
#define PACKED_STRUCT_BEGIN
/** @brief End a packed structure (POSIX: no-op, uses attribute) */
#define PACKED_STRUCT_END

/** @brief Packed structure attribute (POSIX: __attribute__((packed)))
 *
 * On POSIX, use this attribute directly on structure definitions.
 *
 * @par Example:
 * @code{.c}
 * typedef struct {
 *     uint8_t  field1;
 *     uint16_t field2;
 * } PACKED_ATTR packed_struct_t;
 * @endcode
 *
 * @ingroup platform
 */
#define PACKED_ATTR __attribute__((packed))

/** @brief Memory alignment attribute (POSIX: __attribute__((aligned)))
 * @param x Alignment in bytes (must be power of 2)
 *
 * @ingroup platform
 */
#define ALIGNED_ATTR(x) __attribute__((aligned(x)))
#endif

/** @} */

// ============================================================================
// Standard Headers
// ============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// ============================================================================
// Platform Abstraction Modules
// ============================================================================

#include "platform/thread.h"
#include "platform/mutex.h"
#include "platform/rwlock.h"
#include "platform/cond.h"
// Windows socket shutdown constants - define before socket.h includes winsock2.h
#ifdef _WIN32
#ifndef SHUT_RDWR
/** @brief Socket shutdown: Read direction (Windows compatibility) - @ingroup platform */
#define SHUT_RD 0
/** @brief Socket shutdown: Write direction (Windows compatibility) - @ingroup platform */
#define SHUT_WR 1
/** @brief Socket shutdown: Both directions (Windows compatibility) - @ingroup platform */
#define SHUT_RDWR 2
#endif
#endif

#include "platform/socket.h"
#include "platform/terminal.h"
#include "platform/system.h"
#include "util/uthash.h" // Wrapper ensures common.h is included first
#include "debug/lock.h"
#include "platform/file.h"
#include "platform/pipe.h"

// ============================================================================
// Thread-Local Storage and Alignment Macros
// ============================================================================

/**
 * @name Thread-Local Storage and Alignment Macros
 * @{
 */

#ifdef _WIN32
// Windows-specific thread-local storage and alignment
#ifdef _MSC_VER
/** @brief Thread-local storage keyword (MSVC: __declspec(thread))
 *
 * Declares a variable with thread-local storage duration. Each thread has its
 * own independent copy of the variable.
 *
 * @par Example:
 * @code{.c}
 * THREAD_LOCAL int thread_local_var = 0;
 * // Each thread has its own copy of thread_local_var
 * @endcode
 *
 * @note Thread-local variables are initialized once per thread on first access.
 *
 * @ingroup platform
 */
#define THREAD_LOCAL __declspec(thread)

/** @brief 32-byte alignment macro (MSVC: __declspec(align(32)))
 *
 * Aligns a variable or structure member to 32-byte boundary. Useful for
 * SIMD operations (AVX/AVX2 require 32-byte alignment).
 *
 * @ingroup platform
 */
#define ALIGNED_32 __declspec(align(32))

/** @brief 16-byte alignment macro (MSVC: __declspec(align(16)))
 *
 * Aligns a variable or structure member to 16-byte boundary. Useful for
 * SIMD operations (SSE requires 16-byte alignment).
 *
 * @ingroup platform
 */
#define ALIGNED_16 __declspec(align(16))
#else
// Clang on Windows
/** @brief Thread-local storage keyword (Clang/GCC: __thread)
 *
 * @ingroup platform
 */
#define THREAD_LOCAL __thread
/** @brief 32-byte alignment macro (Clang/GCC: __attribute__((aligned(32))))
 *
 * @ingroup platform
 */
#define ALIGNED_32 __attribute__((aligned(32)))
/** @brief 16-byte alignment macro (Clang/GCC: __attribute__((aligned(16))))
 *
 * @ingroup platform
 */
#define ALIGNED_16 __attribute__((aligned(16)))
#endif
#else
// POSIX thread-local storage and alignment
#ifndef THREAD_LOCAL
/** @brief Thread-local storage keyword (POSIX: __thread)
 *
 * @ingroup platform
 */
#define THREAD_LOCAL __thread
/** @brief 32-byte alignment macro (POSIX: __attribute__((aligned(32))))
 *
 * @ingroup platform
 */
#define ALIGNED_32 __attribute__((aligned(32)))
/** @brief 16-byte alignment macro (POSIX: __attribute__((aligned(16))))
 *
 * @ingroup platform
 */
#define ALIGNED_16 __attribute__((aligned(16)))
#endif
#endif

/** @} */

// ============================================================================
// Platform-Specific Compatibility
// ============================================================================

#ifdef _WIN32
// Windows-specific compatibility

/**
 * @name Windows POSIX Compatibility Definitions
 * @{
 */

#ifndef PATH_MAX
/** @brief Maximum path length (Windows: 260 bytes)
 *
 * Standard Windows path length limit (legacy MAX_PATH). This is the legacy limit,
 * but extended paths (up to 32,767 characters) are supported with proper prefixes.
 *
 * @note See PLATFORM_MAX_PATH_LENGTH in system.h for extended path support.
 * @note This is the legacy Windows path limit. Modern Windows supports longer
 *       paths with the \\?\ prefix.
 *
 * @ingroup platform
 */
#define PATH_MAX 260
#endif

// POSIX-style file permissions (not used on Windows, provided for compatibility)
#ifndef S_IRUSR
/** @brief File permission: User read (Windows compatibility) - @ingroup platform */
#define S_IRUSR 0400
/** @brief File permission: User write (Windows compatibility) - @ingroup platform */
#define S_IWUSR 0200
/** @brief File permission: User execute (Windows compatibility) - @ingroup platform */
#define S_IXUSR 0100
/** @brief File permission: Group read (Windows compatibility) - @ingroup platform */
#define S_IRGRP 0040
/** @brief File permission: Group write (Windows compatibility) - @ingroup platform */
#define S_IWGRP 0020
/** @brief File permission: Group execute (Windows compatibility) - @ingroup platform */
#define S_IXGRP 0010
/** @brief File permission: Other read (Windows compatibility) - @ingroup platform */
#define S_IROTH 0004
/** @brief File permission: Other write (Windows compatibility) - @ingroup platform */
#define S_IWOTH 0002
/** @brief File permission: Other execute (Windows compatibility) - @ingroup platform */
#define S_IXOTH 0001
#endif

// Signal constants
#ifndef SIGPIPE
/** @brief Broken pipe signal (Windows compatibility)
 *
 * Signal constant for broken pipe errors. On POSIX, this signal is sent
 * when writing to a pipe with no readers. On Windows, defined for compatibility.
 *
 * @ingroup platform
 */
#define SIGPIPE 13
#endif

// Standard file descriptor constants
#ifndef STDIN_FILENO
/** @brief Standard input file descriptor (Windows compatibility) - @ingroup platform */
#define STDIN_FILENO 0
/** @brief Standard output file descriptor (Windows compatibility) - @ingroup platform */
#define STDOUT_FILENO 1
/** @brief Standard error file descriptor (Windows compatibility) - @ingroup platform */
#define STDERR_FILENO 2
#endif

// Windows socket types
/** @brief Number of file descriptors type (Windows: unsigned long)
 *
 * Type for pollfd array sizes. On Windows, defined as unsigned long.
 *
 * @ingroup platform
 */
typedef unsigned long nfds_t;

// Windows socket control codes
#ifndef SIO_KEEPALIVE_VALS
/** @brief Socket IO control: TCP keepalive values (Windows)
 *
 * Windows-specific socket IO control code for setting TCP keepalive parameters.
 *
 * @ingroup platform
 */
#define SIO_KEEPALIVE_VALS _WSAIOW(IOC_VENDOR, 4)
#endif

/** @} */

/**
 * @name Windows POSIX Function Aliases
 * @{
 */

// Use platform-safe write wrapper
#ifndef write
/** @brief Write function alias (Windows: _write)
 *
 * On Windows, provides POSIX write() by aliasing to _write().
 * Application code should use platform_write() instead.
 *
 * @ingroup platform
 */
#define write _write
#endif

// Windows headers for POSIX-like functions
#include <io.h>
#include <fcntl.h>
#include <process.h>

// POSIX function aliases for Windows
/** @brief Close function alias (Windows: _close) - Use platform functions instead
 *
 * @ingroup platform
 */
#define close _close
/** @brief Lseek function alias (Windows: _lseek) - Use platform functions instead
 *
 * @ingroup platform
 */
#define lseek _lseek
/** @brief Read function alias (Windows: _read) - Use platform functions instead
 *
 * @ingroup platform
 */
#define read _read
/** @brief Unlink function alias (Windows: _unlink) - Use platform functions instead
 *
 * @ingroup platform
 */
#define unlink _unlink

// Additional POSIX function aliases for Windows
/** @brief Isatty function alias (Windows: _isatty) - Use platform_isatty() instead
 *
 * @ingroup platform
 */
#define isatty _isatty
/** @brief Getpid function alias (Windows: _getpid) - Use platform_get_pid() instead
 *
 * @ingroup platform
 */
#define getpid _getpid

/** @} */
/**
 * @name Windows POSIX Compatibility Functions
 * @{
 */

/**
 * @brief Aligned memory allocation (Windows compatibility)
 * @param alignment Memory alignment (must be power of 2)
 * @param size Number of bytes to allocate
 * @return Pointer to aligned memory, or NULL on error
 *
 * Provides POSIX aligned_alloc() on Windows. Memory is aligned to the specified
 * boundary and must be freed with free().
 *
 * @note Alignment must be a power of 2.
 * @note Size must be a multiple of alignment.
 *
 * @ingroup platform
 */
void *aligned_alloc(size_t alignment, size_t size);

/**
 * @brief Get time from specified clock (Windows compatibility)
 * @param clk_id Clock ID (CLOCK_REALTIME, CLOCK_MONOTONIC)
 * @param tp Pointer to timespec structure to receive time
 * @return 0 on success, -1 on error
 *
 * Provides POSIX clock_gettime() on Windows. Supports CLOCK_REALTIME and
 * CLOCK_MONOTONIC clock IDs.
 *
 * @ingroup platform
 */
int clock_gettime(int clk_id, struct timespec *tp);

/**
 * @brief Thread-safe GMT time conversion (Windows compatibility)
 * @param timep Pointer to time_t value
 * @param result Pointer to struct tm to receive result
 * @return Pointer to result structure, or NULL on error
 *
 * Provides POSIX gmtime_r() on Windows. Thread-safe version of gmtime()
 * that writes to the provided result structure instead of a static buffer.
 *
 * @ingroup platform
 */
struct tm *gmtime_r(const time_t *timep, struct tm *result);

/** @} */

/**
 * @brief Microseconds type (Windows compatibility)
 *
 * Type definition for microsecond values. Provided for compatibility with
 * POSIX code that uses useconds_t.
 *
 * @ingroup platform
 */
typedef unsigned int useconds_t;

/**
 * @name Windows POSIX Compatibility Constants
 * @{
 */

// Missing POSIX file flags for Windows
#ifndef O_CLOEXEC
/** @brief Close-on-exec file flag (Windows: no-op, always 0)
 *
 * On POSIX, this flag causes the file descriptor to be closed when exec()
 * is called. On Windows, this concept doesn't apply (no exec()), so it's
 * always 0.
 *
 * @ingroup platform
 */
#define O_CLOEXEC 0
#endif

// Missing POSIX time functions and constants for Windows
#include <time.h>
#ifndef CLOCK_REALTIME
/** @brief Realtime clock ID (Windows compatibility)
 *
 * Clock ID for system-wide realtime clock. Provided for compatibility
 * with POSIX code using clock_gettime().
 *
 * @ingroup platform
 */
#define CLOCK_REALTIME 0
#ifndef CLOCK_MONOTONIC
/** @brief Monotonic clock ID (Windows compatibility)
 *
 * Clock ID for monotonic clock (not affected by system time changes).
 * Provided for compatibility with POSIX code using clock_gettime().
 *
 * @ingroup platform
 */
#define CLOCK_MONOTONIC 1
#endif
#endif

/** @} */

#else
// POSIX-specific includes
#include <unistd.h>
#include <limits.h>
#endif

// ============================================================================
// Cross-Platform Sleep Functions
// ============================================================================

/**
 * @name Cross-Platform Utility Functions
 * @{
 */

/**
 * @brief High-precision sleep function with microsecond precision
 * @param usec Number of microseconds to sleep
 *
 * Sleeps the current thread for the specified number of microseconds with
 * high precision. Supports early wakeup via shutdown signaling.
 *
 * @note On Windows, Sleep() has ~15ms minimum resolution. This function
 *       uses more precise timing mechanisms for microsecond-level accuracy.
 * @note On POSIX, uses usleep() or nanosleep() for microsecond precision.
 *
 * @par Example:
 * @code{.c}
 * platform_sleep_usec(1000);  // Sleep for 1000 microseconds (1 millisecond)
 * @endcode
 *
 * @ingroup platform
 */
void platform_sleep_usec(unsigned int usec);

/**
 * @brief Platform-safe write function
 * @param fd File descriptor to write to
 * @param buf Buffer containing data to write
 * @param count Number of bytes to write
 * @return Number of bytes written on success, -1 on error
 *
 * Cross-platform write function that handles Windows-specific quirks
 * (e.g., CRLF line endings) and provides consistent behavior across platforms.
 *
 * @note On Windows, automatically handles line ending conversion if needed.
 * @note On POSIX, equivalent to standard write().
 *
 * @ingroup platform
 */
ssize_t platform_write(int fd, const void *buf, size_t count);

/** @} */

// ============================================================================
// Utility Macros
// ============================================================================

/**
 * @name Utility Macros
 * @{
 */

/**
 * @brief Suppress unused parameter warnings
 * @param x Parameter name to mark as unused
 *
 * Use this macro to suppress compiler warnings about unused function parameters.
 * Especially useful for callback functions where not all parameters are used.
 *
 * @par Example:
 * @code{.c}
 * void callback(void *data, int flags) {
 *     UNUSED(flags);  // Suppress warning about unused 'flags' parameter
 *     process_data(data);
 * }
 * @endcode
 *
 * @note This macro casts the parameter to void, which effectively tells the
 *       compiler that the parameter is intentionally unused.
 *
 * @ingroup platform
 */
#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif

/** @} */

// ============================================================================
// Restore Default Packing for Application Code
// ============================================================================
// NOTE: windows_compat.h now handles pack(pop) automatically
