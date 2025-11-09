/**
 * @defgroup common Common Definitions
 * @ingroup module_core
 * @brief Code shared throughout the library
 *
 * @file common.h
 * @ingroup common
 * @brief Common definitions, error codes, macros, and types shared throughout the application
 *
 * This header provides core functionality used throughout ascii-chat:
 * - Error and exit codes (unified status values)
 * - Memory allocation macros (with mimalloc support)
 * - Utility macros (MIN, MAX, ARRAY_SIZE)
 * - Protocol constants
 * - Shutdown detection system
 * - Shared initialization function
 *
 * @note This header should be included early in other headers as it provides
 * fundamental types and macros that many other modules depend on.
 */

#pragma once

// DLL export/import macros (must be included first to avoid circular dependencies)
#include "platform/api.h" // IWYU pragma: keep

/* Feature test macros for POSIX functions */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
 * Common Definitions
 * ============================================================================
 */

/* ============================================================================
 * Error and Exit Codes - Unified Status Values (0-255)
 * ============================================================================
 * Single enum for both function return values and process exit codes.
 * Following Unix conventions: 0 = success, 1 = general error, 2 = usage error.
 *
 * Usage:
 *   - Library functions return these codes
 *   - Application code passes these to exit()
 *   - Use FATAL macros in src/ code for automatic error reporting
 */

/* Undefine Windows macros that conflict with our enum values */
#ifdef _WIN32
#undef ERROR_BUFFER_OVERFLOW
#undef ERROR_INVALID_STATE
#endif

/**
 * @brief Error and exit codes - unified status values (0-255)
 *
 * Single enum for both function return values and process exit codes.
 * Following Unix conventions: 0 = success, 1 = general error, 2 = usage error.
 *
 * Error codes are organized into ranges:
 * - 0: Success
 * - 1-2: Standard errors (general, usage)
 * - 3-19: Initialization failures
 * - 20-39: Hardware/Device errors
 * - 40-59: Network errors
 * - 60-79: Security/Crypto errors
 * - 80-99: Runtime errors
 * - 100-127: Signal/Crash handlers
 * - 128-255: Reserved (128+N = terminated by signal N on Unix)
 *
 * @ingroup common
 */
typedef enum {
  /* Standard codes (0-2) - Unix conventions */
  ASCIICHAT_OK = 0,  /**< Success */
  ERROR_GENERAL = 1, /**< Unspecified error */
  ERROR_USAGE = 2,   /**< Invalid command line arguments or options */

  /* Initialization failures (3-19) */
  ERROR_MEMORY = 3,        /**< Memory allocation failed (OOM) */
  ERROR_CONFIG = 4,        /**< Configuration file or settings error */
  ERROR_CRYPTO_INIT = 5,   /**< Cryptographic initialization failed */
  ERROR_LOGGING_INIT = 6,  /**< Logging system initialization failed */
  ERROR_PLATFORM_INIT = 7, /**< Platform-specific initialization failed */

  /* Hardware/Device errors (20-39) */
  ERROR_WEBCAM = 20,            /**< Webcam initialization or capture failed */
  ERROR_WEBCAM_IN_USE = 21,     /**< Webcam is in use by another application */
  ERROR_WEBCAM_PERMISSION = 22, /**< Webcam permission denied */
  ERROR_AUDIO = 23,             /**< Audio device initialization or I/O failed */
  ERROR_AUDIO_IN_USE = 24,      /**< Audio device is in use */
  ERROR_TERMINAL = 25,          /**< Terminal initialization or capability detection failed */

  /* Network errors (40-59) */
  ERROR_NETWORK = 40,          /**< General network error */
  ERROR_NETWORK_BIND = 41,     /**< Cannot bind to port (server) */
  ERROR_NETWORK_CONNECT = 42,  /**< Cannot connect to server (client) */
  ERROR_NETWORK_TIMEOUT = 43,  /**< Network operation timed out */
  ERROR_NETWORK_PROTOCOL = 44, /**< Protocol violation or incompatible version */
  ERROR_NETWORK_SIZE = 45,     /**< Network packet size error */

  /* Security/Crypto errors (60-79) */
  ERROR_CRYPTO = 60,              /**< Cryptographic operation failed */
  ERROR_CRYPTO_KEY = 61,          /**< Key loading, parsing, or generation failed */
  ERROR_CRYPTO_AUTH = 62,         /**< Authentication failed */
  ERROR_CRYPTO_HANDSHAKE = 63,    /**< Cryptographic handshake failed */
  ERROR_CRYPTO_VERIFICATION = 64, /**< Signature or key verification failed */

  /* Runtime errors (80-99) */
  ERROR_THREAD = 80,             /**< Thread creation or management failed */
  ERROR_BUFFER = 81,             /**< Buffer allocation or overflow */
  ERROR_BUFFER_FULL = 82,        /**< Buffer full */
  ERROR_BUFFER_OVERFLOW = 83,    /**< Buffer overflow */
  ERROR_DISPLAY = 84,            /**< Display rendering or output error */
  ERROR_INVALID_STATE = 85,      /**< Invalid program state */
  ERROR_INVALID_PARAM = 86,      /**< Invalid parameter */
  ERROR_INVALID_FRAME = 87,      /**< Invalid frame data */
  ERROR_RESOURCE_EXHAUSTED = 88, /**< System resources exhausted */
  ERROR_FORMAT = 89,             /**< String formatting operation failed */
  ERROR_STRING = 90,             /**< String manipulation operation failed */

  /* Signal/Crash handlers (100-127) */
  ERROR_SIGNAL_INTERRUPT = 100, /**< Interrupted by signal (SIGINT, SIGTERM) */
  ERROR_SIGNAL_CRASH = 101,     /**< Fatal signal (SIGSEGV, SIGABRT, etc.) */
  ERROR_ASSERTION_FAILED = 102, /**< Assertion or invariant violation */

  /* Compression errors (103-104) */
  ERROR_COMPRESSION = 103,   /**< Compression operation failed */
  ERROR_DECOMPRESSION = 104, /**< Decompression operation failed */

  /* Reserved (128-255) - Should not be used */
  /* 128+N typically means "terminated by signal N" on Unix systems */

} asciichat_error_t;

/* Forward declaration for asciichat_fatal_with_context - now after asciichat_error_t is defined */
void asciichat_fatal_with_context(asciichat_error_t code, const char *file, int line, const char *function,
                                  const char *format, ...);

/* ============================================================================
 * Error String Utilities
 * ============================================================================
 */

/**
 * @brief Get human-readable string for error/exit code
 * @param code Error code from asciichat_error_t enum
 * @return Human-readable error string, or "Unknown error" for invalid codes
 * @ingroup common
 */
static inline const char *asciichat_error_string(asciichat_error_t code) {
  switch (code) {
  case ASCIICHAT_OK:
    return "Success";
  case ERROR_GENERAL:
    return "General error";
  case ERROR_USAGE:
    return "Invalid command line usage";
  case ERROR_MEMORY:
    return "Memory allocation failed";
  case ERROR_CONFIG:
    return "Configuration error";
  case ERROR_CRYPTO_INIT:
    return "Cryptographic initialization failed";
  case ERROR_LOGGING_INIT:
    return "Logging initialization failed";
  case ERROR_PLATFORM_INIT:
    return "Platform initialization failed";
  case ERROR_WEBCAM:
    return "Webcam error";
  case ERROR_WEBCAM_IN_USE:
    return "Webcam in use by another application";
  case ERROR_WEBCAM_PERMISSION:
    return "Webcam permission denied";
  case ERROR_AUDIO:
    return "Audio device error";
  case ERROR_AUDIO_IN_USE:
    return "Audio device in use";
  case ERROR_TERMINAL:
    return "Terminal error";
  case ERROR_NETWORK:
    return "Network error";
  case ERROR_NETWORK_BIND:
    return "Cannot bind to network port";
  case ERROR_NETWORK_CONNECT:
    return "Cannot connect to server";
  case ERROR_NETWORK_TIMEOUT:
    return "Network timeout";
  case ERROR_NETWORK_PROTOCOL:
    return "Network protocol error";
  case ERROR_NETWORK_SIZE:
    return "Network packet size error";
  case ERROR_CRYPTO:
    return "Cryptographic error";
  case ERROR_CRYPTO_KEY:
    return "Cryptographic key error";
  case ERROR_CRYPTO_AUTH:
    return "Authentication failed";
  case ERROR_CRYPTO_HANDSHAKE:
    return "Cryptographic handshake failed";
  case ERROR_CRYPTO_VERIFICATION:
    return "Signature verification failed";
  case ERROR_THREAD:
    return "Thread error";
  case ERROR_BUFFER:
    return "Buffer error";
  case ERROR_BUFFER_FULL:
    return "Buffer full";
  case ERROR_BUFFER_OVERFLOW:
    return "Buffer overflow";
  case ERROR_DISPLAY:
    return "Display error";
  case ERROR_INVALID_STATE:
    return "Invalid program state";
  case ERROR_INVALID_PARAM:
    return "Invalid parameter";
  case ERROR_INVALID_FRAME:
    return "Invalid frame data";
  case ERROR_RESOURCE_EXHAUSTED:
    return "System resources exhausted";
  case ERROR_FORMAT:
    return "String formatting operation failed";
  case ERROR_STRING:
    return "String manipulation operation failed";
  case ERROR_SIGNAL_INTERRUPT:
    return "Interrupted by signal";
  case ERROR_SIGNAL_CRASH:
    return "Terminated by fatal signal";
  case ERROR_ASSERTION_FAILED:
    return "Assertion failed";
  default:
    return "Unknown error";
  }
}

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

// =============================================================================
// Protocol Version Constants
// =============================================================================

/** @brief Major protocol version number */
#define PROTOCOL_VERSION_MAJOR 1
/** @brief Minor protocol version number */
#define PROTOCOL_VERSION_MINOR 0

// =============================================================================
// Feature Flags
// =============================================================================

/** @brief Run-length encoding support flag */
#define FEATURE_RLE_ENCODING 0x01
/** @brief Delta frame encoding support flag */
#define FEATURE_DELTA_FRAMES 0x02

// =============================================================================
// Compression Constants
// =============================================================================

/** @brief No compression algorithm */
#define COMPRESS_ALGO_NONE 0x00
/** @brief zlib deflate compression algorithm */
#define COMPRESS_ALGO_ZLIB 0x01
/** @brief LZ4 fast compression algorithm */
#define COMPRESS_ALGO_LZ4 0x02
/** @brief zstd algorithm */
#define COMPRESS_ALGO_ZSTD 0x03

// =============================================================================
// Frame Flags
// =============================================================================

/** @brief Frame includes ANSI color codes */
#define FRAME_FLAG_HAS_COLOR 0x01
/** @brief Frame data is compressed */
#define FRAME_FLAG_IS_COMPRESSED 0x02
/** @brief Frame data is RLE compressed */
#define FRAME_FLAG_RLE_COMPRESSED 0x04
/** @brief Frame was stretched (aspect adjusted) */
#define FRAME_FLAG_IS_STRETCHED 0x08

// =============================================================================
// Pixel Format Constants
// =============================================================================

/** @brief RGB pixel format */
#define PIXEL_FORMAT_RGB 0
/** @brief RGBA pixel format */
#define PIXEL_FORMAT_RGBA 1
/** @brief BGR pixel format */
#define PIXEL_FORMAT_BGR 2
/** @brief BGRA pixel format */
#define PIXEL_FORMAT_BGRA 3

// =============================================================================
// Multi-Client Constants
// =============================================================================

/** @brief Maximum display name length in characters */
#define MAX_DISPLAY_NAME_LEN 32
/** @brief Maximum number of clients supported */
#define MAX_CLIENTS 10

/** @brief Default maximum frame rate (frames per second) */
#define DEFAULT_MAX_FPS 60

/** @brief Runtime configurable maximum frame rate (can be overridden via environment or command line) */
extern int g_max_fps;
/** @brief Maximum frame rate macro (uses g_max_fps if set, otherwise DEFAULT_MAX_FPS) */
#define MAX_FPS (g_max_fps > 0 ? g_max_fps : DEFAULT_MAX_FPS)

/** @brief Frame interval in milliseconds based on MAX_FPS */
#define FRAME_INTERVAL_MS (1000 / MAX_FPS)

/** @brief Frame buffer capacity based on MAX_FPS */
#define FRAME_BUFFER_CAPACITY (MAX_FPS / 4)

/* ============================================================================
 * Shutdown Check System
 * ============================================================================
 * Provides clean separation between library and application for shutdown
 * detection. Library code should never directly access application state.
 *
 * Usage:
 *   Application (server.c/client.c):
 *     shutdown_register_callback(my_shutdown_check_fn);
 *
 *   Library code (logging.c, lock_debug.c, etc.):
 *     if (shutdown_is_requested()) { return; }
 */

/**
 * @brief Shutdown check callback function type
 * @return true if shutdown has been requested, false otherwise
 * @ingroup common
 */
typedef bool (*shutdown_check_fn)(void);

/**
 * @brief Register application's shutdown check function
 * @param callback Function to call to check if shutdown has been requested
 *
 * @note Call this from main() to register the application's shutdown detection function.
 *       Library code should use shutdown_is_requested() instead of accessing application state directly.
 *
 * @ingroup common
 */
void shutdown_register_callback(shutdown_check_fn callback);

/**
 * @brief Check if shutdown has been requested
 * @return true if shutdown has been requested, false otherwise
 *
 * @note Use this in library code to check for shutdown requests without accessing
 *       application state directly. The callback must be registered first with
 *       shutdown_register_callback().
 *
 * @ingroup common
 */
bool shutdown_is_requested(void);

/* ============================================================================
 * Utility Macros
 * ============================================================================
 */

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
#elif defined(DEBUG_MEMORY)
#ifdef NDEBUG
#define ALLOC_MALLOC(size) debug_malloc(size, NULL, 0)
#define ALLOC_CALLOC(count, size) debug_calloc((count), (size), NULL, 0)
#define ALLOC_REALLOC(ptr, size) debug_realloc((ptr), (size), NULL, 0)
#define ALLOC_FREE(ptr) debug_free(ptr, NULL, 0)
#else
#define ALLOC_MALLOC(size) debug_malloc(size, __FILE__, __LINE__)
#define ALLOC_CALLOC(count, size) debug_calloc((count), (size), __FILE__, __LINE__)
#define ALLOC_REALLOC(ptr, size) debug_realloc((ptr), (size), __FILE__, __LINE__)
#define ALLOC_FREE(ptr) debug_free(ptr, __FILE__, __LINE__)
#endif
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
    size_t aligned_size = (((size) + (alignment)-1) / (alignment)) * (alignment);                                      \
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

/* Platform-safe sscanf */
#define SAFE_SSCANF(str, format, ...) sscanf(str, format, __VA_ARGS__)

/* Platform-safe strerror */
#include "platform/abstraction.h" // IWYU pragma: keep
#define SAFE_STRERROR(errnum) platform_strerror(errnum)

/* Safe memory functions */
#define SAFE_MEMCPY(dest, dest_size, src, count) platform_memcpy((dest), (dest_size), (src), (count))
#define SAFE_MEMSET(dest, dest_size, ch, count) platform_memset((dest), (dest_size), (ch), (count))
#define SAFE_MEMMOVE(dest, dest_size, src, count) platform_memmove((dest), (dest_size), (src), (count))
#define SAFE_STRCPY(dest, dest_size, src) platform_strcpy((dest), (dest_size), (src))

/* Safe size_t multiplication with overflow detection */
static inline bool safe_size_mul(size_t a, size_t b, size_t *result) {
  if (result == NULL) {
    return true;
  }

  if (a != 0 && b > SIZE_MAX / a) {
    *result = 0;
    return true;
  }

  *result = a * b;
  return false;
}

/* Safe string formatting */
#define SAFE_SNPRINTF(buffer, buffer_size, ...) (size_t) safe_snprintf((buffer), (buffer_size), __VA_ARGS__)

/**
 * @brief Safe buffer size calculation for snprintf
 *
 * Casts offset to size_t to avoid sign conversion warnings when subtracting from buffer_size.
 * Returns 0 if offset is negative or >= buffer_size (prevents underflow).
 */
#define SAFE_BUFFER_SIZE(buffer_size, offset)                                                                          \
  ((offset) < 0 || (size_t)(offset) >= (buffer_size) ? 0 : (buffer_size) - (size_t)(offset))

/* Include logging.h to provide logging macros to all files that include common.h */
#include "logging.h" // IWYU pragma: keep

/* Memory debugging (only in debug builds, disabled when mimalloc override is active) */
#if defined(DEBUG_MEMORY) && !defined(MI_MALLOC_OVERRIDE)
/**
 * @brief Debug memory allocation function
 * @param size Size to allocate in bytes
 * @param file Source file name (for leak tracking)
 * @param line Line number (for leak tracking)
 * @return Allocated pointer, or NULL on failure
 * @ingroup common
 */
void *debug_malloc(size_t size, const char *file, int line);

/**
 * @brief Debug memory deallocation function
 * @param ptr Pointer to free
 * @param file Source file name (for leak tracking)
 * @param line Line number (for leak tracking)
 * @ingroup common
 */
void debug_free(void *ptr, const char *file, int line);

/**
 * @brief Debug zero-initialized memory allocation
 * @param count Number of elements
 * @param size Size of each element
 * @param file Source file name (for leak tracking)
 * @param line Line number (for leak tracking)
 * @return Allocated pointer, or NULL on failure
 * @ingroup common
 */
void *debug_calloc(size_t count, size_t size, const char *file, int line);

/**
 * @brief Debug memory reallocation function
 * @param ptr Pointer to reallocate
 * @param size New size in bytes
 * @param file Source file name (for leak tracking)
 * @param line Line number (for leak tracking)
 * @return Reallocated pointer, or NULL on failure
 * @ingroup common
 */
void *debug_realloc(void *ptr, size_t size, const char *file, int line);

/**
 * @brief Track aligned allocation for leak detection
 * @param ptr Pointer to aligned allocation
 * @param size Size in bytes
 * @param file Source file name (for leak tracking)
 * @param line Line number (for leak tracking)
 * @ingroup common
 */
void debug_track_aligned(void *ptr, size_t size, const char *file, int line);

/**
 * @brief Print memory leak report
 *
 * Prints comprehensive memory leak report including all unfreed allocations
 * with file, line, and size information.
 *
 * @ingroup common
 */
void debug_memory_report(void);

/**
 * @brief Control stderr output for memory report
 * @param quiet If true, suppress stderr output (still logs to file)
 * @ingroup common
 */
void debug_memory_set_quiet_mode(bool quiet);
#endif /* DEBUG_MEMORY && !MI_MALLOC_OVERRIDE */

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
 * @param default_log_filename Default log filename (e.g., "client.log" or "server.log")
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t asciichat_shared_init(const char *default_log_filename);

#endif // ASCII_CHAT_COMMON_H
