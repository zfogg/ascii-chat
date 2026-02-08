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
  bool update_available;    ///< True if newer version exists
  char latest_version[64];  ///< Latest version tag (e.g., "v0.9.0")
  char latest_sha[41];      ///< Latest release commit SHA (full 40 chars + null)
  char current_version[64]; ///< Current version string
  char current_sha[41];     ///< Current commit SHA
  char release_url[512];    ///< GitHub release page URL
  time_t last_check_time;   ///< Timestamp of last check
  bool check_succeeded;     ///< True if check completed (even if no update)
} update_check_result_t;

/**
 * @brief Installation method for suggesting upgrade command
 */
typedef enum {
  INSTALL_METHOD_HOMEBREW, ///< Installed via Homebrew
  INSTALL_METHOD_ARCH_AUR, ///< Installed via Arch AUR (paru/yay)
  INSTALL_METHOD_GITHUB,   ///< Manual install from GitHub releases
  INSTALL_METHOD_UNKNOWN,  ///< Unknown installation method
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
void update_check_get_upgrade_suggestion(install_method_t method, const char *latest_version, char *buffer,
                                         size_t buffer_size);

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
