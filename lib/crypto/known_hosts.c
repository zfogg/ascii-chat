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
#include "common.h"
#include "asciichat_errno.h" // For asciichat_errno system
#include "keys/keys.h"
#include "util/ip.h"
#include "platform/internal.h"
#include "platform/system.h" // For platform_isatty()
#include "options.h"         // For opt_snapshot_mode
#include "util/path.h"
#include "util/string.h"

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

      // Check if server key is all zeros (no-identity server)
      bool server_key_is_zero = true;
      for (int i = 0; i < 32; i++) {
        if (server_key[i] != 0) {
          server_key_is_zero = false;
          break;
        }
      }

      // Check if stored key is all zeros
      bool stored_key_is_zero = true;
      for (int i = 0; i < 32; i++) {
        if (stored_key.key[i] != 0) {
          stored_key_is_zero = false;
          break;
        }
      }

      // If both keys are zero, this is a secure no-identity connection
      // that was previously accepted by the user
      if (server_key_is_zero && stored_key_is_zero) {
        (void)fclose(f); // fclose() also closes the underlying fd
        log_info("SECURITY: Zero key matches known_hosts - connection verified (no-identity server)");
        return 1; // Match found!
      }

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
// Returns: ASCIICHAT_OK = known host (no-identity entry found),
// ERROR_CRYPTO_VERIFICATION = unknown host, ERROR_CRYPTO = error,
// -1 = previously accepted known host (no-identity entry found)
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
  if (format_ip_with_port(server_ip, port, ip_with_port, sizeof(ip_with_port)) != ASCIICHAT_OK) {
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

  // Check if we're running interactively (stdin is a terminal and not in snapshot mode)
  const char *env_skip_known_hosts_checking = platform_getenv("ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK");
  if (env_skip_known_hosts_checking && strcmp(env_skip_known_hosts_checking, "1") == 0) {
    log_warn("Skipping known_hosts checking. This is a security vulnerability.");
    return true;
  }

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
  char message[512];
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
  safe_fprintf(stderr, message);
  log_file(message);

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
  char ip_with_port[512];
  if (format_ip_with_port(server_ip, port, ip_with_port, sizeof(ip_with_port)) != 0) {
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
