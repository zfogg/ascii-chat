/**
 * @file crypto/keys/https_keys.c
 * @ingroup keys
 * @brief 🌐 HTTPS public key fetching from GitHub/GitLab with URL parsing and caching
 */

#include "https_keys.h"
#include "common.h"
#include "asciichat_errno.h"
#include "platform/string.h"
#include "platform/util.h"
#include "platform/process.h"
#include "platform/filesystem.h"
#include "util/url.h" // For parse_https_url()
#include "network/http_client.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <fcntl.h>

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
  https_url_parts_t url_parts;
  asciichat_error_t parse_result = parse_https_url(url, &url_parts);
  if (parse_result != ASCIICHAT_OK) {
    return parse_result;
  }

  // Use https_get from http_client
  char *response = https_get(url_parts.hostname, url_parts.path);
  SAFE_FREE(url_parts.hostname);
  SAFE_FREE(url_parts.path);

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

  // Strip .gpg suffix if user already included it (e.g., "github:zfogg.gpg")
  char clean_username[BUFFER_SIZE_SMALL];
  SAFE_STRNCPY(clean_username, username, sizeof(clean_username) - 1);
  size_t len = strlen(clean_username);
  if (len > 4 && strcmp(clean_username + len - 4, ".gpg") == 0) {
    clean_username[len - 4] = '\0'; // Remove .gpg suffix
  }

  // Construct GitHub GPG keys URL: https://github.com/username.gpg
  int result = safe_snprintf(url_out, url_size, "https://github.com/%s.gpg", clean_username);
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

  // Strip .gpg suffix if user already included it (e.g., "gitlab:zfogg.gpg")
  char clean_username[BUFFER_SIZE_SMALL];
  SAFE_STRNCPY(clean_username, username, sizeof(clean_username) - 1);
  size_t len = strlen(clean_username);
  if (len > 4 && strcmp(clean_username + len - 4, ".gpg") == 0) {
    clean_username[len - 4] = '\0'; // Remove .gpg suffix
  }

  // Construct GitLab GPG keys URL: https://gitlab.com/username.gpg
  int result = safe_snprintf(url_out, url_size, "https://gitlab.com/%s.gpg", clean_username);
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
  char url[BUFFER_SIZE_SMALL];
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
  char url[BUFFER_SIZE_SMALL];
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
  char url[BUFFER_SIZE_SMALL];
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
  char url[BUFFER_SIZE_SMALL];
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
  if (!*keys_out) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate SSH keys array");
  }

  // Parse each SSH key line
  line_start = response_text;
  size_t parsed_keys = 0;

  while (parsed_keys < key_count && (line_end = strchr(line_start, '\n')) != NULL) {
    size_t line_len = line_end - line_start;

    // Skip empty lines
    if (line_len > 0 && line_start[0] != '\r' && line_start[0] != '\n') {
      // Allocate space for this key line
      (*keys_out)[parsed_keys] = SAFE_MALLOC(line_len + 1, char *);
      if (!(*keys_out)[parsed_keys]) {
        // Cleanup previously allocated keys
        for (size_t i = 0; i < parsed_keys; i++) {
          SAFE_FREE((*keys_out)[i]);
        }
        SAFE_FREE(*keys_out);
        *keys_out = NULL;
        return SET_ERRNO(ERROR_MEMORY, "Failed to allocate SSH key string");
      }

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
      if (!(*keys_out)[parsed_keys]) {
        // Cleanup previously allocated keys
        for (size_t i = 0; i < parsed_keys; i++) {
          SAFE_FREE((*keys_out)[i]);
        }
        SAFE_FREE(*keys_out);
        *keys_out = NULL;
        return SET_ERRNO(ERROR_MEMORY, "Failed to allocate SSH key string");
      }
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

  *num_keys = 0;
  *keys_out = NULL;

  // Check if this looks like a GPG key (starts with -----BEGIN PGP)
  if (strncmp(response_text, "-----BEGIN PGP", 14) != 0) {
    SET_ERRNO(ERROR_CRYPTO_KEY, "Response does not contain a valid GPG key");
    return ERROR_CRYPTO_KEY;
  }

  // Write armored block to temp file
  char temp_file[PLATFORM_MAX_PATH_LENGTH];
  int fd = -1;
  if (platform_create_temp_file(temp_file, sizeof(temp_file), "asc_gpg_import", &fd) != 0) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to create temp file for GPG import");
  }

#ifdef _WIN32
  // On Windows, platform_create_temp_file returns fd=-1, need to open separately
  fd = open(temp_file, O_WRONLY | O_BINARY);
  if (fd < 0) {
    platform_delete_temp_file(temp_file);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to open temp file for writing");
  }
#endif

  ssize_t written = write(fd, response_text, response_len);
  close(fd);

  if (written != (ssize_t)response_len) {
    platform_unlink(temp_file);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to write GPG key to temp file");
  }

  // Import the key using gpg --import
  char import_cmd[BUFFER_SIZE_MEDIUM];
  safe_snprintf(import_cmd, sizeof(import_cmd), "gpg --import '%s' 2>&1", temp_file);
  FILE *import_fp = NULL;
  if (platform_popen(import_cmd, "r", &import_fp) != ASCIICHAT_OK || !import_fp) {
    platform_unlink(temp_file);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to run gpg --import");
  }

  char import_output[2048];
  size_t import_len = fread(import_output, 1, sizeof(import_output) - 1, import_fp);
  import_output[import_len] = '\0';
  platform_pclose(&import_fp);
  platform_unlink(temp_file);

  // Extract ALL key IDs from import output (format: "gpg: key KEYID: ...")
  // GitHub often returns multiple keys in one armored block
  const char *key_marker = "gpg: key ";
  char key_ids[16][17]; // Support up to 16 keys
  size_t key_count = 0;

  log_debug("GPG import output:\n%s", import_output);

  char *search_pos = import_output;
  while (key_count < 16) {
    char *key_line = strstr(search_pos, key_marker);
    if (!key_line)
      break;

    key_line += strlen(key_marker);
    int i = 0;
    while (i < 16 && key_line[i] != ':' && key_line[i] != ' ' && key_line[i] != '\n') {
      key_ids[key_count][i] = key_line[i];
      i++;
    }
    key_ids[key_count][i] = '\0';

    if (i > 0) {
      log_debug("Extracted GPG key ID #%zu: %s", key_count, key_ids[key_count]);
      key_count++;
    }
    search_pos = key_line + i;
  }

  if (key_count == 0) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to extract any key IDs from GPG import output");
  }

  log_debug("Total GPG keys extracted from import: %zu", key_count);

  // Allocate array for results
  *keys_out = SAFE_MALLOC(sizeof(char *) * key_count, char **);
  if (!*keys_out) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate GPG keys array");
  }

  // Process each key ID to get full fingerprint
  size_t valid_keys = 0;
  for (size_t k = 0; k < key_count; k++) {
    // Get full fingerprint from gpg --list-keys output
    char list_cmd[BUFFER_SIZE_SMALL];
    safe_snprintf(list_cmd, sizeof(list_cmd),
                  "gpg --list-keys --with-colons --fingerprint '%s' " PLATFORM_SHELL_NULL_REDIRECT, key_ids[k]);
    FILE *list_fp = NULL;
    if (platform_popen(list_cmd, "r", &list_fp) != ASCIICHAT_OK || !list_fp) {
      continue; // Skip this key if we can't list it
    }

    char list_output[4096];
    size_t list_len = fread(list_output, 1, sizeof(list_output) - 1, list_fp);
    list_output[list_len] = '\0';
    platform_pclose(&list_fp);

    // Check if it contains an Ed25519 key (algorithm 22)
    if (!strstr(list_output, ":22:") && !strstr(list_output, "ed25519")) {
      continue; // Skip non-Ed25519 keys
    }

    // Extract full 40-character fingerprint from "fpr:" line
    // Format: fpr:::::::::FINGERPRINT:
    // The fingerprint is field 10 (after 9 colons)
    char fingerprint[41] = {0};
    const char *fpr_marker = "\nfpr:";
    char *fpr_line = strstr(list_output, fpr_marker);
    if (fpr_line) {
      fpr_line += 1; // Skip the newline
      // Count 9 colons from the start of the line
      int colon_count = 0;
      while (*fpr_line && colon_count < 9) {
        if (*fpr_line == ':')
          colon_count++;
        fpr_line++;
      }
      // Now we should be at the start of the fingerprint
      // Extract up to 40 hex characters
      int fpr_len = 0;
      while (fpr_len < 40 && fpr_line[fpr_len] && fpr_line[fpr_len] != ':' && fpr_line[fpr_len] != '\n') {
        fingerprint[fpr_len] = fpr_line[fpr_len];
        fpr_len++;
      }
      fingerprint[fpr_len] = '\0';
    }

    // If fingerprint extraction failed, use the short key ID
    if (strlen(fingerprint) == 0) {
      log_warn("Failed to extract fingerprint for key %s, using short key ID", key_ids[k]);
      SAFE_STRNCPY(fingerprint, key_ids[k], sizeof(fingerprint));
    }

    log_debug("Key %s -> fingerprint: %s (length: %zu)", key_ids[k], fingerprint, strlen(fingerprint));

    // Allocate and store this key in gpg:KEYID format
    size_t gpg_key_len = strlen("gpg:") + strlen(fingerprint) + 1;
    (*keys_out)[valid_keys] = SAFE_MALLOC(gpg_key_len, char *);
    if (!(*keys_out)[valid_keys]) {
      // Cleanup on allocation failure
      for (size_t cleanup = 0; cleanup < valid_keys; cleanup++) {
        SAFE_FREE((*keys_out)[cleanup]);
      }
      SAFE_FREE(*keys_out);
      *keys_out = NULL;
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate GPG key string");
    }

    safe_snprintf((*keys_out)[valid_keys], gpg_key_len, "gpg:%s", fingerprint);
    log_debug("Added valid Ed25519 key #%zu: %s", valid_keys, (*keys_out)[valid_keys]);
    valid_keys++;
  }

  if (valid_keys == 0) {
    SAFE_FREE(*keys_out);
    *keys_out = NULL;
    return SET_ERRNO(ERROR_CRYPTO_KEY, "No valid Ed25519 keys found in imported GPG keys");
  }

  *num_keys = valid_keys;
  return ASCIICHAT_OK;
}
