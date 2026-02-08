# Update Checker Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add automatic and manual update checking against GitHub releases API with DNS connectivity detection, cache management, installation method detection, and UI integration.

**Architecture:** Three-layer system: (1) Core update checker in `lib/network/update_checker.c` with DNS test, HTTPS GitHub API call, simple JSON parsing, SHA comparison, and cache file management. (2) Installation method detection (Homebrew, Arch AUR, GitHub releases). (3) UI integration in splash/status screens with colored banner and deferred action for `--check-update`.

**Tech Stack:** BearSSL HTTPS (existing `https_get()`), manual JSON parsing (no dependencies), DNS resolution via `getaddrinfo()`, cache file in `~/.config/ascii-chat/`, options system deferred actions, splash/status UI integration.

---

## Task 1: Add Update Checker Header and Data Structures

**Files:**
- Create: `include/ascii-chat/network/update_checker.h`

**Step 1: Write the header file**

Create the header file with data structures and function declarations:

```c
#pragma once

/**
 * @file network/update_checker.h
 * @brief 🔄 Update checker for GitHub releases with DNS connectivity test and cache
 * @ingroup network
 * @addtogroup network
 * @{
 *
 * Provides automatic and manual update checking against GitHub releases API.
 * - Checks once per week automatically at startup
 * - Manual check via --check-update flag
 * - DNS connectivity test before attempting HTTP request
 * - Compares git commit SHA to detect updates
 * - Caches results in ~/.config/ascii-chat/last_update_check
 * - Detects installation method (Homebrew, Arch AUR, GitHub)
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "../common.h"

/**
 * @brief Update check result data
 */
typedef struct {
  bool update_available;              ///< True if newer version exists
  char latest_version[64];            ///< Latest version tag (e.g., "v0.9.0")
  char latest_sha[41];                ///< Latest release commit SHA (full 40 chars + null)
  char current_version[64];           ///< Current version string
  char current_sha[41];               ///< Current commit SHA
  char release_url[512];              ///< GitHub release page URL
  time_t last_check_time;             ///< Timestamp of last check
  bool check_succeeded;               ///< True if check completed (even if no update)
} update_check_result_t;

/**
 * @brief Installation method for suggesting upgrade command
 */
typedef enum {
  INSTALL_METHOD_HOMEBREW,   ///< Installed via Homebrew
  INSTALL_METHOD_ARCH_AUR,   ///< Installed via Arch AUR (paru/yay)
  INSTALL_METHOD_GITHUB,     ///< Manual install from GitHub releases
  INSTALL_METHOD_UNKNOWN,    ///< Unknown installation method
} install_method_t;

/**
 * @brief Check for updates from GitHub releases API
 *
 * Performs DNS connectivity test, fetches latest release from GitHub API,
 * compares SHA with current build, and caches the result.
 *
 * @param[out] result Output structure with check results
 * @return ASCIICHAT_OK on success (check completed), error otherwise
 *
 * @note On timeout or network error, logs warning and marks as checked in cache
 * @note On DNS failure, does not update cache (will retry when online)
 * @note Timeout is 2 seconds for the entire operation
 */
asciichat_error_t update_check_perform(update_check_result_t *result);

/**
 * @brief Load cached update check result
 *
 * Reads cache file from ~/.config/ascii-chat/last_update_check.
 * Cache format (3 lines):
 * - Line 1: Unix timestamp
 * - Line 2: Latest version tag (or empty if check failed)
 * - Line 3: Latest SHA (full 40 chars, or empty if check failed)
 *
 * @param[out] result Output structure with cached results
 * @return ASCIICHAT_OK if cache valid, error if missing/corrupt
 */
asciichat_error_t update_check_load_cache(update_check_result_t *result);

/**
 * @brief Save update check result to cache
 *
 * Writes cache file to ~/.config/ascii-chat/last_update_check.
 *
 * @param result Result to cache
 * @return ASCIICHAT_OK on success, error otherwise
 */
asciichat_error_t update_check_save_cache(const update_check_result_t *result);

/**
 * @brief Check if cache is fresh (< 7 days old)
 *
 * @param result Cached result to check
 * @return true if cache is < 7 days old, false otherwise
 */
bool update_check_is_cache_fresh(const update_check_result_t *result);

/**
 * @brief Detect installation method
 *
 * Checks binary path and system files to determine how ascii-chat was installed.
 *
 * @return Installation method enum
 */
install_method_t update_check_detect_install_method(void);

/**
 * @brief Get upgrade suggestion string
 *
 * Returns appropriate upgrade command or URL based on installation method.
 *
 * @param method Installation method
 * @param latest_version Latest version tag for GitHub URL
 * @param[out] buffer Output buffer for suggestion string
 * @param buffer_size Size of output buffer
 */
void update_check_get_upgrade_suggestion(install_method_t method, const char *latest_version,
                                         char *buffer, size_t buffer_size);

/**
 * @brief Format update notification message
 *
 * Creates human-readable update notification with version comparison and upgrade suggestion.
 * Example: "Update available: v0.8.1 (f8dc35e1) → v0.9.0 (a1b2c3d4). Run: brew upgrade ascii-chat"
 *
 * @param result Update check result
 * @param[out] buffer Output buffer for formatted message
 * @param buffer_size Size of output buffer
 */
void update_check_format_notification(const update_check_result_t *result, char *buffer, size_t buffer_size);

/** @} */
```

**Step 2: Commit**

```bash
git add include/ascii-chat/network/update_checker.h
git commit -m "feat: add update checker header with data structures and API"
```

---

## Task 2: Implement Cache File Management

**Files:**
- Create: `lib/network/update_checker.c`

**Step 1: Write cache file path and basic structure**

```c
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
```

**Step 2: Commit**

```bash
git add lib/network/update_checker.c
git commit -m "feat: implement update check cache file management"
```

---

## Task 3: Implement DNS Connectivity Check

**Files:**
- Modify: `lib/network/update_checker.c`

**Step 1: Add DNS connectivity test function**

Add this function after the cache functions:

```c
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
```

**Step 2: Commit**

```bash
git add lib/network/update_checker.c
git commit -m "feat: add DNS connectivity test for update checker"
```

---

## Task 4: Implement Simple JSON Parsing for GitHub API Response

**Files:**
- Modify: `lib/network/update_checker.c`

**Step 1: Add JSON parsing helper functions**

Add these functions after the DNS test:

```c
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
static bool parse_github_release_json(const char *json, char *tag_name, size_t tag_size,
                                     char *commit_sha, size_t sha_size,
                                     char *html_url, size_t url_size) {
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
```

**Step 2: Commit**

```bash
git add lib/network/update_checker.c
git commit -m "feat: add simple JSON parser for GitHub API responses"
```

---

## Task 5: Implement Core Update Check Logic

**Files:**
- Modify: `lib/network/update_checker.c`

**Step 1: Add main update check function**

```c
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
  if (!test_dns_connectivity()) {
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

  if (!parse_github_release_json(response, latest_tag, sizeof(latest_tag),
                                 latest_sha, sizeof(latest_sha),
                                 release_url, sizeof(release_url))) {
    log_error("Failed to parse GitHub API response");
    SAFE_FREE(response);
    result->check_succeeded = false;
    update_check_save_cache(result);
    return SET_ERRNO(ERROR_PARSE, "Failed to parse GitHub API JSON");
  }

  SAFE_FREE(response);

  // Fill in result
  SAFE_STRNCPY(result->latest_version, latest_tag, sizeof(result->latest_version));
  SAFE_STRNCPY(result->latest_sha, latest_sha, sizeof(result->latest_sha));
  SAFE_STRNCPY(result->release_url, release_url, sizeof(result->release_url));
  result->check_succeeded = true;

  // Compare SHAs to detect update
  result->update_available = (strcmp(result->current_sha, result->latest_sha) != 0);

  if (result->update_available) {
    log_info("Update available: %s (%.*s) → %s (%.*s)",
             result->current_version, 8, result->current_sha,
             result->latest_version, 8, result->latest_sha);
  } else {
    log_info("Already on latest version: %s (%.*s)",
             result->current_version, 8, result->current_sha);
  }

  // Save to cache
  update_check_save_cache(result);

  return ASCIICHAT_OK;
}
```

**Step 2: Commit**

```bash
git add lib/network/update_checker.c
git commit -m "feat: implement core update check logic with GitHub API"
```

---

## Task 6: Implement Installation Method Detection

**Files:**
- Modify: `lib/network/update_checker.c`

**Step 1: Add installation method detection functions**

```c
/**
 * @brief Check if binary path contains Homebrew markers
 */
static bool is_homebrew_install(void) {
  // Check if binary is in Homebrew Cellar directory
  const char *exe_path = platform_get_executable_path();
  if (!exe_path) {
    return false;
  }

  return (strstr(exe_path, "/Cellar/ascii-chat") != NULL ||
          strstr(exe_path, "/opt/homebrew/") != NULL ||
          strstr(exe_path, "/usr/local/Cellar/") != NULL);
}

/**
 * @brief Check if system is Arch Linux
 */
static bool is_arch_linux(void) {
#ifdef __linux__
  // Check /etc/os-release for Arch
  FILE *f = fopen("/etc/os-release", "r");
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
```

**Step 2: Commit**

```bash
git add lib/network/update_checker.c
git commit -m "feat: add installation method detection for Homebrew and Arch"
```

---

## Task 7: Implement Upgrade Suggestion Formatting

**Files:**
- Modify: `lib/network/update_checker.c`

**Step 1: Add formatting functions**

```c
void update_check_get_upgrade_suggestion(install_method_t method, const char *latest_version,
                                         char *buffer, size_t buffer_size) {
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
  snprintf(buffer, buffer_size,
           "Update available: %s (%.8s) → %s (%.8s). %s%s",
           result->current_version, result->current_sha,
           result->latest_version, result->latest_sha,
           (method == INSTALL_METHOD_GITHUB || method == INSTALL_METHOD_UNKNOWN) ? "Download: " : "Run: ",
           suggestion);
}
```

**Step 2: Commit**

```bash
git add lib/network/update_checker.c
git commit -m "feat: add upgrade suggestion formatting for different install methods"
```

---

## Task 8: Add Update Checker to CMake Build

**Files:**
- Modify: `cmake/targets/Libraries.cmake`

**Step 1: Find the network library section and add update_checker.c**

Find the section that lists network library sources (search for `lib/network/http_client.c`) and add:

```cmake
  lib/network/update_checker.c
```

**Step 2: Build and verify**

```bash
cmake --build build
```

Expected: Build succeeds with no errors

**Step 3: Commit**

```bash
git add cmake/targets/Libraries.cmake
git commit -m "build: add update_checker.c to network library"
```

---

## Task 9: Add --check-update Deferred Action

**Files:**
- Modify: `include/ascii-chat/options/actions.h`
- Modify: `lib/options/parsing/actions.c`

**Step 1: Add ACTION_CHECK_UPDATE to actions.h enum**

In `include/ascii-chat/options/actions.h`, add to the `deferred_action_t` enum:

```c
typedef enum {
  ACTION_NONE = 0,
  ACTION_LIST_WEBCAMS,
  ACTION_LIST_MICROPHONES,
  ACTION_LIST_SPEAKERS,
  ACTION_SHOW_CAPABILITIES,
  ACTION_CHECK_UPDATE,  // Add this line
} deferred_action_t;
```

And add function declaration at the end:

```c
/**
 * @brief Check for updates and display results
 *
 * Performs update check against GitHub releases API and displays results.
 * Exits with code 0 on success, 1 on error.
 */
void action_check_update(void);
```

**Step 2: Implement action in actions.c**

Add to `lib/options/parsing/actions.c`:

```c
#include <ascii-chat/network/update_checker.h>

// ... (in action callback section)

void action_check_update(void) {
  // Defer execution until after options are fully parsed
  actions_defer(ACTION_CHECK_UPDATE, NULL);
}

/**
 * @brief Internal implementation of check update action
 *
 * Called during STAGE 8 of options_init() after all initialization is complete.
 */
static void action_check_update_impl(void) {
  printf("Checking for updates...\n");

  update_check_result_t result;
  asciichat_error_t err = update_check_perform(&result);

  if (err != ASCIICHAT_OK) {
    asciichat_error_context_t ctx;
    if (HAS_ERRNO(&ctx)) {
      fprintf(stderr, "Update check failed: %s\n", ctx.context_message);
    }
    exit(1);
  }

  // Display results
  if (result.update_available) {
    char notification[1024];
    update_check_format_notification(&result, notification, sizeof(notification));
    printf("\n%s\n\n", notification);
  } else {
    printf("\nYou are already on the latest version: %s (%.8s)\n\n",
           result.current_version, result.current_sha);
  }

  exit(0);
}
```

**Step 3: Wire up action in actions_execute_deferred()**

Find `actions_execute_deferred()` in `actions.c` and add case:

```c
void actions_execute_deferred(void) {
  deferred_action_t action = actions_get_deferred();
  // const action_args_t *args = actions_get_args();  // Uncomment if needed

  switch (action) {
    case ACTION_NONE:
      return; // No action to execute

    case ACTION_LIST_WEBCAMS:
      action_list_webcams_impl();
      break;

    case ACTION_LIST_MICROPHONES:
      action_list_microphones_impl();
      break;

    case ACTION_LIST_SPEAKERS:
      action_list_speakers_impl();
      break;

    case ACTION_SHOW_CAPABILITIES:
      action_show_capabilities_impl();
      break;

    case ACTION_CHECK_UPDATE:  // Add this case
      action_check_update_impl();
      break;

    default:
      log_error("Unknown deferred action: %d", action);
      break;
  }
}
```

**Step 4: Commit**

```bash
git add include/ascii-chat/options/actions.h lib/options/parsing/actions.c
git commit -m "feat: add --check-update deferred action"
```

---

## Task 10: Register --check-update and --no-check-update Options

**Files:**
- Modify: `lib/options/registry/general.c` (or wherever binary-level options are registered)

**Step 1: Find option registration and add flags**

Search for where `--version` or other binary-level options are registered, then add:

```c
// --check-update action
OPTION_BUILDER_ADD_ACTION(builder, "check-update", '\0',
                          "Check for updates from GitHub releases and exit",
                          action_check_update, OPTION_MODE_BINARY);

// --no-check-update flag
OPTION_BUILDER_ADD_BOOL_FIELD(builder, "no-check-update", '\0',
                              "Disable automatic update checks at startup",
                              offsetof(options_t, no_check_update), false,
                              OPTION_MODE_ALL);
```

**Step 2: Add field to options_t struct**

In `include/ascii-chat/options/options.h`, find the `options_t` struct and add:

```c
typedef struct options_t {
  // ... existing fields ...

  bool no_check_update;  ///< Disable automatic update checks

  // ... rest of fields ...
} options_t;
```

**Step 3: Build and test**

```bash
cmake --build build
./build/bin/ascii-chat --check-update
```

Expected: Runs update check and displays results

**Step 4: Commit**

```bash
git add lib/options/registry/general.c include/ascii-chat/options/options.h
git commit -m "feat: register --check-update and --no-check-update options"
```

---

## Task 11: Integrate Automatic Update Check at Startup

**Files:**
- Modify: `lib/options/options.c` (or `src/main.c` depending on architecture)

**Step 1: Add automatic check after options init**

Find where `options_init()` completes in `src/main.c` (after RCU publishing), add:

```c
#include <ascii-chat/network/update_checker.h>

// ... in main() after options_init() ...

// Automatic update check (if not disabled and cache is stale)
if (!GET_OPTION(no_check_update)) {
  update_check_result_t cached_result;
  asciichat_error_t cache_err = update_check_load_cache(&cached_result);

  // If cache doesn't exist or is stale (>7 days), perform check
  bool should_check = (cache_err != ASCIICHAT_OK) ||
                      !update_check_is_cache_fresh(&cached_result);

  if (should_check) {
    log_debug("Performing automatic update check (cache stale or missing)");
    update_check_result_t result;
    asciichat_error_t check_err = update_check_perform(&result);

    if (check_err != ASCIICHAT_OK) {
      log_warn("Automatic update check failed (will retry later)");
    } else if (result.update_available) {
      char notification[1024];
      update_check_format_notification(&result, notification, sizeof(notification));
      log_warn("%s", notification);
      // Note: Splash/status screens will also display this
    }
  }
}
```

**Step 2: Commit**

```bash
git add src/main.c
git commit -m "feat: add automatic update check at startup with cache"
```

---

## Task 12: Add Update Notification to Splash Screen

**Files:**
- Modify: `lib/ui/splash.c`
- Modify: `include/ascii-chat/ui/splash.h`

**Step 1: Add update notification to splash header callback**

In `lib/ui/splash.c`, add at the top:

```c
#include <ascii-chat/network/update_checker.h>
```

Find the `render_splash_header` or similar function (the callback that renders the splash screen header), and add before the ASCII art:

```c
static void render_splash_header(terminal_size_t term_size, void *user_data) {
  // Check if update is available
  update_check_result_t cached;
  if (update_check_load_cache(&cached) == ASCIICHAT_OK &&
      cached.check_succeeded && cached.update_available) {
    // Display update banner at top
    char notification[1024];
    update_check_format_notification(&cached, notification, sizeof(notification));

    printf("\033[1;33m"); // Yellow/gold color
    printf("╔");
    for (int i = 0; i < term_size.cols - 2; i++) printf("═");
    printf("╗\n");

    printf("║ ⬆️  %s", notification);
    // Pad to end of line
    int msg_len = strlen(notification) + 4; // "║ ⬆️  "
    for (int i = msg_len; i < term_size.cols - 1; i++) printf(" ");
    printf("║\n");

    printf("╚");
    for (int i = 0; i < term_size.cols - 2; i++) printf("═");
    printf("╝\033[0m\n\n");
  }

  // ... rest of existing splash rendering ...
}
```

**Step 2: Commit**

```bash
git add lib/ui/splash.c
git commit -m "feat: display update notification banner on splash screen"
```

---

## Task 13: Add Update Notification to Server Status Screen

**Files:**
- Modify: `lib/ui/server_status.c`

**Step 1: Add update notification to status header**

In `lib/ui/server_status.c`, add at top:

```c
#include <ascii-chat/network/update_checker.h>
```

Find `render_server_status_header()` and add after the title line (around line 2-3 of the header):

```c
static void render_server_status_header(terminal_size_t term_size, void *user_data) {
  const server_status_t *status = (const server_status_t *)user_data;
  if (!status) {
    return;
  }

  // Line 1: Top border
  printf("\033[1;36m━");
  for (int i = 1; i < term_size.cols - 1; i++) {
    printf("━");
  }
  printf("\033[0m\n");

  // Check if update is available - insert banner after top border
  update_check_result_t cached;
  if (update_check_load_cache(&cached) == ASCIICHAT_OK &&
      cached.check_succeeded && cached.update_available) {
    char notification[1024];
    update_check_format_notification(&cached, notification, sizeof(notification));

    printf("\033[1;33m⬆️  %s\033[0m", notification);
    int msg_len = strlen(notification) + 4;
    for (int i = msg_len; i < term_size.cols; i++) printf(" ");
    printf("\n");
  }

  // ... rest of existing status rendering (Line 2: Title, etc.) ...
}
```

**Step 2: Commit**

```bash
git add lib/ui/server_status.c
git commit -m "feat: display update notification banner on server status screen"
```

---

## Task 14: Write Unit Tests for Update Checker

**Files:**
- Create: `tests/unit/network/update_checker_test.c`

**Step 1: Write comprehensive unit tests**

```c
#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <ascii-chat/network/update_checker.h>
#include <ascii-chat/tests/common.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// Test fixture
void setup_update_checker(void) {
  log_set_level(LOG_FATAL);
}

void teardown_update_checker(void) {
  log_set_level(LOG_DEBUG);
}

TestSuite(update_checker, .init = setup_update_checker, .fini = teardown_update_checker);

// =============================================================================
// Cache Tests
// =============================================================================

Test(update_checker, cache_save_and_load) {
  update_check_result_t result = {0};
  result.update_available = true;
  result.last_check_time = time(NULL);
  SAFE_STRNCPY(result.latest_version, "v0.9.0", sizeof(result.latest_version));
  SAFE_STRNCPY(result.latest_sha, "a1b2c3d4567890abcdef1234567890abcdef1234", sizeof(result.latest_sha));
  SAFE_STRNCPY(result.current_version, "v0.8.1", sizeof(result.current_version));
  SAFE_STRNCPY(result.current_sha, "f8dc35e1073b08848f41af015074a3815f9125b4", sizeof(result.current_sha));
  result.check_succeeded = true;

  // Save
  asciichat_error_t err = update_check_save_cache(&result);
  cr_assert_eq(err, ASCIICHAT_OK, "Cache save should succeed");

  // Load
  update_check_result_t loaded = {0};
  err = update_check_load_cache(&loaded);
  cr_assert_eq(err, ASCIICHAT_OK, "Cache load should succeed");

  cr_assert_str_eq(loaded.latest_version, "v0.9.0", "Version should match");
  cr_assert_str_eq(loaded.latest_sha, "a1b2c3d4567890abcdef1234567890abcdef1234", "SHA should match");
  cr_assert(loaded.update_available, "Update available flag should be set");
}

Test(update_checker, cache_freshness) {
  update_check_result_t result = {0};
  result.last_check_time = time(NULL);
  result.check_succeeded = true;

  cr_assert(update_check_is_cache_fresh(&result), "Fresh cache should return true");

  // Simulate old cache (8 days ago)
  result.last_check_time = time(NULL) - (8 * 24 * 60 * 60);
  cr_assert(!update_check_is_cache_fresh(&result), "Stale cache should return false");
}

// =============================================================================
// Installation Method Detection Tests
// =============================================================================

Test(update_checker, detect_install_method) {
  install_method_t method = update_check_detect_install_method();

  // Just verify it returns a valid enum value
  cr_assert(method >= INSTALL_METHOD_HOMEBREW && method <= INSTALL_METHOD_UNKNOWN,
            "Should return valid install method");
}

// =============================================================================
// Formatting Tests
// =============================================================================

Test(update_checker, format_upgrade_suggestion_homebrew) {
  char buffer[512];
  update_check_get_upgrade_suggestion(INSTALL_METHOD_HOMEBREW, "v0.9.0", buffer, sizeof(buffer));
  cr_assert_str_eq(buffer, "brew upgrade ascii-chat", "Homebrew suggestion should match");
}

Test(update_checker, format_upgrade_suggestion_github) {
  char buffer[512];
  update_check_get_upgrade_suggestion(INSTALL_METHOD_GITHUB, "v0.9.0", buffer, sizeof(buffer));
  cr_assert(strstr(buffer, "github.com/zfogg/ascii-chat/releases") != NULL,
            "GitHub suggestion should include releases URL");
}

Test(update_checker, format_notification_message) {
  update_check_result_t result = {0};
  result.update_available = true;
  SAFE_STRNCPY(result.current_version, "v0.8.1", sizeof(result.current_version));
  SAFE_STRNCPY(result.current_sha, "f8dc35e1073b08848f41af015074a3815f9125b4", sizeof(result.current_sha));
  SAFE_STRNCPY(result.latest_version, "v0.9.0", sizeof(result.latest_version));
  SAFE_STRNCPY(result.latest_sha, "a1b2c3d4567890abcdef1234567890abcdef1234", sizeof(result.latest_sha));

  char buffer[1024];
  update_check_format_notification(&result, buffer, sizeof(buffer));

  cr_assert(strstr(buffer, "v0.8.1") != NULL, "Should contain current version");
  cr_assert(strstr(buffer, "v0.9.0") != NULL, "Should contain latest version");
  cr_assert(strstr(buffer, "f8dc35e1") != NULL, "Should contain current SHA (first 8 chars)");
  cr_assert(strstr(buffer, "a1b2c3d4") != NULL, "Should contain latest SHA (first 8 chars)");
}
```

**Step 2: Add test to CMake**

In `tests/CMakeLists.txt` (or appropriate test config), add:

```cmake
add_criterion_test(update_checker tests/unit/network/update_checker_test.c)
```

**Step 3: Run tests**

```bash
cmake --build build --target tests
ctest --test-dir build -R update_checker --output-on-failure
```

Expected: All tests pass

**Step 4: Commit**

```bash
git add tests/unit/network/update_checker_test.c tests/CMakeLists.txt
git commit -m "test: add unit tests for update checker"
```

---

## Task 15: Add Manual Testing and Documentation

**Files:**
- Create: `tests/manual/test_update_checker.sh`
- Modify: `CLAUDE.md`

**Step 1: Create manual test script**

```bash
#!/usr/bin/env bash
# Manual test for update checker functionality

set -e

echo "=== Update Checker Manual Tests ==="
echo

echo "Test 1: Manual update check"
./build/bin/ascii-chat --check-update
echo

echo "Test 2: Disable automatic check"
./build/bin/ascii-chat --no-check-update server --help
echo

echo "Test 3: Check cache file"
CACHE_FILE="$HOME/.config/ascii-chat/last_update_check"
if [ -f "$CACHE_FILE" ]; then
  echo "Cache file exists:"
  cat "$CACHE_FILE"
else
  echo "Cache file not found (may be first run)"
fi
echo

echo "Test 4: Splash screen with update notification"
./build/bin/ascii-chat mirror --snapshot --snapshot-delay 0
echo

echo "All manual tests complete!"
```

**Step 2: Make executable and test**

```bash
chmod +x tests/manual/test_update_checker.sh
./tests/manual/test_update_checker.sh
```

**Step 3: Update CLAUDE.md with update checker docs**

Add to `CLAUDE.md` under a new section:

```markdown
## Update Checking

ascii-chat automatically checks for updates from GitHub releases.

### Automatic Checks

- Runs at startup if cache is >7 days old
- DNS connectivity test before HTTP request (skips if offline)
- Compares git commit SHA to detect updates
- Cache stored in `~/.config/ascii-chat/last_update_check`
- Displays notification on splash/status screens if update available

### Manual Check

```bash
# Check for updates immediately and exit
./build/bin/ascii-chat --check-update

# Disable automatic checks
./build/bin/ascii-chat --no-check-update <mode>
```

### Update Notifications

When an update is available:
- **Splash screen** (client/mirror): Yellow banner at top
- **Status screen** (server/discovery-service): Yellow banner below title
- **Log message**: `log_warn()` with version comparison

Example notification:
```
Update available: v0.8.1 (f8dc35e1) → v0.9.0 (a1b2c3d4). Run: brew upgrade ascii-chat
```

### Installation Method Detection

The update checker detects how ascii-chat was installed:
- **Homebrew**: Binary path contains `/Cellar/ascii-chat` → suggests `brew upgrade ascii-chat`
- **Arch Linux**: `/etc/os-release` contains `ID=arch` → suggests `paru -S ascii-chat` or `yay -S ascii-chat`
- **GitHub**: Default fallback → links to releases page

### Cache File Format

`~/.config/ascii-chat/last_update_check`:
```
1739145600                                          # Unix timestamp
v0.9.0                                              # Latest version tag
a1b2c3d4567890abcdef1234567890abcdef1234          # Latest SHA (40 chars)
```

### Debugging

```bash
# Force update check (ignores cache)
rm ~/.config/ascii-chat/last_update_check
./build/bin/ascii-chat --check-update

# Watch update check in logs
./build/bin/ascii-chat --log-level debug server --grep "/update|github/i"
```
```

**Step 4: Commit**

```bash
git add tests/manual/test_update_checker.sh CLAUDE.md
chmod +x tests/manual/test_update_checker.sh
git commit -m "docs: add update checker manual tests and documentation"
```

---

## Final Verification

**Run full test suite:**

```bash
cmake --build build --target tests
ctest --test-dir build --output-on-failure
```

**Test manual check:**

```bash
./build/bin/ascii-chat --check-update
```

**Test with disabled check:**

```bash
./build/bin/ascii-chat --no-check-update mirror --snapshot --snapshot-delay 0
```

**Verify splash screen shows update notification:**

```bash
# Force stale cache by editing timestamp
./build/bin/ascii-chat mirror
# Look for yellow update banner at top of splash screen
```

**Final commit:**

```bash
git add -A
git commit -m "feat: complete update checker implementation with GitHub API integration

- Add automatic weekly update checks at startup
- Add --check-update flag for manual checks
- Add --no-check-update flag to disable automatic checks
- Implement DNS connectivity test before HTTP requests
- Parse GitHub releases API JSON response
- Compare git SHA to detect updates
- Cache results for 7 days in ~/.config/ascii-chat/last_update_check
- Detect installation method (Homebrew, Arch AUR, GitHub)
- Display update notifications on splash and status screens
- Add comprehensive unit tests and manual test scripts
- Update documentation in CLAUDE.md"
```

---

## Success Criteria

✅ `--check-update` flag performs immediate check and displays results
✅ `--no-check-update` flag disables automatic checks
✅ Automatic check runs at startup if cache >7 days old
✅ DNS test prevents offline HTTP timeouts
✅ GitHub API response parsed correctly (tag_name, target_commitish, html_url)
✅ Git SHA comparison detects updates accurately
✅ Cache file saves/loads timestamp + version + SHA
✅ Installation method detection works for Homebrew and Arch
✅ Splash screen shows yellow update banner when available
✅ Status screen shows yellow update banner when available
✅ Unit tests pass
✅ Manual tests verify end-to-end functionality
✅ Documentation complete in CLAUDE.md
