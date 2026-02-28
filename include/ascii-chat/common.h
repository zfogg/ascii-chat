/**
 * @defgroup common Common Definitions
 * @ingroup module_core
 * @brief ⚙️ Code shared throughout the library
 *
 * @file common.h
 * @brief ⚙️ Common definitions, error codes, macros, and types shared throughout the application
 * @ingroup common
 * @addtogroup common
 * @{
 *
 * This header provides core functionality used throughout ascii-chat:
 * - Error codes (via common/error_codes.h)
 * - Protocol constants (via common/protocol_constants.h)
 * - Application limits (via common/limits.h)
 * - Buffer sizes (via common/buffer_sizes.h)
 * - Memory allocation macros (with mimalloc support)
 * - Thread and synchronization macros
 * - Utility macros (MIN, MAX, ARRAY_SIZE)
 * - Shutdown detection system
 * - Shared initialization function
 *
 * @note This header should be included early in other headers as it provides
 * fundamental types and macros that many other modules depend on.
 */

#pragma once

/* Note: string.h must come before any platform includes
 * that might use memcpy (used in unaligned access helpers below). */
#include <string.h> // For memcpy in unaligned access helpers

// DLL export/import macros (must be included first to avoid circular dependencies)
#include "platform/api.h" // IWYU pragma: keep

/* Feature test macros for POSIX functions */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h> // For malloc/free in ALLOC_* macros

/** @brief Application name for key comments ("ascii-chat") */
#define ASCII_CHAT_APP_NAME "ascii-chat"

#if (defined(__clang__) || defined(__GNUC__)) && !defined(__builtin_c23_va_start)
#define __builtin_c23_va_start(ap, param) __builtin_va_start(ap, param)
#endif

#if (defined(__clang__) || defined(__GNUC__)) && !defined(__builtin_c23_va_end)
#define __builtin_c23_va_end(ap) __builtin_va_end(ap)
#endif

// This fixes clangd errors about missing types. I DID include stdint.h, but
// it's not enough.
#ifndef UINT8_MAX
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
#endif

/* ============================================================================
 * Platform Maximum Path Length
 * ============================================================================
 * Defined here (before logging.h) to avoid circular dependencies.
 * Also defined in platform/system.h for documentation purposes.
 */
#ifdef _WIN32
#include <limits.h>
#ifndef PATH_MAX
#define PATH_MAX 260
#endif
// Windows extended-length path maximum (not the legacy 260 MAX_PATH)
#define PLATFORM_MAX_PATH_LENGTH 32767
#elif defined(__linux__)
#include <limits.h>
#ifndef PATH_MAX
#define PLATFORM_MAX_PATH_LENGTH 4096
#else
#define PLATFORM_MAX_PATH_LENGTH PATH_MAX
#endif
#elif defined(__APPLE__)
#include <sys/syslimits.h>
#ifndef PATH_MAX
#define PLATFORM_MAX_PATH_LENGTH 1024
#else
#define PLATFORM_MAX_PATH_LENGTH PATH_MAX
#endif
#else
// Fallback for unknown platforms
#define PLATFORM_MAX_PATH_LENGTH 4096
#endif

/* ============================================================================
 * Include organized sub-headers for specific domains
 * ============================================================================ */

#include "common/error_codes.h"        // Error codes and error_string function
#include "common/protocol_constants.h" // Protocol version, features, compression, frames
#include "common/limits.h"             // MAX_CLIENTS, FPS limits, display name length
#include "common/buffer_sizes.h"       // Standard buffer size constants
#include "common/log_rates.h"          // Logging rate limit constants
#include "common/shutdown.h"           // Shutdown detection system
#include "common/string_constants.h"   // String literal constants (STR_TRUE, STR_FALSE, etc.)

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration for asciichat_fatal_with_context - now after asciichat_error_t is defined */
void asciichat_fatal_with_context(asciichat_error_t code, const char *file, int line, const char *function,
                                  const char *format, ...);

#ifdef __cplusplus
}
#endif

/* ============================================================================
 * Fatal Error Macros - Exit with Error Message and Stack Trace
 * ============================================================================
 * These macros provide a convenient way to exit the program with a detailed
 * error message. In debug builds, they also print a stack trace.
 *
 * Usage in src/ code:
 *   FATAL(ERROR_WEBCAM, "Custom msg: %d", val);   // Error code + custom message
 */

/* Include platform system header for platform_print_backtrace */
#include "platform/system.h" // IWYU pragma: keep

/**
 * @brief Exit with error code and custom message, with stack trace in debug builds
 * @param code Error code (asciichat_error_t)
 * @param ... Custom message format string and arguments (printf-style)
 *
 * @note In debug builds, includes file, line, and function information.
 *       In release builds, file/line/function are omitted to reduce binary size.
 *
 * @warning This macro terminates the program. Use only for unrecoverable errors.
 *
 * @par Example:
 * @code{.c}
 * FATAL(ERROR_NETWORK_BIND, "Cannot bind to port %d", port_number);
 * @endcode
 *
 * @ingroup common
 */
#ifdef NDEBUG
#define FATAL(code, ...) asciichat_fatal_with_context(code, NULL, 0, NULL, ##__VA_ARGS__)
#else
#define FATAL(code, ...) asciichat_fatal_with_context(code, __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#endif

/* ============================================================================
 * Utility Macros
 * ============================================================================ */

/* Common utility macros */
/** @brief Return the minimum of two values */
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

/** @brief Return the maximum of two values */
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

/** @brief Calculate the number of elements in an array */
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

/* ============================================================================
 * Burst and Throttle Macro
 * ============================================================================
 *
 * @brief Rate-limit code execution with a burst window followed by throttle
 *
 * Allows code to execute frequently for an initial burst period, then refuses
 * to execute again until a throttle period has elapsed since first invocation.
 * The timer resets when called after the throttle period expires.
 *
 * Uses time_get_ns() from util/time.h for high-resolution monotonic timing.
 *
 * Example (backtrace every 500ms, then silence for 10 seconds):
 *   RUN_BURST_AND_THROTTLE(500 * NS_PER_MS_INT, 10 * NS_PER_SEC_INT, {
 *     platform_print_backtrace(1);
 *   });
 *
 * Time values in nanoseconds (use NS_PER_MS_INT, NS_PER_SEC_INT from util/time.h):
 *   - 500 milliseconds = 500 * NS_PER_MS_INT
 *   - 10 seconds = 10 * NS_PER_SEC_INT
 *
 * @param burst_ns    Allow execution for this many nanoseconds
 * @param throttle_ns Refuse execution until this many nanoseconds pass
 * @param code_block  Code to execute when throttle conditions permit (use braces: { ... })
 */
#define RUN_BURST_AND_THROTTLE(burst_ns, throttle_ns, code_block)                                                      \
  do {                                                                                                                 \
    static uint64_t burst_start = 0;                                                                                   \
    uint64_t now = time_get_ns();                                                                                      \
                                                                                                                       \
    /* First call or timer reset */                                                                                    \
    if (burst_start == 0) {                                                                                            \
      burst_start = now;                                                                                               \
      code_block;                                                                                                      \
    } else {                                                                                                           \
      uint64_t elapsed_ns = now - burst_start;                                                                         \
                                                                                                                       \
      if (elapsed_ns < (uint64_t)(burst_ns)) {                                                                         \
        /* Within burst window */                                                                                      \
        code_block;                                                                                                    \
      } else if (elapsed_ns >= (uint64_t)(throttle_ns)) {                                                              \
        /* Throttle period complete, reset and execute */                                                              \
        burst_start = now;                                                                                             \
        code_block;                                                                                                    \
      }                                                                                                                \
      /* else: in throttle period, do nothing */                                                                       \
    }                                                                                                                  \
  } while (0)

/* ============================================================================
 * Memory Allocation with mimalloc Support
 * ============================================================================
 * When USE_MIMALLOC is enabled:
 * - MI_OVERRIDE=ON (glibc): malloc/free automatically redirected to mimalloc
 * - MI_OVERRIDE=OFF (musl): must explicitly use mi_malloc/mi_free
 */

#ifdef USE_MIMALLOC
#include <mimalloc.h>
#define ALLOC_MALLOC(size) mi_malloc(size)
#define ALLOC_CALLOC(count, size) mi_calloc((count), (size))
#define ALLOC_REALLOC(ptr, size) mi_realloc((ptr), (size))
#define ALLOC_FREE(ptr) mi_free(ptr)
#elif defined(DEBUG_MEMORY) && !defined(NDEBUG)
#include "debug/memory.h"
#define ALLOC_MALLOC(size) debug_malloc(size, __FILE__, __LINE__)
#define ALLOC_CALLOC(count, size) debug_calloc((count), (size), __FILE__, __LINE__)
#define ALLOC_REALLOC(ptr, size) debug_realloc((ptr), (size), __FILE__, __LINE__)
#define ALLOC_FREE(ptr) debug_free(ptr, __FILE__, __LINE__)
#else
#define ALLOC_MALLOC(size) malloc(size)
#define ALLOC_CALLOC(count, size) calloc((count), (size))
#define ALLOC_REALLOC(ptr, size) realloc((ptr), (size))
#define ALLOC_FREE(ptr) free(ptr)
#endif

/**
 * @name Memory Debugging Macros
 * @ingroup common
 * @{
 */

/* Safe memory allocation with error checking - returns allocated pointer */
#define SAFE_MALLOC(size, cast)                                                                                        \
  ({                                                                                                                   \
    cast _ptr = (cast)ALLOC_MALLOC(size);                                                                              \
    if (!_ptr) {                                                                                                       \
      FATAL(ERROR_MEMORY, "Memory allocation failed: %zu bytes", (size_t)(size));                                      \
    }                                                                                                                  \
    _ptr;                                                                                                              \
  })

/* Safe zero-initialized memory allocation */
#define SAFE_CALLOC(count, size, cast)                                                                                 \
  ({                                                                                                                   \
    cast _ptr = (cast)ALLOC_CALLOC((count), (size));                                                                   \
    if (!_ptr) {                                                                                                       \
      FATAL(ERROR_MEMORY, "Memory allocation failed: %zu elements x %zu bytes", (size_t)(count), (size_t)(size));      \
    }                                                                                                                  \
    _ptr;                                                                                                              \
  })

/* Safe memory reallocation */
#define SAFE_REALLOC(ptr, size, cast)                                                                                  \
  ({                                                                                                                   \
    void *tmp_ptr = ALLOC_REALLOC((ptr), (size));                                                                      \
    if (!tmp_ptr) {                                                                                                    \
      FATAL(ERROR_MEMORY, "Memory reallocation failed: %zu bytes", (size_t)(size));                                    \
    }                                                                                                                  \
    (cast)(tmp_ptr);                                                                                                   \
  })

/* Helper macro to track aligned allocations when DEBUG_MEMORY is enabled */
#if defined(DEBUG_MEMORY) && !defined(MI_MALLOC_OVERRIDE)
#ifdef NDEBUG
/* Production build: don't include file/line info */
#define TRACK_ALIGNED_ALLOC(ptr, size, file, line) debug_track_aligned((void *)(ptr), (size_t)(size), NULL, 0)
#else
/* Debug build: include file/line info */
#define TRACK_ALIGNED_ALLOC(ptr, size, file, line) debug_track_aligned((void *)(ptr), (size_t)(size), (file), (line))
#endif
#else
#define TRACK_ALIGNED_ALLOC(ptr, size, file, line) ((void)0)
#endif

/* SIMD-aligned memory allocation macros for optimal NEON/AVX performance */
#ifdef USE_MIMALLOC
/* Use mimalloc's aligned allocation (works on all platforms) */
#define SAFE_MALLOC_ALIGNED(size, alignment, cast)                                                                     \
  ({                                                                                                                   \
    cast _ptr = (cast)mi_malloc_aligned((size), (alignment));                                                          \
    if (!_ptr) {                                                                                                       \
      FATAL(ERROR_MEMORY, "Aligned memory allocation failed: %zu bytes, %zu alignment", (size_t)(size),                \
            (size_t)(alignment));                                                                                      \
    }                                                                                                                  \
    TRACK_ALIGNED_ALLOC(_ptr, (size), __FILE__, __LINE__);                                                             \
    _ptr;                                                                                                              \
  })
#else
/* Fall back to platform-specific aligned allocation when mimalloc is disabled */
#ifdef _WIN32
/* Windows uses _aligned_malloc() for aligned allocation */
#include <malloc.h>
#define SAFE_MALLOC_ALIGNED(size, alignment, cast)                                                                     \
  ({                                                                                                                   \
    cast _ptr = (cast)_aligned_malloc((size), (alignment));                                                            \
    if (!_ptr) {                                                                                                       \
      FATAL(ERROR_MEMORY, "Aligned memory allocation failed: %zu bytes, %zu alignment", (size_t)(size),                \
            (size_t)(alignment));                                                                                      \
    }                                                                                                                  \
    TRACK_ALIGNED_ALLOC(_ptr, (size), __FILE__, __LINE__);                                                             \
    _ptr;                                                                                                              \
  })
#elif defined(__APPLE__)
/* macOS uses posix_memalign() for aligned allocation */
#define SAFE_MALLOC_ALIGNED(size, alignment, cast)                                                                     \
  ({                                                                                                                   \
    cast _ptr;                                                                                                         \
    int result = posix_memalign((void **)&_ptr, (alignment), (size));                                                  \
    if (result != 0 || !_ptr) {                                                                                        \
      FATAL(ERROR_MEMORY, "Aligned memory allocation failed: %zu bytes, %zu alignment", (size_t)(size),                \
            (size_t)(alignment));                                                                                      \
    }                                                                                                                  \
    TRACK_ALIGNED_ALLOC(_ptr, (size), __FILE__, __LINE__);                                                             \
    _ptr;                                                                                                              \
  })
#else
/* Linux/other platforms use aligned_alloc() (C11) */
#define SAFE_MALLOC_ALIGNED(size, alignment, cast)                                                                     \
  ({                                                                                                                   \
    size_t aligned_size = (((size) + (alignment) - 1) / (alignment)) * (alignment);                                    \
    cast _ptr = (cast)aligned_alloc((alignment), aligned_size);                                                        \
    if (!_ptr) {                                                                                                       \
      FATAL(ERROR_MEMORY, "Aligned memory allocation failed: %zu bytes, %zu alignment", aligned_size,                  \
            (size_t)(alignment));                                                                                      \
    }                                                                                                                  \
    TRACK_ALIGNED_ALLOC(_ptr, aligned_size, __FILE__, __LINE__);                                                       \
    _ptr;                                                                                                              \
  })
#endif
#endif

/* 16-byte aligned allocation for SIMD operations */
#define SAFE_MALLOC_SIMD(size, cast) SAFE_MALLOC_ALIGNED(size, 16, cast)

/* 16-byte aligned zero-initialized allocation */
#define SAFE_CALLOC_SIMD(count, size, cast)                                                                            \
  ({                                                                                                                   \
    size_t total_size = (count) * (size);                                                                              \
    cast _ptr = SAFE_MALLOC_SIMD(total_size, cast);                                                                    \
    memset(_ptr, 0, total_size);                                                                                       \
    _ptr;                                                                                                              \
  })

/* Safe free that nulls the pointer - available in all builds */
#define SAFE_FREE(ptr)                                                                                                 \
  do {                                                                                                                 \
    if ((ptr) != NULL) {                                                                                               \
      ALLOC_FREE((void *)(ptr));                                                                                       \
      (ptr) = NULL;                                                                                                    \
    }                                                                                                                  \
  } while (0)

/* Safe fclose that nulls the pointer - for use with defer() */
/* Usage: defer(SAFE_FCLOSE(fp)); */
#define SAFE_FCLOSE(fp)                                                                                                \
  do {                                                                                                                 \
    if ((fp) != NULL) {                                                                                                \
      fclose(fp);                                                                                                      \
      (fp) = NULL;                                                                                                     \
    }                                                                                                                  \
  } while (0)

/* Untracked malloc/free - bypass memory tracking for special cases like mode_argv */
/* These use raw malloc/free to avoid appearing in leak reports */
#define UNTRACKED_MALLOC(size, cast)                                                                                   \
  ({                                                                                                                   \
    cast _ptr = (cast)malloc(size);                                                                                    \
    if (!_ptr) {                                                                                                       \
      FATAL(ERROR_MEMORY, "Memory allocation failed: %zu bytes", (size_t)(size));                                      \
    }                                                                                                                  \
    _ptr;                                                                                                              \
  })

#define UNTRACKED_FREE(ptr)                                                                                            \
  do {                                                                                                                 \
    if ((ptr) != NULL) {                                                                                               \
      free((void *)(ptr));                                                                                             \
      (ptr) = NULL;                                                                                                    \
    }                                                                                                                  \
  } while (0)

/* Safe string copy */
#define SAFE_STRNCPY(dst, src, size) platform_strlcpy((dst), (src), (size))

#include "asciichat_errno.h"
/* Safe string duplication with memory tracking */
#define SAFE_STRDUP(dst, src)                                                                                          \
  do {                                                                                                                 \
    if (src) {                                                                                                         \
      size_t _len = strlen(src) + 1;                                                                                   \
      (dst) = SAFE_MALLOC(_len, char *);                                                                               \
      if (dst) {                                                                                                       \
        SAFE_MEMCPY((dst), _len, (src), _len);                                                                         \
      } else {                                                                                                         \
        SET_ERRNO(ERROR_MEMORY, "String duplication failed for: %s", (src));                                           \
      }                                                                                                                \
    } else {                                                                                                           \
      (dst) = NULL;                                                                                                    \
    }                                                                                                                  \
  } while (0)

/* Platform-safe environment variable access */
#define SAFE_GETENV(name) platform_getenv(name)

/* Platform-safe sscanf (only used with numeric format specifiers) */
#ifdef _WIN32
#define SAFE_SSCANF(str, format, ...) sscanf_s(str, format, __VA_ARGS__)
#else
#define SAFE_SSCANF(str, format, ...) sscanf(str, format, __VA_ARGS__)
#endif

/* Platform-safe strerror */
#include "platform/abstraction.h" // IWYU pragma: keep
#define SAFE_STRERROR(errnum) platform_strerror(errnum)

/* Safe memory functions */
#define SAFE_MEMCPY(dest, dest_size, src, count) platform_memcpy((dest), (dest_size), (src), (count))
#define SAFE_MEMSET(dest, dest_size, ch, count) platform_memset((dest), (dest_size), (ch), (count))
#define SAFE_MEMMOVE(dest, dest_size, src, count) platform_memmove((dest), (dest_size), (src), (count))
#define SAFE_STRCPY(dest, dest_size, src) platform_strcpy((dest), (dest_size), (src))

/* ============================================================================
 * Unaligned Memory Access Helpers (Backward Compatibility)
 * ============================================================================
 * NOTE: These functions have been moved to util/bytes.h
 *       This header includes util/bytes.h and provides backward compatibility
 *       macros for existing code using the old names.
 */

#include "util/bytes.h"

/* Backward compatibility aliases for existing code */
#define read_u16_unaligned bytes_read_u16_unaligned
#define read_u32_unaligned bytes_read_u32_unaligned
#define write_u16_unaligned bytes_write_u16_unaligned
#define write_u32_unaligned bytes_write_u32_unaligned
#define safe_size_mul bytes_safe_size_mul

/* Safe string formatting */
// clang-format off
#define SAFE_SNPRINTF(buffer, buffer_size, ...) (size_t)safe_snprintf((buffer), (buffer_size), __VA_ARGS__)
// clang-format on

/**
 * @brief Safe buffer size calculation for snprintf
 *
 * Casts offset to size_t to avoid sign conversion warnings when subtracting from buffer_size.
 * Returns 0 if offset is negative or >= buffer_size (prevents underflow).
 */
#define SAFE_BUFFER_SIZE(buffer_size, offset)                                                                          \
  ((offset) < 0 || (size_t)(offset) >= (buffer_size) ? 0 : (buffer_size) - (size_t)(offset))

/* ============================================================================
 * Thread Creation and Synchronization Macros
 * ============================================================================ */

/**
 * @brief Create a thread or log error and return
 * @param thread Pointer to thread_t variable
 * @param func Thread function (should match void* (*)(void*) signature)
 * @param arg Thread argument
 *
 * Handles common pattern: create thread, log error if failed, then return -1.
 * Example usage:
 *
 *   THREAD_CREATE_OR_RETURN(thread, thread_func, arg);
 *   // Thread created successfully, continue
 *
 * @ingroup common
 */
#define THREAD_CREATE_OR_RETURN(thread, func, arg)                                                                     \
  do {                                                                                                                 \
    if (asciichat_thread_create(&(thread), (func), (arg)) != 0) {                                                      \
      log_error("Failed to create thread: %s", #func);                                                                 \
      return -1;                                                                                                       \
    }                                                                                                                  \
  } while (0)

/**
 * @brief Initialize a mutex or log error and return
 * @param m Pointer to mutex_t variable
 *
 * Handles common pattern: init mutex, log error if failed, then return -1.
 * Example usage:
 *
 *   MUTEX_INIT_OR_RETURN(mutex);
 *   // Mutex initialized successfully, continue
 *
 * @ingroup common
 */
#define MUTEX_INIT_OR_RETURN(m)                                                                                        \
  do {                                                                                                                 \
    if (mutex_init(&(m)) != 0) {                                                                                       \
      log_error("Failed to initialize mutex: %s", #m);                                                                 \
      return -1;                                                                                                       \
    }                                                                                                                  \
  } while (0)

/* Include logging.h to provide logging macros to all files that include common.h */
#include "log/log.h" // IWYU pragma: keep

/** @} */

/* ============================================================================
 * Shared Initialization
 * ============================================================================
 * Common initialization code shared between client and server modes.
 * This function handles platform setup, logging, buffer pools, cleanup
 * registration, and other shared initialization tasks.
 */

/**
 * @brief Initialize common subsystems shared by client and server
 *
 * This function performs initialization that is common to both client and
 * server modes:
 * - Platform initialization (Winsock, etc.)
 * - Logging setup with default filename
 * - Palette configuration
 * - Buffer pool initialization
 * - Cleanup registration (errno, known_hosts, platform, buffer pool)
 * - Mimalloc debug registration (if enabled)
 *
 * Note: Memory debugging setup is handled separately by each mode due to
 * different requirements (client has snapshot mode, server doesn't).
 *
 * Call this BEFORE options_init() so that options parsing can use properly
 * configured logging with colors. This initializes timer system, logging,
 * and platform subsystems.
 *
 * @param log_file Log filename to use (e.g., "client.log" or "ascii-chat.log")
 * @param is_client true for client mode (routes all logs to stderr), false for server mode
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t asciichat_shared_init(const char *log_file, bool is_client);

/**
 * @brief Clean up shared library subsystems
 *
 * Performs cleanup of all subsystems initialized by asciichat_shared_init().
 * All cleanup functions are idempotent and safe to call multiple times.
 *
 * Applications should typically register this with atexit():
 *   asciichat_shared_init(log_file, is_client);
 *   atexit(asciichat_shared_destroy);
 *
 * @note This function is safe to call even if init failed or wasn't called.
 * @note Cleanup order is the reverse of initialization order.
 * @note Library code never calls atexit() - only application code should.
 */
void asciichat_shared_destroy(void);

/* ============================================================================
 * Error Handling Macros
 * ============================================================================
 * Macros for common error handling patterns to reduce code duplication
 */

/**
 * @brief Check result and log error if operation failed
 *
 * Consolidates the pattern:
 * ```c
 * operation_result_t result = operation();
 * if (result != OK) {
 *     log_error("Operation failed: %d", result);
 *     return result;
 * }
 * ```
 *
 * Usage:
 * ```c
 * ASCIICHAT_CHECK_AND_LOG(crypto_result = crypto_operation(args),
 *                         ASCIICHAT_OK, "Crypto operation failed: %d",
 *                         crypto_result);
 * ```
 */
#define ASCIICHAT_CHECK_AND_LOG(expr, ok_value, msg, ...)                                                              \
  do {                                                                                                                 \
    if ((expr) != (ok_value)) {                                                                                        \
      log_error(msg, ##__VA_ARGS__);                                                                                   \
      return (expr);                                                                                                   \
    }                                                                                                                  \
  } while (0)

/* ============================================================================
 * Global Variables for Early Argv Inspection and Color Flag Detection
 * ============================================================================ */

/**
 * @brief Global argc for early argument inspection (e.g., --color flag detection)
 * Set by main() for access from lib/platform/terminal.c during early execution
 */
extern ASCIICHAT_API int g_argc;

/**
 * @brief Global argv for early argument inspection (e.g., --color flag detection)
 * Set by main() for access from lib/platform/terminal.c during early execution
 */
extern ASCIICHAT_API char **g_argv;

/**
 * @brief Was --color explicitly passed in command-line arguments?
 * Set by options_init() before RCU is initialized, used by terminal_should_color_output()
 */
extern ASCIICHAT_API bool g_color_flag_passed;

/**
 * @brief Value of --color flag (true if --color was in argv)
 * Set by options_init() before RCU is initialized, used by terminal_should_color_output()
 */
extern ASCIICHAT_API bool g_color_flag_value;

/** @} */
