#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Platform-specific includes
#ifndef _WIN32
#include <unistd.h>
#endif

// Platform-specific includes - must be first
#include "common.h"

/**
 * @file asciichat_errno.h
 * @brief ⚠️‼️ Comprehensive thread-local error context system for ascii-chat
 * @ingroup errno
 * @addtogroup errno
 * @{
 *
 * ## Overview
 *
 * This header defines ascii-chat's centralized error handling system: a thread-local
 * error number system with full context capture including location information, stack
 * traces (debug builds), and system error integration. Rather than the limited errno
 * semantics of standard C (which loses context), this system captures the full error
 * story: where it happened, why it happened, and what the calling code was trying to do.
 *
 * This is NOT a cosmetic improvement. Error handling is one of the most critical parts
 * of ascii-chat. With 2,880+ SET_ERRNO calls throughout 250+ library files and 50+
 * source files, nearly every significant operation in the codebase uses this system.
 * The densest users are network (91 calls in packet parsing alone), crypto (86+ in SSH
 * key parsing, 86+ in crypto context, 68+ in handshakes), and utilities (53+ in IP parsing).
 *
 * ## Why This Matters
 *
 * Good error messages save hours of debugging. Without context:
 * - Network packet parsing fails silently with "invalid packet"
 * - Crypto handshakes break with "key exchange failed"
 * - IP parsing rejects addresses with "parse error"
 *
 * With this system:
 * - "Invalid packet magic: 0xDEADBEEF (expected 0xCAFEBABE) [packet.c:247 in parse_packet_header]"
 * - "Server signature verification FAILED - rejecting connection [client.c:892 in handle_key_exchange_reply]"
 * - "Failed to parse IP address: unexpected port specifier at position 15 [ip.c:184 in parse_ipv4_address]"
 *
 * ## Core Architecture
 *
 * **Thread-Local Error Context** (asciichat_error_context_t):
 * - Error code: One of 256 possible asciichat_error_t values
 * - Location: File, line, function where error occurred (debug builds capture all three)
 * - Context message: Formatted printf-style explanation of what failed and why
 * - Stack trace: Full backtrace from error site (debug builds with ENABLE_ERRNO_BACKTRACES)
 * - System error: Integration with errno (POSIX) and WSA errors (Windows)
 * - Timestamp: When the error occurred (microseconds since epoch)
 *
 * **Library Interface (lib/ code)**:
 * - Use SET_ERRNO(code, "format", ...) to set errors with context
 * - Use SET_ERRNO_SYS(code, "format", ...) to capture system errno alongside app error
 * - Errors bubble up through the call stack (SET_ERRNO in deeper functions sets context)
 * - Higher-level code can CHECK errors and decide: propagate, recover, or fail
 *
 * **Application Interface (src/ code)**:
 * - Use HAS_ERRNO(&ctx) to check if an error occurred and get full context
 * - Use GET_ERRNO() to check just the error code (returns ASCIICHAT_OK if no error)
 * - Use CLEAR_ERRNO() to reset error state after handling
 * - Integrate with FATAL() to exit with full error details
 *
 * ## Usage Patterns (From Real Code)
 *
 * ### Pattern 1: Input Validation (Network Packet Parsing)
 * ~1000+ locations - the most common pattern
 * @code
 * if (!header || !pkt_type) {
 *   return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: header=%p, pkt_type=%p", header, pkt_type);
 * }
 * if (magic != PACKET_MAGIC) {
 *   return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid packet magic: 0x%x (expected 0x%x)",
 *                    magic, PACKET_MAGIC);
 * }
 * if (len > MAX_PACKET_SIZE) {
 *   return SET_ERRNO(ERROR_NETWORK_SIZE, "Packet too large: %u > %d", len, MAX_PACKET_SIZE);
 * }
 * @endcode
 *
 * ### Pattern 2: Memory Allocation Failure (Crypto Operations)
 * ~400+ locations
 * @code
 * uint8_t *key = SAFE_MALLOC(key_size, uint8_t *);
 * if (!key) {
 *   return SET_ERRNO(ERROR_MEMORY, "Failed to allocate %zu bytes for ephemeral key", key_size);
 * }
 * @endcode
 *
 * ### Pattern 3: System Call Failure with errno (File I/O, Networking)
 * ~166 locations - use SET_ERRNO_SYS to capture both errno and context
 * @code
 * int fd = open(path, O_RDONLY);
 * if (fd < 0) {
 *   return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to open config: %s", path);
 *   // Logs: "Failed to open config: /etc/ascii-chat.conf (errno: 2 - No such file)"
 * }
 * @endcode
 *
 * ### Pattern 4: Error Context Propagation
 * ~50+ locations - deepest call checks error without overwriting
 * @code
 * // In crypto_perform_handshake() - deep function
 * asciichat_error_t result = validate_server_signature(sig_data, server_key);
 * if (result != ASCIICHAT_OK) {
 *   return result;  // Propagates original error context up the call stack
 * }
 * @endcode
 *
 * ### Pattern 5: Application-Level Error Handling
 * ~30 locations - entry points check errors after library calls
 * @code
 * // In src/client/main.c
 * asciichat_error_context_t err_ctx;
 * if (HAS_ERRNO(&err_ctx)) {
 *   FATAL(err_ctx.code, "Connection failed: %s at %s:%d",
 *         err_ctx.context_message, err_ctx.file, err_ctx.line);
 * }
 * @endcode
 *
 * ## Thread Safety Guarantee
 *
 * Each thread has completely independent error state via __thread storage:
 * - Setting error in thread A doesn't affect thread B's error state
 * - No mutex needed (no contention, no lock ordering issues)
 * - Perfect for multi-threaded network servers with per-client threads
 *
 * ## Context Capture Details
 *
 * **In Release Builds (NDEBUG)**:
 * - File, line, function are NULL (stripped for performance)
 * - Context message is captured (explains what went wrong)
 * - System errno captured if SET_ERRNO_SYS used
 * - No stack trace (compiler optimizations make backtraces unreliable)
 * - Total overhead: one string allocation + context copy per error
 *
 * **In Debug Builds**:
 * - File, line, function are captured (automatic via __FILE__, __LINE__, __func__)
 * - Context message is formatted with arguments
 * - System errno and/or WSA error captured
 * - Stack trace captured if ENABLE_ERRNO_BACKTRACES defined (configurable)
 * - Timestamp captured for performance analysis
 *
 * ## Error Statistics (Monitoring & Diagnostics)
 *
 * The system tracks cumulative error counts per error code across all threads:
 * @code
 * asciichat_error_stats_t stats = asciichat_error_stats_get();
 * printf("Total errors seen: %llu\\n", stats.total_errors);
 * printf("Network errors: %llu\\n", stats.error_counts[ERROR_NETWORK]);
 * printf("Crypto errors: %llu\\n", stats.error_counts[ERROR_CRYPTO]);
 * @endcode
 *
 * Useful for:
 * - Detecting error storms (too many of one error type = bug)
 * - Performance monitoring (which errors consume resources?)
 * - Testing validation (no errors in unit tests = good)
 * - Production alerting (sudden spike in specific error = something broke)
 *
 * ## Common Error Codes (From error_codes.h)
 *
 * - `ERROR_MEMORY`: Allocation failed (malloc/calloc)
 * - `ERROR_INVALID_PARAM`: Invalid function argument
 * - `ERROR_INVALID_STATE`: Operation invalid in current state
 * - `ERROR_NETWORK_*`: Network-related errors (bind, listen, connect, etc.)
 * - `ERROR_CRYPTO`: Cryptographic operation failed
 * - `ERROR_CONFIG`: Configuration file or parsing error
 * - `ERROR_AUDIO`: Audio capture/playback error
 * - See lib/common/error_codes.h for complete list
 *
 * ## Debugging with Error Context
 *
 * **In GDB**:
 * @code
 * (gdb) print asciichat_errno_context
 * $1 = {code = ERROR_CRYPTO, file = "lib/crypto/handshake/client.c", line = 892,
 *       function = "handle_key_exchange_reply", context_message = "Signature verification failed",
 *       timestamp = 1730851234567890, ...}
 * @endcode
 *
 * **In Running Program** (if dumped by FATAL):
 * @code
 * [ERROR] ERROR_CRYPTO: Signature verification failed
 * Location: lib/crypto/handshake/client.c:892 in handle_key_exchange_reply()
 * Timestamp: 2025-10-06 14:07:14.567890 UTC
 * System error: 0 (success)
 * Stack trace (11 frames):
 *   [0] handle_key_exchange_reply (lib/crypto/handshake/client.c:892)
 *   [1] crypto_perform_handshake (lib/crypto/crypto.c:156)
 *   [2] connection_authenticate (src/client/connection.c:289)
 *   ... (8 more frames)
 * @endcode
 *
 * @note Use SET_ERRNO() in library code. Macros auto-capture file/line/function.
 * @note Use HAS_ERRNO() in application code to check errors after library calls.
 * @note Stack traces are only captured in debug builds with ENABLE_ERRNO_BACKTRACES.
 * @note Thread-local storage ensures thread safety automatically (no synchronization).
 * @note This is the error handling backbone of ascii-chat. Understand it well.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

/* ============================================================================
 * ascii-chat Error Number System
 * ============================================================================
 * Thread-local error context that captures:
 * - Error code (asciichat_error_t)
 * - File, line, function where error occurred
 * - Stack trace at error site (debug builds)
 * - Optional custom context message
 * - Timestamp of error
 * - System error context (errno)
 */

/* Forward declaration of backtrace_t - will be typedef'd next */
typedef struct {
  void *ptrs[32];       ///< Raw return addresses
  char **symbols;       ///< Symbolized strings (NULL until backtrace_symbolize called)
  int count;            ///< Number of frames captured
  bool tried_symbolize; ///< Track if we've already tried to symbolize (prevents duplicate work)
} backtrace_t;

/**
 * @brief Error context structure
 *
 * Contains full error context information including error code, location,
 * stack trace (debug builds), and optional context message. This structure
 * is stored in thread-local storage for thread-safe error handling.
 *
 * @note Stack traces are only captured in debug builds (DEBUG or ENABLE_ERRNO_BACKTRACES).
 * @note Context message is dynamically allocated and must be freed by the system.
 * @note Thread-local storage ensures each thread has independent error state.
 *
 * @ingroup errno
 */
typedef struct {
  asciichat_error_t code; ///< Error code (asciichat_error_t enum value)
  const char *file;       ///< Source file where error occurred (NULL in release builds)
  int line;               ///< Line number where error occurred (0 in release builds)
  const char *function;   ///< Function name where error occurred (NULL in release builds)
  char *context_message;  ///< Optional custom message (dynamically allocated, owned by system)
  uint64_t timestamp;     ///< Timestamp when error occurred (microseconds since epoch)
  int system_errno;       ///< System errno value (if applicable, 0 otherwise)
  int wsa_error;          ///< Windows socket error code (if applicable, 0 otherwise)
  backtrace_t backtrace;  ///< Stack trace (debug builds only)
  bool has_system_error;  ///< True if system_errno is valid
  bool has_wsa_error;     ///< True if wsa_error is valid
} asciichat_error_context_t;

/* ============================================================================
 * Thread-Local Error Storage
 * ============================================================================
 */

/**
 * @brief Thread-local error context storage
 *
 * Each thread has its own independent error context. This ensures thread-safe
 * error handling without synchronization overhead.
 *
 * @note Use HAS_ERRNO() macro to check for errors and get context.
 * @note Context is automatically updated when errors are set via SET_ERRNO().
 *
 * @ingroup errno
 */

/* Include logging early before extern "C" to avoid C++ headers inside extern "C" */
#include "log/log.h"

#ifdef __cplusplus
extern "C" {
#endif

extern __thread asciichat_error_context_t asciichat_errno_context;

/**
 * @brief Thread-local current error code
 *
 * Current error code for the calling thread. Set to ASCIICHAT_OK when no error.
 * Updated automatically when errors are set via SET_ERRNO() macros.
 *
 * @note Use GET_ERRNO() macro to read the current error code.
 * @note Use CLEAR_ERRNO() macro to clear the error state.
 *
 * @ingroup errno
 */
extern __thread asciichat_error_t asciichat_errno;

/* ============================================================================
 * Library Error Setting Macros
 * ============================================================================
 * Use these in lib/ code to set error context
 */

/**
 * @brief Set error code with custom context message and log it, returning the error code
 * @param code Error code to set
 * @param context_msg Custom message explaining the error
 *
 * Usage in lib/ code:
 *   if (bind(sockfd, ...) < 0) {
 *       return SET_ERRNO(ERROR_NETWORK_BIND, "Cannot bind to port %d", port);
 *   }
 */
#ifdef NDEBUG
#define SET_ERRNO(code, context_msg, ...)                                                                              \
  (asciichat_set_errno_with_message(code, NULL, 0, NULL, context_msg, ##__VA_ARGS__),                                  \
   log_error("SET_ERRNO: " context_msg " (code: %d, meaning: %s)", ##__VA_ARGS__, code, asciichat_error_string(code)), \
   (code))
#else
#define SET_ERRNO(code, context_msg, ...)                                                                              \
  (asciichat_set_errno_with_message(code, __FILE__, __LINE__, __func__, context_msg, ##__VA_ARGS__),                   \
   log_error("SET_ERRNO: " context_msg " (code: %d, meaning: %s)", ##__VA_ARGS__, code, asciichat_error_string(code)), \
   (code))
#endif

/**
 * @brief Set error code with custom message and system error context, returning the error code
 * @param code Error code to set
 * @param context_msg Custom message explaining the error
 *
 * Usage in lib/ code:
 *   if (open(file, O_RDONLY) < 0) {
 *       return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to open config file: %s", path);
 *   }
 */
#ifdef NDEBUG
#define SET_ERRNO_SYS(code, context_msg, ...)                                                                          \
  (asciichat_set_errno_with_system_error_and_message(code, NULL, 0, NULL, platform_get_last_error(), context_msg,      \
                                                     ##__VA_ARGS__),                                                   \
   log_error("SETERRNO_SYS: " context_msg " (code: %d - %s, system error: %d - %s)", ##__VA_ARGS__, code,              \
             asciichat_error_string(code), platform_get_last_error(), platform_strerror(platform_get_last_error())),   \
   (code))
#else
#define SET_ERRNO_SYS(code, context_msg, ...)                                                                          \
  (asciichat_set_errno_with_system_error_and_message(code, __FILE__, __LINE__, __func__, platform_get_last_error(),    \
                                                     context_msg, ##__VA_ARGS__),                                      \
   log_error("SETERRNO_SYS: " context_msg " (code: %d - %s, system error: %d - %s)", ##__VA_ARGS__, code,              \
             asciichat_error_string(code), platform_get_last_error(), platform_strerror(platform_get_last_error())),   \
   (code))
#endif

/* ============================================================================
 * Error Logging Integration Macros
 * ============================================================================
 * These macros combine error setting with automatic logging
 */

/* ============================================================================
 * Application Error Checking Macros
 * ============================================================================
 * Use these in src/ code to check and handle errors
 */

/**
 * @brief Check if an error occurred and get full context
 * @param var Variable to store error context
 *
 * Usage in src/ code:
 *   asciichat_error_context_t err_ctx;
 *   if (HAS_ERRNO(&err_ctx)) {
 *       FATAL(err_ctx.code, "Library error occurred");
 *   }
 */
#define HAS_ERRNO(var) asciichat_has_errno(var)

/**
 * @brief Clear the current error state
 */
#define CLEAR_ERRNO() asciichat_clear_errno()

/**
 * @brief Get current error code (0 if no error)
 */
#define GET_ERRNO() asciichat_get_errno()

/* ============================================================================
 * Implementation Functions
 * @{
 */

/**
 * @brief Set error code with basic context
 * @param code Error code to set (asciichat_error_t)
 * @param file Source file name (can be NULL)
 * @param line Line number (0 if not provided)
 * @param function Function name (can be NULL)
 * @param context_message Custom context message (can be NULL, will be copied)
 *
 * Sets the thread-local error code with basic context information. This is
 * the low-level function used by SET_ERRNO() macros.
 *
 * @note In debug builds, automatically captures stack trace if enabled.
 * @note Context message is copied if provided (caller can free original).
 * @note Thread-safe: Uses thread-local storage (no synchronization needed).
 *
 * @warning Use SET_ERRNO() macros instead of calling this directly.
 *
 * @ingroup errno
 */
void asciichat_set_errno(asciichat_error_t code, const char *file, int line, const char *function,
                         const char *context_message);

/**
 * @brief Set error code with formatted message
 * @param code Error code to set (asciichat_error_t)
 * @param file Source file name (can be NULL)
 * @param line Line number (0 if not provided)
 * @param function Function name (can be NULL)
 * @param format Format string (printf-style)
 * @param ... Format arguments
 *
 * Sets the thread-local error code with a formatted context message. This is
 * the low-level function used by SET_ERRNO() macros.
 *
 * @note Message is formatted using vsnprintf and stored in error context.
 * @note In debug builds, automatically captures stack trace if enabled.
 * @note Thread-safe: Uses thread-local storage.
 *
 * @warning Use SET_ERRNO() macros instead of calling this directly.
 *
 * @ingroup errno
 */
void asciichat_set_errno_with_message(asciichat_error_t code, const char *file, int line, const char *function,
                                      const char *format, ...);

/**
 * @brief Set error code with system error (errno)
 * @param code Error code to set (asciichat_error_t)
 * @param file Source file name (can be NULL)
 * @param line Line number (0 if not provided)
 * @param function Function name (can be NULL)
 * @param sys_errno System errno value
 *
 * Sets the thread-local error code with system error context. Captures the
 * system errno value for detailed error reporting.
 *
 * @note System errno is stored in error context for later retrieval.
 * @note In debug builds, automatically captures stack trace if enabled.
 * @note Thread-safe: Uses thread-local storage.
 *
 * @warning Use SET_ERRNO_SYS() macro instead of calling this directly.
 *
 * @ingroup errno
 */
void asciichat_set_errno_with_system_error(asciichat_error_t code, const char *file, int line, const char *function,
                                           int sys_errno);

/**
 * @brief Set error code with system error and formatted message
 * @param code Error code to set (asciichat_error_t)
 * @param file Source file name (can be NULL)
 * @param line Line number (0 if not provided)
 * @param function Function name (can be NULL)
 * @param sys_errno System errno value
 * @param format Format string (printf-style)
 * @param ... Format arguments
 *
 * Sets the thread-local error code with both system error context and a
 * formatted message. This is the low-level function used by SET_ERRNO_SYS().
 *
 * @note Combines system error capture with formatted message.
 * @note In debug builds, automatically captures stack trace if enabled.
 * @note Thread-safe: Uses thread-local storage.
 *
 * @warning Use SET_ERRNO_SYS() macro instead of calling this directly.
 *
 * @ingroup errno
 */
void asciichat_set_errno_with_system_error_and_message(asciichat_error_t code, const char *file, int line,
                                                       const char *function, int sys_errno, const char *format, ...);

/**
 * @brief Set error code with Windows socket error (WSA error)
 * @param code Error code to set (asciichat_error_t)
 * @param file Source file name (can be NULL)
 * @param line Line number (0 if not provided)
 * @param function Function name (can be NULL)
 * @param wsa_error Windows socket error code
 *
 * Sets the thread-local error code with Windows-specific socket error context.
 * Used for Windows socket operations that fail with WSA error codes.
 *
 * @note WSA error is stored in error context for later retrieval.
 * @note This function is only useful on Windows platforms.
 * @note Thread-safe: Uses thread-local storage.
 *
 * @ingroup errno
 */
void asciichat_set_errno_with_wsa_error(asciichat_error_t code, const char *file, int line, const char *function,
                                        int wsa_error);

/**
 * @brief Check if error occurred and get full context
 * @param context Output structure for error context (must not be NULL)
 * @return true if error occurred, false if no error
 *
 * Checks if an error has occurred and copies the full error context into
 * the provided structure. This is the low-level function used by HAS_ERRNO().
 *
 * @note Returns false if no error has occurred (asciichat_errno == ASCIICHAT_OK).
 * @note If error occurred, context structure is filled with all error information.
 * @note Thread-safe: Uses thread-local storage.
 *
 * @warning Use HAS_ERRNO() macro instead of calling this directly.
 *
 * @ingroup errno
 */
bool asciichat_has_errno(asciichat_error_context_t *context);

/**
 * @brief Check if current error has WSA error code
 * @return true if error has WSA error code, false otherwise
 *
 * Checks whether the current thread-local error context includes a Windows
 * socket error (WSA error) code. Useful for platform-specific error handling.
 *
 * @note Only meaningful on Windows platforms.
 * @note Returns false if no error occurred or error doesn't have WSA code.
 * @note Thread-safe: Uses thread-local storage.
 *
 * @ingroup errno
 */
bool asciichat_has_wsa_error(void);

/**
 * @brief Clear the current error state
 *
 * Clears the thread-local error state, resetting error code to ASCIICHAT_OK
 * and freeing any associated context message. This is the low-level function
 * used by CLEAR_ERRNO() macro.
 *
 * @note Error context structure is reset to default state.
 * @note Any context message is freed automatically.
 * @note Thread-safe: Uses thread-local storage.
 *
 * @warning Use CLEAR_ERRNO() macro instead of calling this directly.
 *
 * @ingroup errno
 */
void asciichat_clear_errno(void);

/**
 * @brief Get current error code
 * @return Current error code (ASCIICHAT_OK if no error)
 *
 * Returns the current thread-local error code. Returns ASCIICHAT_OK if no
 * error has occurred. This is the low-level function used by GET_ERRNO().
 *
 * @note Thread-safe: Uses thread-local storage.
 * @note For full error context, use HAS_ERRNO() instead.
 *
 * @warning Use GET_ERRNO() macro instead of calling this directly.
 *
 * @ingroup errno
 */
asciichat_error_t asciichat_get_errno(void);

/**
 * @brief Exit with error code and context (used by FATAL macro)
 * @param code Error code (asciichat_error_t)
 * @param file Source file name (can be NULL)
 * @param line Line number (0 if not provided)
 * @param function Function name (can be NULL)
 * @param format Format string (printf-style)
 * @param ... Format arguments
 *
 * Low-level function used by FATAL() macro. Sets error context and exits
 * the program with the specified error code. In debug builds, prints stack
 * trace before exiting.
 *
 * @note This function never returns (terminates program).
 * @note In debug builds, prints full error context and stack trace.
 * @note Thread-safe: Uses thread-local storage.
 *
 * @warning This function terminates the program. Use FATAL() macro instead
 *          of calling this directly.
 *
 * @ingroup errno
 */
void asciichat_fatal_with_context(asciichat_error_t code, const char *file, int line, const char *function,
                                  const char *format, ...);

/**
 * @brief Print full error context to stderr
 * @param context Error context to print (must not be NULL)
 *
 * Prints comprehensive error information including error code, location,
 * context message, system errors, and stack trace (if available) to stderr.
 * Useful for debugging and error reporting.
 *
 * @note Stack trace is only printed in debug builds.
 * @note System errors (errno, WSA) are included if present.
 * @note Timestamp and location information are included.
 *
 * @ingroup errno
 */
void asciichat_print_error_context(const asciichat_error_context_t *context);

/** @} */

/* ============================================================================
 * Debug/Development Utilities
 * ============================================================================
 */

#if defined(DEBUG) || defined(ENABLE_ERRNO_BACKTRACES)
/**
 * @brief Print full error context including stack trace
 * @param context Error context to print
 */
#define PRINT_ERRNO_CONTEXT(context) asciichat_print_error_context(context)

/**
 * @brief Assert that no error occurred
 */
#define ASSERT_NO_ERRNO()                                                                                              \
  do {                                                                                                                 \
    asciichat_error_t err = asciichat_get_errno();                                                                     \
    if (err != ASCIICHAT_OK) {                                                                                         \
      asciichat_error_context_t ctx;                                                                                   \
      asciichat_has_errno(&ctx);                                                                                       \
      asciichat_print_error_context(&ctx);                                                                             \
      abort();                                                                                                         \
    }                                                                                                                  \
  } while (0)

/**
 * @brief Print current error context if any error exists
 */
#define PRINT_ERRNO_IF_ERROR()                                                                                         \
  do {                                                                                                                 \
    asciichat_error_context_t ctx;                                                                                     \
    if (HAS_ERRNO(&ctx)) {                                                                                             \
      asciichat_print_error_context(&ctx);                                                                             \
    }                                                                                                                  \
  } while (0)

#else
// No-op versions for Release builds
#define PRINT_ERRNO_CONTEXT(context) ((void)(context))
#define ASSERT_NO_ERRNO() ((void)0)
#define PRINT_ERRNO_IF_ERROR() ((void)0)
#endif

/* ============================================================================
 * Error Statistics and Monitoring
 * @{
 */

/**
 * @brief Error statistics structure
 *
 * Tracks cumulative error statistics across all threads for monitoring
 * and analysis. Statistics include per-error-code counts and aggregate
 * information.
 *
 * @note Statistics are aggregated across all threads.
 * @note Counts are cumulative since initialization.
 *
 * @ingroup errno
 */
typedef struct {
  /** @brief Count for each error code (256 possible error codes) */
  uint64_t error_counts[256];
  /** @brief Total number of errors recorded (sum of all error_counts) */
  uint64_t total_errors;
  /** @brief Timestamp of last error (microseconds since epoch) */
  uint64_t last_error_time;
  /** @brief Error code of last error recorded */
  asciichat_error_t last_error_code;
} asciichat_error_stats_t;

/**
 * @brief Initialize error statistics system
 *
 * Initializes the error statistics tracking system. Must be called before
 * recording any error statistics. Statistics are initialized to zero.
 *
 * @note Idempotent: Safe to call multiple times (no-op after first call).
 * @note Thread-safe: Can be called from any thread.
 *
 * @ingroup errno
 */
void asciichat_error_stats_init(void);

/**
 * @brief Record an error in statistics
 * @param code Error code to record (asciichat_error_t)
 *
 * Records an error occurrence in the statistics system. Updates per-error-code
 * count, total error count, and last error information.
 *
 * @note Thread-safe: Can be called from multiple threads simultaneously.
 * @note Statistics counters are updated atomically.
 *
 * @ingroup errno
 */
void asciichat_error_stats_record(asciichat_error_t code);

/**
 * @brief Print error statistics to stderr
 *
 * Prints comprehensive error statistics including per-error-code counts,
 * total errors, and last error information. Useful for periodic monitoring
 * and debugging.
 *
 * @note Statistics are formatted and printed at INFO level.
 * @note Requires statistics system to be initialized.
 *
 * @ingroup errno
 */
void asciichat_error_stats_print(void);

/**
 * @brief Reset all error statistics to zero
 *
 * Resets all error statistics counters to zero. Useful for resetting
 * statistics between test runs or monitoring periods.
 *
 * @note Thread-safe: Can be called from any thread.
 * @note All counters are reset to zero (total_errors, error_counts, etc.).
 *
 * @ingroup errno
 */
void asciichat_error_stats_reset(void);

/**
 * @brief Get current error statistics
 * @return Current error statistics structure (copy)
 *
 * Retrieves a copy of the current error statistics. Returns all statistics
 * including per-error-code counts, total errors, and last error information.
 *
 * @note Returns a copy of statistics (caller can modify without affecting
 *       internal state).
 * @note Thread-safe: Can be called from any thread.
 *
 * @ingroup errno
 */
asciichat_error_stats_t asciichat_error_stats_get(void);

/** @} */

/* ============================================================================
 * Thread-Safe Error Propagation
 * @{
 *
 * For multi-threaded scenarios where errors need to be propagated across
 * thread boundaries. These functions allow storing error codes per-thread
 * for later retrieval.
 */

/**
 * @brief Get error code for a specific thread
 * @param thread_id Thread ID to query
 * @return Error code for thread, or ASCIICHAT_OK if no error
 *
 * Retrieves the error code for a specific thread. Useful for checking errors
 * from other threads or propagating errors across thread boundaries.
 *
 * @note Thread ID must be valid (positive integer).
 * @note Returns ASCIICHAT_OK if thread has no error or thread ID is invalid.
 *
 * @ingroup errno
 */
asciichat_error_t asciichat_get_thread_error(int thread_id);

/**
 * @brief Set error code for a specific thread
 * @param thread_id Thread ID to set error for
 * @param code Error code to set (asciichat_error_t)
 *
 * Sets the error code for a specific thread. Useful for propagating errors
 * from one thread to another or storing thread-specific error states.
 *
 * @note Thread ID must be valid (positive integer).
 * @note This is separate from thread-local error state (asciichat_errno).
 *
 * @ingroup errno
 */
void asciichat_set_thread_error(int thread_id, asciichat_error_t code);

/**
 * @brief Clear error code for a specific thread
 * @param thread_id Thread ID to clear error for
 *
 * Clears the error code for a specific thread, resetting it to ASCIICHAT_OK.
 *
 * @note Thread ID must be valid (positive integer).
 *
 * @ingroup errno
 */
void asciichat_clear_thread_error(int thread_id);

/** @} */

/* ============================================================================
 * Error System Control Functions
 * @{
 */

/**
 * @brief Suppress error logging and reporting
 * @param suppress If true, suppress error logging; if false, enable logging
 *
 * Controls whether errors are automatically logged when set. When suppressed,
 * errors are still set in thread-local storage but logging is disabled.
 * Useful for testing or when error logging would be too verbose.
 *
 * @note Suppression only affects automatic logging, not manual error checking.
 * @note Errors can still be checked using HAS_ERRNO() and GET_ERRNO().
 *
 * @ingroup errno
 */
void asciichat_errno_suppress(bool suppress);

/**
 * @brief Cleanup error system resources
 *
 * Cleans up error system resources including statistics and thread error
 * storage. Should be called at application shutdown after all error handling
 * is complete.
 *
 * @note Safe to call multiple times (no-op after first call).
 * @note All error statistics and context messages are freed.
 *
 * @ingroup errno
 */
void asciichat_errno_destroy(void);

/** @} */

/* ============================================================================
 * Application Error Checking Macros
 * ============================================================================
 * Use these in src/ code to check and log errors
 */

/**
 * @brief Check if any error occurred and log it if so
 * @param message Custom message to log
 *
 * Usage in src/ code:
 *   if (asciichat_errno != ASCIICHAT_OK) {
 *       LOG_ERRNO_IF_SET("Operation failed");
 *   }
 */
#define LOG_ERRNO_IF_SET(message)                                                                                      \
  do {                                                                                                                 \
    if (asciichat_errno != ASCIICHAT_OK) {                                                                             \
      asciichat_print_error_context(&asciichat_errno_context);                                                         \
    }                                                                                                                  \
  } while (0)

/**
 * @brief Check if a specific error occurred and log it if so
 * @param code Error code to check for
 * @param message Custom message to log
 *
 * Usage in src/ code:
 *   if (asciichat_errno == ERROR_NETWORK) {
 *       LOG_ERRNO_IF_CODE(ERROR_NETWORK, "Network operation failed");
 *   }
 */
#define LOG_ERRNO_IF_CODE(code, message)                                                                               \
  do {                                                                                                                 \
    if (asciichat_errno == (code)) {                                                                                   \
      asciichat_print_error_context(&asciichat_errno_context);                                                         \
    }                                                                                                                  \
  } while (0)

#ifdef __cplusplus
}
#endif

/** @} */ /* errno */
