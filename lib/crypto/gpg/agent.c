/**
 * @file crypto/gpg/agent.c
 * @ingroup crypto
 * @brief GPG agent connection and communication implementation
 */

#include "agent.h"
#include "../keys.h"
#include "common.h"
#include "util/string.h"
#include "log/logging.h"
#include "platform/system.h"

#include <ctype.h>
#include <errno.h>
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

  // 2. For EdDSA/Ed25519, GPG agent requires SETHASH with a hash algorithm
  // GPG agent doesn't support --inquire for SETHASH - the command syntax is:
  //   SETHASH (--hash=<name>)|(<algonumber>) <hexstring>
  // For Ed25519, we hash the message with SHA512 (algo 10) first

  // Hash the message with SHA512 using libsodium
  uint8_t hash[crypto_hash_sha512_BYTES];
  crypto_hash_sha512(hash, message, message_len);

  // Build SETHASH command with SHA512 hash (algo 10)
  // Format: "SETHASH 10 <128 hex chars for 64-byte SHA512 hash>"
  char sethash_cmd[256];
  int offset = safe_snprintf(sethash_cmd, sizeof(sethash_cmd), "SETHASH 10 ");
  for (size_t i = 0; i < crypto_hash_sha512_BYTES; i++) {
    offset += safe_snprintf(sethash_cmd + offset, sizeof(sethash_cmd) - (size_t)offset, "%02X", hash[i]);
  }

  log_debug("Sending SETHASH command with SHA512 hash");
  if (send_agent_command(handle, sethash_cmd) != 0) {
    log_error("Failed to send SETHASH command");
    return -1;
  }

  // Read SETHASH response
  if (read_agent_line(handle, response, sizeof(response)) != 0) {
    log_error("Failed to read SETHASH response");
    return -1;
  }

  if (!is_ok_response(response)) {
    log_error("SETHASH failed: %s", response);
    return -1;
  }

  // 3. Request signature using PKSIGN
  if (send_agent_command(handle, "PKSIGN") != 0) {
    log_error("Failed to send PKSIGN command");
    return -1;
  }

  // Read response - skip status/error lines and wait for data line (D ...)
  // GPG agent sends informational ERR lines that are not fatal (e.g., "Not implemented")
  // Keep reading until we get the actual signature data
  bool found_data = false;
  for (int attempts = 0; attempts < 20; attempts++) {
    if (read_agent_line(handle, response, sizeof(response)) != 0) {
      log_error("Failed to read PKSIGN response");
      return -1;
    }

    log_debug("PKSIGN response line %d: %s", attempts + 1, response);

    // Skip status lines (S INQUIRE_MAXLEN, etc)
    if (response[0] == 'S' && response[1] == ' ') {
      log_debug("Skipping PKSIGN status line: %s", response);
      continue;
    }

    // Skip informational ERR lines (GPG agent sends these even on success)
    // Common ERR codes: 67109141 (IPC cancelled), 67108933 (Not implemented)
    if (strncmp(response, "ERR", 3) == 0) {
      log_debug("Skipping PKSIGN error line (informational): %s", response);
      continue;
    }

    // Check if it's a data line (D followed by space)
    if (response[0] == 'D' && response[1] == ' ') {
      log_debug("Found signature data line");
      found_data = true;
      break;
    }

    // Check for OK (success without data would be unexpected)
    if (strncmp(response, "OK", 2) == 0) {
      log_warn("PKSIGN returned OK without data line");
      continue; // Keep trying in case D line follows
    }

    // Check if GPG agent is sending another INQUIRE (shouldn't happen)
    if (strncmp(response, "INQUIRE", 7) == 0) {
      log_error("Unexpected INQUIRE after PKSIGN: %s", response);
      return -1;
    }

    // Unknown response type
    log_warn("Unexpected PKSIGN response (attempt %d): %s", attempts + 1, response);
  }

  if (!found_data) {
    log_error("Expected D line from PKSIGN after %d attempts", 20);
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

bool gpg_agent_is_available(void) {
  int sock = gpg_agent_connect();
  if (sock < 0) {
    return false;
  }
  gpg_agent_disconnect(sock);
  return true;
}
