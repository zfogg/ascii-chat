#pragma once

/**
 * @file crypto/keys/https_keys.h
 * @brief HTTPS-based key fetching from GitHub and GitLab
 *
 * This module handles fetching SSH and GPG keys from GitHub and GitLab
 * using HTTPS requests with BearSSL for secure communication.
 */

#include "../../common.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// =============================================================================
// HTTPS Key Fetching Functions
// =============================================================================

/**
 * @brief Fetch SSH keys from GitHub using HTTPS
 * @param username GitHub username
 * @param keys_out Output array of SSH key strings (caller must free)
 * @param num_keys Number of keys returned
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t fetch_github_ssh_keys(const char *username, char ***keys_out, size_t *num_keys);

/**
 * @brief Fetch SSH keys from GitLab using HTTPS
 * @param username GitLab username
 * @param keys_out Output array of SSH key strings (caller must free)
 * @param num_keys Number of keys returned
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t fetch_gitlab_ssh_keys(const char *username, char ***keys_out, size_t *num_keys);

/**
 * @brief Fetch GPG keys from GitHub using HTTPS
 * @param username GitHub username
 * @param keys_out Output array of GPG key strings (caller must free)
 * @param num_keys Number of keys returned
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t fetch_github_gpg_keys(const char *username, char ***keys_out, size_t *num_keys);

/**
 * @brief Fetch GPG keys from GitLab using HTTPS
 * @param username GitLab username
 * @param keys_out Output array of GPG key strings (caller must free)
 * @param num_keys Number of keys returned
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t fetch_gitlab_gpg_keys(const char *username, char ***keys_out, size_t *num_keys);

// =============================================================================
// Key Parsing from HTTPS Responses
// =============================================================================

/**
 * @brief Parse SSH keys from HTTPS response text
 * @param response_text The HTTPS response containing SSH keys
 * @param response_len Length of the response text
 * @param keys_out Output array of parsed SSH keys
 * @param num_keys Number of keys parsed
 * @param max_keys Maximum number of keys to parse
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t parse_ssh_keys_from_response(const char *response_text, size_t response_len, char ***keys_out,
                                               size_t *num_keys, size_t max_keys);

/**
 * @brief Parse GPG keys from HTTPS response text
 * @param response_text The HTTPS response containing GPG keys
 * @param response_len Length of the response text
 * @param keys_out Output array of parsed GPG keys
 * @param num_keys Number of keys parsed
 * @param max_keys Maximum number of keys to parse
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t parse_gpg_keys_from_response(const char *response_text, size_t response_len, char ***keys_out,
                                               size_t *num_keys, size_t max_keys);

// =============================================================================
// URL Construction
// =============================================================================

/**
 * @brief Construct GitHub SSH keys URL
 * @param username GitHub username
 * @param url_out Output buffer for the URL
 * @param url_size Size of the URL buffer
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t build_github_ssh_url(const char *username, char *url_out, size_t url_size);

/**
 * @brief Construct GitLab SSH keys URL
 * @param username GitLab username
 * @param url_out Output buffer for the URL
 * @param url_size Size of the URL buffer
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t build_gitlab_ssh_url(const char *username, char *url_out, size_t url_size);

/**
 * @brief Construct GitHub GPG keys URL
 * @param username GitHub username
 * @param url_out Output buffer for the URL
 * @param url_size Size of the URL buffer
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t build_github_gpg_url(const char *username, char *url_out, size_t url_size);

/**
 * @brief Construct GitLab GPG keys URL
 * @param username GitLab username
 * @param url_out Output buffer for the URL
 * @param url_size Size of the URL buffer
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t build_gitlab_gpg_url(const char *username, char *url_out, size_t url_size);
