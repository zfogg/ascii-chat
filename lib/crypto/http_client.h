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
 *       free(keys);
 *   }
 */
char *https_get(const char *hostname, const char *path);

/**
 * Fetch GitHub SSH public keys for a user
 *
 * Convenience function that fetches SSH keys from github.com/username.keys
 * Only returns Ed25519 keys (RSA keys are filtered out).
 *
 * @param username GitHub username
 * @param keys_out Pointer to receive array of key strings (caller must free each string and array)
 * @param num_keys_out Pointer to receive number of keys found
 * @return 0 on success, -1 on failure
 *
 * Example:
 *   char** keys;
 *   size_t num_keys;
 *   if (fetch_github_ssh_keys("zfogg", &keys, &num_keys) == 0) {
 *       for (size_t i = 0; i < num_keys; i++) {
 *           printf("Key %zu: %s\n", i, keys[i]);
 *           free(keys[i]);
 *       }
 *       free(keys);
 *   }
 */
int fetch_github_ssh_keys(const char *username, char ***keys_out, size_t *num_keys_out);

/**
 * Fetch GitLab SSH public keys for a user
 *
 * Convenience function that fetches SSH keys from gitlab.com/username.keys
 * Only returns Ed25519 keys (RSA keys are filtered out).
 *
 * @param username GitLab username
 * @param keys_out Pointer to receive array of key strings (caller must free each string and array)
 * @param num_keys_out Pointer to receive number of keys found
 * @return 0 on success, -1 on failure
 */
int fetch_gitlab_ssh_keys(const char *username, char ***keys_out, size_t *num_keys_out);

/**
 * Fetch GitHub GPG public keys for a user
 *
 * Convenience function that fetches GPG keys from github.com/username.gpg
 * Returns PGP public key blocks in armored ASCII format.
 *
 * @param username GitHub username
 * @param keys_out Pointer to receive array of key strings (caller must free each string and array)
 * @param num_keys_out Pointer to receive number of keys found
 * @return 0 on success, -1 on failure
 *
 * Example:
 *   char** keys;
 *   size_t num_keys;
 *   if (fetch_github_gpg_keys("zfogg", &keys, &num_keys) == 0) {
 *       for (size_t i = 0; i < num_keys; i++) {
 *           printf("GPG Key %zu:\n%s\n", i, keys[i]);
 *           free(keys[i]);
 *       }
 *       free(keys);
 *   }
 */
int fetch_github_gpg_keys(const char *username, char ***keys_out, size_t *num_keys_out);

/**
 * Fetch GitLab GPG public keys for a user
 *
 * Convenience function that fetches GPG keys from gitlab.com/username.gpg
 * Returns PGP public key blocks in armored ASCII format.
 *
 * @param username GitLab username
 * @param keys_out Pointer to receive array of key strings (caller must free each string and array)
 * @param num_keys_out Pointer to receive number of keys found
 * @return 0 on success, -1 on failure
 */
int fetch_gitlab_gpg_keys(const char *username, char ***keys_out, size_t *num_keys_out);
