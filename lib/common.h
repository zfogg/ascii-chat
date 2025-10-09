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

#include <stdlib.h>

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
typedef enum {
  /* Standard codes (0-2) - Unix conventions */
  ASCIICHAT_OK = 0,            /* Success */
  ASCIICHAT_ERROR_GENERAL = 1, /* Unspecified error */
  ASCIICHAT_ERROR_USAGE = 2,   /* Invalid command line arguments or options */

  /* Initialization failures (3-19) */
  ASCIICHAT_ERROR_MEMORY = 3,        /* Memory allocation failed (OOM) */
  ASCIICHAT_ERROR_CONFIG = 4,        /* Configuration file or settings error */
  ASCIICHAT_ERROR_CRYPTO_INIT = 5,   /* Cryptographic initialization failed */
  ASCIICHAT_ERROR_LOGGING_INIT = 6,  /* Logging system initialization failed */
  ASCIICHAT_ERROR_PLATFORM_INIT = 7, /* Platform-specific initialization failed */

  /* Hardware/Device errors (20-39) */
  ASCIICHAT_ERROR_WEBCAM = 20,            /* Webcam initialization or capture failed */
  ASCIICHAT_ERROR_WEBCAM_IN_USE = 21,     /* Webcam is in use by another application */
  ASCIICHAT_ERROR_WEBCAM_PERMISSION = 22, /* Webcam permission denied */
  ASCIICHAT_ERROR_AUDIO = 23,             /* Audio device initialization or I/O failed */
  ASCIICHAT_ERROR_AUDIO_IN_USE = 24,      /* Audio device is in use */
  ASCIICHAT_ERROR_TERMINAL = 25,          /* Terminal initialization or capability detection failed */

  /* Network errors (40-59) */
  ASCIICHAT_ERROR_NETWORK = 40,          /* General network error */
  ASCIICHAT_ERROR_NETWORK_BIND = 41,     /* Cannot bind to port (server) */
  ASCIICHAT_ERROR_NETWORK_CONNECT = 42,  /* Cannot connect to server (client) */
  ASCIICHAT_ERROR_NETWORK_TIMEOUT = 43,  /* Network operation timed out */
  ASCIICHAT_ERROR_NETWORK_PROTOCOL = 44, /* Protocol violation or incompatible version */
  ASCIICHAT_ERROR_NETWORK_SIZE = 45,     /* Network packet size error */

  /* Security/Crypto errors (60-79) */
  ASCIICHAT_ERROR_CRYPTO = 60,              /* Cryptographic operation failed */
  ASCIICHAT_ERROR_CRYPTO_KEY = 61,          /* Key loading, parsing, or generation failed */
  ASCIICHAT_ERROR_CRYPTO_AUTH = 62,         /* Authentication failed */
  ASCIICHAT_ERROR_CRYPTO_HANDSHAKE = 63,    /* Cryptographic handshake failed */
  ASCIICHAT_ERROR_CRYPTO_VERIFICATION = 64, /* Signature or key verification failed */

  /* Runtime errors (80-99) */
  ASCIICHAT_ERROR_THREAD = 80,             /* Thread creation or management failed */
  ASCIICHAT_ERROR_BUFFER = 81,             /* Buffer allocation or overflow */
  ASCIICHAT_ERROR_BUFFER_FULL = 82,        /* Buffer full */
  ASCIICHAT_ERROR_BUFFER_OVERFLOW = 83,    /* Buffer overflow */
  ASCIICHAT_ERROR_DISPLAY = 84,            /* Display rendering or output error */
  ASCIICHAT_ERROR_INVALID_STATE = 85,      /* Invalid program state */
  ASCIICHAT_ERROR_INVALID_PARAM = 86,      /* Invalid parameter */
  ASCIICHAT_ERROR_INVALID_FRAME = 87,      /* Invalid frame data */
  ASCIICHAT_ERROR_RESOURCE_EXHAUSTED = 88, /* System resources exhausted */

  /* Signal/Crash handlers (100-127) */
  ASCIICHAT_ERROR_SIGNAL_INTERRUPT = 100, /* Interrupted by signal (SIGINT, SIGTERM) */
  ASCIICHAT_ERROR_SIGNAL_CRASH = 101,     /* Fatal signal (SIGSEGV, SIGABRT, etc.) */
  ASCIICHAT_ERROR_ASSERTION_FAILED = 102, /* Assertion or invariant violation */

  /* Reserved (128-255) - Should not be used */
  /* 128+N typically means "terminated by signal N" on Unix systems */
} asciichat_error_status_t;

/* ============================================================================
 * Error String Utilities
 * ============================================================================
 */

/* Get human-readable string for error/exit code */
static inline const char *asciichat_error_string(asciichat_error_status_t code) {
  switch (code) {
  case ASCIICHAT_OK:
    return "Success";
  case ASCIICHAT_ERROR_GENERAL:
    return "General error";
  case ASCIICHAT_ERROR_USAGE:
    return "Invalid command line usage";
  case ASCIICHAT_ERROR_MEMORY:
    return "Memory allocation failed";
  case ASCIICHAT_ERROR_CONFIG:
    return "Configuration error";
  case ASCIICHAT_ERROR_CRYPTO_INIT:
    return "Cryptographic initialization failed";
  case ASCIICHAT_ERROR_LOGGING_INIT:
    return "Logging initialization failed";
  case ASCIICHAT_ERROR_PLATFORM_INIT:
    return "Platform initialization failed";
  case ASCIICHAT_ERROR_WEBCAM:
    return "Webcam error";
  case ASCIICHAT_ERROR_WEBCAM_IN_USE:
    return "Webcam in use by another application";
  case ASCIICHAT_ERROR_WEBCAM_PERMISSION:
    return "Webcam permission denied";
  case ASCIICHAT_ERROR_AUDIO:
    return "Audio device error";
  case ASCIICHAT_ERROR_AUDIO_IN_USE:
    return "Audio device in use";
  case ASCIICHAT_ERROR_TERMINAL:
    return "Terminal error";
  case ASCIICHAT_ERROR_NETWORK:
    return "Network error";
  case ASCIICHAT_ERROR_NETWORK_BIND:
    return "Cannot bind to network port";
  case ASCIICHAT_ERROR_NETWORK_CONNECT:
    return "Cannot connect to server";
  case ASCIICHAT_ERROR_NETWORK_TIMEOUT:
    return "Network timeout";
  case ASCIICHAT_ERROR_NETWORK_PROTOCOL:
    return "Network protocol error";
  case ASCIICHAT_ERROR_NETWORK_SIZE:
    return "Network packet size error";
  case ASCIICHAT_ERROR_CRYPTO:
    return "Cryptographic error";
  case ASCIICHAT_ERROR_CRYPTO_KEY:
    return "Cryptographic key error";
  case ASCIICHAT_ERROR_CRYPTO_AUTH:
    return "Authentication failed";
  case ASCIICHAT_ERROR_CRYPTO_HANDSHAKE:
    return "Cryptographic handshake failed";
  case ASCIICHAT_ERROR_CRYPTO_VERIFICATION:
    return "Signature verification failed";
  case ASCIICHAT_ERROR_THREAD:
    return "Thread error";
  case ASCIICHAT_ERROR_BUFFER:
    return "Buffer error";
  case ASCIICHAT_ERROR_BUFFER_FULL:
    return "Buffer full";
  case ASCIICHAT_ERROR_BUFFER_OVERFLOW:
    return "Buffer overflow";
  case ASCIICHAT_ERROR_DISPLAY:
    return "Display error";
  case ASCIICHAT_ERROR_INVALID_STATE:
    return "Invalid program state";
  case ASCIICHAT_ERROR_INVALID_PARAM:
    return "Invalid parameter";
  case ASCIICHAT_ERROR_INVALID_FRAME:
    return "Invalid frame data";
  case ASCIICHAT_ERROR_RESOURCE_EXHAUSTED:
    return "System resources exhausted";
  case ASCIICHAT_ERROR_SIGNAL_INTERRUPT:
    return "Interrupted by signal";
  case ASCIICHAT_ERROR_SIGNAL_CRASH:
    return "Terminated by fatal signal";
  case ASCIICHAT_ERROR_ASSERTION_FAILED:
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
 *   FATAL_ERROR(ASCIICHAT_ERROR_WEBCAM);                    // Error code only
 *   FATAL(ASCIICHAT_ERROR_WEBCAM, "Custom msg: %d", val);   // Error code + custom message
 */

/* Forward declare platform_print_backtrace for stack trace support */
void platform_print_backtrace(void);

/**
 * @brief Exit with error code and standard message, with stack trace in debug builds
 * @param code Error code (asciichat_error_status_t)
 *
 * Usage:
 *   asciichat_error_status_t err = webcam_init(...);
 *   if (err != ASCIICHAT_OK) {
 *       FATAL_ERROR(err);  // Prints "Webcam error" + stack trace + exits with 20
 *   }
 */
#define FATAL_ERROR(code)                                                                                              \
  do {                                                                                                                 \
    (void)fprintf(stderr, "\n");                                                                                       \
    (void)fprintf(stderr, "FATAL ERROR: %s\n", asciichat_error_string(code));                                          \
    (void)fprintf(stderr, "Exit code: %d\n", (int)(code));                                                             \
    (void)fprintf(stderr, "Location: %s:%d in %s()\n", __FILE__, __LINE__, __func__);                                  \
    (void)fflush(stderr);                                                                                              \
    NDEBUG_STACK_TRACE();                                                                                              \
    exit(code);                                                                                                        \
  } while (0)

/**
 * @brief Exit with error code and custom message, with stack trace in debug builds
 * @param code Error code (asciichat_error_status_t)
 * @param ... Custom message format string and arguments (printf-style)
 *
 * Usage:
 *   FATAL(ASCIICHAT_ERROR_NETWORK_BIND, "Cannot bind to port %d", port_number);
 */
#define FATAL(code, ...)                                                                                               \
  do {                                                                                                                 \
    (void)fprintf(stderr, "\n");                                                                                       \
    (void)fprintf(stderr, "FATAL ERROR: ");                                                                            \
    (void)fprintf(stderr, __VA_ARGS__);                                                                                \
    (void)fprintf(stderr, "\n");                                                                                       \
    (void)fprintf(stderr, "Exit code: %d (%s)\n", (int)(code), asciichat_error_string(code));                          \
    (void)fprintf(stderr, "Location: %s:%d in %s()\n", __FILE__, __LINE__, __func__);                                  \
    (void)fflush(stderr);                                                                                              \
    NDEBUG_STACK_TRACE();                                                                                              \
    exit(code);                                                                                                        \
  } while (0)

/**
 * @brief Helper macro to print stack trace only in debug builds
 * Internal use only - used by FATAL_ERROR, FATAL_EXIT, and FATAL macros
 */
#ifndef NDEBUG
/* Debug build - print stack trace */
#define NDEBUG_STACK_TRACE()                                                                                           \
  do {                                                                                                                 \
    (void)fprintf(stderr, "\nStack trace:\n");                                                                         \
    platform_print_backtrace();                                                                                        \
    (void)fprintf(stderr, "\n");                                                                                       \
    (void)fflush(stderr);                                                                                              \
  } while (0)
#else
/* Release build - no stack trace */
#define NDEBUG_STACK_TRACE()                                                                                           \
  do {                                                                                                                 \
  } while (0)
#endif

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

/* Logging levels */
typedef enum { LOG_DEBUG = 0, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL } log_level_t;

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

/* Safe memory allocation with error checking */
#define SAFE_MALLOC(ptr, size, cast)                                                                                   \
  do {                                                                                                                 \
    (ptr) = (cast)malloc(size);                                                                                        \
    if (!(ptr)) {                                                                                                      \
      FATAL(ASCIICHAT_ERROR_MEMORY, "Memory allocation failed: %zu bytes", (size_t)(size));                            \
    }                                                                                                                  \
  } while (0)

/* Safe zero-initialized memory allocation */
#define SAFE_CALLOC(ptr, count, size, cast)                                                                            \
  do {                                                                                                                 \
    (ptr) = (cast)calloc((count), (size));                                                                             \
    if (!(ptr)) {                                                                                                      \
      FATAL(ASCIICHAT_ERROR_MEMORY, "Memory allocation failed: %zu elements x %zu bytes", (size_t)(count),             \
            (size_t)(size));                                                                                           \
    }                                                                                                                  \
  } while (0)

/* Safe memory reallocation */
#define SAFE_REALLOC(ptr, size, cast)                                                                                  \
  do {                                                                                                                 \
    void *tmp_ptr = realloc((ptr), (size));                                                                            \
    if (!(tmp_ptr)) {                                                                                                  \
      FATAL(ASCIICHAT_ERROR_MEMORY, "Memory reallocation failed: %zu bytes", (size_t)(size));                          \
    }                                                                                                                  \
    (ptr) = (cast)(tmp_ptr);                                                                                           \
  } while (0)

/* SIMD-aligned memory allocation macros for optimal NEON/AVX performance */
#ifdef __APPLE__
/* macOS uses posix_memalign() for aligned allocation */
#define SAFE_MALLOC_ALIGNED(ptr, size, alignment, cast)                                                                \
  do {                                                                                                                 \
    int result = posix_memalign((void **)&(ptr), (alignment), (size));                                                 \
    if (result != 0 || !(ptr)) {                                                                                       \
      FATAL(ASCIICHAT_ERROR_MEMORY, "Aligned memory allocation failed: %zu bytes, %zu alignment", (size_t)(size),      \
            (size_t)(alignment));                                                                                      \
    }                                                                                                                  \
    (ptr) = (cast)(ptr);                                                                                               \
  } while (0)
#else
/* Linux/other platforms use aligned_alloc() (C11) */
#define SAFE_MALLOC_ALIGNED(ptr, size, alignment, cast)                                                                \
  do {                                                                                                                 \
    size_t aligned_size = (((size) + (alignment) - 1) / (alignment)) * (alignment);                                    \
    (ptr) = (cast)aligned_alloc((alignment), aligned_size);                                                            \
    if (!(ptr)) {                                                                                                      \
      FATAL(ASCIICHAT_ERROR_MEMORY, "Aligned memory allocation failed: %zu bytes, %zu alignment", aligned_size,        \
            (size_t)(alignment));                                                                                      \
    }                                                                                                                  \
  } while (0)
#endif

/* 16-byte aligned allocation for SIMD operations */
#define SAFE_MALLOC_SIMD(ptr, size, cast) SAFE_MALLOC_ALIGNED(ptr, size, 16, cast)

/* 16-byte aligned zero-initialized allocation */
#define SAFE_CALLOC_SIMD(ptr, count, size, cast)                                                                       \
  do {                                                                                                                 \
    size_t total_size = (count) * (size);                                                                              \
    SAFE_MALLOC_SIMD(ptr, total_size, cast);                                                                           \
    memset((ptr), 0, total_size);                                                                                      \
  } while (0)

/* Safe free that nulls the pointer - available in all builds */
#define SAFE_FREE(ptr)                                                                                                 \
  do {                                                                                                                 \
    if ((ptr) != NULL) {                                                                                               \
      free((ptr));                                                                                                     \
      (ptr) = NULL;                                                                                                    \
    }                                                                                                                  \
  } while (0)

/* Safe string copy */
#include "platform/system.h"
#define SAFE_STRNCPY(dst, src, size) platform_strlcpy((dst), (src), (size))

/* Safe string duplication with platform compatibility */
#ifdef _WIN32
#define SAFE_STRDUP(dst, src)                                                                                          \
  do {                                                                                                                 \
    (dst) = _strdup(src);                                                                                              \
    if (!(dst)) {                                                                                                      \
      FATAL(ASCIICHAT_ERROR_MEMORY, "String duplication failed for: %s", (src) ? (src) : "(null)");                    \
    }                                                                                                                  \
  } while (0)
#else
#define SAFE_STRDUP(dst, src)                                                                                          \
  do {                                                                                                                 \
    (dst) = strdup(src);                                                                                               \
    if (!(dst)) {                                                                                                      \
      FATAL(ASCIICHAT_ERROR_MEMORY, "String duplication failed for: %s", (src) ? (src) : "(null)");                    \
    }                                                                                                                  \
  } while (0)
#endif

/* Rate-limited debug logging - only logs every N calls */
#define LOG_DEBUG_EVERY(name, count, fmt, ...)                                                                         \
  do {                                                                                                                 \
    static int name##_counter = 0;                                                                                     \
    name##_counter++;                                                                                                  \
    if (name##_counter % (count) == 0) {                                                                               \
      log_debug(fmt, ##__VA_ARGS__);                                                                                   \
    }                                                                                                                  \
  } while (0)

/* Platform-safe environment variable access */
#include "platform/system.h"
#define SAFE_GETENV(name) ((char *)platform_getenv(name))

/* Platform-safe sscanf */
#define SAFE_SSCANF(str, format, ...) sscanf(str, format, __VA_ARGS__)

/* Platform-safe strerror */
#include "platform/internal.h"
#define SAFE_STRERROR(errnum) platform_strerror(errnum)

/* Platform-safe file open */
#include "platform/file.h"
#define SAFE_OPEN(path, flags, mode) platform_open(path, flags, mode)

/* Safe memory functions */
#define SAFE_MEMCPY(dest, dest_size, src, count) platform_memcpy((dest), (dest_size), (src), (count))
#define SAFE_MEMSET(dest, dest_size, ch, count) platform_memset((dest), (dest_size), (ch), (count))
#define SAFE_MEMMOVE(dest, dest_size, src, count) platform_memmove((dest), (dest_size), (src), (count))
#define SAFE_STRCPY(dest, dest_size, src) platform_strcpy((dest), (dest_size), (src))

/* Safe string formatting */
#define SAFE_SNPRINTF(buffer, buffer_size, ...) safe_snprintf((buffer), (buffer_size), __VA_ARGS__)

/* Logging functions */
void log_init(const char *filename, log_level_t level);
void log_destroy(void);
void log_set_level(log_level_t level);
log_level_t log_get_level(void);            /* Get current log level */
void log_set_terminal_output(bool enabled); /* Control stderr output to terminal */
bool log_get_terminal_output(void);         /* Get current terminal output setting */
void log_truncate_if_large(void);           /* Manually truncate large log files */
void log_msg(log_level_t level, const char *file, int line, const char *func, const char *fmt, ...);

/* Logging macros */
#define log_debug(...) log_msg(LOG_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_info(...) log_msg(LOG_INFO, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_warn(...) log_msg(LOG_WARN, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_error(...) log_msg(LOG_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_fatal(...) log_msg(LOG_FATAL, __FILE__, __LINE__, __func__, __VA_ARGS__)

/* New functions for coverage testing */

/* Memory debugging (only in debug builds, disabled when mimalloc override is active) */
#if defined(DEBUG_MEMORY) && !defined(MI_MALLOC_OVERRIDE)
void *debug_malloc(size_t size, const char *file, int line);
void debug_free(void *ptr, const char *file, int line);
void *debug_calloc(size_t count, size_t size, const char *file, int line);
void *debug_realloc(void *ptr, size_t size, const char *file, int line);

void debug_memory_report(void);
void debug_memory_set_quiet_mode(bool quiet); /* Control stderr output for memory report */

#define malloc(size) debug_malloc(size, __FILE__, __LINE__)
#define free(ptr) debug_free(ptr, __FILE__, __LINE__)
#define calloc(count, size) debug_calloc((count), (size), __FILE__, __LINE__)
#define realloc(ptr, size) debug_realloc((ptr), (size), __FILE__, __LINE__)
#endif /* DEBUG_MEMORY && !MI_MALLOC_OVERRIDE */
