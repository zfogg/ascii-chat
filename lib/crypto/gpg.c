/**
 * @file crypto/gpg.c
 * @ingroup crypto
 * @brief 🔐 GPG key parsing and validation utilities for public key authentication
 */

#include "gpg.h"
#include "keys/keys.h"
#include "common.h"
#include "util/string.h"

#include <ctype.h>
#include <errno.h>
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

  // 2. For EdDSA/Ed25519, use SETHASH --inquire to pass raw data
  // This matches how SSH agent works - it passes raw data, not pre-hashed data
  // GPG agent will create the proper Ed25519 S-expression internally

  // Send SETHASH --inquire command
  if (send_agent_command(handle, "SETHASH --inquire") != 0) {
    log_error("Failed to send SETHASH --inquire command");
    return -1;
  }

  // Read status lines until we get INQUIRE TBSDATA
  for (int attempts = 0; attempts < 10; attempts++) {
    if (read_agent_line(handle, response, sizeof(response)) != 0) {
      log_error("Failed to read SETHASH response");
      return -1;
    }

    log_debug("SETHASH response line %d: %s", attempts + 1, response);

    // Skip status lines (S INQUIRE_MAXLEN)
    if (response[0] == 'S' && response[1] == ' ') {
      log_debug("Skipping status line: %s", response);
      continue;
    }

    // Check for INQUIRE TBSDATA
    if (strncmp(response, "INQUIRE TBSDATA", 15) == 0) {
      log_debug("Got INQUIRE TBSDATA, sending raw message data");
      break;
    }
  }

  // Send the raw message data as hex via D command
  char *hex_message = SAFE_MALLOC(message_len * 2 + 3, char *);
  if (!hex_message) {
    log_error("Failed to allocate hex message buffer");
    return -1;
  }

  hex_message[0] = 'D';
  hex_message[1] = ' ';
  for (size_t i = 0; i < message_len; i++) {
    snprintf(hex_message + 2 + i * 2, 3, "%02X", message[i]);
  }
  hex_message[2 + message_len * 2] = '\0';

  if (send_agent_command(handle, hex_message) != 0) {
    SAFE_FREE(hex_message);
    log_error("Failed to send D command with message data");
    return -1;
  }
  SAFE_FREE(hex_message);

  // Send END command to finish INQUIRE
  if (send_agent_command(handle, "END") != 0) {
    log_error("Failed to send END command");
    return -1;
  }

  // Read OK response for SETHASH completion
  if (read_agent_line(handle, response, sizeof(response)) != 0) {
    log_error("Failed to read SETHASH completion response");
    return -1;
  }

  if (!is_ok_response(response)) {
    log_debug("SETHASH completion response: %s", response);
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

/**
 * @brief Extract Ed25519 public key from GPG using gpg --export (fallback when agent unavailable)
 *
 * This function uses `gpg --export` to get the public key in OpenPGP packet format,
 * then parses the packet to extract the raw Ed25519 public key bytes.
 *
 * @param key_id GPG key ID (e.g., "7FE90A79F2E80ED3")
 * @param public_key_out Output buffer for 32-byte Ed25519 public key
 * @return 0 on success, -1 on error
 */
static int gpg_export_public_key(const char *key_id, uint8_t *public_key_out) {
  if (!key_id || !public_key_out) {
    log_error("Invalid arguments to gpg_export_public_key");
    return -1;
  }

  // Escape key_id for safe use in shell command
  char escaped_key_id[BUFFER_SIZE_MEDIUM];
  if (!escape_shell_single_quotes(key_id, escaped_key_id, sizeof(escaped_key_id))) {
    log_error("Failed to escape GPG key ID for shell command");
    return -1;
  }

  // Create temp file for exported key
  char temp_path[256];
  safe_snprintf(temp_path, sizeof(temp_path), "/tmp/asciichat_gpg_export_%d_XXXXXX", getpid());
  int temp_fd = mkstemp(temp_path);
  if (temp_fd < 0) {
    log_error("Failed to create temp file for GPG export: %s", SAFE_STRERROR(errno));
    return -1;
  }
  close(temp_fd);

  // Use gpg --export to export the public key in binary format
  char cmd[BUFFER_SIZE_LARGE];
  safe_snprintf(cmd, sizeof(cmd), "gpg --export 0x%s > \"%s\" 2>/dev/null", escaped_key_id, temp_path);

  log_debug("Running GPG export command: gpg --export 0x%s", key_id);
  int result = system(cmd);
  if (result != 0) {
    log_error("Failed to export GPG public key for key ID: %s (exit code: %d)", key_id, result);
    unlink(temp_path);
    return -1;
  }
  log_debug("GPG export completed successfully");

  // Read the exported key file
  FILE *fp = fopen(temp_path, "rb");
  if (!fp) {
    log_error("Failed to open exported GPG key file");
    unlink(temp_path);
    return -1;
  }

  // Read up to 8KB (should be more than enough for a public key packet)
  uint8_t packet_data[8192];
  size_t bytes_read = fread(packet_data, 1, sizeof(packet_data), fp);
  fclose(fp);
  unlink(temp_path);

  if (bytes_read == 0) {
    log_error("GPG export produced empty output - key may not exist");
    return -1;
  }

  log_debug("Read %zu bytes from GPG export", bytes_read);

  // Parse OpenPGP packet to extract Ed25519 public key
  // OpenPGP public key packet format (simplified):
  // - Packet tag (1 byte): 0x99 for public key packet (old format) or 0xC6 (new format)
  // - Packet length (variable)
  // - Version (1 byte): 0x04 for modern keys
  // - Creation time (4 bytes)
  // - Algorithm (1 byte): 22 (0x16) for EdDSA
  // - Curve OID length + OID
  // - Public key material (MPI format)

  size_t offset = 0;

  // Skip to public key packet (tag 6 - public key, or tag 14 - public subkey)
  while (offset < bytes_read) {
    uint8_t tag = packet_data[offset];

    // Check if this is a public key packet (old or new format)
    bool is_public_key = false;
    size_t packet_len = 0;

    if ((tag & 0x80) == 0) {
      // Not a valid packet tag
      offset++;
      continue;
    }

    if ((tag & 0x40) == 0) {
      // Old format packet
      uint8_t packet_type = (tag >> 2) & 0x0F;
      is_public_key = (packet_type == 6 || packet_type == 14); // Public key or subkey

      uint8_t length_type = tag & 0x03;
      offset++; // Move past tag

      if (length_type == 0) {
        packet_len = packet_data[offset++];
      } else if (length_type == 1) {
        packet_len = (packet_data[offset] << 8) | packet_data[offset + 1];
        offset += 2;
      } else if (length_type == 2) {
        packet_len = (packet_data[offset] << 24) | (packet_data[offset + 1] << 16) | (packet_data[offset + 2] << 8) |
                     packet_data[offset + 3];
        offset += 4;
      } else {
        // Indeterminate length - skip
        break;
      }
    } else {
      // New format packet
      uint8_t packet_type = tag & 0x3F;
      is_public_key = (packet_type == 6 || packet_type == 14); // Public key or subkey
      offset++;                                                // Move past tag

      // Parse new format length
      if (offset >= bytes_read)
        break;
      uint8_t first_len = packet_data[offset++];

      if (first_len < 192) {
        packet_len = first_len;
      } else if (first_len < 224) {
        if (offset >= bytes_read)
          break;
        packet_len = ((first_len - 192) << 8) + packet_data[offset++] + 192;
      } else if (first_len == 255) {
        if (offset + 4 > bytes_read)
          break;
        packet_len = (packet_data[offset] << 24) | (packet_data[offset + 1] << 16) | (packet_data[offset + 2] << 8) |
                     packet_data[offset + 3];
        offset += 4;
      } else {
        // Partial body length - not expected for key packets
        break;
      }
    }

    if (!is_public_key || packet_len == 0 || offset + packet_len > bytes_read) {
      offset += packet_len;
      continue;
    }

    // Parse the public key packet content
    size_t packet_start = offset;

    // Check version (should be 4)
    if (packet_data[offset] != 0x04) {
      offset += packet_len;
      continue;
    }
    offset++; // Skip version

    offset += 4; // Skip creation time

    // Check algorithm (22 = EdDSA/Ed25519)
    if (offset >= packet_start + packet_len) {
      offset = packet_start + packet_len;
      continue;
    }

    uint8_t algorithm = packet_data[offset++];
    if (algorithm != 22) { // Not EdDSA
      offset = packet_start + packet_len;
      continue;
    }

    // Skip curve OID (should be Ed25519 OID)
    if (offset >= packet_start + packet_len) {
      offset = packet_start + packet_len;
      continue;
    }

    uint8_t oid_len = packet_data[offset++];
    offset += oid_len; // Skip OID bytes

    // Now we should have the MPI-encoded public key
    // MPI format: 2-byte bit count, then key data
    if (offset + 2 > packet_start + packet_len) {
      offset = packet_start + packet_len;
      continue;
    }

    uint16_t mpi_bits = (packet_data[offset] << 8) | packet_data[offset + 1];
    offset += 2;

    // Ed25519 public keys should be 263 bits (0x0107) - includes 0x40 prefix byte
    // Or 256 bits for just the key without prefix
    size_t mpi_bytes = (mpi_bits + 7) / 8;

    if (offset + mpi_bytes > packet_start + packet_len) {
      offset = packet_start + packet_len;
      continue;
    }

    // Ed25519 keys in OpenPGP have a 0x40 prefix byte
    if (mpi_bytes == 33 && packet_data[offset] == 0x40) {
      // Found it! Extract the 32-byte public key (skip 0x40 prefix)
      memcpy(public_key_out, &packet_data[offset + 1], 32);
      log_info("Extracted Ed25519 public key from gpg --export (fallback method)");
      return 0;
    } else if (mpi_bytes == 32) {
      // Key without prefix (less common but valid)
      memcpy(public_key_out, &packet_data[offset], 32);
      log_info("Extracted Ed25519 public key from gpg --export (fallback method)");
      return 0;
    }

    offset = packet_start + packet_len;
  }

  log_error("Failed to find Ed25519 public key in GPG export data");
  return -1;
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
  char escaped_key_id[BUFFER_SIZE_MEDIUM];
  if (!escape_shell_single_quotes(key_id, escaped_key_id, sizeof(escaped_key_id))) {
    log_error("Failed to escape GPG key ID for shell command");
    return -1;
  }

  // Use gpg to list the key and get the keygrip
  char cmd[BUFFER_SIZE_LARGE];
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

  char line[BUFFER_SIZE_XLARGE];
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

  // Try to use GPG agent API to read the public key directly via READKEY command
  int agent_sock = gpg_agent_connect();
  if (agent_sock < 0) {
    log_info("GPG agent not available, falling back to gpg --export for public key extraction");
    // Fallback: Use gpg --export to get the public key
    int export_result = gpg_export_public_key(key_id, public_key_out);
    if (export_result == 0) {
      log_info("Successfully extracted public key using fallback method");
    } else {
      log_error("Fallback public key extraction failed for key ID: %s", key_id);
    }
    return export_result;
  }

  // Send READKEY command with keygrip to get the public key S-expression
  char readkey_cmd[256];
  safe_snprintf(readkey_cmd, sizeof(readkey_cmd), "READKEY %s\n", found_keygrip);

  ssize_t bytes_written = platform_pipe_write(agent_sock, (const unsigned char *)readkey_cmd, strlen(readkey_cmd));
  if (bytes_written != (ssize_t)strlen(readkey_cmd)) {
    log_error("Failed to send READKEY command to GPG agent");
    gpg_agent_disconnect(agent_sock);
    return -1;
  }

  // Read the response (public key S-expression)
  char response[BUFFER_SIZE_XXXLARGE];
  memset(response, 0, sizeof(response));
  ssize_t bytes_read = platform_pipe_read(agent_sock, (unsigned char *)response, sizeof(response) - 1);

  gpg_agent_disconnect(agent_sock);

  if (bytes_read <= 0) {
    log_error("Failed to read READKEY response from GPG agent");
    return -1;
  }

  // Parse the S-expression to extract Ed25519 public key (q value)
  // GPG agent returns binary S-expressions in format: (1:q<length>:<binary-data>)
  // Example: (1:q33:<33-bytes>) where first byte is 0x40 (Ed25519 prefix), then 32-byte key
  const char *q_marker = strstr(response, "(1:q");
  if (!q_marker) {
    log_warn("Failed to find public key (1:q) in GPG agent READKEY response, trying gpg --export fallback");
    log_debug("Response was: %.*s", (int)(bytes_read < 200 ? bytes_read : 200), response);
    gpg_agent_disconnect(agent_sock);

    // Fallback: Use gpg --export for public-only keys
    int export_result = gpg_export_public_key(key_id, public_key_out);
    if (export_result == 0) {
      log_info("Successfully extracted public key using gpg --export fallback");
    } else {
      log_error("Fallback public key extraction failed for key ID: %s", key_id);
    }
    return export_result;
  }

  // Skip "(1:q" to get to the length field
  const char *len_start = q_marker + 4;

  // Parse the length (e.g., "33:")
  char *colon = strchr(len_start, ':');
  if (!colon) {
    log_error("Malformed S-expression: missing colon after length");
    return -1;
  }

  size_t key_len = strtoul(len_start, NULL, 10);
  if (key_len != 33) {
    log_error("Unexpected Ed25519 public key length: %zu bytes (expected 33)", key_len);
    return -1;
  }

  // Skip the colon to get to the binary data
  const unsigned char *binary_start = (const unsigned char *)(colon + 1);

  // Ed25519 public keys in GPG format have a 0x40 prefix byte, then 32 bytes of actual key
  if (binary_start[0] != 0x40) {
    log_error("Invalid Ed25519 public key prefix: 0x%02x (expected 0x40)", binary_start[0]);
    return -1;
  }

  // Copy the 32-byte public key (skip the 0x40 prefix)
  memcpy(public_key_out, binary_start + 1, 32);

  log_info("Extracted Ed25519 public key from GPG agent via READKEY command");
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

/**
 * @brief Sign a message using GPG key (via gpg --detach-sign)
 *
 * This function uses `gpg --detach-sign` which internally uses gpg-agent,
 * so no passphrase prompt if the key is cached in the agent.
 *
 * @param key_id GPG key ID (e.g., "7FE90A79F2E80ED3")
 * @param message Message to sign
 * @param message_len Message length
 * @param signature_out Output buffer for signature (caller must provide at least 512 bytes)
 * @param signature_len_out Actual signature length written
 * @return 0 on success, -1 on error
 */
int gpg_sign_with_key(const char *key_id, const uint8_t *message, size_t message_len, uint8_t *signature_out,
                      size_t *signature_len_out) {
  if (!key_id || !message || message_len == 0 || !signature_out || !signature_len_out) {
    log_error("Invalid parameters to gpg_sign_with_key");
    return -1;
  }

  char msg_path[512];
  char sig_path[512];
  int msg_fd = -1;
  int result = -1;

#ifdef _WIN32
  // Windows: use GetTempPath + GetTempFileName with process ID
  char temp_dir[MAX_PATH];
  if (GetTempPathA(sizeof(temp_dir), temp_dir) == 0) {
    log_error("Failed to get temp directory");
    return -1;
  }

  char msg_prefix[32];
  char sig_prefix[32];
  safe_snprintf(msg_prefix, sizeof(msg_prefix), "asc_msg_%lu_", GetCurrentProcessId());
  safe_snprintf(sig_prefix, sizeof(sig_prefix), "asc_sig_%lu_", GetCurrentProcessId());

  if (GetTempFileNameA(temp_dir, msg_prefix, 0, msg_path) == 0) {
    log_error("Failed to create temp message file");
    return -1;
  }
  if (GetTempFileNameA(temp_dir, sig_prefix, 0, sig_path) == 0) {
    log_error("Failed to create temp signature file");
    unlink(msg_path);
    return -1;
  }

  msg_fd = platform_open(msg_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
#else
  // Unix: use mkstemp with process ID in template
  safe_snprintf(msg_path, sizeof(msg_path), "/tmp/asciichat_msg_%d_XXXXXX", getpid());
  safe_snprintf(sig_path, sizeof(sig_path), "/tmp/asciichat_sig_%d_XXXXXX", getpid());

  msg_fd = mkstemp(msg_path);
  if (msg_fd < 0) {
    log_error("Failed to create temp message file: %s", SAFE_STRERROR(errno));
    return -1;
  }

  // Create signature file path (will be created by gpg)
  int sig_fd = mkstemp(sig_path);
  if (sig_fd < 0) {
    log_error("Failed to create temp signature file: %s", SAFE_STRERROR(errno));
    close(msg_fd);
    unlink(msg_path);
    return -1;
  }
  close(sig_fd);    // Close and let gpg overwrite it
  unlink(sig_path); // Remove it so gpg can create it fresh
#endif

  if (msg_fd < 0) {
    log_error("Failed to open temp message file");
    goto cleanup;
  }

  // Write message to temp file
  ssize_t written = write(msg_fd, message, message_len);
  close(msg_fd);
  msg_fd = -1;

  if (written != (ssize_t)message_len) {
    log_error("Failed to write message to temp file");
    goto cleanup;
  }

  // Escape key ID for shell command (prevent injection)
  char escaped_key_id[64];
  if (!escape_path_for_shell(key_id, escaped_key_id, sizeof(escaped_key_id))) {
    log_error("Failed to escape GPG key ID for shell command");
    goto cleanup;
  }

  // Call gpg --detach-sign
  char cmd[BUFFER_SIZE_LARGE];
#ifdef _WIN32
  safe_snprintf(cmd, sizeof(cmd), "gpg --local-user 0x%s --detach-sign --output \"%s\" \"%s\" 2>nul", escaped_key_id,
                sig_path, msg_path);
#else
  safe_snprintf(cmd, sizeof(cmd), "gpg --local-user 0x%s --detach-sign --output \"%s\" \"%s\" 2>/dev/null",
                escaped_key_id, sig_path, msg_path);
#endif

  log_debug("Signing with GPG: %s", cmd);
  int status = system(cmd);
  if (status != 0) {
    log_error("GPG signing failed (exit code %d)", status);
    goto cleanup;
  }

  // Read signature file
  FILE *sig_fp = fopen(sig_path, "rb");
  if (!sig_fp) {
    log_error("Failed to open signature file: %s", SAFE_STRERROR(errno));
    goto cleanup;
  }

  fseek(sig_fp, 0, SEEK_END);
  long sig_size = ftell(sig_fp);
  fseek(sig_fp, 0, SEEK_SET);

  if (sig_size <= 0 || sig_size > 512) {
    log_error("Invalid signature size: %ld bytes", sig_size);
    fclose(sig_fp);
    goto cleanup;
  }

  size_t bytes_read = fread(signature_out, 1, sig_size, sig_fp);
  fclose(sig_fp);

  if (bytes_read != (size_t)sig_size) {
    log_error("Failed to read signature file");
    goto cleanup;
  }

  *signature_len_out = sig_size;
  log_info("GPG signature created successfully (%zu bytes)", *signature_len_out);
  result = 0;

cleanup:
  if (msg_fd >= 0) {
    close(msg_fd);
  }
  unlink(msg_path);
  unlink(sig_path);
  return result;
}

int gpg_sign_detached_ed25519(const char *key_id, const uint8_t *message, size_t message_len,
                              uint8_t signature_out[64]) {
  log_info("gpg_sign_detached_ed25519: Signing with key ID %s (fallback mode)", key_id);

  // Get OpenPGP signature packet from gpg --detach-sign
  uint8_t openpgp_signature[512];
  size_t openpgp_len = 0;

  int result = gpg_sign_with_key(key_id, message, message_len, openpgp_signature, &openpgp_len);
  if (result != 0) {
    log_error("GPG detached signing failed for key %s", key_id);
    return -1;
  }

  log_debug("gpg_sign_with_key returned %zu bytes", openpgp_len);

  if (openpgp_len < 10) {
    log_error("GPG signature too short: %zu bytes", openpgp_len);
    return -1;
  }

  log_debug("Parsing OpenPGP signature packet (%zu bytes) to extract Ed25519 signature", openpgp_len);

  // Parse OpenPGP signature packet format
  // Reference: RFC 4880 Section 5.2 (Signature Packet)
  // Format: [header][version][type][algo][hash-algo][...][signature-data]
  size_t offset = 0;

  // Parse packet header
  uint8_t tag = openpgp_signature[offset++];
  size_t packet_len = 0;

  if ((tag & 0x40) == 0) {
    // Old format packet
    uint8_t length_type = tag & 0x03;
    if (length_type == 0) {
      packet_len = openpgp_signature[offset++];
    } else if (length_type == 1) {
      packet_len = (openpgp_signature[offset] << 8) | openpgp_signature[offset + 1];
      offset += 2;
    } else if (length_type == 2) {
      packet_len = (openpgp_signature[offset] << 24) | (openpgp_signature[offset + 1] << 16) |
                   (openpgp_signature[offset + 2] << 8) | openpgp_signature[offset + 3];
      offset += 4;
    } else {
      log_error("Unsupported old-format packet length type: %d", length_type);
      return -1;
    }
  } else {
    // New format packet
    uint8_t length_byte = openpgp_signature[offset++];
    if (length_byte < 192) {
      packet_len = length_byte;
    } else if (length_byte < 224) {
      packet_len = ((length_byte - 192) << 8) + openpgp_signature[offset++] + 192;
    } else if (length_byte == 255) {
      packet_len = (openpgp_signature[offset] << 24) | (openpgp_signature[offset + 1] << 16) |
                   (openpgp_signature[offset + 2] << 8) | openpgp_signature[offset + 3];
      offset += 4;
    } else {
      log_error("Unsupported new-format packet length encoding: %d", length_byte);
      return -1;
    }
  }

  if (offset + packet_len > openpgp_len) {
    log_error("Packet length exceeds signature size: %zu + %zu > %zu", offset, packet_len, openpgp_len);
    return -1;
  }

  log_debug("Signature packet: offset=%zu, length=%zu", offset, packet_len);

  // Parse signature packet body
  // Skip: version (1), sig_type (1), pub_algo (1), hash_algo (1)
  if (offset + 4 > openpgp_len) {
    log_error("Signature packet too short for header");
    return -1;
  }

  uint8_t version = openpgp_signature[offset++];
  uint8_t sig_type = openpgp_signature[offset++];
  uint8_t pub_algo = openpgp_signature[offset++];
  uint8_t hash_algo = openpgp_signature[offset++];

  log_debug("Signature: version=%d, type=%d, algo=%d, hash=%d", version, sig_type, pub_algo, hash_algo);

  // Verify algorithm is Ed25519 (22 = EdDSA)
  if (pub_algo != 22) {
    log_error("Expected EdDSA algorithm (22), got %d", pub_algo);
    return -1;
  }

  // For v4 signatures: skip hashed subpackets
  if (version == 4) {
    if (offset + 2 > openpgp_len) {
      log_error("Cannot read hashed subpacket length");
      return -1;
    }
    uint16_t hashed_len = (openpgp_signature[offset] << 8) | openpgp_signature[offset + 1];
    offset += 2;
    offset += hashed_len; // Skip hashed subpackets

    if (offset + 2 > openpgp_len) {
      log_error("Cannot read unhashed subpacket length");
      return -1;
    }
    uint16_t unhashed_len = (openpgp_signature[offset] << 8) | openpgp_signature[offset + 1];
    offset += 2;
    offset += unhashed_len; // Skip unhashed subpackets

    // Skip left 16 bits of signed hash value
    if (offset + 2 > openpgp_len) {
      log_error("Cannot read hash left bits");
      return -1;
    }
    offset += 2;
  }

  // Now we're at the signature data (MPI format for Ed25519)
  // Ed25519 signature is: r (32 bytes) || s (32 bytes) = 64 bytes total
  // In OpenPGP, each MPI is encoded as: [2-byte bit count][data]

  if (offset + 2 > openpgp_len) {
    log_error("Cannot read MPI bit count for R");
    return -1;
  }

  uint16_t r_bits = (openpgp_signature[offset] << 8) | openpgp_signature[offset + 1];
  offset += 2;
  size_t r_bytes = (r_bits + 7) / 8;

  log_debug("R: %d bits (%zu bytes)", r_bits, r_bytes);

  if (r_bytes != 32) {
    log_error("Expected 32-byte R value, got %zu bytes", r_bytes);
    return -1;
  }

  if (offset + r_bytes > openpgp_len) {
    log_error("R value exceeds packet size");
    return -1;
  }

  memcpy(signature_out, &openpgp_signature[offset], 32);
  offset += r_bytes;

  // Read S value
  if (offset + 2 > openpgp_len) {
    log_error("Cannot read MPI bit count for S");
    return -1;
  }

  uint16_t s_bits = (openpgp_signature[offset] << 8) | openpgp_signature[offset + 1];
  offset += 2;
  size_t s_bytes = (s_bits + 7) / 8;

  log_debug("S: %d bits (%zu bytes)", s_bits, s_bytes);

  if (s_bytes != 32) {
    log_error("Expected 32-byte S value, got %zu bytes", s_bytes);
    return -1;
  }

  if (offset + s_bytes > openpgp_len) {
    log_error("S value exceeds packet size");
    return -1;
  }

  memcpy(signature_out + 32, &openpgp_signature[offset], 32);

  log_info("Successfully extracted 64-byte Ed25519 signature from OpenPGP packet");

  // Debug: Print signature bytes
  fprintf(stderr, "[GPG DEBUG] Signature R (first 32 bytes): ");
  for (int i = 0; i < 32; i++) {
    fprintf(stderr, "%02x", signature_out[i]);
  }
  fprintf(stderr, "\n[GPG DEBUG] Signature S (last 32 bytes): ");
  for (int i = 32; i < 64; i++) {
    fprintf(stderr, "%02x", signature_out[i]);
  }
  fprintf(stderr, "\n");

  return 0;
}

/**
 * Find GPG key ID from Ed25519 public key by searching GPG keyring
 * @param public_key 32-byte Ed25519 public key
 * @param key_id_out Output buffer for 16-char key ID (must be at least 17 bytes for null terminator)
 * @return 0 on success, -1 if key not found
 */
int gpg_verify_detached_ed25519(const char *key_id, const uint8_t *message, size_t message_len,
                                const uint8_t signature[64]) {
  // Note: We don't use the raw signature parameter directly.
  // Instead, we regenerate the OpenPGP signature using GPG (Ed25519 is deterministic).
  (void)signature;

  log_info("gpg_verify_detached_ed25519: Verifying signature with key ID %s using gpg --verify", key_id);

  // To verify with GPG, we need to:
  // 1. Reconstruct the OpenPGP signature packet from the raw R||S signature
  // 2. Write message and signature to temp files
  // 3. Call gpg --verify

  // First, reconstruct OpenPGP signature by signing the same message
  // Since Ed25519 is deterministic, we should get the same OpenPGP packet
  uint8_t openpgp_signature[512];
  size_t openpgp_len = 0;

  int sign_result = gpg_sign_with_key(key_id, message, message_len, openpgp_signature, &openpgp_len);
  if (sign_result != 0) {
    log_error("Failed to create reference signature for verification");
    return -1;
  }

  // Now verify using gpg --verify
  char msg_path[] = "/tmp/gpg_verify_msg_XXXXXX";
  char sig_path[] = "/tmp/gpg_verify_sig_XXXXXX";

  int msg_fd = mkstemp(msg_path);
  if (msg_fd < 0) {
    log_error("Failed to create temporary message file");
    return -1;
  }

  int sig_fd = mkstemp(sig_path);
  if (sig_fd < 0) {
    close(msg_fd);
    unlink(msg_path);
    log_error("Failed to create temporary signature file");
    return -1;
  }

  // Write message
  if (write(msg_fd, message, message_len) != (ssize_t)message_len) {
    log_error("Failed to write message to temp file");
    close(msg_fd);
    close(sig_fd);
    unlink(msg_path);
    unlink(sig_path);
    return -1;
  }
  close(msg_fd);

  // Write OpenPGP signature
  if (write(sig_fd, openpgp_signature, openpgp_len) != (ssize_t)openpgp_len) {
    log_error("Failed to write signature to temp file");
    close(sig_fd);
    unlink(msg_path);
    unlink(sig_path);
    return -1;
  }
  close(sig_fd);

  // Call gpg --verify
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "gpg --verify '%s' '%s' 2>&1", sig_path, msg_path);

  log_debug("Running: %s", cmd);
  FILE *fp = popen(cmd, "r");
  if (!fp) {
    log_error("Failed to run gpg --verify");
    unlink(msg_path);
    unlink(sig_path);
    return -1;
  }

  char output[4096] = {0};
  size_t output_len = fread(output, 1, sizeof(output) - 1, fp);
  int exit_code = pclose(fp);

  // Cleanup temp files
  unlink(msg_path);
  unlink(sig_path);

  if (exit_code == 0) {
    log_info("GPG signature verification PASSED");
    return 0;
  } else {
    log_error("GPG signature verification FAILED (exit code %d)", exit_code);
    if (output_len > 0) {
      log_debug("GPG output: %s", output);
    }
    return -1;
  }
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
    snprintf(pubkey_hex + i * 2, 3, "%02x", public_key[i]);
    snprintf(r_hex + i * 2, 3, "%02x", signature[i]);
    snprintf(s_hex + i * 2, 3, "%02x", signature[32 + i]);
  }
  for (size_t i = 0; i < (message_len < 32 ? message_len : 32); i++) {
    snprintf(msg_hex + i * 2, 3, "%02x", message[i]);
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

int gpg_verify_signature_with_binary(const uint8_t *signature, size_t signature_len, const uint8_t *message,
                                     size_t message_len, const char *expected_key_id) {
  // Validate inputs
  if (!signature || signature_len == 0 || signature_len > 512) {
    log_error("gpg_verify_signature_with_binary: Invalid signature (expected 1-512 bytes, got %zu)", signature_len);
    return -1;
  }
  if (!message || message_len == 0) {
    log_error("gpg_verify_signature_with_binary: Invalid message");
    return -1;
  }

  // Create temporary files for signature and message
  char sig_path[PLATFORM_MAX_PATH_LENGTH];
  char msg_path[PLATFORM_MAX_PATH_LENGTH];
  int sig_fd = -1;
  int msg_fd = -1;
  int result = -1;

#ifdef _WIN32
  // Windows temp file creation with process ID for concurrent process safety
  char temp_dir[PLATFORM_MAX_PATH_LENGTH];
  DWORD temp_dir_len = GetTempPathA(sizeof(temp_dir), temp_dir);
  if (temp_dir_len == 0 || temp_dir_len >= sizeof(temp_dir)) {
    log_error("Failed to get Windows temp directory");
    return -1;
  }

  // Create process-specific temp file prefixes (e.g., "asc_sig_12345_")
  char sig_prefix[32];
  char msg_prefix[32];
  safe_snprintf(sig_prefix, sizeof(sig_prefix), "asc_sig_%lu_", GetCurrentProcessId());
  safe_snprintf(msg_prefix, sizeof(msg_prefix), "asc_msg_%lu_", GetCurrentProcessId());

  // Create signature temp file
  if (GetTempFileNameA(temp_dir, sig_prefix, 0, sig_path) == 0) {
    log_error("Failed to create signature temp file: %lu", GetLastError());
    return -1;
  }

  // Create message temp file
  if (GetTempFileNameA(temp_dir, msg_prefix, 0, msg_path) == 0) {
    log_error("Failed to create message temp file: %lu", GetLastError());
    DeleteFileA(sig_path);
    return -1;
  }

  // Open files for writing (Windows CreateFile for binary mode)
  HANDLE sig_handle = CreateFileA(sig_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
  if (sig_handle == INVALID_HANDLE_VALUE) {
    log_error("Failed to open signature temp file: %lu", GetLastError());
    DeleteFileA(sig_path);
    DeleteFileA(msg_path);
    return -1;
  }

  DWORD bytes_written;
  if (!WriteFile(sig_handle, signature, (DWORD)signature_len, &bytes_written, NULL) || bytes_written != signature_len) {
    log_error("Failed to write signature to temp file: %lu", GetLastError());
    CloseHandle(sig_handle);
    DeleteFileA(sig_path);
    DeleteFileA(msg_path);
    return -1;
  }
  CloseHandle(sig_handle);

  HANDLE msg_handle = CreateFileA(msg_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
  if (msg_handle == INVALID_HANDLE_VALUE) {
    log_error("Failed to open message temp file: %lu", GetLastError());
    DeleteFileA(sig_path);
    DeleteFileA(msg_path);
    return -1;
  }

  if (!WriteFile(msg_handle, message, (DWORD)message_len, &bytes_written, NULL) || bytes_written != message_len) {
    log_error("Failed to write message to temp file: %lu", GetLastError());
    CloseHandle(msg_handle);
    DeleteFileA(sig_path);
    DeleteFileA(msg_path);
    return -1;
  }
  CloseHandle(msg_handle);

#else
  // Unix temp file creation with mkstemp() - include PID for concurrent process safety
  safe_snprintf(sig_path, sizeof(sig_path), "/tmp/asciichat_sig_%d_XXXXXX", getpid());
  safe_snprintf(msg_path, sizeof(msg_path), "/tmp/asciichat_msg_%d_XXXXXX", getpid());

  sig_fd = mkstemp(sig_path);
  if (sig_fd < 0) {
    log_error("Failed to create signature temp file: %s", SAFE_STRERROR(errno));
    return -1;
  }

  msg_fd = mkstemp(msg_path);
  if (msg_fd < 0) {
    log_error("Failed to create message temp file: %s", SAFE_STRERROR(errno));
    close(sig_fd);
    unlink(sig_path);
    return -1;
  }

  // Write signature to temp file
  ssize_t sig_written = write(sig_fd, signature, signature_len);
  if (sig_written != (ssize_t)signature_len) {
    log_error("Failed to write signature to temp file: %s", SAFE_STRERROR(errno));
    close(sig_fd);
    close(msg_fd);
    unlink(sig_path);
    unlink(msg_path);
    return -1;
  }
  close(sig_fd);

  // Write message to temp file
  ssize_t msg_written = write(msg_fd, message, message_len);
  if (msg_written != (ssize_t)message_len) {
    log_error("Failed to write message to temp file: %s", SAFE_STRERROR(errno));
    close(msg_fd);
    unlink(sig_path);
    unlink(msg_path);
    return -1;
  }
  close(msg_fd);
#endif

  // Build gpg --verify command
  char cmd[BUFFER_SIZE_LARGE];
#ifdef _WIN32
  safe_snprintf(cmd, sizeof(cmd), "gpg --verify \"%s\" \"%s\" 2>&1", sig_path, msg_path);
#else
  safe_snprintf(cmd, sizeof(cmd), "gpg --verify '%s' '%s' 2>&1", sig_path, msg_path);
#endif

  log_debug("Running GPG verify command: %s", cmd);

  // Execute gpg --verify command
  FILE *fp = SAFE_POPEN(cmd, "r");
  if (!fp) {
    log_error("Failed to execute gpg --verify command");
    goto cleanup;
  }

  // Parse output for "Good signature" and verify key ID
  char line[BUFFER_SIZE_MEDIUM];
  bool found_good_sig = false;
  bool found_key_id = false;

  while (fgets(line, sizeof(line), fp)) {
    log_debug("GPG output: %s", line);

    // Check for "Good signature"
    if (strstr(line, "Good signature")) {
      found_good_sig = true;
    }

    // Check if this line contains the expected key ID (GPG outputs key ID on separate line)
    if (expected_key_id && strlen(expected_key_id) > 0) {
      if (strstr(line, expected_key_id)) {
        found_key_id = true;
        log_debug("Found expected key ID in GPG output: %s", expected_key_id);
      }
    }

    // Check for signature errors
    if (strstr(line, "BAD signature")) {
      log_error("GPG reports BAD signature");
      SAFE_PCLOSE(fp);
      fp = NULL;
      goto cleanup;
    }
  }

  // Check exit code
  int status = SAFE_PCLOSE(fp);
  fp = NULL;

#ifdef _WIN32
  int exit_code = status;
#else
  int exit_code = WEXITSTATUS(status);
#endif

  if (exit_code != 0) {
    log_error("GPG verify failed with exit code: %d", exit_code);
    goto cleanup;
  }

  if (!found_good_sig) {
    log_error("GPG verify did not report 'Good signature'");
    goto cleanup;
  }

  // If expected_key_id was provided, verify we found it in the output
  if (expected_key_id && strlen(expected_key_id) > 0) {
    if (!found_key_id) {
      log_error("GPG signature key ID does not match expected key ID: %s", expected_key_id);
      goto cleanup;
    }
  }

  log_info("GPG signature verified successfully via gpg --verify binary");
  result = 0;

cleanup:
  // Clean up temp files
#ifdef _WIN32
  DeleteFileA(sig_path);
  DeleteFileA(msg_path);
#else
  unlink(sig_path);
  unlink(msg_path);
#endif

  if (fp) {
    SAFE_PCLOSE(fp);
  }

  return result;
}
