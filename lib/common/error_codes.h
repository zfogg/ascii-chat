/**
 * @defgroup error_codes Error and Exit Codes
 * @ingroup module_core
 * @brief Error codes and status values for ascii-chat
 *
 * @file error_codes.h
 * @brief Error and exit codes - unified status values (0-255)
 * @ingroup error_codes
 * @addtogroup error_codes
 * @{
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
 */

#pragma once

#include <stdint.h>

/* Undefine Windows macros that conflict with our enum values */
#ifdef _WIN32
#undef ERROR_BUFFER_OVERFLOW
#undef ERROR_INVALID_STATE
#undef ERROR_FILE_NOT_FOUND
#endif

/**
 * @brief Error and exit codes - unified status values (0-255)
 *
 * Single enum for both function return values and process exit codes.
 * Following Unix conventions: 0 = success, 1 = general error, 2 = usage error.
 *
 * @ingroup error_codes
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
  ERROR_INIT = 8,          /**< General initialization failed */

  /* Hardware/Device errors (20-39) */
  ERROR_WEBCAM = 20,            /**< Webcam initialization or capture failed */
  ERROR_WEBCAM_IN_USE = 21,     /**< Webcam is in use by another application */
  ERROR_WEBCAM_PERMISSION = 22, /**< Webcam permission denied */
  ERROR_AUDIO = 23,             /**< Audio device initialization or I/O failed */
  ERROR_AUDIO_IN_USE = 24,      /**< Audio device is in use */
  ERROR_TERMINAL = 25,          /**< Terminal initialization or capability detection failed */
  ERROR_MEDIA_INIT = 26,        /**< Media source initialization failed */
  ERROR_MEDIA_OPEN = 27,        /**< Failed to open media file or stream */
  ERROR_MEDIA_DECODE = 28,      /**< Media decoding failed */
  ERROR_MEDIA_SEEK = 29,        /**< Media seek operation failed */
  ERROR_NOT_SUPPORTED = 30,     /**< Operation not supported */

  /* Network errors (40-59) */
  ERROR_NETWORK = 40,          /**< General network error */
  ERROR_NETWORK_BIND = 41,     /**< Cannot bind to port (server) */
  ERROR_NETWORK_CONNECT = 42,  /**< Cannot connect to server (client) */
  ERROR_NETWORK_TIMEOUT = 43,  /**< Network operation timed out */
  ERROR_NETWORK_PROTOCOL = 44, /**< Protocol violation or incompatible version */
  ERROR_NETWORK_SIZE = 45,     /**< Network packet size error */

  /* Session and protocol errors (46-55) */
  ERROR_RATE_LIMITED = 46,        /**< Rate limit exceeded */
  ERROR_SESSION_NOT_FOUND = 47,   /**< Session not found or expired */
  ERROR_SESSION_FULL = 48,        /**< Session has reached max participants */
  ERROR_INVALID_PASSWORD = 49,    /**< Incorrect password */
  ERROR_INVALID_SIGNATURE = 50,   /**< Invalid cryptographic signature */
  ERROR_ACDS_STRING_TAKEN = 51,   /**< Requested session string already in use (ACDS) */
  ERROR_ACDS_STRING_INVALID = 52, /**< Invalid session string format (ACDS) */
  ERROR_INTERNAL = 53,            /**< Internal server error */
  ERROR_UNKNOWN_PACKET = 54,      /**< Unknown packet type received */

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
  ERROR_NOT_FOUND = 91,          /**< Resource not found in registry or lookup */

  /* Signal/Crash handlers (100-127) */
  ERROR_SIGNAL_INTERRUPT = 100, /**< Interrupted by signal (SIGINT, SIGTERM) */
  ERROR_SIGNAL_CRASH = 101,     /**< Fatal signal (SIGSEGV, SIGABRT, etc.) */
  ERROR_ASSERTION_FAILED = 102, /**< Assertion or invariant violation */

  /* Compression errors (103-104) */
  ERROR_COMPRESSION = 103,   /**< Compression operation failed */
  ERROR_DECOMPRESSION = 104, /**< Decompression operation failed */

  /* File system errors (105-109) */
  ERROR_FILE_OPERATION = 105, /**< File or directory operation failed */
  ERROR_FILE_NOT_FOUND = 106, /**< File or directory not found */

  /* Process errors (110-119) */
  ERROR_PROCESS_FAILED = 110, /**< Process execution or termination failed */

  /* YouTube/URL streaming errors (111-116) */
  ERROR_YOUTUBE_INVALID_URL = 111,       /**< Invalid YouTube URL format */
  ERROR_YOUTUBE_EXTRACT_FAILED = 112,    /**< YouTube URL extraction/parsing failed */
  ERROR_YOUTUBE_UNPLAYABLE = 113,        /**< Video cannot be played (age-restricted, geo-blocked, etc.) */
  ERROR_YOUTUBE_NETWORK = 114,           /**< Network error fetching YouTube watch page */
  ERROR_YOUTUBE_NOT_SUPPORTED = 115,     /**< YouTube support not compiled in (requires libytdl) */

  /* Reserved (128-255) - Should not be used */
  /* 128+N typically means "terminated by signal N" on Unix systems */

} asciichat_error_t;

/**
 * @brief Get human-readable string for error/exit code
 * @param code Error code from asciichat_error_t enum
 * @return Human-readable error string, or "Unknown error" for invalid codes
 * @ingroup error_codes
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
  case ERROR_INIT:
    return "Initialization failed";
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
  case ERROR_MEDIA_INIT:
    return "Media source initialization failed";
  case ERROR_MEDIA_OPEN:
    return "Failed to open media file or stream";
  case ERROR_MEDIA_DECODE:
    return "Media decoding failed";
  case ERROR_MEDIA_SEEK:
    return "Media seek operation failed";
  case ERROR_NOT_SUPPORTED:
    return "Operation not supported";
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
  case ERROR_RATE_LIMITED:
    return "Rate limit exceeded";
  case ERROR_SESSION_NOT_FOUND:
    return "Session not found";
  case ERROR_SESSION_FULL:
    return "Session is full";
  case ERROR_INVALID_PASSWORD:
    return "Invalid password";
  case ERROR_INVALID_SIGNATURE:
    return "Invalid signature";
  case ERROR_ACDS_STRING_TAKEN:
    return "Session string already in use";
  case ERROR_ACDS_STRING_INVALID:
    return "Invalid session string";
  case ERROR_INTERNAL:
    return "Internal server error";
  case ERROR_UNKNOWN_PACKET:
    return "Unknown packet type";
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
  case ERROR_NOT_FOUND:
    return "Resource not found";
  case ERROR_SIGNAL_INTERRUPT:
    return "Interrupted by signal";
  case ERROR_SIGNAL_CRASH:
    return "Terminated by fatal signal";
  case ERROR_ASSERTION_FAILED:
    return "Assertion failed";
  case ERROR_COMPRESSION:
    return "Compression operation failed";
  case ERROR_DECOMPRESSION:
    return "Decompression operation failed";
  case ERROR_FILE_OPERATION:
    return "File or directory operation failed";
  case ERROR_FILE_NOT_FOUND:
    return "File or directory not found";
  case ERROR_PROCESS_FAILED:
    return "Process execution or termination failed";
  case ERROR_YOUTUBE_INVALID_URL:
    return "Invalid YouTube URL format";
  case ERROR_YOUTUBE_EXTRACT_FAILED:
    return "YouTube URL extraction failed";
  case ERROR_YOUTUBE_UNPLAYABLE:
    return "Video cannot be played";
  case ERROR_YOUTUBE_NETWORK:
    return "YouTube network error";
  case ERROR_YOUTUBE_NOT_SUPPORTED:
    return "YouTube support not compiled";
  default:
    return "Unknown error";
  }
}

/** @} */
