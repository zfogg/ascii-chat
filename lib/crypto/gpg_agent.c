#include "gpg_agent.h"
#include "keys.h"
#include "common.h"
#include "platform/socket.h"

#include <errno.h>
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// Maximum response size from gpg-agent
#define GPG_AGENT_MAX_RESPONSE 8192

/**
 * Get gpg-agent socket path
 */
static int get_agent_socket_path(char *path_out, size_t path_size) {
  // Try gpgconf first
  FILE *fp = popen("gpgconf --list-dirs agent-socket 2>/dev/null", "r");
  if (fp) {
    if (fgets(path_out, path_size, fp)) {
      // Remove trailing newline
      size_t len = strlen(path_out);
      if (len > 0 && path_out[len - 1] == '\n') {
        path_out[len - 1] = '\0';
      }
      pclose(fp);
      return 0;
    }
    pclose(fp);
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

  return 0;
}

/**
 * Read a line from gpg-agent (Assuan protocol)
 * Returns: 0 on success, -1 on error
 */
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

/**
 * Send a command to gpg-agent
 */
static int send_agent_command(int sock, const char *command) {
  size_t len = strlen(command);
  char *cmd_with_newline = malloc(len + 2);
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

void gpg_agent_disconnect(int sock) {
  if (sock >= 0) {
    send_agent_command(sock, "BYE");
    close(sock);
  }
}

int gpg_agent_sign(int sock, const char *keygrip, const uint8_t *message, size_t message_len, uint8_t *signature_out,
                   size_t *signature_len_out) {
  if (sock < 0 || !keygrip || !message || !signature_out || !signature_len_out) {
    log_error("Invalid arguments to gpg_agent_sign");
    return -1;
  }

  char response[GPG_AGENT_MAX_RESPONSE];

  // 1. Set the key to use (SIGKEY command)
  char sigkey_cmd[128];
  snprintf(sigkey_cmd, sizeof(sigkey_cmd), "SIGKEY %s", keygrip);
  if (send_agent_command(sock, sigkey_cmd) != 0) {
    log_error("Failed to send SIGKEY command");
    return -1;
  }

  if (read_agent_line(sock, response, sizeof(response)) != 0) {
    log_error("Failed to read SIGKEY response");
    return -1;
  }

  if (!is_ok_response(response)) {
    log_error("SIGKEY failed: %s", response);
    return -1;
  }

  // 2. Set the hash/data to sign
  // For Ed25519, we use SHA-512 as that's what Ed25519 internally uses
  // Convert message to hex
  char *hex_message = malloc(message_len * 2 + 1);
  if (!hex_message) {
    log_error("Failed to allocate hex message buffer");
    return -1;
  }

  for (size_t i = 0; i < message_len; i++) {
    sprintf(hex_message + i * 2, "%02X", message[i]);
  }

  char sethash_cmd[GPG_AGENT_MAX_RESPONSE];
  snprintf(sethash_cmd, sizeof(sethash_cmd), "SETHASH --hash=sha512 %s", hex_message);
  free(hex_message);

  if (send_agent_command(sock, sethash_cmd) != 0) {
    log_error("Failed to send SETHASH command");
    return -1;
  }

  if (read_agent_line(sock, response, sizeof(response)) != 0) {
    log_error("Failed to read SETHASH response");
    return -1;
  }

  if (!is_ok_response(response)) {
    log_error("SETHASH failed: %s", response);
    return -1;
  }

  // 3. Request signature
  if (send_agent_command(sock, "PKSIGN") != 0) {
    log_error("Failed to send PKSIGN command");
    return -1;
  }

  // Read response (could be D line with signature data, then OK)
  if (read_agent_line(sock, response, sizeof(response)) != 0) {
    log_error("Failed to read PKSIGN response");
    return -1;
  }

  // Check if it's a data line (D followed by space and hex data)
  if (response[0] != 'D' || response[1] != ' ') {
    log_error("Expected D line from PKSIGN, got: %s", response);
    return -1;
  }

  // Parse S-expression signature
  // Format: D (sig-val(eddsa(r 32-bytes)(s 32-bytes)))
  // For now, we'll extract the hex data after "D "
  const char *hex_data = response + 2;

  // For Ed25519, gpg-agent returns S-expression format
  // We need to extract the r and s values (32 bytes each)
  // This is a simplified parser - full S-expression parsing would be more robust

  // Look for the hex signature data (skip S-expression structure)
  const char *sig_start = strstr(hex_data, "(r #");
  if (!sig_start) {
    log_error("Could not find r value in signature S-expression");
    return -1;
  }
  sig_start += 4; // Skip "(r #"

  // Extract 64 hex chars for r (32 bytes)
  char r_hex[65] = {0};
  strncpy(r_hex, sig_start, 64);

  // Look for s value
  const char *s_start = strstr(sig_start, "(s #");
  if (!s_start) {
    log_error("Could not find s value in signature S-expression");
    return -1;
  }
  s_start += 4; // Skip "(s #"

  // Extract 64 hex chars for s (32 bytes)
  char s_hex[65] = {0};
  strncpy(s_hex, s_start, 64);

  // Convert hex to binary
  if (sodium_hex2bin(signature_out, 32, r_hex, 64, NULL, NULL, NULL) != 0) {
    log_error("Failed to decode r value");
    return -1;
  }

  if (sodium_hex2bin(signature_out + 32, 32, s_hex, 64, NULL, NULL, NULL) != 0) {
    log_error("Failed to decode s value");
    return -1;
  }

  *signature_len_out = 64;

  // Read final OK
  if (read_agent_line(sock, response, sizeof(response)) != 0) {
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
  snprintf(cmd, sizeof(cmd), "gpg --list-keys --with-keygrip --with-colons 0x%s 2>/dev/null", key_id);

  FILE *fp = popen(cmd, "r");
  if (!fp) {
    log_error("Failed to run gpg command");
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

  pclose(fp);

  if (!found_key || strlen(found_keygrip) == 0) {
    log_error("Could not find GPG key with ID: %s", key_id);
    return -1;
  }

  log_debug("Found keygrip for key %s: %s", key_id, found_keygrip);

  // Export the public key in ASCII armor format and parse it with existing parse_gpg_key()
  snprintf(cmd, sizeof(cmd), "gpg --export --armor 0x%s 2>/dev/null", key_id);
  fp = popen(cmd, "r");
  if (!fp) {
    log_error("Failed to export GPG public key");
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
  pclose(fp);

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
