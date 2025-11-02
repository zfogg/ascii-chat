#pragma once

/**
 * @file http_client.h
 * @brief Simple HTTPS client for fetching public keys from GitHub/GitLab
 *
 * This module provides basic HTTPS GET functionality using BearSSL for TLS.
 * It's designed specifically for fetching SSH/GPG public keys from GitHub and GitLab.
 *
 * Security: Uses system CA certificates for trust validation (20-year longevity).
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <stddef.h>
#include "../common.h"

/**
 * Perform HTTPS GET request
 *
 * Makes a secure HTTPS connection to the specified hostname and fetches
 * the resource at the given path. Uses system CA certificates for validation.
 *
 * @param hostname Server hostname (e.g., "github.com")
 * @param path Resource path (e.g., "/username.keys")
 * @return Allocated string containing response body (caller must free), or NULL on error
 *
 * Example:
 *   char* keys = https_get("github.com", "/zfogg.keys");
 *   if (keys) {
 *       printf("Keys: %s\n", keys);
 *       SAFE_FREE(keys);
 *   }
 */
char *https_get(const char *hostname, const char *path);

// NOTE: fetch_github_ssh_keys, fetch_gitlab_ssh_keys, fetch_github_gpg_keys,
// and fetch_gitlab_gpg_keys have been moved to crypto/keys/https_keys.h
// They properly belong in the keys module, not the http_client module.
