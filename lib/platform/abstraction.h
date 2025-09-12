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
 * @date January 2025
 */

// ============================================================================
// Platform Detection
// ============================================================================

#ifdef _WIN32
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

// String and memory functions
#include "platform/string.h"

// System functions
#include "platform/system.h"

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

// Re-define write after declarations on Windows only
#define write _write
#else
// POSIX-specific includes
#include <unistd.h>
#include <limits.h>
#endif

// ============================================================================
// Utility Macros
// ============================================================================

// Min/Max macros if not defined
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

// Array size macro
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

// Unused parameter macro
#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif
