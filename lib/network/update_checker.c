/**
 * @file network/update_checker.c
 * @brief Update checker implementation with GitHub API and cache management
 */

#include <ascii-chat/network/update_checker.h>
#include <ascii-chat/network/http_client.h>
#include <ascii-chat/network/dns.h>
#include <ascii-chat/version.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/platform/socket.h>
#include <ascii-chat/platform/network.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/platform/util.h>
#include <ascii-chat/util/path.h>
#include <ascii-chat/util/time.h>
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
  char *config_dir = get_config_dir();
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
    return SET_ERRNO(ERROR_FILE_OPERATION, "Could not determine cache file path");
  }

  FILE *f = platform_fopen("file_stream", cache_path, "r");
  if (!f) {
    const char *error_msg = file_read_error_message(cache_path);
    SAFE_FREE(cache_path);
    return SET_ERRNO(ERROR_FILE_OPERATION, "%s", error_msg);
  }

  // Read line 1: timestamp
  char line[512];
  if (!fgets(line, sizeof(line), f)) {
    fclose(f);
    SAFE_FREE(cache_path);
    return SET_ERRNO(ERROR_FILE_OPERATION, "Failed to read timestamp from cache");
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

  // Fill in current version/SHA
  SAFE_STRNCPY(result->current_version, ASCII_CHAT_VERSION_STRING, sizeof(result->current_version));
  SAFE_STRNCPY(result->current_sha, ASCII_CHAT_GIT_COMMIT_HASH, sizeof(result->current_sha));

  // Determine if update is available using version comparison (if we have cached data)
  if (result->latest_version[0] != '\0') {
    semantic_version_t current_ver = version_parse(result->current_version);
    semantic_version_t latest_ver = version_parse(result->latest_version);

    if (current_ver.valid && latest_ver.valid) {
      int cmp = version_compare(latest_ver, current_ver);
      result->update_available = (cmp > 0); // Update available if latest > current
      result->check_succeeded = true;
    } else {
      // Cache contains invalid version data - delete it
      log_warn("Update cache contains invalid version data (current:%s, latest:%s) - deleting corrupted cache",
               result->current_version, result->latest_version);
      if (remove(cache_path) != 0) {
        log_warn("Failed to delete corrupted cache file: %s", cache_path);
      }
      SAFE_FREE(cache_path);
      return SET_ERRNO(ERROR_FORMAT, "Corrupted cache file deleted");
    }
  }

  SAFE_FREE(cache_path);
  return ASCIICHAT_OK;
}

asciichat_error_t update_check_save_cache(const update_check_result_t *result) {
  if (!result) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL result pointer");
  }

  char *cache_path = get_cache_file_path();
  if (!cache_path) {
    return SET_ERRNO(ERROR_FILE_OPERATION, "Could not determine cache file path");
  }

  FILE *f = platform_fopen("file_stream", cache_path, "w");
  if (!f) {
    const char *error_msg = file_write_error_message(cache_path);
    SAFE_FREE(cache_path);
    return SET_ERRNO(ERROR_FILE_OPERATION, "%s", error_msg);
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

asciichat_error_t update_check_perform(update_check_result_t *result) {
  if (!result) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL result pointer");
  }

  memset(result, 0, sizeof(*result));

  // Fill in current version and SHA
  SAFE_STRNCPY(result->current_version, ASCII_CHAT_VERSION_STRING, sizeof(result->current_version));
  SAFE_STRNCPY(result->current_sha, ASCII_CHAT_GIT_COMMIT_HASH, sizeof(result->current_sha));
  result->last_check_time = time(NULL);

  // Test DNS connectivity first
  if (!dns_test_connectivity(GITHUB_API_HOSTNAME)) {
    log_warn("No internet connectivity detected, skipping update check");
    // Don't update cache - we'll retry when online
    return SET_ERRNO(ERROR_NETWORK, "DNS connectivity test failed");
  }

  // Fetch latest release from GitHub API
  log_info("Checking for updates from GitHub releases...");
  char *response = https_get(GITHUB_API_HOSTNAME, GITHUB_RELEASES_PATH);
  if (!response) {
    log_warn("Failed to fetch GitHub releases API (timeout or network error)");
    // Mark as checked even though it failed (prevents repeated offline attempts)
    result->check_succeeded = false;
    update_check_save_cache(result);
    return SET_ERRNO(ERROR_NETWORK, "Failed to fetch GitHub releases");
  }

  // Parse JSON response
  char latest_tag[64] = {0};
  char latest_sha[41] = {0};
  char release_url[512] = {0};

  if (!parse_github_release_json(response, latest_tag, sizeof(latest_tag), latest_sha, sizeof(latest_sha), release_url,
                                 sizeof(release_url))) {
    log_error("Failed to parse GitHub API response");
    SAFE_FREE(response);
    result->check_succeeded = false;
    update_check_save_cache(result);
    return SET_ERRNO(ERROR_FORMAT, "Failed to parse GitHub API JSON");
  }

  SAFE_FREE(response);

  // Fill in result
  SAFE_STRNCPY(result->latest_version, latest_tag, sizeof(result->latest_version));
  SAFE_STRNCPY(result->latest_sha, latest_sha, sizeof(result->latest_sha));
  SAFE_STRNCPY(result->release_url, release_url, sizeof(result->release_url));
  result->check_succeeded = true;

  // Compare versions semantically
  semantic_version_t current_ver = version_parse(result->current_version);
  semantic_version_t latest_ver = version_parse(result->latest_version);

  if (!current_ver.valid || !latest_ver.valid) {
    log_warn("Failed to parse version strings for comparison (current: %s, latest: %s)", result->current_version,
             result->latest_version);
    result->update_available = false;
  } else {
    int cmp = version_compare(latest_ver, current_ver);
    result->update_available = (cmp > 0); // Update available if latest > current
  }

  if (result->update_available) {
    log_info("Update available: %s (%.*s) → %s (%.*s)", result->current_version, 8, result->current_sha,
             result->latest_version, 8, result->latest_sha);
  } else {
    log_info("Already on latest version: %s (%.*s)", result->current_version, 8, result->current_sha);
  }

  // Save to cache
  update_check_save_cache(result);

  return ASCIICHAT_OK;
}

/**
 * @brief Check if binary path contains Homebrew markers
 */
static bool is_homebrew_install(void) {
  // Check if binary is in Homebrew Cellar directory
  char exe_path[1024];
  if (!platform_get_executable_path(exe_path, sizeof(exe_path))) {
    return false;
  }

  return (strstr(exe_path, "/Cellar/ascii-chat") != NULL || strstr(exe_path, "/opt/homebrew/") != NULL ||
          strstr(exe_path, "/usr/local/Cellar/") != NULL);
}

/**
 * @brief Check if system is Arch Linux
 */
static bool is_arch_linux(void) {
#ifdef __linux__
  // Check /etc/os-release for Arch
  FILE *f = platform_fopen("file_stream", "/etc/os-release", "r");
  if (!f) {
    return false;
  }

  char line[256];
  bool is_arch = false;
  while (fgets(line, sizeof(line), f)) {
    if (strstr(line, "ID=arch") || strstr(line, "ID=\"arch\"")) {
      is_arch = true;
      break;
    }
  }

  fclose(f);
  return is_arch;
#else
  return false;
#endif
}

install_method_t update_check_detect_install_method(void) {
  if (is_homebrew_install()) {
    return INSTALL_METHOD_HOMEBREW;
  }

  if (is_arch_linux()) {
    return INSTALL_METHOD_ARCH_AUR;
  }

  // Default to GitHub releases
  return INSTALL_METHOD_GITHUB;
}

void update_check_get_upgrade_suggestion(install_method_t method, const char *latest_version, char *buffer,
                                         size_t buffer_size) {
  if (!buffer || buffer_size == 0) {
    return;
  }

  switch (method) {
  case INSTALL_METHOD_HOMEBREW:
    snprintf(buffer, buffer_size, "brew upgrade ascii-chat");
    break;

  case INSTALL_METHOD_ARCH_AUR:
    // Check if paru is available, otherwise suggest yay
    if (system("command -v paru >/dev/null 2>&1") == 0) {
      snprintf(buffer, buffer_size, "paru -S ascii-chat");
    } else {
      snprintf(buffer, buffer_size, "yay -S ascii-chat");
    }
    break;

  case INSTALL_METHOD_GITHUB:
  case INSTALL_METHOD_UNKNOWN:
  default:
    snprintf(buffer, buffer_size, "https://github.com/zfogg/ascii-chat/releases/tag/%s",
             latest_version ? latest_version : "latest");
    break;
  }
}

void update_check_format_notification(const update_check_result_t *result, char *buffer, size_t buffer_size) {
  if (!result || !buffer || buffer_size == 0) {
    return;
  }

  // Get upgrade suggestion
  install_method_t method = update_check_detect_install_method();
  char suggestion[512];
  update_check_get_upgrade_suggestion(method, result->latest_version, suggestion, sizeof(suggestion));

  // Format: "Update available: v0.8.1 (f8dc35e1) → v0.9.0 (a1b2c3d4). Run: brew upgrade ascii-chat"
  snprintf(buffer, buffer_size, "Update available: %s (%.8s) → %s (%.8s). %s%s", result->current_version,
           result->current_sha, result->latest_version, result->latest_sha,
           (method == INSTALL_METHOD_GITHUB || method == INSTALL_METHOD_UNKNOWN) ? "Download: " : "Run: ", suggestion);
}

asciichat_error_t update_check_startup(update_check_result_t *result) {
  update_check_result_t local_result;
  update_check_result_t *target = result ? result : &local_result;

  // Try to load from cache first
  asciichat_error_t cache_err = update_check_load_cache(target);
  if (cache_err == ASCIICHAT_OK && update_check_is_cache_fresh(target)) {
    // Cache is fresh, use it
    log_debug("Using cached update check result (age: %.1f days)",
              (time(NULL) - target->last_check_time) / (double)SEC_PER_DAY);
    return ASCIICHAT_OK;
  }

  // Cache is stale or missing, perform fresh check
  log_debug("Performing automatic update check (cache %s)", cache_err == ASCIICHAT_OK ? "stale" : "missing");
  asciichat_error_t check_err = update_check_perform(target);
  if (check_err != ASCIICHAT_OK) {
    // Check failed, but don't fail startup
    log_debug("Automatic update check failed (continuing startup)");
    return check_err;
  }

  return ASCIICHAT_OK;
}
