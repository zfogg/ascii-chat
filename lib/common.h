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
 * Exit Codes - Process Exit Status Values (0-255)
 * ============================================================================
 * These codes are returned to the shell when the program exits.
 * Following Unix conventions where 0 = success, 1 = general error, 2 = usage error.
 *
 * Usage: Use these with exit() in src/ code only.
 * Library code should return asciichat_error_t values instead.
 */
typedef enum {
  /* Standard exit codes (0-2) - Unix conventions */
  EXIT_OK = 0,            /* Success */
  EXIT_GENERAL_ERROR = 1, /* Unspecified error */
  EXIT_USAGE_ERROR = 2,   /* Invalid command line arguments or options */

  /* Application-specific exit codes (3-127) */
  /* Initialization failures (3-19) */
  EXIT_MEMORY_ERROR = 3,        /* Memory allocation failed (OOM) */
  EXIT_CONFIG_ERROR = 4,        /* Configuration file or settings error */
  EXIT_CRYPTO_INIT_ERROR = 5,   /* Cryptographic initialization failed */
  EXIT_LOGGING_INIT_ERROR = 6,  /* Logging system initialization failed */
  EXIT_PLATFORM_INIT_ERROR = 7, /* Platform-specific initialization failed */

  /* Hardware/Device errors (20-39) */
  EXIT_WEBCAM_ERROR = 20,      /* Webcam initialization or capture failed */
  EXIT_WEBCAM_IN_USE = 21,     /* Webcam is in use by another application */
  EXIT_WEBCAM_PERMISSION = 22, /* Webcam permission denied */
  EXIT_AUDIO_ERROR = 23,       /* Audio device initialization or I/O failed */
  EXIT_AUDIO_IN_USE = 24,      /* Audio device is in use */
  EXIT_TERMINAL_ERROR = 25,    /* Terminal initialization or capability detection failed */

  /* Network errors (40-59) */
  EXIT_NETWORK_ERROR = 40,          /* General network error */
  EXIT_NETWORK_BIND_ERROR = 41,     /* Cannot bind to port (server) */
  EXIT_NETWORK_CONNECT_ERROR = 42,  /* Cannot connect to server (client) */
  EXIT_NETWORK_TIMEOUT = 43,        /* Network operation timed out */
  EXIT_NETWORK_PROTOCOL_ERROR = 44, /* Protocol violation or incompatible version */

  /* Security/Crypto errors (60-79) */
  EXIT_CRYPTO_ERROR = 60,               /* Cryptographic operation failed */
  EXIT_CRYPTO_KEY_ERROR = 61,           /* Key loading, parsing, or generation failed */
  EXIT_CRYPTO_AUTH_FAILED = 62,         /* Authentication failed */
  EXIT_CRYPTO_HANDSHAKE_FAILED = 63,    /* Cryptographic handshake failed */
  EXIT_CRYPTO_VERIFICATION_FAILED = 64, /* Signature or key verification failed */

  /* Runtime errors (80-99) */
  EXIT_THREAD_ERROR = 80,       /* Thread creation or management failed */
  EXIT_BUFFER_ERROR = 81,       /* Buffer allocation or overflow */
  EXIT_DISPLAY_ERROR = 82,      /* Display rendering or output error */
  EXIT_INVALID_STATE = 83,      /* Invalid program state */
  EXIT_RESOURCE_EXHAUSTED = 84, /* System resources exhausted */

  /* Signal/Crash handlers (100-127) */
  EXIT_SIGNAL_INTERRUPT = 100, /* Interrupted by signal (SIGINT, SIGTERM) */
  EXIT_SIGNAL_CRASH = 101,     /* Fatal signal (SIGSEGV, SIGABRT, etc.) */
  EXIT_ASSERTION_FAILED = 102, /* Assertion or invariant violation */

  /* Reserved (128-255) - Should not be used */
  /* 128+N typically means "terminated by signal N" on Unix systems */
} asciichat_exit_code_t;

/* ============================================================================
 * Internal Error Codes - Function Return Values (negative)
 * ============================================================================
 * These codes are used internally for function return values.
 * They are negative to distinguish from positive success values and zero.
 *
 * Usage: Return these from lib/ functions. Convert to exit codes using
 * asciichat_error_to_exit() before calling exit() in src/ code.
 */
typedef enum {
  ASCIICHAT_OK = 0,
  ASCIICHAT_ERR_GENERAL = -1,
  ASCIICHAT_ERR_MALLOC = -2,
  ASCIICHAT_ERR_NETWORK = -3,
  ASCIICHAT_ERR_NETWORK_SIZE = -4,
  ASCIICHAT_ERR_WEBCAM = -5,
  ASCIICHAT_ERR_WEBCAM_IN_USE = -6,
  ASCIICHAT_ERR_INVALID_PARAM = -7,
  ASCIICHAT_ERR_TIMEOUT = -8,
  ASCIICHAT_ERR_BUFFER_FULL = -9,
  ASCIICHAT_ERR_BUFFER_ACCESS = -10,
  ASCIICHAT_ERR_BUFFER_OVERFLOW = -11,
  ASCIICHAT_ERR_JPEG = -12,
  ASCIICHAT_ERR_TERMINAL = -13,
  ASCIICHAT_ERR_THREAD = -14,
  ASCIICHAT_ERR_AUDIO = -15,
  ASCIICHAT_ERR_DISPLAY = -16,
  ASCIICHAT_ERR_INVALID_FRAME = -17,
  ASCIICHAT_ERR_CRYPTO = -18,
  ASCIICHAT_ERR_CRYPTO_KEY = -19,
  ASCIICHAT_ERR_CRYPTO_AUTH = -20,
} asciichat_error_t;

/* ============================================================================
 * Error Code Conversion and String Utilities
 * ============================================================================
 */

/* Convert internal error code to exit code */
static inline asciichat_exit_code_t asciichat_error_to_exit(asciichat_error_t error) {
  switch (error) {
  case ASCIICHAT_OK:
    return EXIT_OK;
  case ASCIICHAT_ERR_MALLOC:
    return EXIT_MEMORY_ERROR;
  case ASCIICHAT_ERR_NETWORK:
  case ASCIICHAT_ERR_NETWORK_SIZE:
    return EXIT_NETWORK_ERROR;
  case ASCIICHAT_ERR_WEBCAM:
    return EXIT_WEBCAM_ERROR;
  case ASCIICHAT_ERR_WEBCAM_IN_USE:
    return EXIT_WEBCAM_IN_USE;
  case ASCIICHAT_ERR_INVALID_PARAM:
    return EXIT_USAGE_ERROR;
  case ASCIICHAT_ERR_TIMEOUT:
    return EXIT_NETWORK_TIMEOUT;
  case ASCIICHAT_ERR_BUFFER_FULL:
  case ASCIICHAT_ERR_BUFFER_ACCESS:
  case ASCIICHAT_ERR_BUFFER_OVERFLOW:
    return EXIT_BUFFER_ERROR;
  case ASCIICHAT_ERR_TERMINAL:
    return EXIT_TERMINAL_ERROR;
  case ASCIICHAT_ERR_THREAD:
    return EXIT_THREAD_ERROR;
  case ASCIICHAT_ERR_AUDIO:
    return EXIT_AUDIO_ERROR;
  case ASCIICHAT_ERR_DISPLAY:
    return EXIT_DISPLAY_ERROR;
  case ASCIICHAT_ERR_CRYPTO:
    return EXIT_CRYPTO_ERROR;
  case ASCIICHAT_ERR_CRYPTO_KEY:
    return EXIT_CRYPTO_KEY_ERROR;
  case ASCIICHAT_ERR_CRYPTO_AUTH:
    return EXIT_CRYPTO_AUTH_FAILED;
  default:
    return EXIT_GENERAL_ERROR;
  }
}

/* Get human-readable error string for internal error code */
static inline const char *asciichat_error_string(asciichat_error_t error) {
  switch (error) {
  case ASCIICHAT_OK:
    return "Success";
  case ASCIICHAT_ERR_GENERAL:
    return "General error";
  case ASCIICHAT_ERR_MALLOC:
    return "Memory allocation failed";
  case ASCIICHAT_ERR_NETWORK:
    return "Network error";
  case ASCIICHAT_ERR_NETWORK_SIZE:
    return "Network packet size error";
  case ASCIICHAT_ERR_WEBCAM:
    return "Webcam error";
  case ASCIICHAT_ERR_WEBCAM_IN_USE:
    return "Webcam already in use by another application";
  case ASCIICHAT_ERR_INVALID_PARAM:
    return "Invalid parameter";
  case ASCIICHAT_ERR_TIMEOUT:
    return "Operation timed out";
  case ASCIICHAT_ERR_BUFFER_FULL:
    return "Buffer full";
  case ASCIICHAT_ERR_BUFFER_ACCESS:
    return "Buffer access error";
  case ASCIICHAT_ERR_BUFFER_OVERFLOW:
    return "Buffer overflow";
  case ASCIICHAT_ERR_JPEG:
    return "JPEG processing error";
  case ASCIICHAT_ERR_TERMINAL:
    return "Terminal error";
  case ASCIICHAT_ERR_THREAD:
    return "Thread error";
  case ASCIICHAT_ERR_AUDIO:
    return "Audio error";
  case ASCIICHAT_ERR_DISPLAY:
    return "Display error";
  case ASCIICHAT_ERR_INVALID_FRAME:
    return "Invalid frame data";
  case ASCIICHAT_ERR_CRYPTO:
    return "Cryptographic error";
  case ASCIICHAT_ERR_CRYPTO_KEY:
    return "Cryptographic key error";
  case ASCIICHAT_ERR_CRYPTO_AUTH:
    return "Authentication failed";
  default:
    return "Unknown error";
  }
}

/* Get human-readable string for exit code */
static inline const char *asciichat_exit_code_string(asciichat_exit_code_t code) {
  switch (code) {
  case EXIT_OK:
    return "Success";
  case EXIT_GENERAL_ERROR:
    return "General error";
  case EXIT_USAGE_ERROR:
    return "Invalid command line usage";
  case EXIT_MEMORY_ERROR:
    return "Memory allocation failed";
  case EXIT_CONFIG_ERROR:
    return "Configuration error";
  case EXIT_CRYPTO_INIT_ERROR:
    return "Cryptographic initialization failed";
  case EXIT_LOGGING_INIT_ERROR:
    return "Logging initialization failed";
  case EXIT_PLATFORM_INIT_ERROR:
    return "Platform initialization failed";
  case EXIT_WEBCAM_ERROR:
    return "Webcam error";
  case EXIT_WEBCAM_IN_USE:
    return "Webcam in use by another application";
  case EXIT_WEBCAM_PERMISSION:
    return "Webcam permission denied";
  case EXIT_AUDIO_ERROR:
    return "Audio device error";
  case EXIT_AUDIO_IN_USE:
    return "Audio device in use";
  case EXIT_TERMINAL_ERROR:
    return "Terminal error";
  case EXIT_NETWORK_ERROR:
    return "Network error";
  case EXIT_NETWORK_BIND_ERROR:
    return "Cannot bind to network port";
  case EXIT_NETWORK_CONNECT_ERROR:
    return "Cannot connect to server";
  case EXIT_NETWORK_TIMEOUT:
    return "Network timeout";
  case EXIT_NETWORK_PROTOCOL_ERROR:
    return "Network protocol error";
  case EXIT_CRYPTO_ERROR:
    return "Cryptographic error";
  case EXIT_CRYPTO_KEY_ERROR:
    return "Cryptographic key error";
  case EXIT_CRYPTO_AUTH_FAILED:
    return "Authentication failed";
  case EXIT_CRYPTO_HANDSHAKE_FAILED:
    return "Cryptographic handshake failed";
  case EXIT_CRYPTO_VERIFICATION_FAILED:
    return "Signature verification failed";
  case EXIT_THREAD_ERROR:
    return "Thread error";
  case EXIT_BUFFER_ERROR:
    return "Buffer error";
  case EXIT_DISPLAY_ERROR:
    return "Display error";
  case EXIT_INVALID_STATE:
    return "Invalid program state";
  case EXIT_RESOURCE_EXHAUSTED:
    return "System resources exhausted";
  case EXIT_SIGNAL_INTERRUPT:
    return "Interrupted by signal";
  case EXIT_SIGNAL_CRASH:
    return "Terminated by fatal signal";
  case EXIT_ASSERTION_FAILED:
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
 *   FATAL_ERROR(ASCIICHAT_ERR_WEBCAM);         // Takes internal error code
 *   FATAL_EXIT(EXIT_WEBCAM_ERROR);             // Takes exit code directly
 *   FATAL(EXIT_WEBCAM_ERROR, "Custom msg");    // Exit code + custom message
 */

/* Forward declare platform_print_backtrace for stack trace support */
void platform_print_backtrace(void);

/**
 * @brief Exit with error code converted from internal error, with message and stack trace
 * @param error Internal error code (asciichat_error_t)
 *
 * Usage:
 *   asciichat_error_t err = webcam_init(...);
 *   if (err != ASCIICHAT_OK) {
 *       FATAL_ERROR(err);  // Prints "Webcam error" + stack trace + exits with 20
 *   }
 */
#define FATAL_ERROR(error)                                                                                             \
  do {                                                                                                                 \
    asciichat_exit_code_t exit_code = asciichat_error_to_exit(error);                                                  \
    (void)fprintf(stderr, "\n");                                                                                       \
    (void)fprintf(stderr, "FATAL ERROR: %s\n", asciichat_error_string(error));                                         \
    (void)fprintf(stderr, "Exit code: %d (%s)\n", exit_code, asciichat_exit_code_string(exit_code));                   \
    (void)fprintf(stderr, "Location: %s:%d in %s()\n", __FILE__, __LINE__, __func__);                                  \
    (void)fflush(stderr);                                                                                              \
    /* Print stack trace in debug builds only */                                                                       \
    NDEBUG_STACK_TRACE();                                                                                              \
    exit(exit_code);                                                                                                   \
  } while (0)

/**
 * @brief Exit with explicit exit code, with message and stack trace
 * @param code Exit code (asciichat_exit_code_t)
 *
 * Usage:
 *   FATAL_EXIT(EXIT_NETWORK_BIND_ERROR);  // Exits with code 41
 */
#define FATAL_EXIT(code)                                                                                               \
  do {                                                                                                                 \
    (void)fprintf(stderr, "\n");                                                                                       \
    (void)fprintf(stderr, "FATAL ERROR: %s\n", asciichat_exit_code_string(code));                                      \
    (void)fprintf(stderr, "Exit code: %d\n", (int)(code));                                                             \
    (void)fprintf(stderr, "Location: %s:%d in %s()\n", __FILE__, __LINE__, __func__);                                  \
    (void)fflush(stderr);                                                                                              \
    NDEBUG_STACK_TRACE();                                                                                              \
    exit(code);                                                                                                        \
  } while (0)

/**
 * @brief Exit with explicit exit code and custom message, with stack trace
 * @param code Exit code (asciichat_exit_code_t)
 * @param ... Custom message format string and arguments (printf-style)
 *
 * Usage:
 *   FATAL(EXIT_NETWORK_BIND_ERROR, "Cannot bind to port %d", port_number);
 */
#define FATAL(code, ...)                                                                                               \
  do {                                                                                                                 \
    (void)fprintf(stderr, "\n");                                                                                       \
    (void)fprintf(stderr, "FATAL ERROR: ");                                                                            \
    (void)fprintf(stderr, __VA_ARGS__);                                                                                \
    (void)fprintf(stderr, "\n");                                                                                       \
    (void)fprintf(stderr, "Exit code: %d (%s)\n", (int)(code), asciichat_exit_code_string(code));                      \
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

// RGB value clamping utility function
static inline uint8_t clamp_rgb(int value) {
  if (value < 0)
    return 0;
  if (value > 255)
    return 255;
  return (uint8_t)value;
}

#define ASCIICHAT_WEBCAM_ERROR_STRING "Webcam capture failed"

// Frame rate configuration - Windows terminals struggle with high FPS
#ifdef _WIN32
#define DEFAULT_MAX_FPS 30 // Windows terminals can't handle more than this
#else
#define DEFAULT_MAX_FPS 60 // macOS/Linux terminals can handle higher rates
#endif

// Allow runtime override via environment variable or command line
extern int g_max_fps;
#define MAX_FPS (g_max_fps > 0 ? g_max_fps : DEFAULT_MAX_FPS)

#define FRAME_INTERVAL_MS (1000 / MAX_FPS)

#define FRAME_BUFFER_CAPACITY (MAX_FPS / 4)

// Global variables to store last known image dimensions for aspect ratio
// recalculation
extern unsigned short int last_image_width, last_image_height;

/* Logging levels */
typedef enum { LOG_DEBUG = 0, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL } log_level_t;

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
      log_error("Memory allocation failed: %zu bytes", (size_t)(size));                                                \
      /*return ASCIICHAT_ERR_MALLOC;*/                                                                                 \
      exit(ASCIICHAT_ERR_MALLOC);                                                                                      \
    }                                                                                                                  \
  } while (0)

/* Safe zero-initialized memory allocation */
#define SAFE_CALLOC(ptr, count, size, cast)                                                                            \
  do {                                                                                                                 \
    (ptr) = (cast)calloc((count), (size));                                                                             \
    if (!(ptr)) {                                                                                                      \
      log_error("Memory allocation failed: %zu elements x %zu bytes", (size_t)(count), (size_t)(size));                \
      /*return ASCIICHAT_ERR_MALLOC;*/                                                                                 \
      exit(ASCIICHAT_ERR_MALLOC);                                                                                      \
    }                                                                                                                  \
  } while (0)

/* Safe memory reallocation */
#define SAFE_REALLOC(ptr, size, cast)                                                                                  \
  do {                                                                                                                 \
    void *tmp_ptr = realloc((ptr), (size));                                                                            \
    if (!(tmp_ptr)) {                                                                                                  \
      log_error("Memory reallocation failed: %zu bytes", (size_t)(size));                                              \
      /*return ASCIICHAT_ERR_MALLOC;*/                                                                                 \
      exit(ASCIICHAT_ERR_MALLOC);                                                                                      \
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
      log_error("Aligned memory allocation failed: %zu bytes, %zu alignment", (size_t)(size), (size_t)(alignment));    \
      exit(ASCIICHAT_ERR_MALLOC);                                                                                      \
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
      log_error("Aligned memory allocation failed: %zu bytes, %zu alignment", aligned_size, (size_t)(alignment));      \
      exit(ASCIICHAT_ERR_MALLOC);                                                                                      \
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
      log_error("String duplication failed for: %s", (src) ? (src) : "(null)");                                        \
      exit(ASCIICHAT_ERR_MALLOC);                                                                                      \
    }                                                                                                                  \
  } while (0)
#else
#define SAFE_STRDUP(dst, src)                                                                                          \
  do {                                                                                                                 \
    (dst) = strdup(src);                                                                                               \
    if (!(dst)) {                                                                                                      \
      log_error("String duplication failed for: %s", (src) ? (src) : "(null)");                                        \
      exit(ASCIICHAT_ERR_MALLOC);                                                                                      \
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

void format_bytes_pretty(size_t bytes, char *out, size_t out_capacity);

/* Safe parsing functions using strtoul instead of sscanf */
int safe_parse_size_message(const char *message, unsigned int *width, unsigned int *height);
int safe_parse_audio_message(const char *message, unsigned int *num_samples);

/* New functions for coverage testing */

/* Memory debugging (only in debug builds, disabled when mimalloc override is active) */
#if defined(DEBUG_MEMORY) && !defined(MI_MALLOC_OVERRIDE)
void *debug_malloc(size_t size, const char *file, int line);
void debug_free(void *ptr, const char *file, int line);
void debug_memory_report(void);
void debug_memory_set_quiet_mode(bool quiet); /* Control stderr output for memory report */
void *debug_calloc(size_t count, size_t size, const char *file, int line);
void *debug_realloc(void *ptr, size_t size, const char *file, int line);

#define malloc(size) debug_malloc(size, __FILE__, __LINE__)
#define free(ptr) debug_free(ptr, __FILE__, __LINE__)
#define calloc(count, size) debug_calloc((count), (size), __FILE__, __LINE__)
#define realloc(ptr, size) debug_realloc((ptr), (size), __FILE__, __LINE__)
#endif /* DEBUG_MEMORY && !MI_MALLOC_OVERRIDE */

/* Path utilities (shared between logging, backtraces, etc.) */
/**
 * Extract relative path from an absolute path.
 * Searches for PROJECT_SOURCE_ROOT and returns the path relative to it.
 * Handles both Unix (/) and Windows (\) path separators.
 * Falls back to just the filename if source root not found.
 *
 * @param file Absolute file path (typically from __FILE__)
 * @return Relative path from project root, or filename if not found
 */
const char *extract_project_relative_path(const char *file);
