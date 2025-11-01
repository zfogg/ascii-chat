#pragma once

/* Feature test macros for POSIX functions */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

typedef enum {
  /* Standard codes (0-2) - Unix conventions */
  ASCIICHAT_OK = 0,  /* Success */
  ERROR_GENERAL = 1, /* Unspecified error */
  ERROR_USAGE = 2,   /* Invalid command line arguments or options */

  /* Initialization failures (3-19) */
  ERROR_MEMORY = 3,        /* Memory allocation failed (OOM) */
  ERROR_CONFIG = 4,        /* Configuration file or settings error */
  ERROR_CRYPTO_INIT = 5,   /* Cryptographic initialization failed */
  ERROR_LOGGING_INIT = 6,  /* Logging system initialization failed */
  ERROR_PLATFORM_INIT = 7, /* Platform-specific initialization failed */

  /* Hardware/Device errors (20-39) */
  ERROR_WEBCAM = 20,            /* Webcam initialization or capture failed */
  ERROR_WEBCAM_IN_USE = 21,     /* Webcam is in use by another application */
  ERROR_WEBCAM_PERMISSION = 22, /* Webcam permission denied */
  ERROR_AUDIO = 23,             /* Audio device initialization or I/O failed */
  ERROR_AUDIO_IN_USE = 24,      /* Audio device is in use */
  ERROR_TERMINAL = 25,          /* Terminal initialization or capability detection failed */

  /* Network errors (40-59) */
  ERROR_NETWORK = 40,          /* General network error */
  ERROR_NETWORK_BIND = 41,     /* Cannot bind to port (server) */
  ERROR_NETWORK_CONNECT = 42,  /* Cannot connect to server (client) */
  ERROR_NETWORK_TIMEOUT = 43,  /* Network operation timed out */
  ERROR_NETWORK_PROTOCOL = 44, /* Protocol violation or incompatible version */
  ERROR_NETWORK_SIZE = 45,     /* Network packet size error */

  /* Security/Crypto errors (60-79) */
  ERROR_CRYPTO = 60,              /* Cryptographic operation failed */
  ERROR_CRYPTO_KEY = 61,          /* Key loading, parsing, or generation failed */
  ERROR_CRYPTO_AUTH = 62,         /* Authentication failed */
  ERROR_CRYPTO_HANDSHAKE = 63,    /* Cryptographic handshake failed */
  ERROR_CRYPTO_VERIFICATION = 64, /* Signature or key verification failed */

  /* Runtime errors (80-99) */
  ERROR_THREAD = 80,             /* Thread creation or management failed */
  ERROR_BUFFER = 81,             /* Buffer allocation or overflow */
  ERROR_BUFFER_FULL = 82,        /* Buffer full */
  ERROR_BUFFER_OVERFLOW = 83,    /* Buffer overflow */
  ERROR_DISPLAY = 84,            /* Display rendering or output error */
  ERROR_INVALID_STATE = 85,      /* Invalid program state */
  ERROR_INVALID_PARAM = 86,      /* Invalid parameter */
  ERROR_INVALID_FRAME = 87,      /* Invalid frame data */
  ERROR_RESOURCE_EXHAUSTED = 88, /* System resources exhausted */
  ERROR_FORMAT = 89,             /* String formatting operation failed */
  ERROR_STRING = 90,             /* String manipulation operation failed */

  /* Signal/Crash handlers (100-127) */
  ERROR_SIGNAL_INTERRUPT = 100, /* Interrupted by signal (SIGINT, SIGTERM) */
  ERROR_SIGNAL_CRASH = 101,     /* Fatal signal (SIGSEGV, SIGABRT, etc.) */
  ERROR_ASSERTION_FAILED = 102, /* Assertion or invariant violation */

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

/* Get human-readable string for error/exit code */
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
#include "platform/system.h"

/**
 * @brief Exit with error code and custom message, with stack trace in debug builds
 * @param code Error code (asciichat_error_t)
 * @param ... Custom message format string and arguments (printf-style)
 *
 * Usage:
 *   FATAL(ERROR_NETWORK_BIND, "Cannot bind to port %d", port_number);
 */
#define FATAL(code, ...) asciichat_fatal_with_context(code, __FILE__, __LINE__, __func__, ##__VA_ARGS__)

// =============================================================================
// Protocol Version Constants
// =============================================================================

#define PROTOCOL_VERSION_MAJOR 1 // Major protocol version
#define PROTOCOL_VERSION_MINOR 0 // Minor protocol version

// =============================================================================
// Feature Flags
// =============================================================================

#define FEATURE_RLE_ENCODING 0x01 // Run-length encoding support
#define FEATURE_DELTA_FRAMES 0x02 // Delta frame encoding support

// =============================================================================
// Compression Constants
// =============================================================================

#define COMPRESS_ALGO_NONE 0x00 // No compression
#define COMPRESS_ALGO_ZLIB 0x01 // zlib deflate compression
#define COMPRESS_ALGO_LZ4 0x02  // LZ4 fast compression

// =============================================================================
// Frame Flags
// =============================================================================

#define FRAME_FLAG_HAS_COLOR 0x01      // Frame includes ANSI color codes
#define FRAME_FLAG_IS_COMPRESSED 0x02  // Frame data is compressed
#define FRAME_FLAG_RLE_COMPRESSED 0x04 // Frame data is RLE compressed
#define FRAME_FLAG_IS_STRETCHED 0x08   // Frame was stretched (aspect adjusted)

// =============================================================================
// Pixel Format Constants
// =============================================================================

#define PIXEL_FORMAT_RGB 0  // RGB pixel format
#define PIXEL_FORMAT_RGBA 1 // RGBA pixel format
#define PIXEL_FORMAT_BGR 2  // BGR pixel format
#define PIXEL_FORMAT_BGRA 3 // BGRA pixel format

// =============================================================================
// Multi-Client Constants
// =============================================================================

#define MAX_DISPLAY_NAME_LEN 32 // Maximum display name length
#define MAX_CLIENTS 10          // Maximum number of clients

// Frame rate configuration - With 1ms timer resolution enabled, Windows can now handle 60 FPS
#ifdef _WIN32
#define DEFAULT_MAX_FPS 60 // Windows with timeBeginPeriod(1) can handle 60 FPS
#else
#define DEFAULT_MAX_FPS 60 // macOS/Linux terminals can handle higher rates
#endif

// Allow runtime override via environment variable or command line
extern int g_max_fps;
#define MAX_FPS (g_max_fps > 0 ? g_max_fps : DEFAULT_MAX_FPS)

#define FRAME_INTERVAL_MS (1000 / MAX_FPS)

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

/* Shutdown check callback type */
typedef bool (*shutdown_check_fn)(void);

/* Register application's shutdown check function (call from main()) */
void shutdown_register_callback(shutdown_check_fn callback);

/* Check if shutdown has been requested (call from library code) */
bool shutdown_is_requested(void);

/* ============================================================================
 * Utility Macros
 * ============================================================================
 */

/* Common utility macros */
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

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
    _ptr;                                                                                                              \
  })
#else
/* Fall back to platform-specific aligned allocation when mimalloc is disabled */
#ifdef __APPLE__
/* macOS uses posix_memalign() for aligned allocation */
#define SAFE_MALLOC_ALIGNED(size, alignment, cast)                                                                     \
  ({                                                                                                                   \
    cast _ptr;                                                                                                         \
    int result = posix_memalign((void **)&_ptr, (alignment), (size));                                                  \
    if (result != 0 || !_ptr) {                                                                                        \
      FATAL(ERROR_MEMORY, "Aligned memory allocation failed: %zu bytes, %zu alignment", (size_t)(size),                \
            (size_t)(alignment));                                                                                      \
    }                                                                                                                  \
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
#define SAFE_GETENV(name) ((char *)platform_getenv(name))

/* Platform-safe sscanf */
#define SAFE_SSCANF(str, format, ...) sscanf(str, format, __VA_ARGS__)

/* Platform-safe strerror */
#include "platform/abstraction.h"
#define SAFE_STRERROR(errnum) platform_strerror(errnum)

/* Safe memory functions */
#define SAFE_MEMCPY(dest, dest_size, src, count) platform_memcpy((dest), (dest_size), (src), (count))
#define SAFE_MEMSET(dest, dest_size, ch, count) platform_memset((dest), (dest_size), (ch), (count))
#define SAFE_MEMMOVE(dest, dest_size, src, count) platform_memmove((dest), (dest_size), (src), (count))
#define SAFE_STRCPY(dest, dest_size, src) platform_strcpy((dest), (dest_size), (src))

/* Safe string formatting */
#define SAFE_SNPRINTF(buffer, buffer_size, ...) safe_snprintf((buffer), (buffer_size), __VA_ARGS__)

/* Include logging.h to provide logging macros to all files that include common.h */
#include "logging.h"

/* Memory debugging (only in debug builds, disabled when mimalloc override is active) */
#if defined(DEBUG_MEMORY) && !defined(MI_MALLOC_OVERRIDE)
void *debug_malloc(size_t size, const char *file, int line);
void debug_free(void *ptr, const char *file, int line);
void *debug_calloc(size_t count, size_t size, const char *file, int line);
void *debug_realloc(void *ptr, size_t size, const char *file, int line);

void debug_memory_report(void);
void debug_memory_set_quiet_mode(bool quiet); /* Control stderr output for memory report */
#endif                                        /* DEBUG_MEMORY && !MI_MALLOC_OVERRIDE */
