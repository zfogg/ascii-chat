/**
 * @file crypto/keys/https_keys.c
 * @ingroup keys
 * @brief 🌐 HTTPS public key fetching from GitHub/GitLab with URL parsing and caching
 */

#include "https_keys.h"
#include "../../common.h"
#include "../../asciichat_errno.h"
#include "../../platform/string.h"
#include "../http_client.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// =============================================================================
// Helper Functions
// =============================================================================

/**
 * Internal helper to fetch keys via HTTPS
 * Parses URL to extract hostname and path, then calls https_get
 */
static asciichat_error_t https_fetch_keys(const char *url, char **response_text, size_t *response_len) {
  if (!url || !response_text || !response_len) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
    return ERROR_INVALID_PARAM;
  }

  // Parse URL to extract hostname and path
  // Expected format: https://hostname/path
  if (strncmp(url, "https://", 8) != 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "URL must start with https://");
    return ERROR_INVALID_PARAM;
  }

  const char *hostname_start = url + 8; // Skip "https://"
  const char *path_start = strchr(hostname_start, '/');

  if (!path_start) {
    SET_ERRNO(ERROR_INVALID_PARAM, "URL must include a path");
    return ERROR_INVALID_PARAM;
  }

  // Extract hostname
  size_t hostname_len = path_start - hostname_start;
  if (hostname_len == 0 || hostname_len > 255) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid hostname length");
    return ERROR_INVALID_PARAM;
  }

  char hostname[256];
  memcpy(hostname, hostname_start, hostname_len);
  hostname[hostname_len] = '\0';

  // Use https_get from http_client
  char *response = https_get(hostname, path_start);
  if (!response) {
    SET_ERRNO(ERROR_NETWORK, "Failed to fetch from %s", url);
    return ERROR_NETWORK;
  }

  *response_text = response;
  *response_len = strlen(response);
  return ASCIICHAT_OK;
}

// =============================================================================
// URL Construction
// =============================================================================

asciichat_error_t build_github_ssh_url(const char *username, char *url_out, size_t url_size) {
  if (!username || !url_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: username=%p, url_out=%p", username, url_out);
    return ERROR_INVALID_PARAM;
  }

  if (url_size < 64) {
    SET_ERRNO(ERROR_INVALID_PARAM, "URL buffer too small: %zu (minimum 64)", url_size);
    return ERROR_INVALID_PARAM;
  }

  // Construct GitHub SSH keys URL: https://github.com/username.keys
  int result = safe_snprintf(url_out, url_size, "https://github.com/%s.keys", username);
  if (result < 0 || result >= (int)url_size) {
    SET_ERRNO(ERROR_STRING, "Failed to construct GitHub SSH URL");
    return ERROR_STRING;
  }

  return ASCIICHAT_OK;
}

asciichat_error_t build_gitlab_ssh_url(const char *username, char *url_out, size_t url_size) {
  if (!username || !url_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: username=%p, url_out=%p", username, url_out);
    return ERROR_INVALID_PARAM;
  }

  if (url_size < 64) {
    SET_ERRNO(ERROR_INVALID_PARAM, "URL buffer too small: %zu (minimum 64)", url_size);
    return ERROR_INVALID_PARAM;
  }

  // Construct GitLab SSH keys URL: https://gitlab.com/username.keys
  int result = safe_snprintf(url_out, url_size, "https://gitlab.com/%s.keys", username);
  if (result < 0 || result >= (int)url_size) {
    SET_ERRNO(ERROR_STRING, "Failed to construct GitLab SSH URL");
    return ERROR_STRING;
  }

  return ASCIICHAT_OK;
}

asciichat_error_t build_github_gpg_url(const char *username, char *url_out, size_t url_size) {
  if (!username || !url_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: username=%p, url_out=%p", username, url_out);
    return ERROR_INVALID_PARAM;
  }

  if (url_size < 64) {
    SET_ERRNO(ERROR_INVALID_PARAM, "URL buffer too small: %zu (minimum 64)", url_size);
    return ERROR_INVALID_PARAM;
  }

  // Construct GitHub GPG keys URL: https://github.com/username.gpg
  int result = safe_snprintf(url_out, url_size, "https://github.com/%s.gpg", username);
  if (result < 0 || result >= (int)url_size) {
    SET_ERRNO(ERROR_STRING, "Failed to construct GitHub GPG URL");
    return ERROR_STRING;
  }

  return ASCIICHAT_OK;
}

asciichat_error_t build_gitlab_gpg_url(const char *username, char *url_out, size_t url_size) {
  if (!username || !url_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: username=%p, url_out=%p", username, url_out);
    return ERROR_INVALID_PARAM;
  }

  if (url_size < 64) {
    SET_ERRNO(ERROR_INVALID_PARAM, "URL buffer too small: %zu (minimum 64)", url_size);
    return ERROR_INVALID_PARAM;
  }

  // Construct GitLab GPG keys URL: https://gitlab.com/username.gpg
  int result = safe_snprintf(url_out, url_size, "https://gitlab.com/%s.gpg", username);
  if (result < 0 || result >= (int)url_size) {
    SET_ERRNO(ERROR_STRING, "Failed to construct GitLab GPG URL");
    return ERROR_STRING;
  }

  return ASCIICHAT_OK;
}

// =============================================================================
// HTTPS Key Fetching Implementation
// =============================================================================

asciichat_error_t fetch_github_ssh_keys(const char *username, char ***keys_out, size_t *num_keys) {
  if (!username || !keys_out || !num_keys) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: username=%p, keys_out=%p, num_keys=%p", username, keys_out,
              num_keys);
    return ERROR_INVALID_PARAM;
  }

  // Build the GitHub SSH keys URL
  char url[256];
  asciichat_error_t url_result = build_github_ssh_url(username, url, sizeof(url));
  if (url_result != ASCIICHAT_OK) {
    return url_result;
  }

  // Fetch the keys using HTTPS
  char *response_text = NULL;
  size_t response_len = 0;
  asciichat_error_t fetch_result = https_fetch_keys(url, &response_text, &response_len);
  if (fetch_result != ASCIICHAT_OK) {
    return fetch_result;
  }

  // Parse SSH keys from the response
  asciichat_error_t parse_result =
      parse_ssh_keys_from_response(response_text, response_len, keys_out, num_keys, MAX_CLIENTS);

  // Clean up response text
  SAFE_FREE(response_text);

  return parse_result;
}

asciichat_error_t fetch_gitlab_ssh_keys(const char *username, char ***keys_out, size_t *num_keys) {
  if (!username || !keys_out || !num_keys) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: username=%p, keys_out=%p, num_keys=%p", username, keys_out,
              num_keys);
    return ERROR_INVALID_PARAM;
  }

  // Build the GitLab SSH keys URL
  char url[256];
  asciichat_error_t url_result = build_gitlab_ssh_url(username, url, sizeof(url));
  if (url_result != ASCIICHAT_OK) {
    return url_result;
  }

  // Fetch the keys using HTTPS
  char *response_text = NULL;
  size_t response_len = 0;
  asciichat_error_t fetch_result = https_fetch_keys(url, &response_text, &response_len);
  if (fetch_result != ASCIICHAT_OK) {
    return fetch_result;
  }

  // Parse SSH keys from the response
  asciichat_error_t parse_result =
      parse_ssh_keys_from_response(response_text, response_len, keys_out, num_keys, MAX_CLIENTS);

  // Clean up response text
  SAFE_FREE(response_text);

  return parse_result;
}

asciichat_error_t fetch_github_gpg_keys(const char *username, char ***keys_out, size_t *num_keys) {
  if (!username || !keys_out || !num_keys) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: username=%p, keys_out=%p, num_keys=%p", username, keys_out,
              num_keys);
    return ERROR_INVALID_PARAM;
  }

  // Build the GitHub GPG keys URL
  char url[256];
  asciichat_error_t url_result = build_github_gpg_url(username, url, sizeof(url));
  if (url_result != ASCIICHAT_OK) {
    return url_result;
  }

  // Fetch the keys using HTTPS
  char *response_text = NULL;
  size_t response_len = 0;
  asciichat_error_t fetch_result = https_fetch_keys(url, &response_text, &response_len);
  if (fetch_result != ASCIICHAT_OK) {
    return fetch_result;
  }

  // Parse GPG keys from the response
  asciichat_error_t parse_result =
      parse_gpg_keys_from_response(response_text, response_len, keys_out, num_keys, MAX_CLIENTS);

  // Clean up response text
  SAFE_FREE(response_text);

  return parse_result;
}

asciichat_error_t fetch_gitlab_gpg_keys(const char *username, char ***keys_out, size_t *num_keys) {
  if (!username || !keys_out || !num_keys) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: username=%p, keys_out=%p, num_keys=%p", username, keys_out,
              num_keys);
    return ERROR_INVALID_PARAM;
  }

  // Build the GitLab GPG keys URL
  char url[256];
  asciichat_error_t url_result = build_gitlab_gpg_url(username, url, sizeof(url));
  if (url_result != ASCIICHAT_OK) {
    return url_result;
  }

  // Fetch the keys using HTTPS
  char *response_text = NULL;
  size_t response_len = 0;
  asciichat_error_t fetch_result = https_fetch_keys(url, &response_text, &response_len);
  if (fetch_result != ASCIICHAT_OK) {
    return fetch_result;
  }

  // Parse GPG keys from the response
  asciichat_error_t parse_result =
      parse_gpg_keys_from_response(response_text, response_len, keys_out, num_keys, MAX_CLIENTS);

  // Clean up response text
  SAFE_FREE(response_text);

  return parse_result;
}

// =============================================================================
// Key Parsing from HTTPS Responses
// =============================================================================

asciichat_error_t parse_ssh_keys_from_response(const char *response_text, size_t response_len, char ***keys_out,
                                               size_t *num_keys, size_t max_keys) {
  if (!response_text || !keys_out || !num_keys) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for SSH key parsing");
    return ERROR_INVALID_PARAM;
  }

  *num_keys = 0;
  *keys_out = NULL;

  // Count the number of SSH keys in the response
  size_t key_count = 0;
  const char *line_start = response_text;
  const char *line_end;

  while ((line_end = strchr(line_start, '\n')) != NULL) {
    size_t line_len = line_end - line_start;
    if (line_len > 0 && line_start[0] != '\r' && line_start[0] != '\n') {
      key_count++;
    }
    line_start = line_end + 1;
  }

  // Handle last line if it doesn't end with newline
  if (line_start < response_text + response_len) {
    key_count++;
  }

  if (key_count == 0) {
    SET_ERRNO(ERROR_CRYPTO_KEY, "No SSH keys found in response");
    return ERROR_CRYPTO_KEY;
  }

  if (key_count > max_keys) {
    key_count = max_keys;
  }

  // Allocate array for key strings
  *keys_out = SAFE_MALLOC(sizeof(char *) * key_count, char **);

  // Parse each SSH key line
  line_start = response_text;
  size_t parsed_keys = 0;

  while (parsed_keys < key_count && (line_end = strchr(line_start, '\n')) != NULL) {
    size_t line_len = line_end - line_start;

    // Skip empty lines
    if (line_len > 0 && line_start[0] != '\r' && line_start[0] != '\n') {
      // Allocate space for this key line
      (*keys_out)[parsed_keys] = SAFE_MALLOC(line_len + 1, char *);

      // Copy the key line
      memcpy((*keys_out)[parsed_keys], line_start, line_len);
      (*keys_out)[parsed_keys][line_len] = '\0';

      parsed_keys++;
    }

    line_start = line_end + 1;
  }

  // Handle last line if it doesn't end with newline
  if (parsed_keys < key_count && line_start < response_text + response_len) {
    size_t line_len = (response_text + response_len) - line_start;
    if (line_len > 0) {
      (*keys_out)[parsed_keys] = SAFE_MALLOC(line_len + 1, char *);
      memcpy((*keys_out)[parsed_keys], line_start, line_len);
      (*keys_out)[parsed_keys][line_len] = '\0';
      parsed_keys++;
    }
  }

  *num_keys = parsed_keys;
  return ASCIICHAT_OK;
}

asciichat_error_t parse_gpg_keys_from_response(const char *response_text, size_t response_len, char ***keys_out,
                                               size_t *num_keys, size_t max_keys) {
  (void)max_keys;
  if (!response_text || !keys_out || !num_keys) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for GPG key parsing");
    return ERROR_INVALID_PARAM;
  }

  // For GPG keys, we typically get a single armored key block
  // This is a simplified implementation - real GPG parsing is more complex

  *num_keys = 0;
  *keys_out = NULL;

  // Check if this looks like a GPG key (starts with -----BEGIN PGP)
  if (strncmp(response_text, "-----BEGIN PGP", 14) != 0) {
    SET_ERRNO(ERROR_CRYPTO_KEY, "Response does not contain a valid GPG key");
    return ERROR_CRYPTO_KEY;
  }

  // Allocate space for one GPG key
  *keys_out = SAFE_MALLOC(sizeof(char *), char **);
  (*keys_out)[0] = SAFE_MALLOC(response_len + 1, char *);

  // Copy the entire GPG key
  memcpy((*keys_out)[0], response_text, response_len);
  (*keys_out)[0][response_len] = '\0';

  *num_keys = 1;
  return ASCIICHAT_OK;
}
