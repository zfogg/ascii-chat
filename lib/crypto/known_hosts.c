#include "known_hosts.h"
#include "common.h"
#include "asciichat_errno.h" // For asciichat_errno system
#include "keys/keys.h"
#include "ip.h"
#include "platform/internal.h"
#include "platform/system.h" // For platform_isatty()
#include "options.h"         // For opt_snapshot_mode
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
#include <sys/stat.h>
#include <strings.h>
#include <unistd.h> // For STDIN_FILENO on POSIX
#endif

#ifdef _WIN32
#define KNOWN_HOSTS_PATH "~\\.ascii-chat\\known_hosts"
#else
#define KNOWN_HOSTS_PATH "~/.ascii-chat/known_hosts"
#endif

static char *expand_path(const char *path) {
  if (path[0] == '~') {
    const char *home = platform_getenv("HOME");
    if (!home) {
      // On Windows, try USERPROFILE
      home = platform_getenv("USERPROFILE");
      if (!home)
        return NULL;
    }

    char *expanded;
    size_t total_len = strlen(home) + strlen(path) + 1;
    expanded = SAFE_MALLOC(total_len, char *);
    if (!expanded) {
      return NULL;
    }
    safe_snprintf(expanded, total_len, "%s%s", home, path + 1);
    return expanded;
  }
  return platform_strdup(path);
}

const char *get_known_hosts_path(void) {
  static char *path = NULL;
  if (!path) {
    path = expand_path(KNOWN_HOSTS_PATH);
  }
  return path;
}

// Format: IP:port x25519 <hex> [comment]
// IPv4 example: 192.0.2.1:8080 x25519 1234abcd... ascii-chat-server
// IPv6 example: [2001:db8::1]:8080 x25519 1234abcd... ascii-chat-server
asciichat_error_t check_known_host(const char *server_ip, uint16_t port, const uint8_t server_key[32]) {
  const char *path = get_known_hosts_path();
  int fd = platform_open(path, PLATFORM_O_RDONLY, 0600);
  FILE *f = platform_fdopen(fd, "r");
  if (!f) {
    // File doesn't exist - this is an unknown host that needs verification
    log_warn("Known hosts file does not exist: %s", path);
    return ASCIICHAT_OK; // Return 0 to indicate unknown host (first connection)
  }

  char line[2048];
  char expected_prefix[512];

  // Format IP:port with proper bracket notation for IPv6
  char ip_with_port[512];
  if (format_ip_with_port(server_ip, port, ip_with_port, sizeof(ip_with_port)) != 0) {
    (void)fclose(f); // fclose() also closes the underlying fd
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

      if (strncmp(key_type, "no-identity", 11) == 0) {
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
      char server_key_hex[65], stored_key_hex[65];
      for (int i = 0; i < 32; i++) {
        safe_snprintf(server_key_hex + i * 2, 3, "%02x", server_key[i]);
        safe_snprintf(stored_key_hex + i * 2, 3, "%02x", stored_key.key[i]);
      }
      server_key_hex[64] = '\0';
      stored_key_hex[64] = '\0';
      log_debug("SECURITY_DEBUG: Server key: %s", server_key_hex);
      log_debug("SECURITY_DEBUG: Stored key: %s", stored_key_hex);

      // Compare keys (constant-time to prevent timing attacks)
      if (sodium_memcmp(server_key, stored_key.key, 32) == 0) {
        (void)fclose(f); // fclose() also closes the underlying fd
        log_info("SECURITY: Server key matches known_hosts - connection verified");
        return 1; // Match found!
      }
      log_debug("SECURITY_DEBUG: Key mismatch, continuing search...");
    }
  }

  (void)fclose(f); // fclose() also closes the underlying fd

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
// Returns: 1 = known host (no-identity entry found), 0 = unknown host, -1 = error
asciichat_error_t check_known_host_no_identity(const char *server_ip, uint16_t port) {
  const char *path = get_known_hosts_path();
  int fd = platform_open(path, PLATFORM_O_RDONLY, 0600);

  FILE *f = NULL;
  if (fd >= 0) {
    f = platform_fdopen(fd, "r");
  }

  if (!f) {
    // File doesn't exist - this is an unknown host that needs verification
    log_warn("Known hosts file does not exist: %s", path);
    return ASCIICHAT_OK; // Return 0 to indicate unknown host (first connection)
  }

  char line[2048];
  char expected_prefix[512];

  // Format IP:port with proper bracket notation for IPv6
  char ip_with_port[512];
  if (format_ip_with_port(server_ip, port, ip_with_port, sizeof(ip_with_port)) != 0) {
    (void)fclose(f); // fclose() also closes the underlying fd
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid IP format: %s", server_ip);
  }

  // Add space after IP:port for prefix matching
  safe_snprintf(expected_prefix, sizeof(expected_prefix), "%s ", ip_with_port);

  while (fgets(line, sizeof(line), f)) {
    if (line[0] == '#')
      continue; // Comment

    if (strncmp(line, expected_prefix, strlen(expected_prefix)) == 0) {
      // Found matching IP:port
      (void)fclose(f); // fclose() also closes the underlying fd

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
      if (strncmp(key_type, "no-identity", 11) == 0) {
        // This is a server without identity key
        log_debug("Found no-identity entry for server");

        // SECURITY: Even for "known" servers without identity keys, we should
        // require user confirmation due to the security implications
        log_warn("SECURITY WARNING: Connecting to server without identity key verification");
        log_warn("SECURITY WARNING: This connection is vulnerable to man-in-the-middle attacks");
        log_warn("SECURITY WARNING: Anyone can intercept your connection and read your data");
        log_warn("SECURITY WARNING: Consider asking the server administrator to use --key for proper authentication");

        // SECURITY FIX: Always require user confirmation for no-identity servers
        // This prevents silent MITM attacks even for "known" servers
        if (!prompt_unknown_host_no_identity(server_ip, port)) {
          return ERROR_CRYPTO; // User declined - abort connection
        }

        return 1; // Known host (no-identity entry) - user confirmed
      }

      // If we found a normal identity key entry, this is a mismatch
      // Server previously had identity key but now has none
      log_warn("Server previously had identity key but now has none - potential security issue");
      return ERROR_CRYPTO_VERIFICATION; // Mismatch - server changed from identity to no-identity
    }
  }

  (void)fclose(f);     // fclose() also closes the underlying fd
  return ASCIICHAT_OK; // Not found = first connection
}

asciichat_error_t add_known_host(const char *server_ip, uint16_t port, const uint8_t server_key[32]) {
  const char *path = get_known_hosts_path();
  if (!path) {
    SET_ERRNO(ERROR_CONFIG, "Failed to get known hosts file path");
    return ERROR_CONFIG;
  }

  // Create directory if needed - handle both Windows and Unix paths
  char *dir = platform_strdup(path);
  if (!dir) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for directory path");
    return ERROR_MEMORY;
  }

  // Find the last path separator (handle both / and \)
  char *last_slash = strrchr(dir, '/');
  char *last_backslash = strrchr(dir, '\\');
  char *last_sep = (last_slash > last_backslash) ? last_slash : last_backslash;

  if (last_sep) {
    *last_sep = '\0';
    log_debug("Creating directory: %s", dir);
    int mkdir_result = mkdir(dir, 0700);
    if (mkdir_result != 0 && errno != EEXIST) {
      // mkdir failed and it's not because the directory already exists
      // Verify if directory actually exists despite the error (Windows compatibility)
      int test_fd = platform_open(dir, PLATFORM_O_RDONLY, 0);
      if (test_fd < 0) {
        // Directory doesn't exist and we couldn't create it
        SAFE_FREE(dir);
        return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to create directory: %s", dir);
      }
      // Directory exists despite error, close the test fd
      platform_close(test_fd);
      log_debug("Directory already exists: %s", dir);
    } else if (mkdir_result == 0) {
      log_debug("Directory created successfully: %s", dir);
    } else {
      // errno == EEXIST - directory already exists
      log_debug("Directory already exists: %s", dir);
    }
  }
  SAFE_FREE(dir);

  // Create the file if it doesn't exist, then append to it
  log_debug("KNOWN_HOSTS: Attempting to create/open file: %s", path);
  int fd = platform_open(path, PLATFORM_O_CREAT | PLATFORM_O_WRONLY | PLATFORM_O_APPEND, 0600);
  if (fd < 0) {
    return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to create/open known hosts file: %s", path);
  }
  log_debug("KNOWN_HOSTS: Successfully opened file: %s (fd=%d)", path, fd);

  FILE *f = platform_fdopen(fd, "a");
  if (!f) {
    platform_close(fd);
    return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to open known hosts file for writing: %s", path);
  }

  // Format IP:port with proper bracket notation for IPv6
  char ip_with_port[512];
  if (format_ip_with_port(server_ip, port, ip_with_port, sizeof(ip_with_port)) != 0) {
    (void)fclose(f); // fclose() also closes the underlying fd
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid IP format: %s", server_ip);
  }

  // Convert key to hex for storage
  char hex[65];
  bool is_placeholder = true;
  for (int i = 0; i < 32; i++) {
    safe_snprintf(hex + i * 2, 3, "%02x", server_key[i]);
    if (server_key[i] != 0) {
      is_placeholder = false;
    }
  }

  // Write to file and check for errors
  int fprintf_result;
  if (is_placeholder) {
    // Server has no identity key - store as placeholder
    fprintf_result = safe_fprintf(
        f, "%s no-identity 0000000000000000000000000000000000000000000000000000000000000000 ascii-chat-server\n",
        ip_with_port);
  } else {
    // Server has identity key - store normally
    fprintf_result = safe_fprintf(f, "%s x25519 %s ascii-chat-server\n", ip_with_port, hex);
  }

  // Check if fprintf failed
  if (fprintf_result < 0) {
    (void)fclose(f); // fclose() also closes the underlying fd
    return SET_ERRNO_SYS(ERROR_CONFIG, "CRITICAL SECURITY ERROR: Failed to write to known_hosts file: %s", path);
  }

  // Flush to ensure data is written
  if (fflush(f) != 0) {
    (void)fclose(f); // fclose() also closes the underlying fd
    return SET_ERRNO_SYS(ERROR_CONFIG, "CRITICAL SECURITY ERROR: Failed to flush known_hosts file: %s", path);
  }

  (void)fclose(f); // fclose() also closes the underlying fd
  log_debug("KNOWN_HOSTS: Successfully added host to known_hosts file: %s", path);

  return ASCIICHAT_OK;
}

asciichat_error_t remove_known_host(const char *server_ip, uint16_t port) {
  const char *path = get_known_hosts_path();
  int fd = platform_open(path, PLATFORM_O_RDONLY, 0600);
  FILE *f = platform_fdopen(fd, "r");
  if (!f) {
    SET_ERRNO_SYS(ERROR_CONFIG, "Failed to open known hosts file: %s", path);
    return ERROR_CONFIG;
  }

  // Format IP:port with proper bracket notation for IPv6
  char ip_with_port[512];
  if (format_ip_with_port(server_ip, port, ip_with_port, sizeof(ip_with_port)) != 0) {
    (void)fclose(f); // fclose() also closes the underlying fd
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid IP format: %s", server_ip);
  }

  // Read all lines into memory
  char **lines = NULL;
  size_t num_lines = 0;
  char line[2048];

  char expected_prefix[512];
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
  (void)fclose(f); // fclose() also closes the underlying fd

  // Write back the filtered lines
  fd = platform_open(path, PLATFORM_O_WRONLY, 0600);
  f = platform_fdopen(fd, "w");
  if (!f) {
    // Cleanup on error - fdopen failed, so fd is still open but f is NULL
    for (size_t i = 0; i < num_lines; i++) {
      SAFE_FREE(lines[i]);
    }
    SAFE_FREE(lines);
    platform_close(fd); // Close fd directly since fdopen failed
    return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to open known hosts file: %s", path);
  }

  for (size_t i = 0; i < num_lines; i++) {
    (void)fputs(lines[i], f);
    SAFE_FREE(lines[i]);
  }
  SAFE_FREE(lines);
  (void)fclose(f); // fclose() also closes the underlying fd

  log_debug("KNOWN_HOSTS: Successfully removed host from known_hosts file: %s", path);
  return ASCIICHAT_OK;
}

// Compute SHA256 fingerprint of key for display
void compute_key_fingerprint(const uint8_t key[32], char fingerprint[65]) {
  uint8_t hash[32];
  crypto_hash_sha256(hash, key, 32);

  for (int i = 0; i < 32; i++) {
    safe_snprintf(fingerprint + i * 2, 3, "%02x", hash[i]);
  }
  fingerprint[64] = '\0';
}

// Interactive prompt for unknown host - returns true if user wants to add, false to abort
bool prompt_unknown_host(const char *server_ip, uint16_t port, const uint8_t server_key[32]) {
  char fingerprint[65];
  compute_key_fingerprint(server_key, fingerprint);

  // Format IP:port with proper bracket notation for IPv6
  char ip_with_port[512];
  if (format_ip_with_port(server_ip, port, ip_with_port, sizeof(ip_with_port)) != 0) {
    // Fallback to basic format if error
    safe_snprintf(ip_with_port, sizeof(ip_with_port), "%s:%u", server_ip, port);
  }

  safe_fprintf(stderr, "\n");
  safe_fprintf(stderr, "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
  safe_fprintf(stderr, "@    WARNING: REMOTE HOST IDENTIFICATION NOT KNOWN!      @\n");
  safe_fprintf(stderr, "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
  safe_fprintf(stderr, "\n");
  safe_fprintf(stderr, "The authenticity of host '%s' can't be established.\n", ip_with_port);
  safe_fprintf(stderr, "Ed25519 key fingerprint is SHA256:%s\n", fingerprint);
  safe_fprintf(stderr, "\n");

  // Check if we're running interactively (stdin is a terminal and not in snapshot mode)
  if (!platform_isatty(STDIN_FILENO) || opt_snapshot_mode) {
    // SECURITY: Non-interactive mode - REJECT unknown hosts to prevent MITM attacks
    SET_ERRNO(ERROR_CRYPTO, "SECURITY: Cannot verify unknown host in non-interactive mode");
    log_error("SECURITY: Unknown host '%s' rejected in non-interactive mode", ip_with_port);
    log_error("SECURITY: This prevents man-in-the-middle attacks when running without a TTY");
    safe_fprintf(stderr, "ERROR: Cannot verify unknown host in non-interactive mode.\n");
    safe_fprintf(stderr, "ERROR: This connection may be a man-in-the-middle attack!\n");
    safe_fprintf(stderr, "\n");
    safe_fprintf(stderr, "To connect to this host:\n");
    safe_fprintf(stderr, "  1. Run the client interactively (from a terminal with TTY)\n");
    safe_fprintf(stderr, "  2. Verify the fingerprint: SHA256:%s\n", fingerprint);
    safe_fprintf(stderr, "  3. Accept the host when prompted\n");
    safe_fprintf(stderr, "  4. The host will be added to: %s\n", get_known_hosts_path());
    safe_fprintf(stderr, "\n");
    safe_fprintf(stderr, "Connection aborted for security.\n");
    safe_fprintf(stderr, "\n");
    return false; // REJECT unknown hosts in non-interactive mode
  }

  // Interactive mode - prompt user
  safe_fprintf(stderr, "Are you sure you want to continue connecting (yes/no)? ");
  (void)fflush(stderr);

  char response[10];
  if (fgets(response, sizeof(response), stdin) == NULL) {
    SET_ERRNO(ERROR_CRYPTO, "Failed to read user response from stdin");
    return false;
  }

  // Accept "yes" or "y" (case insensitive)
  if (strncasecmp(response, "yes", 3) == 0 || strncasecmp(response, "y", 1) == 0) {
    safe_fprintf(stderr, "Warning: Permanently added '%s' to the list of known hosts.\n", ip_with_port);
    safe_fprintf(stderr, "\n");
    return true;
  }

  safe_fprintf(stderr, "Connection aborted by user.\n");
  return false;
}

// Display MITM warning with key comparison and prompt user for confirmation
// Returns true if user accepts the risk and wants to continue, false otherwise
bool display_mitm_warning(const char *server_ip, uint16_t port, const uint8_t expected_key[32],
                          const uint8_t received_key[32]) {
  char expected_fp[65], received_fp[65];
  compute_key_fingerprint(expected_key, expected_fp);
  compute_key_fingerprint(received_key, received_fp);

  const char *known_hosts_path = get_known_hosts_path();

  // Format IP:port with proper bracket notation for IPv6
  char ip_with_port[512];
  if (format_ip_with_port(server_ip, port, ip_with_port, sizeof(ip_with_port)) != 0) {
    // Fallback to basic format if error
    safe_snprintf(ip_with_port, sizeof(ip_with_port), "%s:%u", server_ip, port);
  }

  safe_fprintf(stderr, "\n");
  safe_fprintf(stderr, "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
  safe_fprintf(stderr, "@    WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!     @\n");
  safe_fprintf(stderr, "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
  safe_fprintf(stderr, "\n");
  safe_fprintf(stderr, "IT IS POSSIBLE THAT SOMEONE IS DOING SOMETHING NASTY!\n");
  safe_fprintf(stderr, "Someone could be eavesdropping on you right now (man-in-the-middle attack)!\n");
  safe_fprintf(stderr, "It is also possible that the host key has just been changed.\n");
  safe_fprintf(stderr, "\n");
  safe_fprintf(stderr, "The fingerprint for the Ed25519 key sent by the remote host is:\n");
  safe_fprintf(stderr, "SHA256:%s\n", received_fp);
  safe_fprintf(stderr, "\n");
  safe_fprintf(stderr, "Expected fingerprint:\n");
  safe_fprintf(stderr, "SHA256:%s\n", expected_fp);
  safe_fprintf(stderr, "\n");
  safe_fprintf(stderr, "Please contact your system administrator.\n");
  safe_fprintf(stderr, "\n");
  safe_fprintf(stderr, "Add correct host key in %s to get rid of this message.\n", known_hosts_path);
  safe_fprintf(stderr, "Offending key for IP address %s was found at:\n", ip_with_port);
  safe_fprintf(stderr, "%s\n", known_hosts_path);
  safe_fprintf(stderr, "\n");
  safe_fprintf(stderr, "To update the key, run:\n");
  safe_fprintf(stderr, "  # Windows PowerShell:\n");
  safe_fprintf(stderr, "  (Get-Content '%s') | Where-Object { $_ -notmatch '^%s ' } | Set-Content '%s'\n",
               known_hosts_path, ip_with_port, known_hosts_path);
  safe_fprintf(stderr, "  # Unix/Linux (grep approach - most reliable):\n");
  safe_fprintf(stderr, "  grep -v '^%s ' %s > %s.tmp && mv %s.tmp %s\n", ip_with_port, known_hosts_path,
               known_hosts_path, known_hosts_path, known_hosts_path);
  safe_fprintf(stderr, "  # Alternative sed (may not work on all systems):\n");
  safe_fprintf(stderr, "  sed -i '' '/%s /d' %s\n", ip_with_port, known_hosts_path);
  safe_fprintf(stderr, "  # Or manually edit %s to remove lines starting with '%s '\n", known_hosts_path, ip_with_port);
  safe_fprintf(stderr, "\n");
  safe_fprintf(stderr, "Host key verification failed.\n");
  safe_fprintf(stderr, "\n");

  return false;
}

// Interactive prompt for unknown host without identity key
// Returns true if user wants to continue, false to abort
bool prompt_unknown_host_no_identity(const char *server_ip, uint16_t port) {
  // Format IP:port with proper bracket notation for IPv6
  char ip_with_port[512];
  if (format_ip_with_port(server_ip, port, ip_with_port, sizeof(ip_with_port)) != 0) {
    // Fallback to basic format if error
    safe_snprintf(ip_with_port, sizeof(ip_with_port), "%s:%u", server_ip, port);
  }

  safe_fprintf(stderr, "\n");
  safe_fprintf(stderr, "The authenticity of host '%s' can't be established.\n", ip_with_port);
  safe_fprintf(stderr, "The server has no identity key to verify its authenticity.\n");
  safe_fprintf(stderr, "\n");
  safe_fprintf(stderr, "WARNING: This connection is vulnerable to man-in-the-middle attacks!\n");
  safe_fprintf(stderr, "Anyone can intercept your connection and read your data.\n");
  safe_fprintf(stderr, "\n");
  safe_fprintf(stderr, "To secure this connection:\n");
  safe_fprintf(stderr, "  1. Server should use --key to provide an identity key\n");
  safe_fprintf(stderr, "  2. Client should use --server-key to verify the server\n");
  safe_fprintf(stderr, "\n");

  // Check if we're running interactively (stdin is a terminal and not in snapshot mode)
  if (!platform_isatty(STDIN_FILENO) || opt_snapshot_mode) {
    // SECURITY: Non-interactive mode - REJECT unknown hosts without identity
    SET_ERRNO(ERROR_CRYPTO, "SECURITY: Cannot verify server without identity key in non-interactive mode");
    log_error("SECURITY: Server has no identity key - rejected in non-interactive mode");
    log_error("SECURITY: This prevents man-in-the-middle attacks when running without a TTY");
    safe_fprintf(stderr, "ERROR: Cannot verify server without identity key in non-interactive mode.\n");
    safe_fprintf(stderr, "ERROR: This connection is vulnerable to man-in-the-middle attacks!\n");
    safe_fprintf(stderr, "\n");
    safe_fprintf(stderr, "To connect to this host:\n");
    safe_fprintf(stderr, "  1. Run the client interactively (from a terminal with TTY)\n");
    safe_fprintf(stderr, "  2. Verify you trust this server despite no identity key\n");
    safe_fprintf(stderr, "  3. Accept the risk when prompted\n");
    safe_fprintf(stderr, "  OR better: Ask server admin to use --key for proper authentication\n");
    safe_fprintf(stderr, "\n");
    safe_fprintf(stderr, "Connection aborted for security.\n");
    safe_fprintf(stderr, "\n");
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
    safe_fprintf(stderr, "Warning: Proceeding with unverified connection.\n");
    safe_fprintf(stderr, "Your data may be intercepted by attackers!\n");
    safe_fprintf(stderr, "\n");
    return true;
  }

  safe_fprintf(stderr, "Connection aborted by user.\n");
  return false;
}
