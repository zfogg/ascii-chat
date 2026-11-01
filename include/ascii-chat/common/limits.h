/**
 * @defgroup limits Application Limits
 * @ingroup module_core
 * @brief Limits for clients, frame rates, display names, and other constraints
 *
 * @file limits.h
 * @brief Application limits and constraints
 * @ingroup limits
 * @addtogroup limits
 * @{
 */

#pragma once

/** Maximum members supported by session storage, hosting and elections. */
#define MAX_SESSION_PARTICIPANTS 128

/** Maximum Ed25519 identity keys in one session create sequence. */
#define MAX_SESSION_KEYS 32

/** Maximum ASCII DNS hostname bytes, excluding the NUL and a trailing root dot.
 * This is the existing is_valid_hostname() policy, not a Unicode codepoint limit. */
#define MAX_HOSTNAME_LENGTH 253

/** Maximum bytes in an ASCII DNS label, excluding separators and NUL. */
#define MAX_HOSTNAME_LABEL_LENGTH 63

/** Existing endpoint/OS hostname storage capacity, including NUL.
 * Also holds bracketed IPv6 literals and zone identifiers; do not use it as DNS validation. */
#define HOSTNAME_BUFFER_SIZE 256

/** Maximum normalized schemeless URL bytes, excluding NUL (includes added http://).
 * Explicit-scheme URLs parsed into allocated components have no such fixed limit. */
#define MAX_URL_LENGTH 4095
#define URL_BUFFER_SIZE (MAX_URL_LENGTH + 1)

/** yt-dlp cache capacities, including NUL; these are not URL parser limits. */
#define MEDIA_URL_CACHE_SIZE 2048
#define MEDIA_STREAM_URL_SIZE 8192

/* ============================================================================
 * Multi-Client Constants
 * ============================================================================ */

/** @brief Maximum display name length in characters */
#define MAX_DISPLAY_NAME_LEN 32

/** @brief Maximum client ID length - format: "noun.N (transport:port)" e.g., "mountain.0 (tcp:48036)" */
#define MAX_CLIENT_ID_LEN 64

/** @brief Maximum possible clients (static array size) - actual runtime limit set by --max-clients (1-128) */
#define MAX_CLIENTS MAX_SESSION_PARTICIPANTS

/** @brief Default maximum frame rate (frames per second) */
#define DEFAULT_MAX_FPS 60

/** @brief Frame interval in milliseconds based on default FPS (compile-time constant) */
#define FRAME_INTERVAL_MS (1000 / DEFAULT_MAX_FPS)

/** @brief Frame buffer capacity based on default FPS (compile-time constant) */
#define FRAME_BUFFER_CAPACITY (DEFAULT_MAX_FPS / 4)

/** @} */
