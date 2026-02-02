/**
 * @file crypto/ssh_agent.c
 * @ingroup crypto
 * @brief 🔌 SSH agent protocol implementation for key authentication via ssh-agent
 */

#include <ascii-chat/crypto/ssh/ssh_agent.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/bytes.h> // For write_u32_be, read_u32_be
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sodium.h>
#include <ascii-chat/platform/pipe.h>
#include <ascii-chat/platform/agent.h>
#include <ascii-chat/log/logging.h>

#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#include <fcntl.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

// Open the SSH agent pipe/socket
static pipe_t ssh_agent_open_pipe(void) {
  char pipe_path[256];
  if (platform_get_ssh_agent_socket(pipe_path, sizeof(pipe_path)) != 0) {
    log_debug("Failed to get SSH agent socket path");
    return INVALID_PIPE_VALUE;
  }
  return platform_pipe_connect(pipe_path);
}

bool ssh_agent_is_available(void) {
  /* Try to open the SSH agent connection */
  pipe_t pipe = ssh_agent_open_pipe();
  if (pipe != INVALID_PIPE_VALUE) {
    platform_pipe_close(pipe);
    log_debug("ssh-agent is available");
    return true;
  }
  log_debug("ssh-agent not available");
  return false;
}

bool ssh_agent_has_key(const public_key_t *public_key) {
  if (public_key == NULL) {
    log_warn("NULL is not a valid public key");
    return false;
  }

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
  uint32_t num_keys = read_u32_be(response + 5);

  // Parse keys and check if our public key matches
  size_t pos = 9;
  for (uint32_t i = 0; i < num_keys && pos + 4 < (size_t)bytes_read; i++) {
    // Read key blob length
    uint32_t blob_len = read_u32_be(response + pos);
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
    uint32_t type_len = read_u32_be(response + blob_pos);
    blob_pos += 4 + type_len;

    // Read public key data
    if (blob_pos + 4 > pos + blob_len) {
      pos += blob_len;
      continue;
    }
    uint32_t pubkey_len = read_u32_be(response + blob_pos);
    blob_pos += 4;

    // Compare public key (should be 32 bytes for Ed25519)
    // Use constant-time comparison to prevent timing side channels
    if (pubkey_len == 32 && blob_pos + 32 <= pos + blob_len) {
      if (sodium_memcmp(response + blob_pos, public_key->key, 32) == 0) {
        log_debug("Found matching key in ssh-agent");
        return true;
      }
    }

    pos += blob_len;

    // Skip comment string length + comment
    if (pos + 4 > (size_t)bytes_read)
      break;
    uint32_t comment_len = read_u32_be(response + pos);
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

  log_debug("Adding key to ssh-agent: %s", key_path ? key_path : "(memory)");

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
  write_u32_be(buf + pos, len);
  pos += 4;
  // Binary protocol: intentionally not null-terminated
  memcpy(buf + pos, "ssh-ed25519", 11);
  pos += 11;

  // Public key (32 bytes) - last 32 bytes of the 64-byte ed25519 key
  len = 32;
  write_u32_be(buf + pos, len);
  pos += 4;
  memcpy(buf + pos, private_key->key.ed25519 + 32, 32); // Public key is second half
  pos += 32;

  // Private key (64 bytes - full ed25519 key: 32-byte seed + 32-byte public)
  len = 64;
  write_u32_be(buf + pos, len);
  pos += 4;
  memcpy(buf + pos, private_key->key.ed25519, 64);
  pos += 64;

  // Comment (key path)
  len = key_path ? strlen(key_path) : 0;

  // SECURITY: Validate key path length to prevent buffer overflow
  // Buffer is BUFFER_SIZE_XXLARGE (4096), pos is ~128 at this point, need 4 bytes for length prefix
  size_t max_key_path_len = sizeof(buf) - pos - 4;
  if (len > max_key_path_len) {
    platform_pipe_close(pipe);
    sodium_memzero(buf, sizeof(buf));
    return SET_ERRNO(ERROR_BUFFER_OVERFLOW, "SSH key path too long: %u bytes (max %zu)", len, max_key_path_len);
  }

  write_u32_be(buf + pos, len);
  pos += 4;
  if (len > 0) {
    memcpy(buf + pos, key_path, len);
    pos += len;
  }

  // Write message length at start (excluding the 4-byte length field itself)
  uint32_t msg_len = pos - 4;
  write_u32_be(buf, msg_len);

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
    log_debug("Successfully added key to ssh-agent");
    return ASCIICHAT_OK;
  } else if (response_type == 5) {
    return SET_ERRNO(ERROR_CRYPTO, "ssh-agent rejected key (SSH_AGENT_FAILURE)");
  } else {
    return SET_ERRNO(ERROR_CRYPTO, "ssh-agent returned unexpected response: %d", response_type);
  }
}

asciichat_error_t ssh_agent_sign(const public_key_t *public_key, const uint8_t *message, size_t message_len,
                                 uint8_t signature[64]) {
  if (!public_key || !message || !signature) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: public_key=%p, message=%p, signature=%p", public_key,
                     message, signature);
  }

  if (public_key->type != KEY_TYPE_ED25519) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Only Ed25519 keys are supported for SSH agent signing");
  }

  // Connect to SSH agent
  pipe_t pipe = ssh_agent_open_pipe();
  if (pipe == INVALID_PIPE_VALUE) {
    return SET_ERRNO(ERROR_CRYPTO, "Cannot connect to ssh-agent");
  }

  // Build SSH2_AGENTC_SIGN_REQUEST message (type 13)
  // Format: uint32 length, byte type, string key_blob, string data, uint32 flags
  // For Ed25519, key_blob is: string "ssh-ed25519", string public_key(32 bytes)

  const char *key_type = "ssh-ed25519";
  uint32_t key_type_len = (uint32_t)strlen(key_type);

  // Calculate total message length
  // 1 (type) + 4 (key_blob_len) + key_blob_size + 4 (data_len) + data_size + 4 (flags)
  uint32_t key_blob_size = 4 + key_type_len + 4 + 32; // string(key_type) + string(pubkey)
  uint32_t total_len = 1 + 4 + key_blob_size + 4 + message_len + 4;

  uint8_t *buf = SAFE_MALLOC(total_len + 4, uint8_t *); // +4 for length prefix
  if (!buf) {
    platform_pipe_close(pipe);
    return SET_ERRNO(ERROR_CRYPTO, "Out of memory for SSH agent sign request");
  }

  uint32_t offset = 0;

  // Write total message length (excluding this 4-byte length field)
  write_u32_be(buf + offset, total_len);
  offset += 4;

  // Write message type (13 = SSH2_AGENTC_SIGN_REQUEST)
  buf[offset++] = 13;

  // Write key_blob length
  write_u32_be(buf + offset, key_blob_size);
  offset += 4;

  // Write key_blob: string(key_type)
  write_u32_be(buf + offset, key_type_len);
  offset += 4;
  memcpy(buf + offset, key_type, key_type_len);
  offset += key_type_len;

  // Write key_blob: string(public_key)
  write_u32_be(buf + offset, 32);
  offset += 4;
  memcpy(buf + offset, public_key->key, 32);
  offset += 32;

  // Write data to sign
  write_u32_be(buf + offset, (uint32_t)message_len);
  offset += 4;
  memcpy(buf + offset, message, message_len);
  offset += (uint32_t)message_len;

  // Write flags (0 = default)
  write_u32_be(buf + offset, 0);
  offset += 4;

  // Send request
  ssize_t written = platform_pipe_write(pipe, buf, total_len + 4);
  sodium_memzero(buf, total_len + 4);
  SAFE_FREE(buf);

  if (written < 0 || (size_t)written != total_len + 4) {
    platform_pipe_close(pipe);
    return SET_ERRNO(ERROR_CRYPTO, "Failed to write SSH agent sign request");
  }

  // Read response
  uint8_t response[BUFFER_SIZE_XXLARGE];
  ssize_t read_bytes = platform_pipe_read(pipe, response, sizeof(response));
  platform_pipe_close(pipe);

  if (read_bytes < 5) {
    return SET_ERRNO(ERROR_CRYPTO, "Failed to read SSH agent sign response (read %zd bytes)", read_bytes);
  }

  // Response format: uint32 length, byte type, data...
  // We validate length implicitly by checking read_bytes and parsing the full response
  (void)read_u32_be(response); // Read but don't need explicit length check
  uint8_t response_type = response[4];

  // Check for SSH2_AGENT_SIGN_RESPONSE (14)
  if (response_type != 14) {
    if (response_type == 5) {
      return SET_ERRNO(ERROR_CRYPTO, "ssh-agent refused to sign (SSH_AGENT_FAILURE)");
    }
    return SET_ERRNO(ERROR_CRYPTO, "ssh-agent returned unexpected response type: %d (expected 14)", response_type);
  }

  // Parse signature blob
  // Response format: uint32 len, byte type(14), string signature_blob
  if (read_bytes < 9) {
    return SET_ERRNO(ERROR_CRYPTO, "SSH agent response too short (no signature blob length)");
  }

  uint32_t sig_blob_len = read_u32_be(response + 5);
  uint32_t expected_total = 4 + 1 + 4 + sig_blob_len;

  if ((size_t)read_bytes < expected_total) {
    return SET_ERRNO(ERROR_CRYPTO, "SSH agent response truncated (expected %u bytes, got %zd)", expected_total,
                     read_bytes);
  }

  // Signature blob format for Ed25519: string "ssh-ed25519", string signature(64 bytes)
  uint32_t offset_sig = 9;
  uint32_t sig_type_len = read_u32_be(response + offset_sig);
  offset_sig += 4;

  if (offset_sig + sig_type_len + 4 > (uint32_t)read_bytes) {
    return SET_ERRNO(ERROR_CRYPTO, "SSH agent signature blob truncated at signature type");
  }

  // Verify signature type is "ssh-ed25519"
  if (sig_type_len != 11 || memcmp(response + offset_sig, "ssh-ed25519", 11) != 0) {
    return SET_ERRNO(ERROR_CRYPTO, "SSH agent returned non-Ed25519 signature");
  }
  offset_sig += sig_type_len;

  // Read signature bytes
  uint32_t sig_len = read_u32_be(response + offset_sig);
  offset_sig += 4;

  if (sig_len != 64) {
    return SET_ERRNO(ERROR_CRYPTO, "SSH agent returned invalid Ed25519 signature length: %u (expected 64)", sig_len);
  }

  if (offset_sig + 64 > (uint32_t)read_bytes) {
    return SET_ERRNO(ERROR_CRYPTO, "SSH agent signature blob truncated at signature bytes");
  }

  // Copy signature to output
  memcpy(signature, response + offset_sig, 64);

  log_debug("SSH agent successfully signed %zu bytes with Ed25519 key", message_len);
  return ASCIICHAT_OK;
}
