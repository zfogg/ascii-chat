#pragma once

/**
 * @file util/url.h
 * @brief 🌐 Production-Grade URL Validation and Parsing
 * @ingroup util
 * @addtogroup util
 * @{
 *
 * This header provides robust URL validation and parsing using PCRE2 regular expressions.
 * Based on the production-grade HTTP(S) URL regex by Diego Perini (MIT License).
 *
 * CORE FEATURES:
 * ==============
 * - Production-grade HTTP(S) URL validation via PCRE2
 * - Comprehensive URL parsing (extract scheme, host, port, path)
 * - IPv4, IPv6, and hostname support (localhost, bare hosts, FQDNs)
 * - Private IP filtering (10.x, 172.16-31.x, 192.168.x, 127.x, 169.254.x)
 * - JIT-compiled regex for 10-100x performance boost
 * - Input validation and error reporting
 * - Consistent error codes (asciichat_error_t)
 * - Memory-safe operations
 *
 * USAGE EXAMPLES:
 * ===============
 * Fast validation only (bool return):
 * @code
 * if (url_is_valid("https://github.com/user/repo")) {
 *     // URL is valid and can be safely used
 * }
 * @endcode
 *
 * Full parsing (extract components):
 * @code
 * url_parts_t parts = {0};
 * if (url_parse("https://api.github.com:443/users", &parts) == ASCIICHAT_OK) {
 *     printf("scheme: %s, host: %s, port: %d, path: %s\\n",
 *            parts.scheme, parts.host, parts.port, parts.path);
 *     url_parts_free(&parts);
 * }
 * @endcode
 *
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#include <stdint.h>
#include <stdbool.h>
#include "../asciichat_errno.h"

/**
 * @brief Structure containing parsed URL components
 * @note All string fields are allocated with SAFE_MALLOC() and must be freed by caller
 */
typedef struct {
  char *scheme;   ///< Extracted scheme: "http" or "https" (allocated)
  char *userinfo; ///< Optional userinfo (user:pass) - may be NULL (allocated if present)
  char *host;     ///< Extracted hostname or IP address (allocated)
  char *ipv6;     ///< IPv6 address if host is bracketed IPv6 - may be NULL (allocated if present)
  int port;       ///< Extracted port number (0 if not specified)
  char *path;     ///< Extracted path, query, and fragment (allocated if present, may be NULL)
} url_parts_t;

/**
 * @brief Fast URL validation using production-grade HTTP(S) regex
 * @param url Input URL string to validate
 * @return True if URL is valid, false otherwise
 *
 * Quickly validates a URL without extracting components. Uses PCRE2 with JIT
 * compilation for 10-100x performance over manual parsing.
 *
 * URL ACCEPTANCE CRITERIA:
 * - Scheme: http or https (case-insensitive)
 * - Host: Public IPv4, IPv6, localhost, FQDN, or bare hostname
 * - Rejects: Private IPs (10.x, 172.16-31.x, 192.168.x, 127.x, 169.254.x)
 * - Port: Optional, must be 1-65535 if present
 * - Path: Optional, may include query and fragment
 *
 * @par Example
 * @code
 * if (url_is_valid("https://github.com/octocat")) {
 *     // Safe to use URL
 * }
 * @endcode
 *
 * @ingroup util
 */
bool url_is_valid(const char *url);

/**
 * @brief Parse HTTP(S) URL into components
 * @param url Input URL string to parse
 * @param parts_out Output structure for parsed components (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Validates and parses an HTTP(S) URL into its component parts using
 * production-grade PCRE2 regex with JIT compilation.
 *
 * URL ACCEPTANCE CRITERIA:
 * - Scheme: http or https (case-insensitive)
 * - Host: Public IPv4, IPv6, localhost, FQDN, or bare hostname
 * - Rejects: Private IPs (10.x, 172.16-31.x, 192.168.x, 127.x, 169.254.x)
 * - Port: Optional, 1-65535
 * - Path/Query/Fragment: Optional
 *
 * EXTRACTED COMPONENTS:
 * - scheme: "http" or "https" (always present)
 * - host: Hostname, IPv4, or IPv6 (always present)
 * - port: Port number (0 if not specified)
 * - userinfo: user:pass@ prefix (NULL if not present)
 * - ipv6: IPv6 address (NULL unless host is bracketed IPv6)
 * - path: Path, query, and fragment (NULL if not present)
 *
 * MEMORY MANAGEMENT:
 * - All allocated strings must be freed using url_parts_free(parts_out)
 * - Do NOT use SAFE_FREE() on individual fields - use url_parts_free()
 *
 * @note Returns ERROR_INVALID_PARAM if URL format is invalid
 * @note Returns ERROR_MEMORY if allocation fails
 *
 * @par Example
 * @code
 * url_parts_t parts = {0};
 * if (url_parse("https://user:pass@example.com:8443/api/v1?key=val#section", &parts) == ASCIICHAT_OK) {
 *     printf("Host: %s, Port: %d, Path: %s\\n", parts.host, parts.port, parts.path);
 *     url_parts_free(&parts);
 * }
 * @endcode
 *
 * @ingroup util
 */
asciichat_error_t url_parse(const char *url, url_parts_t *parts_out);

/**
 * @brief Free parsed URL components
 * @param parts Pointer to url_parts_t structure to free
 *
 * Safely frees all allocated string fields in a url_parts_t structure.
 * Safe to call multiple times on same structure.
 *
 * @par Example
 * @code
 * url_parts_t parts = {0};
 * url_parse("https://example.com", &parts);
 * // Use parts...
 * url_parts_free(&parts);
 * // All internal strings are freed, parts is zeroed
 * @endcode
 *
 * @ingroup util
 */
void url_parts_free(url_parts_t *parts);

/** @} */
