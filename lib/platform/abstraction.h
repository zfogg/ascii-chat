#pragma once

/**
 * @file abstraction.h
 * @brief Cross-platform abstraction layer for ASCII-Chat
 *
 * This header provides a comprehensive abstraction layer that enables ASCII-Chat
 * to run seamlessly on Windows, Linux, and macOS.
 *
 * The abstraction layer is organized into modular components:
 * - Threading primitives (thread.h)
 * - Socket operations (socket.h)
 * - Terminal I/O operations (terminal.h)
 * - String and memory functions (string.h)
 * - System functions (system.h)
 * - File I/O operations (file.h)
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

// ============================================================================
// Platform Detection
// ============================================================================

#ifdef _WIN32
// Suppress Microsoft deprecation warnings for POSIX functions
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _CRT_NONSTDC_NO_WARNINGS
#define _CRT_NONSTDC_NO_WARNINGS
#endif
#define PLATFORM_WINDOWS 1
#define PLATFORM_POSIX 0
#else
#define PLATFORM_WINDOWS 0
#define PLATFORM_POSIX 1
#endif

// ============================================================================
// Compiler Attributes
// ============================================================================

#ifdef _WIN32
// MSVC doesn't support __attribute__((packed)) - use #pragma pack instead
#define PACKED_STRUCT_BEGIN __pragma(pack(push, 1))
#define PACKED_STRUCT_END __pragma(pack(pop))
#define PACKED_ATTR
#define ALIGNED_ATTR(x) __declspec(align(x))
#else
// GCC/Clang packed struct support
#define PACKED_STRUCT_BEGIN
#define PACKED_STRUCT_END
#define PACKED_ATTR __attribute__((packed))
#define ALIGNED_ATTR(x) __attribute__((aligned(x)))
#endif

// ============================================================================
// Standard Headers
// ============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>

// ============================================================================
// Platform Abstraction Modules
// ============================================================================

// Threading primitives
#include "platform/thread.h"
#include "platform/mutex.h"
#include "platform/rwlock.h"
#include "platform/cond.h"

// Socket operations
#include "platform/socket.h"

// Terminal I/O
#include "platform/terminal.h"

// System functions
#include "platform/system.h"

// Lock debugging
#include "lock_debug.h"

// File I/O
#include "platform/file.h"

// ============================================================================
// Thread-Local Storage and Alignment Macros
// ============================================================================

#ifdef _WIN32
// Windows-specific thread-local storage and alignment
#ifdef _MSC_VER
#define THREAD_LOCAL __declspec(thread)
#define ALIGNED_32 __declspec(align(32))
#define ALIGNED_16 __declspec(align(16))
#else
// Clang on Windows
#define THREAD_LOCAL __thread
#define ALIGNED_32 __attribute__((aligned(32)))
#define ALIGNED_16 __attribute__((aligned(16)))
#endif
#else
// POSIX thread-local storage and alignment
#ifndef THREAD_LOCAL
#define THREAD_LOCAL __thread
#define ALIGNED_32 __attribute__((aligned(32)))
#define ALIGNED_16 __attribute__((aligned(16)))
#endif
#endif

// ============================================================================
// Platform-Specific Compatibility
// ============================================================================

#ifdef _WIN32
// Windows-specific compatibility
#ifndef PATH_MAX
#define PATH_MAX 260
#endif

// POSIX-style file permissions (not used on Windows)
#ifndef S_IRUSR
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001
#endif

// Signal constants
#ifndef SIGPIPE
#define SIGPIPE 13
#endif

// Standard file descriptor constants
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
// Windows socket types
typedef unsigned long nfds_t;
// Windows socket shutdown constants
#ifndef SHUT_RDWR
#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2
// Windows socket control codes
#ifndef SIO_KEEPALIVE_VALS
#define SIO_KEEPALIVE_VALS _WSAIOW(IOC_VENDOR, 4)
#endif
#endif
#endif

// Use platform-safe write wrapper
#ifndef write
#define write _write
#endif
// Windows headers for POSIX-like functions
#include <io.h>
#include <fcntl.h>
#include <process.h>
// POSIX function aliases for Windows
#define close _close
#define lseek _lseek
#define read _read
#define unlink _unlink
// Additional POSIX function aliases for Windows
#define isatty _isatty
#define getpid _getpid
// Windows aligned_alloc declaration (implemented in system.c)
void *aligned_alloc(size_t alignment, size_t size);
// Windows POSIX time function declarations (implemented in system.c)
int clock_gettime(int clk_id, struct timespec *tp);
struct tm *gmtime_r(const time_t *timep, struct tm *result);
// Windows usleep declaration (implemented in system.c)
int usleep(unsigned int usec);
// Define useconds_t for Windows
typedef unsigned int useconds_t;

// Missing POSIX file flags for Windows
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
// Missing POSIX time functions and constants for Windows
#include <time.h>
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif
#endif

#else
// POSIX-specific includes
#include <unistd.h>
#include <limits.h>
#endif

// ============================================================================
// Cross-Platform Sleep Functions
// ============================================================================

// High-precision sleep function with shutdown support
// Microsecond precision sleep
void platform_sleep_usec(unsigned int usec);

// Platform-safe write function
ssize_t platform_write(int fd, const void *buf, size_t count);


// ============================================================================
// Utility Macros
// ============================================================================

// Unused parameter macro
#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif

// ============================================================================
// Restore Default Packing for Application Code
// ============================================================================
// NOTE: windows_compat.h now handles pack(pop) automatically
