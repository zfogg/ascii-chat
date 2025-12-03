#pragma once

/**
 * @file crypto/http_client.h
 * @brief Simple HTTPS client for fetching public keys from GitHub/GitLab
 * @ingroup crypto
 * @addtogroup crypto
 * @{
 *
 * This module provides basic HTTPS GET functionality using BearSSL for TLS.
 * It's designed specifically for fetching SSH/GPG public keys from GitHub and GitLab.
 *
 * @note TLS library: Uses BearSSL for TLS connections.
 *       BearSSL is a minimal TLS implementation with system CA certificate support.
 *
 * @note Security: Uses system CA certificates for trust validation (20-year longevity).
 *       Validates server certificates against system trust store.
 *
 * @note Key fetching: Fetches keys from GitHub/GitLab endpoints:
 *       - GitHub SSH keys: `https://github.com/username.keys`
 *       - GitHub GPG keys: `https://github.com/username.gpg`
 *       - GitLab SSH keys: `https://gitlab.com/username.keys`
 *       - GitLab GPG keys: `https://gitlab.com/username.gpg`
 *
 * @note Key fetching functions: fetch_github_ssh_keys, fetch_gitlab_ssh_keys,
 *       fetch_github_gpg_keys, and fetch_gitlab_gpg_keys have been moved to
 *       crypto/keys/https_keys.h. They properly belong in the keys module.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <stddef.h>

/**
 * @name HTTPS Client
 * @{
 */

/**
 * @brief Perform HTTPS GET request
 * @param hostname Server hostname (e.g., "github.com", must not be NULL)
 * @param path Resource path (e.g., "/username.keys", must not be NULL)
 * @return Allocated string containing response body (caller must free), or NULL on error
 *
 * Makes a secure HTTPS connection to the specified hostname and fetches
 * the resource at the given path. Uses system CA certificates for validation.
 *
 * @note TLS connection: Uses BearSSL for TLS connections.
 *       Validates server certificate against system CA certificates.
 *
 * @note Memory management: Returns allocated string that caller must free.
 *       Use SAFE_FREE() to free the returned string.
 *
 * @note Error handling: Returns NULL on error (network error, TLS error, etc.).
 *       Error messages are logged via logging system.
 *
 * @note Hostname format: Hostname should be domain name only (no "https://" prefix).
 *       Example: "github.com", not "https://github.com".
 *
 * @note Path format: Path should start with "/" for absolute paths.
 *       Example: "/username.keys", not "username.keys".
 *
 * @note Response body: Returns complete response body as null-terminated string.
 *       Response may contain multiple keys (one per line for SSH keys).
 *
 * @warning Network dependency: Requires network connectivity and valid hostname.
 *          May fail if network is unavailable or hostname is unreachable.
 *
 * @warning Memory leak: Caller must free returned string using SAFE_FREE().
 *          Function allocates memory that must be freed.
 *
 * @warning Certificate validation: Uses system CA certificates for validation.
 *          May fail if system CA certificates are missing or outdated.
 *
 * @ingroup crypto
 */
char *https_get(const char *hostname, const char *path);

/** @} */
