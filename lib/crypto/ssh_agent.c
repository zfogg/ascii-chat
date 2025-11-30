/**
 * @file crypto/ssh_agent.c
 * @ingroup crypto
 * @brief 🔌 SSH agent protocol implementation for key authentication via ssh-agent
 */

#include "ssh_agent.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sodium.h>
#include "platform/pipe.h"

#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#include <fcntl.h>
#define SAFE_CLOSE _close
#define SAFE_UNLINK _unlink
#define SAFE_POPEN _popen
#define SAFE_PCLOSE _pclose
#else
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#define SAFE_CLOSE close
#define SAFE_UNLINK unlink
#define SAFE_POPEN popen
#define SAFE_PCLOSE pclose
#endif

// Open the SSH agent pipe/socket
static pipe_t ssh_agent_open_pipe(void) {
  const char *auth_sock = SAFE_GETENV("SSH_AUTH_SOCK");

#ifdef _WIN32
  // On Windows, use named pipe path (default or from SSH_AUTH_SOCK)
  const char *pipe_path = (auth_sock && strlen(auth_sock) > 0) ? auth_sock : "\\\\.\\pipe\\openssh-ssh-agent";
  return platform_pipe_connect(pipe_path);
#else
  // On Unix, use Unix domain socket path from SSH_AUTH_SOCK
  if (!auth_sock || strlen(auth_sock) == 0) {
    log_debug("SSH_AUTH_SOCK not set, cannot connect to ssh-agent");
    return INVALID_PIPE_VALUE;
  }
  return platform_pipe_connect(auth_sock);
#endif
}

bool ssh_agent_is_available(void) {
  // Check if SSH_AUTH_SOCK environment variable is set
  const char *auth_sock = SAFE_GETENV("SSH_AUTH_SOCK");

#ifdef _WIN32
  // On Windows, if SSH_AUTH_SOCK is not set, try to open the Windows named pipe to check availability
  if (!auth_sock || strlen(auth_sock) == 0) {
    pipe_t pipe = ssh_agent_open_pipe();
    if (pipe != INVALID_PIPE_VALUE) {
      platform_pipe_close(pipe); // Close immediately after checking
      log_debug("ssh-agent is available via Windows named pipe (SSH_AUTH_SOCK not set)");
      return true;
    } else {
      log_debug("ssh-agent not available: SSH_AUTH_SOCK not set and Windows named pipe not accessible");
      return false;
    }
  }

  // SSH_AUTH_SOCK is set on Windows
  log_debug("ssh-agent appears available (SSH_AUTH_SOCK=%s)", auth_sock);
  return true;
#else
  // Unix: SSH_AUTH_SOCK is required
  if (!auth_sock || strlen(auth_sock) == 0) {
    log_debug("ssh-agent not available: SSH_AUTH_SOCK not set");
    return false;
  }

  // Check if Unix socket exists and is accessible
  if (access(auth_sock, W_OK) != 0) {
    log_debug("ssh-agent not available: cannot access socket at %s", auth_sock);
    return false;
  }
  log_debug("ssh-agent is available at %s", auth_sock);
  return true;
#endif
}

bool ssh_agent_has_key(const public_key_t *public_key) {
  // Use SSH agent protocol to list keys (works on both Windows and Unix)
  pipe_t pipe = ssh_agent_open_pipe();
  if (pipe == INVALID_PIPE_VALUE) {
    return false;
  }

  // Build SSH2_AGENTC_REQUEST_IDENTITIES message (type 11)
  unsigned char request[5];
  request[0] = 0; // length: 1 (4-byte big-endian)
  request[1] = 0;
  request[2] = 0;
  request[3] = 1;
  request[4] = 11; // SSH2_AGENTC_REQUEST_IDENTITIES

  // Send request
  ssize_t bytes_written = platform_pipe_write(pipe, request, 5);
  if (bytes_written != 5) {
    platform_pipe_close(pipe);
    return false;
  }

  // Read response
  unsigned char response[BUFFER_SIZE_XXXLARGE];
  ssize_t bytes_read = platform_pipe_read(pipe, response, sizeof(response));
  if (bytes_read < 9) {
    platform_pipe_close(pipe);
    return false;
  }

  platform_pipe_close(pipe);

  // Parse response: type should be SSH2_AGENT_IDENTITIES_ANSWER (12)
  uint8_t resp_type = response[4];
  if (resp_type != 12) {
    return false;
  }

  // Number of keys at bytes 5-8
  uint32_t num_keys = (response[5] << 24) | (response[6] << 16) | (response[7] << 8) | response[8];

  // Parse keys and check if our public key matches
  size_t pos = 9;
  for (uint32_t i = 0; i < num_keys && pos + 4 < (size_t)bytes_read; i++) {
    // Read key blob length
    uint32_t blob_len =
        (response[pos] << 24) | (response[pos + 1] << 16) | (response[pos + 2] << 8) | response[pos + 3];
    pos += 4;

    if (pos + blob_len > (size_t)bytes_read)
      break;

    // Parse the blob to extract the Ed25519 public key
    size_t blob_pos = pos;
    // Skip key type string
    if (blob_pos + 4 > pos + blob_len) {
      pos += blob_len;
      continue;
    }
    uint32_t type_len = (response[blob_pos] << 24) | (response[blob_pos + 1] << 16) | (response[blob_pos + 2] << 8) |
                        response[blob_pos + 3];
    blob_pos += 4 + type_len;

    // Read public key data
    if (blob_pos + 4 > pos + blob_len) {
      pos += blob_len;
      continue;
    }
    uint32_t pubkey_len = (response[blob_pos] << 24) | (response[blob_pos + 1] << 16) | (response[blob_pos + 2] << 8) |
                          response[blob_pos + 3];
    blob_pos += 4;

    // Compare public key (should be 32 bytes for Ed25519)
    if (pubkey_len == 32 && blob_pos + 32 <= pos + blob_len) {
      if (memcmp(response + blob_pos, public_key->key, 32) == 0) {
        log_debug("Found matching key in ssh-agent");
        return true;
      }
    }

    pos += blob_len;

    // Skip comment string length + comment
    if (pos + 4 > (size_t)bytes_read)
      break;
    uint32_t comment_len =
        (response[pos] << 24) | (response[pos + 1] << 16) | (response[pos + 2] << 8) | response[pos + 3];
    pos += 4 + comment_len;
  }

  return false;
}

asciichat_error_t ssh_agent_add_key(const private_key_t *private_key, const char *key_path) {
  if (private_key == NULL) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Cannot add key to ssh-agent: private_key is NULL");
  }
  if (private_key->type != KEY_TYPE_ED25519) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Cannot add key to ssh-agent: only Ed25519 keys supported");
  }

  log_info("Adding key to ssh-agent: %s", key_path ? key_path : "(memory)");

  // Open the pipe/socket for this operation (works on both Windows and Unix)
  pipe_t pipe = ssh_agent_open_pipe();
  if (pipe == INVALID_PIPE_VALUE) {
    return SET_ERRNO(ERROR_CRYPTO, "Failed to connect to ssh-agent");
  }

  // Build SSH agent protocol message: SSH2_AGENTC_ADD_IDENTITY (17)
  // Message format:
  //   uint32: message length
  //   byte:   SSH2_AGENTC_ADD_IDENTITY (17)
  //   string: key type ("ssh-ed25519")
  //   string: public key (32 bytes)
  //   string: private key (64 bytes)
  //   string: comment (key path or empty)

  unsigned char buf[BUFFER_SIZE_XXLARGE];
  size_t pos = 4; // Reserve space for length prefix

  // Message type: SSH2_AGENTC_ADD_IDENTITY
  buf[pos++] = 17;

  // Key type: "ssh-ed25519" (11 bytes)
  uint32_t len = 11;
  buf[pos++] = (len >> 24) & 0xFF;
  buf[pos++] = (len >> 16) & 0xFF;
  buf[pos++] = (len >> 8) & 0xFF;
  buf[pos++] = len & 0xFF;
  // NOLINTNEXTLINE(bugprone-not-null-terminated-result) - Binary protocol, intentionally not null-terminated
  memcpy(buf + pos, "ssh-ed25519", 11);
  pos += 11;

  // Public key (32 bytes) - last 32 bytes of the 64-byte ed25519 key
  len = 32;
  buf[pos++] = (len >> 24) & 0xFF;
  buf[pos++] = (len >> 16) & 0xFF;
  buf[pos++] = (len >> 8) & 0xFF;
  buf[pos++] = len & 0xFF;
  memcpy(buf + pos, private_key->key.ed25519 + 32, 32); // Public key is second half
  pos += 32;

  // Private key (64 bytes - full ed25519 key: 32-byte seed + 32-byte public)
  len = 64;
  buf[pos++] = (len >> 24) & 0xFF;
  buf[pos++] = (len >> 16) & 0xFF;
  buf[pos++] = (len >> 8) & 0xFF;
  buf[pos++] = len & 0xFF;
  memcpy(buf + pos, private_key->key.ed25519, 64);
  pos += 64;

  // Comment (key path)
  len = key_path ? strlen(key_path) : 0;
  buf[pos++] = (len >> 24) & 0xFF;
  buf[pos++] = (len >> 16) & 0xFF;
  buf[pos++] = (len >> 8) & 0xFF;
  buf[pos++] = len & 0xFF;
  if (len > 0) {
    memcpy(buf + pos, key_path, len);
    pos += len;
  }

  // Write message length at start (excluding the 4-byte length field itself)
  uint32_t msg_len = pos - 4;
  buf[0] = (msg_len >> 24) & 0xFF;
  buf[1] = (msg_len >> 16) & 0xFF;
  buf[2] = (msg_len >> 8) & 0xFF;
  buf[3] = msg_len & 0xFF;

  // Send message to agent
  ssize_t bytes_written = platform_pipe_write(pipe, buf, pos);
  if (bytes_written != (ssize_t)pos) {
    platform_pipe_close(pipe);
    sodium_memzero(buf, sizeof(buf));
    return SET_ERRNO_SYS(ERROR_CRYPTO, "Failed to write to ssh-agent pipe");
  }

  // Read response
  unsigned char response[BUFFER_SIZE_SMALL];
  ssize_t bytes_read = platform_pipe_read(pipe, response, sizeof(response));
  if (bytes_read < 5) {
    platform_pipe_close(pipe);
    sodium_memzero(buf, sizeof(buf));
    return SET_ERRNO_SYS(ERROR_CRYPTO, "Failed to read from ssh-agent pipe");
  }

  // Done with the pipe - close it
  platform_pipe_close(pipe);
  sodium_memzero(buf, sizeof(buf));

  // Check response: should be SSH_AGENT_SUCCESS (6)
  // Response format: uint32 length, byte message_type
  uint8_t response_type = response[4];
  if (response_type == 6) {
    log_info("Successfully added key to ssh-agent");
    return ASCIICHAT_OK;
  } else if (response_type == 5) {
    return SET_ERRNO(ERROR_CRYPTO, "ssh-agent rejected key (SSH_AGENT_FAILURE)");
  } else {
    return SET_ERRNO(ERROR_CRYPTO, "ssh-agent returned unexpected response: %d", response_type);
  }
}
