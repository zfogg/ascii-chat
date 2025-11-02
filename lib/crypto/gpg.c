#include "gpg.h"
#include "keys/keys.h"
#include "common.h"
#include "util/string.h"

#include <ctype.h>
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_LIBGCRYPT
#include <gcrypt.h>
#endif

#ifdef _WIN32
#include <windows.h>
#define SAFE_POPEN _popen
#define SAFE_PCLOSE _pclose
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#define SAFE_POPEN popen
#define SAFE_PCLOSE pclose
#endif

// Maximum response size from gpg-agent
#define GPG_AGENT_MAX_RESPONSE 8192

/**
 * Get gpg-agent socket path (Unix) or named pipe path (Windows)
 */
static int get_agent_socket_path(char *path_out, size_t path_size) {
#ifdef _WIN32
  // On Windows, GPG4Win uses a named pipe
  // Try gpgconf first to get the correct path
  FILE *fp = SAFE_POPEN("gpgconf --list-dirs agent-socket 2>nul", "r");
  if (fp) {
    if (fgets(path_out, path_size, fp)) {
      // Remove trailing newline
      size_t len = strlen(path_out);
      if (len > 0 && path_out[len - 1] == '\n') {
        path_out[len - 1] = '\0';
      }
      SAFE_PCLOSE(fp);
      return 0;
    }
    SAFE_PCLOSE(fp);
  }

  // Fallback to default GPG4Win location
  const char *appdata = SAFE_GETENV("APPDATA");
  if (appdata) {
    safe_snprintf(path_out, path_size, "%s\\gnupg\\S.gpg-agent", appdata);
  } else {
    log_error("Could not determine APPDATA directory");
    return -1;
  }
#else
  // Try gpgconf first
  FILE *fp = SAFE_POPEN("gpgconf --list-dirs agent-socket 2>/dev/null", "r");
  if (fp) {
    if (fgets(path_out, path_size, fp)) {
      // Remove trailing newline
      size_t len = strlen(path_out);
      if (len > 0 && path_out[len - 1] == '\n') {
        path_out[len - 1] = '\0';
      }
      SAFE_PCLOSE(fp);
      return 0;
    }
    SAFE_PCLOSE(fp);
  }

  // Fallback to default location
  const char *gnupg_home = SAFE_GETENV("GNUPGHOME");
  if (gnupg_home) {
    safe_snprintf(path_out, path_size, "%s/S.gpg-agent", gnupg_home);
  } else {
    const char *home = SAFE_GETENV("HOME");
    if (!home) {
      log_error("Could not determine home directory");
      return -1;
    }
    safe_snprintf(path_out, path_size, "%s/.gnupg/S.gpg-agent", home);
  }
#endif

  return 0;
}

/**
 * Read a line from gpg-agent (Assuan protocol)
 * Returns: 0 on success, -1 on error
 */
#ifdef _WIN32
static int read_agent_line(HANDLE pipe, char *buf, size_t buf_size) {
  size_t pos = 0;
  while (pos < buf_size - 1) {
    char c;
    DWORD bytes_read;
    if (!ReadFile(pipe, &c, 1, &bytes_read, NULL) || bytes_read != 1) {
      if (GetLastError() == ERROR_BROKEN_PIPE) {
        log_error("GPG agent connection closed");
      } else {
        log_error("Error reading from GPG agent: %lu", GetLastError());
      }
      return -1;
    }

    if (c == '\n') {
      buf[pos] = '\0';
      return 0;
    }

    buf[pos++] = c;
  }

  log_error("GPG agent response too long");
  return -1;
}
#else
static int read_agent_line(int sock, char *buf, size_t buf_size) {
  size_t pos = 0;
  while (pos < buf_size - 1) {
    char c;
    ssize_t n = recv(sock, &c, 1, 0);
    if (n <= 0) {
      if (n == 0) {
        log_error("GPG agent connection closed");
      } else {
        log_error("Error reading from GPG agent: %s", SAFE_STRERROR(errno));
      }
      return -1;
    }

    if (c == '\n') {
      buf[pos] = '\0';
      return 0;
    }

    buf[pos++] = c;
  }

  log_error("GPG agent response too long");
  return -1;
}
#endif

/**
 * Send a command to gpg-agent
 */
#ifdef _WIN32
static int send_agent_command(HANDLE pipe, const char *command) {
  size_t len = strlen(command);
  char *cmd_with_newline;
  cmd_with_newline = SAFE_MALLOC(len + 2, char *);
  if (!cmd_with_newline) {
    log_error("Failed to allocate memory for command");
    return -1;
  }

  memcpy(cmd_with_newline, command, len);
  cmd_with_newline[len] = '\n';
  cmd_with_newline[len + 1] = '\0';

  DWORD bytes_written;
  BOOL result = WriteFile(pipe, cmd_with_newline, (DWORD)(len + 1), &bytes_written, NULL);
  SAFE_FREE(cmd_with_newline);

  if (!result || bytes_written != (len + 1)) {
    log_error("Failed to send command to GPG agent: %lu", GetLastError());
    return -1;
  }

  return 0;
}
#else
static int send_agent_command(int sock, const char *command) {
  size_t len = strlen(command);
  char *cmd_with_newline;
  cmd_with_newline = SAFE_MALLOC(len + 2, char *);
  if (!cmd_with_newline) {
    log_error("Failed to allocate memory for command");
    return -1;
  }

  memcpy(cmd_with_newline, command, len);
  cmd_with_newline[len] = '\n';
  cmd_with_newline[len + 1] = '\0';

  ssize_t sent = send(sock, cmd_with_newline, len + 1, 0);
  SAFE_FREE(cmd_with_newline);

  if (sent != (ssize_t)(len + 1)) {
    log_error("Failed to send command to GPG agent");
    return -1;
  }

  return 0;
}
#endif

/**
 * Check if response is OK
 */
static bool is_ok_response(const char *line) {
  return strncmp(line, "OK", 2) == 0;
}

#ifdef _WIN32
// On Windows, we return HANDLE cast to int (handle values are always even on Windows)
int gpg_agent_connect(void) {
  char pipe_path[PLATFORM_MAX_PATH_LENGTH];
  if (get_agent_socket_path(pipe_path, sizeof(pipe_path)) != 0) {
    log_error("Failed to get GPG agent pipe path");
    return -1;
  }

  log_debug("Connecting to GPG agent at: %s", pipe_path);

  // Wait for pipe to be available (gpg-agent may take time to start)
  if (!WaitNamedPipeA(pipe_path, 5000)) {
    log_error("GPG agent pipe not available: %lu", GetLastError());
    return -1;
  }

  HANDLE pipe = CreateFileA(pipe_path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

  if (pipe == INVALID_HANDLE_VALUE) {
    log_error("Failed to connect to GPG agent pipe: %lu", GetLastError());
    return -1;
  }

  // Set pipe to message mode
  DWORD mode = PIPE_READMODE_BYTE;
  if (!SetNamedPipeHandleState(pipe, &mode, NULL, NULL)) {
    log_error("Failed to set pipe mode: %lu", GetLastError());
    CloseHandle(pipe);
    return -1;
  }

  // Read initial greeting
  char response[GPG_AGENT_MAX_RESPONSE];
  if (read_agent_line(pipe, response, sizeof(response)) != 0) {
    log_error("Failed to read GPG agent greeting");
    CloseHandle(pipe);
    return -1;
  }

  if (!is_ok_response(response)) {
    log_error("Unexpected GPG agent greeting: %s", response);
    CloseHandle(pipe);
    return -1;
  }

  log_debug("Connected to GPG agent successfully");
  return (int)(intptr_t)pipe;
}
#else
int gpg_agent_connect(void) {
  char socket_path[PLATFORM_MAX_PATH_LENGTH];
  if (get_agent_socket_path(socket_path, sizeof(socket_path)) != 0) {
    log_error("Failed to get GPG agent socket path");
    return -1;
  }

  log_debug("Connecting to GPG agent at: %s", socket_path);

  int sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0) {
    log_error("Failed to create socket: %s", SAFE_STRERROR(errno));
    return -1;
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  SAFE_STRNCPY(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    log_error("Failed to connect to GPG agent: %s", SAFE_STRERROR(errno));
    close(sock);
    return -1;
  }

  // Read initial greeting
  char response[GPG_AGENT_MAX_RESPONSE];
  if (read_agent_line(sock, response, sizeof(response)) != 0) {
    log_error("Failed to read GPG agent greeting");
    close(sock);
    return -1;
  }

  if (!is_ok_response(response)) {
    log_error("Unexpected GPG agent greeting: %s", response);
    close(sock);
    return -1;
  }

  log_debug("Connected to GPG agent successfully");

  // Set loopback pinentry mode to avoid interactive prompts
  // This allows GPG agent to work in non-interactive environments
  if (send_agent_command(sock, "OPTION pinentry-mode=loopback") != 0) {
    log_warn("Failed to set loopback pinentry mode (continuing anyway)");
  } else {
    // Read response for OPTION command
    if (read_agent_line(sock, response, sizeof(response)) != 0) {
      log_warn("Failed to read OPTION command response (continuing anyway)");
    } else if (is_ok_response(response)) {
      log_debug("Loopback pinentry mode enabled");
    } else {
      log_warn("Failed to enable loopback pinentry mode: %s (continuing anyway)", response);
    }
  }

  return sock;
}
#endif

#ifdef _WIN32
void gpg_agent_disconnect(int handle_as_int) {
  if (handle_as_int >= 0) {
    HANDLE pipe = (HANDLE)(intptr_t)handle_as_int;
    send_agent_command(pipe, "BYE");
    CloseHandle(pipe);
  }
}
#else
void gpg_agent_disconnect(int sock) {
  if (sock >= 0) {
    send_agent_command(sock, "BYE");
    close(sock);
  }
}
#endif

int gpg_agent_sign(int handle_as_int, const char *keygrip, const uint8_t *message, size_t message_len,
                   uint8_t *signature_out, size_t *signature_len_out) {
  if (handle_as_int < 0 || !keygrip || !message || !signature_out || !signature_len_out) {
    log_error("Invalid arguments to gpg_agent_sign");
    return -1;
  }

#ifdef _WIN32
  HANDLE handle = (HANDLE)(intptr_t)handle_as_int;
#else
  int handle = handle_as_int;
#endif

  char response[GPG_AGENT_MAX_RESPONSE];

  // 1. Set the key to use (SIGKEY command)
  char sigkey_cmd[128];
  safe_snprintf(sigkey_cmd, sizeof(sigkey_cmd), "SIGKEY %s", keygrip);
  if (send_agent_command(handle, sigkey_cmd) != 0) {
    log_error("Failed to send SIGKEY command");
    return -1;
  }

  if (read_agent_line(handle, response, sizeof(response)) != 0) {
    log_error("Failed to read SIGKEY response");
    return -1;
  }

  if (!is_ok_response(response)) {
    log_error("SIGKEY failed: %s", response);
    return -1;
  }

  // 2. Set the data to sign using INQUIRE protocol
  // IMPORTANT: For Ed25519 (EdDSA), use SETHASH --inquire!
  // This tells GPG agent to send "INQUIRE TBSDATA" asking for the raw message.
  // GPG agent then wraps it as: (data(flags eddsa)(hash-algo sha512)(value <message>))
  // This produces a standard RFC 8032 compatible Ed25519 signature.

  // Send SETHASH --inquire to initiate the INQUIRE protocol
  log_debug("Sending SETHASH --inquire for Ed25519 signing");

  if (send_agent_command(handle, "SETHASH --inquire") != 0) {
    log_error("Failed to send SETHASH --inquire command");
    return -1;
  }

  // Read response - may get status lines (S INQUIRE_MAXLEN) before INQUIRE
  // Keep reading until we get the INQUIRE line
  bool got_inquire = false;
  for (int i = 0; i < 5; i++) { // Max 5 lines to avoid infinite loop
    if (read_agent_line(handle, response, sizeof(response)) != 0) {
      log_error("Failed to read SETHASH --inquire response");
      return -1;
    }

    // Skip status lines (start with "S ")
    if (response[0] == 'S' && response[1] == ' ') {
      log_debug("Skipping status line: %s", response);
      continue;
    }

    // Check for INQUIRE response
    if (strncmp(response, "INQUIRE TBSDATA", 15) == 0) {
      got_inquire = true;
      break;
    }

    log_error("Expected 'INQUIRE TBSDATA', got: %s", response);
    return -1;
  }

  if (!got_inquire) {
    log_error("Did not receive INQUIRE TBSDATA after multiple lines");
    return -1;
  }

  // Convert message to hex string for sending
  char *hex_message;
  hex_message = SAFE_MALLOC(message_len * 2 + 1, char *);
  if (!hex_message) {
    log_error("Failed to allocate hex message buffer");
    return -1;
  }

  for (size_t i = 0; i < message_len; i++) {
    sprintf(hex_message + i * 2, "%02X", message[i]);
  }
  hex_message[message_len * 2] = '\0';

  // Send the data using D command
  char data_cmd[GPG_AGENT_MAX_RESPONSE];
  safe_snprintf(data_cmd, sizeof(data_cmd), "D %s", hex_message);

  log_debug("Sending %zu-byte message in response to INQUIRE", message_len);

  SAFE_FREE(hex_message);

  if (send_agent_command(handle, data_cmd) != 0) {
    log_error("Failed to send D command with message data");
    return -1;
  }

  // Send END to complete the INQUIRE
  if (send_agent_command(handle, "END") != 0) {
    log_error("Failed to send END command");
    return -1;
  }

  // Read OK response
  if (read_agent_line(handle, response, sizeof(response)) != 0) {
    log_error("Failed to read INQUIRE completion response");
    return -1;
  }

  if (!is_ok_response(response)) {
    log_error("INQUIRE completion failed: %s", response);
    return -1;
  }

  // 3. Request signature
  if (send_agent_command(handle, "PKSIGN") != 0) {
    log_error("Failed to send PKSIGN command");
    return -1;
  }

  // Read response (could be D line with signature data, then OK)
  if (read_agent_line(handle, response, sizeof(response)) != 0) {
    log_error("Failed to read PKSIGN response");
    return -1;
  }

  // Check if it's a data line (D followed by space and hex data)
  if (response[0] != 'D' || response[1] != ' ') {
    log_error("Expected D line from PKSIGN, got: %s", response);
    return -1;
  }

  // Parse S-expression signature from GPG agent
  // GPG agent returns: D <percent-encoded-sexp>
  // Example: D (7:sig-val(5:eddsa(1:r32:%<hex>)(1:s32:%<hex>)))
  // The signature is 64 bytes total: R (32) + S (32)

  // DEBUG: Print first 200 chars of response to see format
  char debug_buf[201];
  size_t response_len = strlen(response);
  size_t debug_len = response_len < 200 ? response_len : 200;
  memcpy(debug_buf, response, debug_len);
  debug_buf[debug_len] = '\0';
  log_debug("GPG agent D line (first 200 bytes): %s", debug_buf);

  const char *data = response + 2; // Skip "D "

  // The response format from GPG agent for EdDSA is percent-encoded
  // We need to decode it to get the raw binary signature
  // For now, let's try the simple approach: find the raw data

  // Look for the pattern that indicates where R starts: "(1:r32:"
  const char *r_marker = strstr(data, "(1:r32:");
  if (!r_marker) {
    log_error("Could not find r value marker in S-expression");
    return -1;
  }

  // Skip the marker to get to the actual R data
  const char *r_data = r_marker + 7; // strlen("(1:r32:")

  // Look for the pattern that indicates where S starts: "(1:s32:"
  const char *s_marker = strstr(r_data + 32, "(1:s32:");
  if (!s_marker) {
    log_error("Could not find s value marker in S-expression");
    return -1;
  }

  // Skip the marker to get to the actual S data
  const char *s_data = s_marker + 7; // strlen("(1:s32:")

  // Copy the raw binary data
  memcpy(signature_out, r_data, 32);
  memcpy(signature_out + 32, s_data, 32);

  *signature_len_out = 64;

  // DEBUG: Print signature in hex
  char sig_hex[129];
  for (int i = 0; i < 64; i++) {
    safe_snprintf(sig_hex + i * 2, 3, "%02x", (unsigned char)signature_out[i]);
  }
  sig_hex[128] = '\0';
  log_debug("Extracted signature (64 bytes): %s", sig_hex);

  // Read final OK
  if (read_agent_line(handle, response, sizeof(response)) != 0) {
    log_error("Failed to read final PKSIGN response");
    return -1;
  }

  if (!is_ok_response(response)) {
    log_error("PKSIGN final response not OK: %s", response);
    return -1;
  }

  log_debug("Successfully signed message with GPG agent");
  return 0;
}

int gpg_get_public_key(const char *key_id, uint8_t *public_key_out, char *keygrip_out) {
  if (!key_id || !public_key_out) {
    log_error("Invalid arguments to gpg_get_public_key");
    return -1;
  }

  // SECURITY: Validate key_id to prevent command injection
  // GPG key IDs should be hexadecimal (0-9, a-f, A-F)
  if (!validate_shell_safe(key_id, NULL)) {
    log_error("Invalid GPG key ID format - contains unsafe characters: %s", key_id);
    return -1;
  }

  // Additional validation: ensure key_id is hex alphanumeric
  for (size_t i = 0; key_id[i] != '\0'; i++) {
    if (!isxdigit((unsigned char)key_id[i])) {
      log_error("Invalid GPG key ID format - must be hexadecimal: %s", key_id);
      return -1;
    }
  }

  // Escape key_id for safe use in shell command (single quotes)
  char escaped_key_id[512];
  if (!escape_shell_single_quotes(key_id, escaped_key_id, sizeof(escaped_key_id))) {
    log_error("Failed to escape GPG key ID for shell command");
    return -1;
  }

  // Use gpg to list the key and get the keygrip
  char cmd[1024];
#ifdef _WIN32
  safe_snprintf(cmd, sizeof(cmd), "gpg --list-keys --with-keygrip --with-colons 0x%s 2>nul", escaped_key_id);
#else
  safe_snprintf(cmd, sizeof(cmd), "gpg --list-keys --with-keygrip --with-colons 0x%s 2>/dev/null", escaped_key_id);
#endif
  FILE *fp = SAFE_POPEN(cmd, "r");
  if (!fp) {
    log_error("Failed to run gpg command - GPG may not be installed");
#ifdef _WIN32
    log_error("To install GPG on Windows, download Gpg4win from:");
    log_error("  https://www.gpg4win.org/download.html");
#elif defined(__APPLE__)
    log_error("To install GPG on macOS, use Homebrew:");
    log_error("  brew install gnupg");
#else
    log_error("To install GPG on Linux:");
    log_error("  Debian/Ubuntu: sudo apt-get install gnupg");
    log_error("  Fedora/RHEL:   sudo dnf install gnupg2");
    log_error("  Arch Linux:    sudo pacman -S gnupg");
    log_error("  Alpine Linux:  sudo apk add gnupg");
#endif
    return -1;
  }

  char line[2048];
  char found_keygrip[128] = {0};
  bool found_key = false;

  // Parse gpg output
  // Format: pub:..., grp:::::::::<keygrip>:
  while (fgets(line, sizeof(line), fp)) {
    if (strncmp(line, "pub:", 4) == 0) {
      // Found the public key line
      found_key = true;
    } else if (found_key && strncmp(line, "grp:", 4) == 0) {
      // Extract keygrip
      // Format: grp:::::::::D52FF935FBA59609EE65E1685287828242A1EA1A:
      // (8 empty fields, then keygrip, then final colon)
      const char *grp_start = line + 4;
      int colon_count = 0;
      while (*grp_start && colon_count < 8) {
        if (*grp_start == ':') {
          colon_count++;
        }
        grp_start++;
      }

      if (colon_count == 8) {
        const char *grp_end = strchr(grp_start, ':');
        if (grp_end) {
          size_t grp_len = grp_end - grp_start;
          if (grp_len < sizeof(found_keygrip)) {
            memcpy(found_keygrip, grp_start, grp_len);
            found_keygrip[grp_len] = '\0';

            if (keygrip_out) {
              SAFE_STRNCPY(keygrip_out, found_keygrip, 41);
            }
          }
        }
      }
      break;
    }
  }

  SAFE_PCLOSE(fp);

  if (!found_key || strlen(found_keygrip) == 0) {
    log_error("Could not find GPG key with ID: %s", key_id);
    return -1;
  }

  log_debug("Found keygrip for key %s: %s", key_id, found_keygrip);

  // Export the public key in ASCII armor format and parse it with existing parse_gpg_key()
  // Use escaped_key_id (already validated and escaped above)
#ifdef _WIN32
  safe_snprintf(cmd, sizeof(cmd), "gpg --export --armor 0x%s 2>nul", escaped_key_id);
#else
  safe_snprintf(cmd, sizeof(cmd), "gpg --export --armor 0x%s 2>/dev/null", escaped_key_id);
#endif
  fp = SAFE_POPEN(cmd, "r");
  if (!fp) {
    log_error("Failed to export GPG public key - GPG may not be installed");
#ifdef _WIN32
    log_error("To install GPG on Windows, download Gpg4win from:");
    log_error("  https://www.gpg4win.org/download.html");
#elif defined(__APPLE__)
    log_error("To install GPG on macOS, use Homebrew:");
    log_error("  brew install gnupg");
#else
    log_error("To install GPG on Linux:");
    log_error("  Debian/Ubuntu: sudo apt-get install gnupg");
    log_error("  Fedora/RHEL:   sudo dnf install gnupg2");
    log_error("  Arch Linux:    sudo pacman -S gnupg");
    log_error("  Alpine Linux:  sudo apk add gnupg");
#endif
    return -1;
  }

  // Read the exported key (PGP ASCII armor)
  char exported_key[8192] = {0};
  size_t offset = 0;
  char line_buf[512];
  while (fgets(line_buf, sizeof(line_buf), fp)) {
    size_t line_len = strlen(line_buf);
    if (offset + line_len < sizeof(exported_key) - 1) {
      memcpy(exported_key + offset, line_buf, line_len);
      offset += line_len;
    }
  }
  SAFE_PCLOSE(fp);

  if (offset == 0) {
    log_error("Failed to read exported GPG key");
    return -1;
  }

  // Parse using the existing parse_gpg_key() function
  public_key_t temp_key;
  if (parse_gpg_key(exported_key, &temp_key) != 0) {
    log_error("Failed to parse GPG key from export");
    return -1;
  }

  // Extract the public key
  memcpy(public_key_out, temp_key.key, 32);
  log_info("Extracted Ed25519 public key from GPG keyring using parse_gpg_key()");
  return 0;
}

bool gpg_agent_is_available(void) {
  int sock = gpg_agent_connect();
  if (sock < 0) {
    return false;
  }
  gpg_agent_disconnect(sock);
  return true;
}

int gpg_verify_signature(const uint8_t *public_key, const uint8_t *message, size_t message_len,
                         const uint8_t *signature) {
#ifdef HAVE_LIBGCRYPT
  gcry_error_t err;
  gcry_sexp_t s_pubkey = NULL;
  gcry_sexp_t s_sig = NULL;
  gcry_sexp_t s_data = NULL;

  // Initialize libgcrypt if not already done
  if (!gcry_control(GCRYCTL_INITIALIZATION_FINISHED_P)) {
    gcry_check_version(NULL);
    gcry_control(GCRYCTL_DISABLE_SECMEM, 0);
    gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
  }

  // Build public key S-expression: (public-key (ecc (curve Ed25519) (flags eddsa) (q %b)))
  // CRITICAL: Must include (flags eddsa) to match libgcrypt's Ed25519 test suite!
  // See libgcrypt/tests/t-ed25519.c line 246-251
  err = gcry_sexp_build(&s_pubkey, NULL, "(public-key (ecc (curve Ed25519) (flags eddsa) (q %b)))", 32, public_key);
  if (err) {
    log_error("gpg_verify_signature: Failed to build public key S-expression: %s", gcry_strerror(err));
    return -1;
  }

  // Build signature S-expression: (sig-val (eddsa (r %b) (s %b)))
  // Signature is 64 bytes: first 32 bytes are R, last 32 bytes are S
  err = gcry_sexp_build(&s_sig, NULL, "(sig-val (eddsa (r %b) (s %b)))", 32, signature, 32, signature + 32);
  if (err) {
    log_error("gpg_verify_signature: Failed to build signature S-expression: %s", gcry_strerror(err));
    gcry_sexp_release(s_pubkey);
    return -1;
  }

  // Build data S-expression with raw message
  // CRITICAL: According to libgcrypt's test suite (t-ed25519.c line 273),
  // Ed25519 data should be: (data (value %b)) with NO FLAGS!
  // The (flags eddsa) belongs in the KEY S-expression above, NOT in the data.
  // GPG agent's internal format is different - this is the correct libgcrypt API usage.
  err = gcry_sexp_build(&s_data, NULL, "(data (value %b))", message_len, message);
  if (err) {
    log_error("gpg_verify_signature: Failed to build data S-expression: %s", gcry_strerror(err));
    gcry_sexp_release(s_pubkey);
    gcry_sexp_release(s_sig);
    return -1;
  }

  // Debug logging
  char pubkey_hex[65];
  char r_hex[65];
  char s_hex[65];
  char msg_hex[128];

  for (int i = 0; i < 32; i++) {
    sprintf(pubkey_hex + i * 2, "%02x", public_key[i]);
    sprintf(r_hex + i * 2, "%02x", signature[i]);
    sprintf(s_hex + i * 2, "%02x", signature[32 + i]);
  }
  for (size_t i = 0; i < (message_len < 32 ? message_len : 32); i++) {
    sprintf(msg_hex + i * 2, "%02x", message[i]);
  }

  log_debug("gpg_verify_signature: pubkey=%s", pubkey_hex);
  log_debug("gpg_verify_signature: R=%s", r_hex);
  log_debug("gpg_verify_signature: S=%s", s_hex);
  log_debug("gpg_verify_signature: msg=%s (len=%zu)", msg_hex, message_len);

  // Verify the signature
  err = gcry_pk_verify(s_sig, s_data, s_pubkey);

  // Clean up S-expressions
  gcry_sexp_release(s_pubkey);
  gcry_sexp_release(s_sig);
  gcry_sexp_release(s_data);

  if (err) {
    log_debug("gpg_verify_signature: Signature verification failed: %s", gcry_strerror(err));
    return -1;
  }

  log_debug("gpg_verify_signature: Signature verified successfully");
  return 0;
#else
  // Explicitly mark parameters as unused when libgcrypt is not available
  (void)public_key;
  (void)message;
  (void)message_len;
  (void)signature;
  log_error("gpg_verify_signature: libgcrypt not available");
  return -1;
#endif
}
