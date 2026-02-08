/**
 * @file network/update_checker.c
 * @brief Update checker implementation with GitHub API and cache management
 */

#include <ascii-chat/network/update_checker.h>
#include <ascii-chat/network/http_client.h>
#include <ascii-chat/version.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/platform/socket.h>
#include <ascii-chat/platform/network.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/util/path.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/common.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

// Cache file is stored in ~/.config/ascii-chat/last_update_check
#define UPDATE_CHECK_CACHE_FILENAME "last_update_check"

// Check is fresh if < 7 days old
#define UPDATE_CHECK_CACHE_MAX_AGE_SECONDS (7 * 24 * 60 * 60)

// DNS timeout for connectivity check
#define DNS_TIMEOUT_SECONDS 2

// GitHub API endpoint
#define GITHUB_API_HOSTNAME "api.github.com"
#define GITHUB_RELEASES_PATH "/repos/zfogg/ascii-chat/releases/latest"

/**
 * @brief Get cache file path
 * @return Allocated string with cache file path (caller must free)
 */
static char *get_cache_file_path(void) {
  // Get config directory
  char *config_dir = path_get_ascii_chat_config_dir();
  if (!config_dir) {
    log_error("Failed to get config directory for update check cache");
    return NULL;
  }

  // Build cache file path
  size_t path_len = strlen(config_dir) + strlen(UPDATE_CHECK_CACHE_FILENAME) + 2;
  char *cache_path = SAFE_MALLOC(path_len, char *);
  snprintf(cache_path, path_len, "%s%s", config_dir, UPDATE_CHECK_CACHE_FILENAME);
  SAFE_FREE(config_dir);

  return cache_path;
}

asciichat_error_t update_check_load_cache(update_check_result_t *result) {
  if (!result) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL result pointer");
  }

  memset(result, 0, sizeof(*result));

  char *cache_path = get_cache_file_path();
  if (!cache_path) {
    return SET_ERRNO(ERROR_IO, "Could not determine cache file path");
  }

  FILE *f = fopen(cache_path, "r");
  if (!f) {
    SAFE_FREE(cache_path);
    return SET_ERRNO(ERROR_IO, "Cache file does not exist");
  }

  // Read line 1: timestamp
  char line[512];
  if (!fgets(line, sizeof(line), f)) {
    fclose(f);
    SAFE_FREE(cache_path);
    return SET_ERRNO(ERROR_IO, "Failed to read timestamp from cache");
  }
  result->last_check_time = (time_t)atoll(line);

  // Read line 2: latest version (may be empty if check failed)
  if (fgets(line, sizeof(line), f)) {
    // Remove newline
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') {
      line[len - 1] = '\0';
    }
    SAFE_STRNCPY(result->latest_version, line, sizeof(result->latest_version));
  }

  // Read line 3: latest SHA (may be empty if check failed)
  if (fgets(line, sizeof(line), f)) {
    // Remove newline
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') {
      line[len - 1] = '\0';
    }
    SAFE_STRNCPY(result->latest_sha, line, sizeof(result->latest_sha));
  }

  fclose(f);
  SAFE_FREE(cache_path);

  // Fill in current version/SHA
  SAFE_STRNCPY(result->current_version, ASCII_CHAT_VERSION_STRING, sizeof(result->current_version));
  SAFE_STRNCPY(result->current_sha, ASCII_CHAT_GIT_COMMIT_HASH, sizeof(result->current_sha));

  // Determine if update is available (if we have cached data)
  if (result->latest_sha[0] != '\0' && result->current_sha[0] != '\0') {
    result->update_available = (strcmp(result->latest_sha, result->current_sha) != 0);
    result->check_succeeded = true;
  }

  return ASCIICHAT_OK;
}

asciichat_error_t update_check_save_cache(const update_check_result_t *result) {
  if (!result) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL result pointer");
  }

  char *cache_path = get_cache_file_path();
  if (!cache_path) {
    return SET_ERRNO(ERROR_IO, "Could not determine cache file path");
  }

  FILE *f = fopen(cache_path, "w");
  if (!f) {
    SAFE_FREE(cache_path);
    return SET_ERRNO(ERROR_IO, "Failed to open cache file for writing");
  }

  // Write timestamp
  fprintf(f, "%lld\n", (long long)result->last_check_time);

  // Write version (may be empty if check failed)
  fprintf(f, "%s\n", result->latest_version);

  // Write SHA (may be empty if check failed)
  fprintf(f, "%s\n", result->latest_sha);

  fclose(f);
  SAFE_FREE(cache_path);

  return ASCIICHAT_OK;
}

bool update_check_is_cache_fresh(const update_check_result_t *result) {
  if (!result || result->last_check_time == 0) {
    return false;
  }

  time_t now = time(NULL);
  time_t age = now - result->last_check_time;

  return (age < UPDATE_CHECK_CACHE_MAX_AGE_SECONDS);
}

/**
 * @brief Test DNS connectivity by resolving api.github.com
 * @return true if DNS resolution succeeds, false otherwise
 */
static bool test_dns_connectivity(void) {
  struct addrinfo hints, *result = NULL;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET; // IPv4
  hints.ai_socktype = SOCK_STREAM;

  log_debug("Testing DNS connectivity to %s...", GITHUB_API_HOSTNAME);

  int ret = getaddrinfo(GITHUB_API_HOSTNAME, "443", &hints, &result);
  if (ret != 0) {
    log_warn("DNS resolution failed for %s: %s", GITHUB_API_HOSTNAME, gai_strerror(ret));
    return false;
  }

  if (result) {
    freeaddrinfo(result);
  }

  log_debug("DNS connectivity test succeeded");
  return true;
}

/**
 * @brief Extract JSON string field value
 * @param json JSON string to parse
 * @param field Field name to extract (e.g., "tag_name")
 * @param[out] output Output buffer
 * @param output_size Size of output buffer
 * @return true if field found and extracted, false otherwise
 */
static bool parse_json_string_field(const char *json, const char *field, char *output, size_t output_size) {
  if (!json || !field || !output || output_size == 0) {
    return false;
  }

  // Build search pattern: "field":"value"
  char pattern[128];
  snprintf(pattern, sizeof(pattern), "\"%s\"", field);

  const char *field_start = strstr(json, pattern);
  if (!field_start) {
    return false;
  }

  // Skip past field name and find opening quote
  const char *value_start = strchr(field_start + strlen(pattern), '"');
  if (!value_start) {
    return false;
  }
  value_start++; // Skip opening quote

  // Find closing quote
  const char *value_end = strchr(value_start, '"');
  if (!value_end) {
    return false;
  }

  // Copy value
  size_t value_len = value_end - value_start;
  if (value_len >= output_size) {
    value_len = output_size - 1;
  }

  memcpy(output, value_start, value_len);
  output[value_len] = '\0';

  return true;
}

/**
 * @brief Parse GitHub releases API response
 * @param json JSON response body
 * @param[out] tag_name Latest release tag (e.g., "v0.9.0")
 * @param tag_size Size of tag_name buffer
 * @param[out] commit_sha Commit SHA for the release
 * @param sha_size Size of commit_sha buffer
 * @param[out] html_url Release page URL
 * @param url_size Size of html_url buffer
 * @return true if parsing succeeded, false otherwise
 */
static bool parse_github_release_json(const char *json, char *tag_name, size_t tag_size, char *commit_sha,
                                      size_t sha_size, char *html_url, size_t url_size) {
  if (!json) {
    return false;
  }

  // Extract tag_name
  if (!parse_json_string_field(json, "tag_name", tag_name, tag_size)) {
    log_error("Failed to parse tag_name from GitHub API response");
    return false;
  }

  // Extract target_commitish (the SHA)
  if (!parse_json_string_field(json, "target_commitish", commit_sha, sha_size)) {
    log_error("Failed to parse target_commitish from GitHub API response");
    return false;
  }

  // Extract html_url
  if (!parse_json_string_field(json, "html_url", html_url, url_size)) {
    log_error("Failed to parse html_url from GitHub API response");
    return false;
  }

  return true;
}
