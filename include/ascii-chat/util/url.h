#pragma once

/**
 * @file util/url.h
 * @brief 🌐 URL Parsing Utilities
 * @ingroup util
 * @addtogroup util
 * @{
 *
 * This header provides utilities for parsing URLs, specifically HTTPS URLs used
 * for fetching public keys and other resources over HTTPS.
 *
 * CORE FEATURES:
 * ==============
 * - HTTPS URL parsing (extract hostname and path)
 * - Input validation and error reporting
 * - Consistent error codes (asciichat_error_t)
 * - Memory-safe operations
 *
 * USAGE EXAMPLE:
 * ==============
 * @code
 * https_url_parts_t parts;
 * asciichat_error_t result = parse_https_url("https://github.com/user.keys", &parts);
 * if (result == ASCIICHAT_OK) {
 *     char *response = https_get(parts.hostname, parts.path);
 *     // Use response...
 *     SAFE_FREE(parts.hostname);
 *     SAFE_FREE(parts.path);
 * }
 * @endcode
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <stdint.h>
#include <stdbool.h>
#include <ascii-chat/asciichat_errno.h>

/**
 * @brief Structure containing parsed HTTPS URL components
 * @note Both hostname and path are allocated with SAFE_MALLOC() and must be freed by caller
 */
typedef struct {
  char *hostname; ///< Extracted hostname (allocated, caller must free)
  char *path;     ///< Extracted path including leading '/' (allocated, caller must free)
} https_url_parts_t;

/**
 * @brief Parse HTTPS URL into hostname and path components
 * @param url Input URL string (must start with "https://")
 * @param parts_out Output structure for parsed components (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Parses a full HTTPS URL and extracts the hostname and path components.
 * The resulting hostname and path are allocated with SAFE_MALLOC() and must
 * be freed by the caller.
 *
 * URL FORMAT:
 * - Must start with "https://"
 * - Must include a path component (at least one '/')
 * - Hostname cannot be empty
 *
 * VALIDATION:
 * - Checks for "https://" prefix
 * - Validates hostname is not empty
 * - Validates hostname length (max 255 chars)
 * - Ensures path component exists
 *
 * @note Both parts_out->hostname and parts_out->path must be freed by caller
 *       using SAFE_FREE()
 * @note Returns ERROR_INVALID_PARAM if URL format is invalid
 * @note Returns ERROR_INVALID_PARAM if hostname length exceeds 255 chars
 * @note Returns ERROR_MEMORY if allocation fails
 *
 * @par Example
 * @code
 * https_url_parts_t parts;
 * if (parse_https_url("https://api.github.com/users/octocat.keys", &parts) == ASCIICHAT_OK) {
 *     // parts.hostname = "api.github.com"
 *     // parts.path = "/users/octocat.keys"
 *     SAFE_FREE(parts.hostname);
 *     SAFE_FREE(parts.path);
 * }
 * @endcode
 *
 * @ingroup util
 */
asciichat_error_t parse_https_url(const char *url, https_url_parts_t *parts_out);

/** @} */
