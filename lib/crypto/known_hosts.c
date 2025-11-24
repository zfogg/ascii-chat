/**
 * @file crypto/known_hosts.c
 * @ingroup crypto
 * @brief 📜 SSH known_hosts file parser for host key verification and trust management
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sodium.h>
#ifdef _WIN32
#include <direct.h>
#include <fcntl.h>
#include <io.h> // For _fileno() and STDIN_FILENO on Windows
#define mkdir(path, mode) _mkdir(path)
#define strncasecmp _strnicmp
#ifndef EEXIST
#define EEXIST 17 // Standard errno value for "File exists"
#endif
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#else
#include <fcntl.h> // For O_RDONLY, O_WRONLY, O_CREAT, O_APPEND on POSIX
#include <sys/stat.h>
#include <strings.h>
#include <unistd.h> // For STDIN_FILENO on POSIX
#endif

#include "known_hosts.h"
#include "crypto.h"          // For CRYPTO_* hex size and string literal constants
#include "common.h"          // For BUFFER_SIZE_* constants
#include "asciichat_errno.h" // For asciichat_errno system
#include "keys/keys.h"
#include "util/ip.h"
#include "platform/internal.h"
#include "platform/system.h" // For platform_isatty() and FILE_PERM_* constants
#include "options.h"         // For opt_snapshot_mode
#include "util/path.h"
#include "util/string.h"
#include "tooling/defer/defer.h"

// Global variable to cache the expanded known_hosts path
static char *g_known_hosts_path_cache = NULL;

static char *duplicate_normalized_path(const char *path) {
  if (!path) {
    return NULL;
  }

  char normalized[PLATFORM_MAX_PATH_LENGTH];
  if (!path_normalize_copy(path, normalized, sizeof(normalized))) {
    return NULL;
  }

  size_t len = strlen(normalized) + 1;
  char *copy = SAFE_MALLOC(len, char *);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, normalized, len);
  return copy;
}

static bool try_set_known_hosts_path(const char *candidate, const char *const *allowed_bases,
                                     size_t allowed_base_count) {
  if (!candidate || g_known_hosts_path_cache) {
    return false;
  }

  if (!path_is_absolute(candidate)) {
    log_warn("Rejected known_hosts path (not absolute): %s", candidate);
    return false;
  }

  if (!path_is_within_any_base(candidate, allowed_bases, allowed_base_count)) {
    log_warn("Rejected known_hosts path (outside allowed base): %s", candidate);
    return false;
  }

  char *normalized = duplicate_normalized_path(candidate);
  if (!normalized) {
    log_error("Failed to normalize known_hosts path candidate: %s", candidate);
    return false;
  }

  g_known_hosts_path_cache = normalized;
  log_debug("KNOWN_HOSTS: Using path %s", g_known_hosts_path_cache);
  return true;
}

/**
 * @brief Get the path to the known_hosts file with platform-specific fallbacks
 *
 * This function determines the appropriate location for the known_hosts file
 * by attempting multiple strategies in order of preference. It caches the
 * result to avoid repeated filesystem operations.
 *
 * PATH RESOLUTION STRATEGY:
 * 1. Configuration directory (XDG_CONFIG_HOME or platform equivalent)
 * 2. Standard known_hosts path (~/.ascii-chat/known_hosts)
 * 3. Home directory fallback (~/.ascii-chat/known_hosts)
 * 4. Temporary directory fallback (/tmp/.ascii-chat/known_hosts or %TEMP%)
 * 5. Hard-coded safe default as last resort
 *
 * SECURITY CONSIDERATIONS:
 * - All paths must be absolute to prevent path traversal attacks
 * - Paths are validated against allowed base directories
 * - Environment variables are checked but not blindly trusted
 * - On failure, falls back to system-wide safe defaults
 *
 * PLATFORM BEHAVIOR:
 * - Unix/Linux: Prefers ~/.config/ascii-chat/known_hosts (XDG), falls back to /tmp
 * - Windows: Prefers %APPDATA%\.ascii-chat\known_hosts, falls back to %TEMP%
 * - macOS: Follows Unix behavior with XDG support
 *
 * @return Pointer to cached known_hosts path (do not free), or NULL on total failure
 * @note The returned pointer is valid for the lifetime of the program
 * @note This function is thread-safe through internal caching
 * @note Path construction handles both Unix (/) and Windows (\) separators
 *
 * @ingroup crypto
 */
const char *get_known_hosts_path(void) {
  // Return cached path if already determined
  if (!g_known_hosts_path_cache) {
    // Get platform-specific base directories for path validation
    char *config_dir = get_config_dir();
    defer(SAFE_FREE(config_dir));
    char *home_dir = expand_path("~");
    defer(SAFE_FREE(home_dir));

    // Build list of allowed base directories for path validation
    const char *allowed_bases[6] = {0};
    size_t allowed_base_count = 0;

    // Add config directory to allowed bases if valid
    if (config_dir && path_is_absolute(config_dir)) {
      allowed_bases[allowed_base_count++] = config_dir;
    }
    // Add home directory to allowed bases if valid
    if (home_dir && path_is_absolute(home_dir)) {
      allowed_bases[allowed_base_count++] = home_dir;
    }

    // Determine platform-specific temporary directory
    const char *temp_base = NULL;
#ifdef _WIN32
    // Windows: Try %TEMP%, then %TMP%, fallback to system temp
    temp_base = platform_getenv("TEMP");
    if (!temp_base || !path_is_absolute(temp_base)) {
      temp_base = platform_getenv("TMP");
    }
    if (!temp_base || !path_is_absolute(temp_base)) {
      temp_base = "C:\\Windows\\Temp";
    }
#else
    // Unix/Linux/macOS: Use /tmp with ascii-chat subdirectory
    temp_base = "/tmp/.ascii-chat";
#endif
    if (temp_base && path_is_absolute(temp_base)) {
      allowed_bases[allowed_base_count++] = temp_base;
    }

    char candidate_buf[PLATFORM_MAX_PATH_LENGTH];

    // Strategy 1: Try configuration directory (highest priority)
    if (config_dir && !g_known_hosts_path_cache) {
      size_t config_len = strlen(config_dir);
      size_t total_len = config_len + strlen("known_hosts") + 1;
      if (total_len < sizeof(candidate_buf)) {
        safe_snprintf(candidate_buf, sizeof(candidate_buf), "%sknown_hosts", config_dir);
        (void)try_set_known_hosts_path(candidate_buf, allowed_bases, allowed_base_count);
      } else {
        log_warn("Config directory path too long when constructing known_hosts path");
      }
    }

    // Strategy 2: Try standard KNOWN_HOSTS_PATH define (e.g., ~/.ascii-chat/known_hosts)
    if (!g_known_hosts_path_cache) {
      char *expanded = expand_path(KNOWN_HOSTS_PATH);
      defer(SAFE_FREE(expanded));
      if (expanded) {
        (void)try_set_known_hosts_path(expanded, allowed_bases, allowed_base_count);
      }
    }

    // Strategy 3: Try home directory with .ascii-chat subdirectory
    if (!g_known_hosts_path_cache && home_dir && path_is_absolute(home_dir)) {
      size_t home_len = strlen(home_dir);
      char suffix[BUFFER_SIZE_SMALL];
      safe_snprintf(suffix, sizeof(suffix), ".ascii-chat%sknown_hosts", PATH_SEPARATOR_STR);
      bool needs_sep = (home_len > 0) && (home_dir[home_len - 1] != PATH_DELIM);
      size_t total_len = home_len + (needs_sep ? 1 : 0) + strlen(suffix) + 1;
      if (total_len < sizeof(candidate_buf)) {
        safe_snprintf(candidate_buf, sizeof(candidate_buf),
                      "%s%s%s", home_dir, needs_sep ? PATH_SEPARATOR_STR : "", suffix);
        (void)try_set_known_hosts_path(candidate_buf, allowed_bases, allowed_base_count);
      }
    }

    // Strategy 4: Try temporary directory as fallback
    if (!g_known_hosts_path_cache && temp_base && path_is_absolute(temp_base)) {
      size_t temp_len = strlen(temp_base);
      char suffix[BUFFER_SIZE_SMALL];
#ifdef _WIN32
      safe_snprintf(suffix, sizeof(suffix), "ascii-chat%sknown_hosts", PATH_SEPARATOR_STR);
#else
      safe_snprintf(suffix, sizeof(suffix), "known_hosts");
#endif
      bool needs_sep = (temp_len > 0) && (temp_base[temp_len - 1] != PATH_DELIM);
      size_t total_len = temp_len + (needs_sep ? 1 : 0) + strlen(suffix) + 1;
      if (total_len < sizeof(candidate_buf)) {
        safe_snprintf(candidate_buf, sizeof(candidate_buf),
                      "%s%s%s", temp_base, needs_sep ? PATH_SEPARATOR_STR : "", suffix);
        (void)try_set_known_hosts_path(candidate_buf, allowed_bases, allowed_base_count);
      }
    }

    // Strategy 5: Last resort - use hard-coded safe default
    if (!g_known_hosts_path_cache) {
#ifdef _WIN32
      const char *safe_default = "C:\\Windows\\Temp\\ascii-chat\\known_hosts";
#else
      const char *safe_default = "/tmp/.ascii-chat/known_hosts";
#endif
      if (!try_set_known_hosts_path(safe_default, allowed_bases, allowed_base_count)) {
        log_error("Fallback known_hosts path %s failed validation; known_hosts will not be available", safe_default);
        g_known_hosts_path_cache = NULL;
      }
    }

  }
  return g_known_hosts_path_cache;
}

// Format: IP:port x25519 <hex> [comment]
// IPv4 example: 192.0.2.1:8080 x25519 1234abcd... ascii-chat
// IPv6 example: [2001:db8::1]:8080 x25519 1234abcd... ascii-chat
asciichat_error_t check_known_host(const char *server_ip, uint16_t port, const uint8_t server_key[32]) {
  // Validate parameters first
  if (!server_ip || !server_key) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: server_ip=%p, server_key=%p", server_ip, server_key);
  }

  const char *path = get_known_hosts_path();
  int fd = platform_open(path, PLATFORM_O_RDONLY, FILE_PERM_PRIVATE);
  if (fd < 0) {
    // File doesn't exist - this is an unknown host that needs verification
    log_warn("Known hosts file does not exist: %s", path);
    return ASCIICHAT_OK; // Return 0 to indicate unknown host (first connection)
  }
  FILE *f = platform_fdopen(fd, "r");
  defer(if (f) fclose(f));
  if (!f) {
    // Failed to open file descriptor as FILE*
    platform_close(fd);
    log_warn("Failed to open known hosts file: %s", path);
    return ASCIICHAT_OK; // Return 0 to indicate unknown host (first connection)
  }

  char line[BUFFER_SIZE_XLARGE];
  char expected_prefix[BUFFER_SIZE_MEDIUM];

  // Format IP:port with proper bracket notation for IPv6
  char ip_with_port[BUFFER_SIZE_MEDIUM];
  if (format_ip_with_port(server_ip, port, ip_with_port, sizeof(ip_with_port)) != ASCIICHAT_OK) {

    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid IP format: %s", server_ip);
  }

  // Add space after IP:port for prefix matching
  safe_snprintf(expected_prefix, sizeof(expected_prefix), "%s ", ip_with_port);

  // Search through ALL matching entries to find one that matches the server key
  bool found_entries = false;
  while (fgets(line, sizeof(line), f)) {
    if (line[0] == '#')
      continue; // Comment

    if (strncmp(line, expected_prefix, strlen(expected_prefix)) == 0) {
      // Found matching IP:port - check if this entry matches the server key
      found_entries = true;
      size_t prefix_len = strlen(expected_prefix);
      size_t line_len = strlen(line);
      if (line_len < prefix_len) {
        // Line is too short to contain the prefix - skip this entry
        continue;
      }
      char *key_type = line + prefix_len;

      if (strncmp(key_type, NO_IDENTITY_MARKER, strlen(NO_IDENTITY_MARKER)) == 0) {
        // This is a "no-identity" entry, but server is presenting an identity key
        // This is a key mismatch - continue searching for a matching identity key
        log_debug("SECURITY_DEBUG: Found no-identity entry, but server has identity key - continuing search");
        continue;
      }

      // Parse key from line (normal identity key)
      // Format: x25519 <hex_key> <comment>
      // Extract just the hex key part
      char *hex_key_start = strchr(key_type, ' ');
      if (!hex_key_start) {
        log_debug("SECURITY_DEBUG: No space found in key type: %s", key_type);
        continue; // Try next entry
      }
      hex_key_start++; // Skip the space

      // Find the end of the hex key (next space or end of line)
      char *hex_key_end = strchr(hex_key_start, ' ');
      if (hex_key_end) {
        *hex_key_end = '\0'; // Null-terminate the hex key
      }

      public_key_t stored_key;
      if (parse_public_key(hex_key_start, &stored_key) != 0) {
        log_debug("SECURITY_DEBUG: Failed to parse key from hex: %s", hex_key_start);
        continue; // Try next entry
      }

      // DEBUG: Print both keys for comparison
      char server_key_hex[CRYPTO_HEX_KEY_SIZE_NULL], stored_key_hex[CRYPTO_HEX_KEY_SIZE_NULL];
      for (int i = 0; i < CRYPTO_KEY_SIZE; i++) {
        safe_snprintf(server_key_hex + i * 2, 3, "%02x", server_key[i]);
        safe_snprintf(stored_key_hex + i * 2, 3, "%02x", stored_key.key[i]);
      }
      server_key_hex[CRYPTO_HEX_KEY_SIZE] = '\0';
      stored_key_hex[CRYPTO_HEX_KEY_SIZE] = '\0';
      log_debug("SECURITY_DEBUG: Server key: %s", server_key_hex);
      log_debug("SECURITY_DEBUG: Stored key: %s", stored_key_hex);

      // Check if server key is all zeros (no-identity server)
      bool server_key_is_zero = true;
      for (int i = 0; i < ED25519_PUBLIC_KEY_SIZE; i++) {
        if (server_key[i] != 0) {
          server_key_is_zero = false;
          break;
        }
      }

      // Check if stored key is all zeros
      bool stored_key_is_zero = true;
      for (int i = 0; i < ED25519_PUBLIC_KEY_SIZE; i++) {
        if (stored_key.key[i] != 0) {
          stored_key_is_zero = false;
          break;
        }
      }

      // If both keys are zero, this is a secure no-identity connection
      // that was previously accepted by the user
      if (server_key_is_zero && stored_key_is_zero) {
        log_info("SECURITY: Zero key matches known_hosts - connection verified (no-identity server)");
        return 1; // Match found!
      }

      // Compare keys (constant-time to prevent timing attacks)
      if (sodium_memcmp(server_key, stored_key.key, ED25519_PUBLIC_KEY_SIZE) == 0) {
        log_info("SECURITY: Server key matches known_hosts - connection verified");
        return 1; // Match found!
      }
      log_debug("SECURITY_DEBUG: Key mismatch, continuing search...");
    }
  }

  // No matching key found - check if we found any entries at all
  if (found_entries) {
    // We found entries for this IP:port but none matched the server key
    // This is a key mismatch (potential MITM attack)
    log_error("SECURITY: Server key does NOT match any known_hosts entries!");
    log_error("SECURITY: This indicates a possible man-in-the-middle attack!");
    return ERROR_CRYPTO_VERIFICATION; // Key mismatch - MITM warning!
  }

  // No entries found for this IP:port - first connection
  return ASCIICHAT_OK; // Not found = first connection
}

// Check known_hosts for servers without identity key (no-identity entries)
// Returns: ASCIICHAT_OK = known host (no-identity entry found),
// ERROR_CRYPTO_VERIFICATION = unknown host, ERROR_CRYPTO = error,
// -1 = previously accepted known host (no-identity entry found)
asciichat_error_t check_known_host_no_identity(const char *server_ip, uint16_t port) {
  const char *path = get_known_hosts_path();
  int fd = platform_open(path, PLATFORM_O_RDONLY, FILE_PERM_PRIVATE);
  if (fd < 0) {
    // File doesn't exist - this is an unknown host that needs verification
    log_warn("Known hosts file does not exist: %s", path);
    return ASCIICHAT_OK; // Return 0 to indicate unknown host (first connection)
  }

  FILE *f = platform_fdopen(fd, "r");
  if (!f) {
    // Failed to open file descriptor as FILE*
    platform_close(fd);
    log_warn("Failed to open known hosts file: %s", path);
    return ASCIICHAT_OK; // Return 0 to indicate unknown host (first connection)
  }

  char line[BUFFER_SIZE_XLARGE];
  char expected_prefix[BUFFER_SIZE_MEDIUM];

  // Format IP:port with proper bracket notation for IPv6
  char ip_with_port[BUFFER_SIZE_MEDIUM];
  if (format_ip_with_port(server_ip, port, ip_with_port, sizeof(ip_with_port)) != ASCIICHAT_OK) {

    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid IP format: %s", server_ip);
  }

  // Add space after IP:port for prefix matching
  safe_snprintf(expected_prefix, sizeof(expected_prefix), "%s ", ip_with_port);

  while (fgets(line, sizeof(line), f)) {
    if (line[0] == '#')
      continue; // Comment

    if (strncmp(line, expected_prefix, strlen(expected_prefix)) == 0) {
      // Found matching IP:port
  

      // Check if this is a "no-identity" entry
      // Bounds check: ensure line is long enough to contain the prefix
      size_t prefix_len = strlen(expected_prefix);
      size_t line_len = strlen(line);
      if (line_len < prefix_len) {
        // Line is too short to contain the prefix - this shouldn't happen
        // but let's be safe and treat as unknown host
        return ASCIICHAT_OK;
      }
      char *key_type = line + prefix_len;
      // Skip leading whitespace
      while (*key_type == ' ' || *key_type == '\t') {
        key_type++;
      }
      if (strncmp(key_type, "no-identity", 11) == 0) {
        // This is a server without identity key that was previously accepted by the user
        // No warnings or user confirmation needed - user already accepted this server
        return -1; // Known host (no-identity entry) - secure connection
      }

      // If we found a normal identity key entry, this is a mismatch
      // Server previously had identity key but now has none
      log_warn("Server previously had identity key but now has none - potential security issue");
      return ERROR_CRYPTO_VERIFICATION; // Mismatch - server changed from identity to no-identity
    }
  }


  return ASCIICHAT_OK; // Not found = first connection
}

/**
 * @brief Recursively create directories (like mkdir -p)
 * @param path Full directory path to create
 * @return ASCIICHAT_OK on success, error code on failure
 */
static asciichat_error_t mkdir_recursive(const char *path) {
  if (!path || !*path) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid path for mkdir_recursive");
  }

  // Make a mutable copy of the path
  size_t len = strlen(path);
  char *tmp = SAFE_MALLOC(len + 1, char *);
  defer(SAFE_FREE(tmp));
  if (!tmp) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for path");
  }
  memcpy(tmp, path, len + 1);

  // Handle absolute paths (skip leading / or drive letter on Windows)
  char *p = tmp;
#ifdef _WIN32
  // Skip drive letter (e.g., "C:")
  if (len >= 2 && tmp[1] == ':') {
    p += 2;
  }
#endif
  // Skip leading separators (handle both / and \ on all platforms)
  while (*p == '/' || *p == '\\') {
    p++;
  }

  // Create directories one level at a time
  for (; *p; p++) {
    if (*p == '/' || *p == '\\') {
      *p = '\0'; // Temporarily truncate

      // Try to create this directory level
      int result = mkdir(tmp, DIR_PERM_PRIVATE);
      if (result != 0 && errno != EEXIST) {
        // mkdir failed - check if directory actually exists (Windows quirk)
        int test_fd = platform_open(tmp, PLATFORM_O_RDONLY, 0);
        if (test_fd < 0) {
          return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to create directory: %s", tmp);
        }
        platform_close(test_fd);
      }

      *p = PATH_DELIM; // Restore path separator (use platform-specific separator)
    }
  }

  // Create the final directory
      int result = mkdir(tmp, DIR_PERM_PRIVATE);
  if (result != 0 && errno != EEXIST) {
    int test_fd = platform_open(tmp, PLATFORM_O_RDONLY, 0);
    if (test_fd < 0) {
      return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to create directory: %s", tmp);
    }
    platform_close(test_fd);
  }

  return ASCIICHAT_OK;
}

asciichat_error_t add_known_host(const char *server_ip, uint16_t port, const uint8_t server_key[32]) {
  // Validate parameters first
  if (!server_ip || !server_key) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: server_ip=%p, server_key=%p", server_ip, server_key);
  }

  // Check for empty string (must be after NULL check)
  size_t ip_len = strlen(server_ip);
  if (ip_len == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Empty hostname/IP");
  }

  const char *path = get_known_hosts_path();
  if (!path || path[0] == '\0') {
    SET_ERRNO(ERROR_CONFIG, "Failed to get known hosts file path");
    return ERROR_CONFIG;
  }

  // Create parent directories recursively (like mkdir -p)
  size_t path_len = strlen(path);
  if (path_len == 0) {
    SET_ERRNO(ERROR_CONFIG, "Empty known hosts file path");
    return ERROR_CONFIG;
  }
  char *dir = SAFE_MALLOC(path_len + 1, char *);
  defer(SAFE_FREE(dir));
  if (!dir) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for directory path");
    return ERROR_MEMORY;
  }
  memcpy(dir, path, path_len + 1);

  // Find the last path separator
  char *last_sep = strrchr(dir, PATH_DELIM);

  if (last_sep) {
    *last_sep = '\0'; // Truncate to get directory path
    asciichat_error_t result = mkdir_recursive(dir);
    if (result != ASCIICHAT_OK) {
      return result; // Error already set by mkdir_recursive
    }
  }

  // Create the file if it doesn't exist, then append to it
  // Note: Temporarily removed log_debug to avoid potential crashes during debugging
  // log_debug("KNOWN_HOSTS: Attempting to create/open file: %s", path);
  // Use "a" mode for append-only (simpler and works better with chmod)
  FILE *f = platform_fopen(path, "a");
  defer(if (f) fclose(f));
  if (!f) {
    // log_debug("KNOWN_HOSTS: platform_fopen failed: %s (errno=%d)", SAFE_STRERROR(errno), errno);
    return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to create/open known hosts file: %s", path);
  }
  // Set secure permissions (0600) - only owner can read/write
  // Note: chmod may fail if file was just created by fopen, but that's okay
  (void)platform_chmod(path, FILE_PERM_PRIVATE);
  // log_debug("KNOWN_HOSTS: Successfully opened file: %s", path);

  // Format IP:port with proper bracket notation for IPv6
  char ip_with_port[BUFFER_SIZE_MEDIUM];
  if (format_ip_with_port(server_ip, port, ip_with_port, sizeof(ip_with_port)) != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid IP format: %s", server_ip);
  }

  // Convert key to hex for storage
  char hex[CRYPTO_HEX_KEY_SIZE_NULL] = {0}; // Initialize to zeros for safety
  bool is_placeholder = true;
  // Build hex string byte by byte to avoid buffer overflow issues
  for (int i = 0; i < ED25519_PUBLIC_KEY_SIZE; i++) {
    // Convert each byte to 2 hex digits directly
    uint8_t byte = server_key[i];
    hex[i * 2] = "0123456789abcdef"[byte >> 4];      // High nibble
    hex[i * 2 + 1] = "0123456789abcdef"[byte & 0xf]; // Low nibble
    if (byte != 0) {
      is_placeholder = false;
    }
  }
  hex[CRYPTO_HEX_KEY_SIZE] = '\0'; // Ensure null termination (64 hex digits + null terminator)

  // Write to file and check for errors
  int fprintf_result;
  if (is_placeholder) {
    // Server has no identity key - store as placeholder
    fprintf_result =
        safe_fprintf(f, "%s %s 0000000000000000000000000000000000000000000000000000000000000000 %s\n",
                     ip_with_port, NO_IDENTITY_MARKER, ASCII_CHAT_APP_NAME);
  } else {
    // Server has identity key - store normally
    fprintf_result = safe_fprintf(f, "%s %s %s %s\n", ip_with_port, X25519_KEY_TYPE, hex, ASCII_CHAT_APP_NAME);
  }

  // Check if fprintf failed
  if (fprintf_result < 0) {
    return SET_ERRNO_SYS(ERROR_CONFIG, "CRITICAL SECURITY ERROR: Failed to write to known_hosts file: %s", path);
  }

  // Flush to ensure data is written
  if (fflush(f) != 0) {
    return SET_ERRNO_SYS(ERROR_CONFIG, "CRITICAL SECURITY ERROR: Failed to flush known_hosts file: %s", path);
  }

  log_debug("KNOWN_HOSTS: Successfully added host to known_hosts file: %s", path);

  return ASCIICHAT_OK;
}

asciichat_error_t remove_known_host(const char *server_ip, uint16_t port) {
  // Validate parameters first
  if (!server_ip) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameter: server_ip=%p", server_ip);
  }

  const char *path = get_known_hosts_path();
  int fd = platform_open(path, PLATFORM_O_RDONLY, FILE_PERM_PRIVATE);
  if (fd < 0) {
    // File doesn't exist - nothing to remove, return success
    return ASCIICHAT_OK;
  }
  FILE *f = platform_fdopen(fd, "r");
  defer(if (f) fclose(f));
  if (!f) {
    platform_close(fd);
    SET_ERRNO_SYS(ERROR_CONFIG, "Failed to open known hosts file: %s", path);
    return ERROR_CONFIG;
  }

  // Format IP:port with proper bracket notation for IPv6
  char ip_with_port[BUFFER_SIZE_MEDIUM];
  if (format_ip_with_port(server_ip, port, ip_with_port, sizeof(ip_with_port)) != ASCIICHAT_OK) {

    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid IP format: %s", server_ip);
  }

  // Read all lines into memory
  char **lines = NULL;
  defer(SAFE_FREE(lines));
  size_t num_lines = 0;
  char line[BUFFER_SIZE_XLARGE];

  char expected_prefix[BUFFER_SIZE_MEDIUM];
  safe_snprintf(expected_prefix, sizeof(expected_prefix), "%s ", ip_with_port);

  while (fgets(line, sizeof(line), f)) {
    // Skip lines that match this IP:port
    if (strncmp(line, expected_prefix, strlen(expected_prefix)) != 0) {
      // Keep this line
      char **new_lines = SAFE_REALLOC((void *)lines, (num_lines + 1) * sizeof(char *), char **);
      if (new_lines) {
        lines = new_lines;
        lines[num_lines] = platform_strdup(line);
        num_lines++;
      }
    }
  }
  // Close first file before opening for write
  if (f) {
    fclose(f);
    f = NULL;  // Prevent double-close by defer
  }

  // Write back the filtered lines
  fd = platform_open(path, PLATFORM_O_WRONLY | PLATFORM_O_CREAT | PLATFORM_O_TRUNC, FILE_PERM_PRIVATE);
  f = platform_fdopen(fd, "w");
  if (!f) {
    // Cleanup on error - fdopen failed, so fd is still open but f is NULL
    for (size_t i = 0; i < num_lines; i++) {
      SAFE_FREE(lines[i]);
    }
    platform_close(fd); // Close fd directly since fdopen failed
    return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to open known hosts file: %s", path);
  }

  for (size_t i = 0; i < num_lines; i++) {
    (void)fputs(lines[i], f);
    SAFE_FREE(lines[i]);
  }
  // f will be closed by first defer() at function exit

  log_debug("KNOWN_HOSTS: Successfully removed host from known_hosts file: %s", path);
  return ASCIICHAT_OK;
}

// Compute SHA256 fingerprint of key for display
void compute_key_fingerprint(const uint8_t key[ED25519_PUBLIC_KEY_SIZE], char fingerprint[CRYPTO_HEX_KEY_SIZE_NULL]) {
  uint8_t hash[HMAC_SHA256_SIZE];
  crypto_hash_sha256(hash, key, ED25519_PUBLIC_KEY_SIZE);

  // Build hex string byte by byte to avoid buffer overflow issues
  for (int i = 0; i < HMAC_SHA256_SIZE; i++) {
    uint8_t byte = hash[i];
    fingerprint[i * 2] = "0123456789abcdef"[byte >> 4];      // High nibble
    fingerprint[i * 2 + 1] = "0123456789abcdef"[byte & 0xf]; // Low nibble
  }
  fingerprint[CRYPTO_HEX_KEY_SIZE] = '\0';
}

// Interactive prompt for unknown host - returns true if user wants to add, false to abort
bool prompt_unknown_host(const char *server_ip, uint16_t port, const uint8_t server_key[32]) {
  char fingerprint[CRYPTO_HEX_KEY_SIZE_NULL];
  compute_key_fingerprint(server_key, fingerprint);

  // Format IP:port with proper bracket notation for IPv6
  char ip_with_port[BUFFER_SIZE_MEDIUM];
  if (format_ip_with_port(server_ip, port, ip_with_port, sizeof(ip_with_port)) != ASCIICHAT_OK) {
    // Fallback to basic format if error
    safe_snprintf(ip_with_port, sizeof(ip_with_port), "%s:%u", server_ip, port);
  }

  // Check if we're running interactively (stdin is a terminal and not in snapshot mode)
  log_debug("SECURITY_DEBUG: Checking environment bypass variable");
  const char *env_skip_known_hosts_checking = platform_getenv("ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK");
  log_debug("SECURITY_DEBUG: env_skip_known_hosts_checking=%s",
            env_skip_known_hosts_checking ? env_skip_known_hosts_checking : "NULL");
  if (env_skip_known_hosts_checking && strcmp(env_skip_known_hosts_checking, STR_ONE) == 0) {
    log_warn("Skipping known_hosts checking. This is a security vulnerability.");
    return true;
  }
  log_debug("SECURITY_DEBUG: Environment bypass not set, continuing with normal flow");

  if (!platform_isatty(STDIN_FILENO) || opt_snapshot_mode) {
    // SECURITY: Non-interactive mode - REJECT unknown hosts to prevent MITM attacks
    SET_ERRNO(ERROR_CRYPTO, "SECURITY: Cannot verify unknown host in non-interactive mode");
    log_error("ERROR: Cannot verify unknown host in non-interactive mode without environment variable bypass.\n"
              "This connection may be a man-in-the-middle attack!\n"
              "\n"
              "To connect to this host:\n"
              "  1. Run the client interactively (from a terminal with TTY)\n"
              "  2. Verify the fingerprint: SHA256:%s\n"
              "  3. Accept the host when prompted\n"
              "  4. The host will be added to: %s\n"
              "\n"
              "Connection aborted for security.\n"
              "To bypass this check, set the environment variable ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK to 1",
              fingerprint, get_known_hosts_path());
    return false; // REJECT unknown hosts in non-interactive mode
  }

  // Interactive mode - prompt user
  char message[BUFFER_SIZE_LARGE]; // Accommodates full message + IPv6 address + fingerprint
  safe_snprintf(message, sizeof(message),
                "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n"
                "@    WARNING: REMOTE HOST IDENTIFICATION NOT KNOWN!      @\n"
                "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n"
                "\n"
                "The authenticity of host '%s' can't be established.\n"
                "Ed25519 key fingerprint is SHA256:%s\n"
                "\n"
                "Are you sure you want to continue connecting (yes/no)? ",
                ip_with_port, fingerprint);
  safe_fprintf(stderr,"%s", message);
  log_file("%s", message);

  char response[10];
  if (fgets(response, sizeof(response), stdin) == NULL) {
    SET_ERRNO(ERROR_CRYPTO, "Failed to read user response from stdin");
    return false;
  }

  // Accept "yes" or "y" (case insensitive)
  if (strncasecmp(response, "yes", 3) == 0 || strncasecmp(response, "y", 1) == 0) {
    log_warn("Warning: Permanently added '%s' to the list of known hosts.", ip_with_port);
    return true;
  }

  log_warn("Connection aborted by user.");
  return false;
}

// Display MITM warning with key comparison and prompt user for confirmation
// Returns true if user accepts the risk and wants to continue, false otherwise
bool display_mitm_warning(const char *server_ip, uint16_t port, const uint8_t expected_key[32],
                          const uint8_t received_key[32]) {
  char expected_fp[CRYPTO_HEX_KEY_SIZE_NULL], received_fp[CRYPTO_HEX_KEY_SIZE_NULL];
  compute_key_fingerprint(expected_key, expected_fp);
  compute_key_fingerprint(received_key, received_fp);

  const char *known_hosts_path = get_known_hosts_path();

  // Format IP:port with proper bracket notation for IPv6
  char ip_with_port[BUFFER_SIZE_MEDIUM];
  if (format_ip_with_port(server_ip, port, ip_with_port, sizeof(ip_with_port)) != ASCIICHAT_OK) {
    // Fallback to basic format if error
    safe_snprintf(ip_with_port, sizeof(ip_with_port), "%s:%u", server_ip, port);
  }

  char escaped_ip_with_port[128];
  escape_ascii(ip_with_port, "[]", escaped_ip_with_port, 128);
  log_warn("\n"
           "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n"
           "@    WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!     @\n"
           "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n"
           "\n"
           "IT IS POSSIBLE THAT SOMEONE IS DOING SOMETHING NASTY!\n"
           "Someone could be eavesdropping on you right now (man-in-the-middle attack)!\n"
           "It is also possible that the host key has just been changed.\n"
           "\n"
           "The fingerprint for the Ed25519 key sent by the remote host is:\n"
           "SHA256:%s\n"
           "\n"
           "Expected fingerprint:\n"
           "SHA256:%s\n"
           "\n"
           "Please contact your system administrator.\n"
           "\n"
           "Add correct host key in %s to get rid of this message.\n"
           "Offending key for IP address %s was found at:\n"
           "%s\n"
           "\n"
           "To update the key, run:\n"
           "  # Linux/macOS:\n"
           "    sed -i '' '/%s /d' ~/.ascii-chat/known_hosts\n"
           "    # or run this instead:\n"
           "    cat ~/.ascii-chat/known_hosts | grep -v '%s ' > /tmp/x; cp /tmp/x ~/.ascii-chat/known_hosts\n"
           "  # Windows PowerShell:\n"
           "    (Get-Content ~/.ascii-chat/known_hosts) | Where-Object { $_ -notmatch '^%s ' } | Set-Content "
           "~/.ascii-chat/known_hosts\n"
           "  # Or manually edit ~/.ascii-chat/known_hosts to remove lines starting with '%s '\n"
           "\n"
           "Host key verification failed.\n"
           "\n",
           received_fp, expected_fp, known_hosts_path, ip_with_port, known_hosts_path, ip_with_port,
           escaped_ip_with_port, ip_with_port, ip_with_port);

  return false;
}

// Interactive prompt for unknown host without identity key
// Returns true if user wants to continue, false to abort
bool prompt_unknown_host_no_identity(const char *server_ip, uint16_t port) {
  // Format IP:port with proper bracket notation for IPv6
  char ip_with_port[BUFFER_SIZE_MEDIUM];
  if (format_ip_with_port(server_ip, port, ip_with_port, sizeof(ip_with_port)) != ASCIICHAT_OK) {
    // Fallback to basic format if error
    safe_snprintf(ip_with_port, sizeof(ip_with_port), "%s:%u", server_ip, port);
  }

  log_warn("\n"
           "The authenticity of host '%s' can't be established.\n"
           "The server has no identity key to verify its authenticity.\n"
           "\n"
           "WARNING: This connection is vulnerable to man-in-the-middle attacks!\n"
           "Anyone can intercept your connection and read your data.\n"
           "\n"
           "To secure this connection:\n"
           "  1. Server should use --key to provide an identity key\n"
           "  2. Client should use --server-key to verify the server\n"
           "\n",
           ip_with_port);

  // Check if we're running interactively (stdin is a terminal and not in snapshot mode)
  if (!platform_isatty(STDIN_FILENO) || opt_snapshot_mode) {
    // SECURITY: Non-interactive mode - REJECT unknown hosts without identity
    SET_ERRNO(ERROR_CRYPTO, "SECURITY: Cannot verify server without identity key in non-interactive mode");
    log_error("ERROR: Cannot verify server without identity key in non-interactive mode.\n"
              "ERROR: This connection is vulnerable to man-in-the-middle attacks!\n"
              "\n"
              "To connect to this host:\n"
              "  1. Run the client interactively (from a terminal with TTY)\n"
              "  2. Verify you trust this server despite no identity key\n"
              "  3. Accept the risk when prompted\n"
              "  OR better: Ask server admin to use --key for proper authentication\n"
              "\n"
              "Connection aborted for security.\n"
              "\n");
    return false; // REJECT unknown hosts without identity in non-interactive mode
  }

  // Interactive mode - prompt user
  safe_fprintf(stderr, "Are you sure you want to continue connecting (yes/no)? ");
  (void)fflush(stderr);

  char response[10];
  if (fgets(response, sizeof(response), stdin) == NULL) {
    SET_ERRNO(ERROR_CRYPTO, "Failed to read user response from stdin (no identity host)");
    return false;
  }

  // Accept "yes" or "y" (case insensitive)
  if (strncasecmp(response, "yes", 3) == 0 || strncasecmp(response, "y", 1) == 0) {
    log_warn("Warning: Proceeding with unverified connection.\n"
             "Your data may be intercepted by attackers!\n"
             "\n");
    return true;
  }

  safe_fprintf(stderr, "Connection aborted by user.\n");
  return false;
}

// Cleanup function to free the cached known_hosts path
void known_hosts_cleanup(void) {
  if (g_known_hosts_path_cache) {
    SAFE_FREE(g_known_hosts_path_cache);
    g_known_hosts_path_cache = NULL;
  }
}
