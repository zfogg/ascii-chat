#pragma once

/**
 * @file crypto/keys/https_keys.h
 * @ingroup keys
 * @brief HTTPS-based key fetching from GitHub and GitLab
 *
 * This module handles fetching SSH and GPG keys from GitHub and GitLab
 * using HTTPS requests with BearSSL for secure communication.
 *
 * @note Network operations: Requires network connectivity and valid usernames.
 *       May fail if network is unavailable or usernames don't exist.
 *
 * @note TLS security: Uses BearSSL for HTTPS connections with system CA certificates.
 *       Validates server certificates against system trust store.
 *
 * @note Key format: Only Ed25519 SSH keys are parsed from responses.
 *       Other key types (RSA, ECDSA) are skipped.
 *
 * @note GPG support: GPG key fetching code exists but GPG parsing may not work
 *       until GPG support is re-enabled.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include "../../common.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @name HTTPS Key Fetching
 * @{
 *
 * Fetch SSH/GPG keys from GitHub and GitLab using HTTPS (BearSSL).
 * Requires network connectivity and valid usernames.
 */

/**
 * @brief Fetch SSH keys from GitHub using HTTPS
 * @param username GitHub username (must not be NULL)
 * @param keys_out Output array of SSH key strings (caller must free each string and array)
 * @param num_keys Output parameter for number of keys fetched
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Fetches SSH keys from GitHub: GET https://github.com/username.keys
 *
 * @note Network operation: Requires network connectivity and valid GitHub username.
 *       May fail if network is unavailable or username doesn't exist.
 *
 * @note Key format: Only Ed25519 SSH keys are parsed from response.
 *       Other key types (RSA, ECDSA) are skipped.
 *
 * @note Memory management: Caller must free each key string and the keys array:
 *       ```c
 *       for (size_t i = 0; i < num_keys; i++) {
 *         free(keys_out[i]);
 *       }
 *       free(keys_out);
 *       ```
 *
 * @note Response parsing: Parses response as one key per line.
 *       Each line is parsed as SSH key format ("ssh-ed25519 AAAAC3... comment").
 *
 * @warning Network dependency: Requires active network connection.
 *          May fail if GitHub is unreachable or username doesn't exist.
 *
 * @warning Memory leak: Caller must free returned strings and array.
 *          Function allocates memory that must be freed.
 *
 * @ingroup keys
 */
asciichat_error_t fetch_github_ssh_keys(const char *username, char ***keys_out, size_t *num_keys);

/**
 * @brief Fetch SSH keys from GitLab using HTTPS
 * @param username GitLab username (must not be NULL)
 * @param keys_out Output array of SSH key strings (caller must free each string and array)
 * @param num_keys Output parameter for number of keys fetched
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Fetches SSH keys from GitLab: GET https://gitlab.com/username.keys
 *
 * @note Network operation: Requires network connectivity and valid GitLab username.
 *       May fail if network is unavailable or username doesn't exist.
 *
 * @note Key format: Only Ed25519 SSH keys are parsed from response.
 *       Other key types (RSA, ECDSA) are skipped.
 *
 * @note Memory management: Caller must free each key string and the keys array (same as fetch_github_ssh_keys).
 *
 * @warning Network dependency: Requires active network connection.
 *          May fail if GitLab is unreachable or username doesn't exist.
 *
 * @ingroup keys
 */
asciichat_error_t fetch_gitlab_ssh_keys(const char *username, char ***keys_out, size_t *num_keys);

/**
 * @brief Fetch GPG keys from GitHub using HTTPS
 * @param username GitHub username (must not be NULL)
 * @param keys_out Output array of GPG key strings (caller must free each string and array)
 * @param num_keys Output parameter for number of keys fetched
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Fetches GPG keys from GitHub: GET https://github.com/username.gpg
 *
 * @note Network operation: Requires network connectivity and valid GitHub username.
 *       May fail if network is unavailable or username doesn't exist.
 *
 * @note GPG format: Fetches GPG keys in armored format.
 *       Response contains GPG key blocks (-----BEGIN PGP PUBLIC KEY BLOCK-----).
 *
 * @note Memory management: Caller must free each key string and the keys array (same as fetch_github_ssh_keys).
 *
 * @warning Network dependency: Requires active network connection.
 * @warning GPG support: GPG key parsing may not work until GPG support is re-enabled.
 *
 * @ingroup keys
 */
asciichat_error_t fetch_github_gpg_keys(const char *username, char ***keys_out, size_t *num_keys);

/**
 * @brief Fetch GPG keys from GitLab using HTTPS
 * @param username GitLab username (must not be NULL)
 * @param keys_out Output array of GPG key strings (caller must free each string and array)
 * @param num_keys Output parameter for number of keys fetched
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Fetches GPG keys from GitLab: GET https://gitlab.com/username.gpg
 *
 * @note Network operation: Requires network connectivity and valid GitLab username.
 *       May fail if network is unavailable or username doesn't exist.
 *
 * @note GPG format: Fetches GPG keys in armored format.
 *       Response contains GPG key blocks (-----BEGIN PGP PUBLIC KEY BLOCK-----).
 *
 * @note Memory management: Caller must free each key string and the keys array (same as fetch_gitlab_ssh_keys).
 *
 * @warning Network dependency: Requires active network connection.
 * @warning GPG support: GPG key parsing may not work until GPG support is re-enabled.
 *
 * @ingroup keys
 */
asciichat_error_t fetch_gitlab_gpg_keys(const char *username, char ***keys_out, size_t *num_keys);

/** @} */

/**
 * @name Key Parsing from HTTPS Responses
 * @{
 */

/**
 * @brief Parse SSH keys from HTTPS response text
 * @param response_text HTTPS response containing SSH keys (must not be NULL)
 * @param response_len Length of response text
 * @param keys_out Output array of parsed SSH keys (must not be NULL)
 * @param num_keys Output parameter for number of keys parsed (must not be NULL)
 * @param max_keys Maximum number of keys to parse (must be > 0)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Parses SSH keys from HTTPS response text (one key per line).
 * Extracts Ed25519 keys and allocates memory for each key string.
 *
 * @note Response format: Expects one SSH key per line.
 *       Format: "ssh-ed25519 AAAAC3... comment" (one key per line)
 *
 * @note Key filtering: Only Ed25519 SSH keys are parsed.
 *       Other key types (RSA, ECDSA) are skipped.
 *
 * @note Memory allocation: Allocates memory for each key string.
 *       Caller must free each string and the array.
 *
 * @note Maximum keys: Stops parsing after max_keys keys are found.
 *       Function returns ASCIICHAT_OK even if more keys exist in response.
 *
 * @warning Memory leak: Caller must free returned strings and array.
 *          Function allocates memory that must be freed.
 *
 * @ingroup keys
 */
asciichat_error_t parse_ssh_keys_from_response(const char *response_text, size_t response_len, char ***keys_out,
                                               size_t *num_keys, size_t max_keys);

/**
 * @brief Parse GPG keys from HTTPS response text
 * @param response_text HTTPS response containing GPG keys (must not be NULL)
 * @param response_len Length of response text
 * @param keys_out Output array of parsed GPG keys (must not be NULL)
 * @param num_keys Output parameter for number of keys parsed (must not be NULL)
 * @param max_keys Maximum number of keys to parse (must be > 0)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Parses GPG keys from HTTPS response text (armored format).
 * Extracts GPG key blocks and allocates memory for each key string.
 *
 * @note Response format: Expects GPG keys in armored format.
 *       Format: "-----BEGIN PGP PUBLIC KEY BLOCK-----" ... "-----END PGP PUBLIC KEY BLOCK-----"
 *
 * @note Key extraction: Finds GPG key blocks (BEGIN/END markers) and extracts each block.
 *       Allocates memory for each complete GPG key block.
 *
 * @note Memory allocation: Allocates memory for each key string.
 *       Caller must free each string and the array.
 *
 * @note Maximum keys: Stops parsing after max_keys keys are found.
 *       Function returns ASCIICHAT_OK even if more keys exist in response.
 *
 * @warning Memory leak: Caller must free returned strings and array.
 *          Function allocates memory that must be freed.
 *
 * @warning GPG support: GPG key parsing may not work until GPG support is re-enabled.
 *
 * @ingroup keys
 */
asciichat_error_t parse_gpg_keys_from_response(const char *response_text, size_t response_len, char ***keys_out,
                                               size_t *num_keys, size_t max_keys);

/** @} */

/**
 * @name URL Construction
 * @{
 */

/**
 * @brief Construct GitHub SSH keys URL
 * @param username GitHub username (must not be NULL)
 * @param url_out Output buffer for URL (must not be NULL)
 * @param url_size Size of URL buffer (must be >= 64)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Constructs GitHub SSH keys URL: https://github.com/username.keys
 *
 * @note URL format: Uses standard GitHub SSH keys endpoint.
 *       Format: "https://github.com/{username}.keys"
 *
 * @note Buffer requirements: URL buffer must be at least 64 bytes.
 *       Format: "https://github.com/" (19 chars) + username + ".keys" (6 chars) = 25 + username length
 *
 * @warning URL buffer must be large enough for formatted URL.
 *          Function validates buffer size but may overflow if buffer is too small.
 *
 * @ingroup keys
 */
asciichat_error_t build_github_ssh_url(const char *username, char *url_out, size_t url_size);

/**
 * @brief Construct GitLab SSH keys URL
 * @param username GitLab username (must not be NULL)
 * @param url_out Output buffer for URL (must not be NULL)
 * @param url_size Size of URL buffer (must be >= 64)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Constructs GitLab SSH keys URL: https://gitlab.com/username.keys
 *
 * @note URL format: Uses standard GitLab SSH keys endpoint.
 *       Format: "https://gitlab.com/{username}.keys"
 *
 * @note Buffer requirements: URL buffer must be at least 64 bytes (same as build_github_ssh_url).
 *
 * @warning URL buffer must be large enough for formatted URL.
 *
 * @ingroup keys
 */
asciichat_error_t build_gitlab_ssh_url(const char *username, char *url_out, size_t url_size);

/**
 * @brief Construct GitHub GPG keys URL
 * @param username GitHub username (must not be NULL)
 * @param url_out Output buffer for URL (must not be NULL)
 * @param url_size Size of URL buffer (must be >= 64)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Constructs GitHub GPG keys URL: https://github.com/username.gpg
 *
 * @note URL format: Uses standard GitHub GPG keys endpoint.
 *       Format: "https://github.com/{username}.gpg"
 *
 * @note Buffer requirements: URL buffer must be at least 64 bytes (same as build_github_ssh_url).
 *
 * @warning URL buffer must be large enough for formatted URL.
 *
 * @ingroup keys
 */
asciichat_error_t build_github_gpg_url(const char *username, char *url_out, size_t url_size);

/**
 * @brief Construct GitLab GPG keys URL
 * @param username GitLab username (must not be NULL)
 * @param url_out Output buffer for URL (must not be NULL)
 * @param url_size Size of URL buffer (must be >= 64)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Constructs GitLab GPG keys URL: https://gitlab.com/username.gpg
 *
 * @note URL format: Uses standard GitLab GPG keys endpoint.
 *       Format: "https://gitlab.com/{username}.gpg"
 *
 * @note Buffer requirements: URL buffer must be at least 64 bytes (same as build_gitlab_ssh_url).
 *
 * @warning URL buffer must be large enough for formatted URL.
 *
 * @ingroup keys
 */
asciichat_error_t build_gitlab_gpg_url(const char *username, char *url_out, size_t url_size);

/** @} */
