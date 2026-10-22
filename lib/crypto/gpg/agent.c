/**
 * @file crypto/gpg/agent.c
 * @ingroup crypto
 * @brief GPG agent connection and communication implementation
 */

#include <ascii-chat/crypto/gpg/agent.h>
#include <ascii-chat/crypto/keys.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/agent.h>
#include <ascii-chat/platform/pipe.h>

#include <ctype.h>
#include <errno.h>
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

// Maximum response size from gpg-agent
#define GPG_AGENT_MAX_RESPONSE 8192

/**
 * Read a line from gpg-agent (Assuan protocol)
 * Returns: 0 on success, -1 on error
 */
static int read_agent_line(pipe_t pipe, char *buf, size_t buf_size, size_t *line_len) {
  size_t pos = 0;
  while (pos < buf_size - 1) {
    char c;
    ssize_t n = platform_pipe_read(pipe, &c, 1);
    if (n <= 0) {
      if (n == 0) {
        log_error("GPG agent connection closed");
      } else {
        log_error("Error reading from GPG agent");
      }
      return -1;
    }

    if (c == '\n') {
      buf[pos] = '\0';
      if (line_len)
        *line_len = pos;
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
static int send_agent_command(pipe_t pipe, const char *command) {
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

  ssize_t sent = platform_pipe_write(pipe, cmd_with_newline, len + 1);
  SAFE_FREE(cmd_with_newline);

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

int gpg_agent_connect(void) {
  char agent_path[PLATFORM_MAX_PATH_LENGTH];
  if (platform_get_gpg_agent_socket(agent_path, sizeof(agent_path)) != 0) {
    log_error("Failed to get GPG agent path");
    return -1;
  }

  log_debug("Connecting to GPG agent at: %s", agent_path);

  // Use platform abstraction for pipe/socket connection
  pipe_t pipe = platform_pipe_connect(agent_path);
  if (!platform_pipe_is_valid(pipe)) {
    log_error("Failed to connect to GPG agent");
    return -1;
  }

  // Read initial greeting
  char response[GPG_AGENT_MAX_RESPONSE];
  if (read_agent_line(pipe, response, sizeof(response), NULL) != 0) {
    log_error("Failed to read GPG agent greeting");
    platform_pipe_close(pipe);
    return -1;
  }

  if (!is_ok_response(response)) {
    log_error("Unexpected GPG agent greeting: %s", response);
    platform_pipe_close(pipe);
    return -1;
  }

  log_debug("Connected to GPG agent successfully");

  // Set loopback pinentry mode to avoid interactive prompts
  // This allows GPG agent to work in non-interactive environments
  if (send_agent_command(pipe, "OPTION pinentry-mode=loopback") != 0) {
    log_warn("Failed to set loopback pinentry mode (continuing anyway)");
  } else {
    // Read response for OPTION command
    if (read_agent_line(pipe, response, sizeof(response), NULL) != 0) {
      log_warn("Failed to read OPTION command response (continuing anyway)");
    } else if (is_ok_response(response)) {
      log_debug("Loopback pinentry mode enabled");
    } else {
      log_warn("Failed to enable loopback pinentry mode: %s (continuing anyway)", response);
    }
  }

  return (int)(intptr_t)pipe;
}

void gpg_agent_disconnect(int handle_as_int) {
  if (handle_as_int >= 0) {
    pipe_t pipe = (pipe_t)(intptr_t)handle_as_int;
    send_agent_command(pipe, "BYE");
    platform_pipe_close(pipe);
  }
}

int gpg_agent_sign(int handle_as_int, const char *keygrip, const uint8_t *message, size_t message_len,
                   uint8_t *signature_out, size_t *signature_len_out) {
  if (handle_as_int < 0 || !keygrip || !message || !signature_out || !signature_len_out) {
    log_error("Invalid arguments");
    return -1;
  }

  pipe_t handle = (pipe_t)(intptr_t)handle_as_int;
  char response[GPG_AGENT_MAX_RESPONSE];

  // 1. Set the key to use (SIGKEY command)
  char sigkey_cmd[128];
  safe_snprintf(sigkey_cmd, sizeof(sigkey_cmd), "SIGKEY %s", keygrip);
  if (send_agent_command(handle, sigkey_cmd) != 0) {
    log_error("Failed to send SIGKEY command");
    return -1;
  }

  if (read_agent_line(handle, response, sizeof(response), NULL) != 0) {
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
  if (read_agent_line(handle, response, sizeof(response), NULL) != 0) {
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
  size_t response_len = 0;
  for (int attempts = 0; attempts < 20; attempts++) {
    if (read_agent_line(handle, response, sizeof(response), &response_len) != 0) {
      log_error("Failed to read PKSIGN response");
      return -1;
    }

    log_debug("PKSIGN response line %d (%zu bytes)", attempts + 1, response_len);

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

  // Assuan escapes binary bytes as %HH. Decode before reading canonical S-expression fields.
  uint8_t decoded[GPG_AGENT_MAX_RESPONSE];
  size_t decoded_len = 0;
  for (size_t input = 2; input < response_len; ++input) {
    if (decoded_len == sizeof(decoded)) {
      log_error("GPG agent signature response exceeds buffer");
      return -1;
    }
    if (response[input] == '%') {
      if (response_len - input < 3 || !isxdigit((unsigned char)response[input + 1]) ||
          !isxdigit((unsigned char)response[input + 2])) {
        log_error("Invalid percent escape in GPG agent signature");
        return -1;
      }
      char hex[3] = {response[input + 1], response[input + 2], 0};
      decoded[decoded_len++] = (uint8_t)strtoul(hex, NULL, 16);
      input += 2;
    } else {
      decoded[decoded_len++] = (uint8_t)response[input];
    }
  }
  static const char prefix[] = "(7:sig-val(5:eddsa(1:r32:";
  static const char separator[] = ")(1:s32:";
  static const char suffix[] = ")))";
  const size_t prefix_len = sizeof(prefix) - 1;
  const size_t separator_len = sizeof(separator) - 1;
  const size_t expected_len = prefix_len + 32 + separator_len + 32 + sizeof(suffix) - 1;
  if (decoded_len != expected_len || memcmp(decoded, prefix, prefix_len) != 0 ||
      memcmp(decoded + prefix_len + 32, separator, separator_len) != 0 ||
      memcmp(decoded + expected_len - (sizeof(suffix) - 1), suffix, sizeof(suffix) - 1) != 0) {
    log_error("Invalid Ed25519 signature S-expression from GPG agent");
    return -1;
  }
  memcpy(signature_out, decoded + prefix_len, 32);
  memcpy(signature_out + 32, decoded + prefix_len + 32 + separator_len, 32);
  *signature_len_out = 64;

  // DEBUG: Print signature in hex
  char sig_hex[129];
  for (int i = 0; i < 64; i++) {
    safe_snprintf(sig_hex + i * 2, 3, "%02x", (unsigned char)signature_out[i]);
  }
  sig_hex[128] = '\0';
  log_debug("Extracted signature (64 bytes): %s", sig_hex);

  // Read final OK
  if (read_agent_line(handle, response, sizeof(response), &response_len) != 0) {
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
