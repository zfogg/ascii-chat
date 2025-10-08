#include "gpg_agent.h"
#include "keys.h"
#include "common.h"
#include "platform/socket.h"

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
    snprintf(path_out, path_size, "%s\\gnupg\\S.gpg-agent", appdata);
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
    snprintf(path_out, path_size, "%s/S.gpg-agent", gnupg_home);
  } else {
    const char *home = SAFE_GETENV("HOME");
    if (!home) {
      log_error("Could not determine home directory");
      return -1;
    }
    snprintf(path_out, path_size, "%s/.gnupg/S.gpg-agent", home);
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
  SAFE_MALLOC(cmd_with_newline, len + 2, char *);
  if (!cmd_with_newline) {
    log_error("Failed to allocate memory for command");
    return -1;
  }

  memcpy(cmd_with_newline, command, len);
  cmd_with_newline[len] = '\n';
  cmd_with_newline[len + 1] = '\0';

  DWORD bytes_written;
  BOOL result = WriteFile(pipe, cmd_with_newline, (DWORD)(len + 1), &bytes_written, NULL);
  free(cmd_with_newline);

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
  SAFE_MALLOC(cmd_with_newline, len + 2, char *);
  if (!cmd_with_newline) {
    log_error("Failed to allocate memory for command");
    return -1;
  }

  memcpy(cmd_with_newline, command, len);
  cmd_with_newline[len] = '\n';
  cmd_with_newline[len + 1] = '\0';

  ssize_t sent = send(sock, cmd_with_newline, len + 1, 0);
  free(cmd_with_newline);

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

/**
 * Check if response is an error
 */
static bool is_err_response(const char *line) {
  return strncmp(line, "ERR", 3) == 0;
}

#ifdef _WIN32
// On Windows, we return HANDLE cast to int (handle values are always even on Windows)
int gpg_agent_connect(void) {
  char pipe_path[512];
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
  char socket_path[512];
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
  snprintf(sigkey_cmd, sizeof(sigkey_cmd), "SIGKEY %s", keygrip);
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

  // 2. Set the hash to sign
  // For Ed25519 with GPG agent: compute SHA-512 hash of the message first
  // GPG agent's Ed25519 implementation expects: SETHASH --hash=sha512 <64-byte-hash-as-hex>
  // This matches GPG's internal behavior: signature = EdDSA_sign(SHA512(message))

  // Compute SHA-512 hash (64 bytes)
  uint8_t message_hash[64];
  crypto_hash_sha512(message_hash, message, message_len);

  log_debug("Computing SHA-512 hash of message (%zu bytes)", message_len);

  // Convert hash to hex string (128 hex chars)
  char *hex_hash;
  SAFE_MALLOC(hex_hash, 129, char *); // 64 bytes * 2 + null terminator
  if (!hex_hash) {
    log_error("Failed to allocate hex hash buffer");
    return -1;
  }

  for (size_t i = 0; i < 64; i++) {
    sprintf(hex_hash + i * 2, "%02X", message_hash[i]);
  }
  hex_hash[128] = '\0';

  // Send SETHASH command with --hash=sha512 flag
  char sethash_cmd[GPG_AGENT_MAX_RESPONSE];
  snprintf(sethash_cmd, sizeof(sethash_cmd), "SETHASH --hash=sha512 %s", hex_hash);

  log_debug("Sending SETHASH --hash=sha512 with 64-byte hash");

  free(hex_hash);

  if (send_agent_command(handle, sethash_cmd) != 0) {
    log_error("Failed to send SETHASH command");
    return -1;
  }

  if (read_agent_line(handle, response, sizeof(response)) != 0) {
    log_error("Failed to read SETHASH response");
    return -1;
  }

  if (!is_ok_response(response)) {
    log_error("SETHASH failed: %s", response);
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

  // Parse S-expression signature
  // Format: D (7:sig-val(5:eddsa(1:r32:BINARY_R_DATA)(1:s32:BINARY_S_DATA)))
  // The format is: (1:r32:...) where 32: means "32 bytes of binary data follow"
  const char *data = response + 2;

  // Find "(1:r32:" - this marks the start of the r value (binary format)
  const char *r_start = strstr(data, "(1:r32:");
  if (!r_start) {
    log_error("Could not find r value in signature S-expression");
    return -1;
  }
  r_start += 7; // Skip "(1:r32:"

  // Copy 32 bytes of r value directly (it's already binary)
  memcpy(signature_out, r_start, 32);

  // Find "(1:s32:" - this marks the start of the s value (binary format)
  const char *s_start = strstr(r_start, "(1:s32:");
  if (!s_start) {
    log_error("Could not find s value in signature S-expression");
    return -1;
  }
  s_start += 7; // Skip "(1:s32:"

  // Copy 32 bytes of s value directly (it's already binary)
  memcpy(signature_out + 32, s_start, 32);

  *signature_len_out = 64;

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

  // Use gpg to list the key and get the keygrip
  char cmd[512];
#ifdef _WIN32
  snprintf(cmd, sizeof(cmd), "gpg --list-keys --with-keygrip --with-colons 0x%s 2>nul", key_id);
#else
  snprintf(cmd, sizeof(cmd), "gpg --list-keys --with-keygrip --with-colons 0x%s 2>/dev/null", key_id);
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
#ifdef _WIN32
  snprintf(cmd, sizeof(cmd), "gpg --export --armor 0x%s 2>nul", key_id);
#else
  snprintf(cmd, sizeof(cmd), "gpg --export --armor 0x%s 2>/dev/null", key_id);
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
