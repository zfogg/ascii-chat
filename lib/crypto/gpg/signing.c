/**
 * @file crypto/gpg/signing.c
 * @ingroup crypto
 * @brief GPG signing operations implementation
 */

#include "signing.h"
#include "agent.h"
#include "../keys.h"
#include "common.h"
#include "util/string.h"
#include "util/validation.h"
#include "log/logging.h"
#include "platform/system.h"
#include "platform/filesystem.h"
#include "platform/process.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

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

  char msg_path[PLATFORM_MAX_PATH_LENGTH];
  char sig_path[PLATFORM_MAX_PATH_LENGTH];
  int msg_fd = -1;
  int sig_fd = -1;
  int result = -1;

  // Create temp files using platform abstraction
  if (platform_create_temp_file(msg_path, sizeof(msg_path), "asciichat_msg", &msg_fd) != 0) {
    log_error("Failed to create temp message file");
    return -1;
  }

  if (platform_create_temp_file(sig_path, sizeof(sig_path), "asciichat_sig", &sig_fd) != 0) {
    log_error("Failed to create temp signature file");
    platform_delete_temp_file(msg_path);
    return -1;
  }

  // Close signature file descriptor (will be created by gpg)
  if (sig_fd >= 0) {
    close(sig_fd);
  }
  platform_delete_temp_file(sig_path); // Remove it so gpg can create it fresh

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
  safe_snprintf(cmd, sizeof(cmd),
                "gpg --local-user 0x%s --detach-sign --output \"%s\" \"%s\" " PLATFORM_SHELL_NULL_REDIRECT,
                escaped_key_id, sig_path, msg_path);

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
  log_debug("GPG signature created successfully (%zu bytes)", *signature_len_out);
  result = 0;

cleanup:
  if (msg_fd >= 0) {
    close(msg_fd);
  }
  platform_delete_temp_file(msg_path);
  platform_delete_temp_file(sig_path);
  return result;
}

int gpg_sign_detached_ed25519(const char *key_id, const uint8_t *message, size_t message_len,
                              uint8_t signature_out[64]) {
  log_debug("gpg_sign_detached_ed25519: Signing with key ID %s (fallback mode)", key_id);

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

  log_debug("Successfully extracted 64-byte Ed25519 signature from OpenPGP packet");

  // Debug: Log signature components
  char hex_r[65], hex_s[65];
  for (int i = 0; i < 32; i++) {
    safe_snprintf(hex_r + i * 2, 3, "%02x", signature_out[i]);
    safe_snprintf(hex_s + i * 2, 3, "%02x", signature_out[i + 32]);
  }
  hex_r[64] = hex_s[64] = '\0';
  log_debug("Signature R (first 32 bytes): %s", hex_r);
  log_debug("Signature S (last 32 bytes): %s", hex_s);

  return 0;
}

/**
 * Find GPG key ID from Ed25519 public key by searching GPG keyring
 * @param public_key 32-byte Ed25519 public key
 * @param key_id_out Output buffer for 16-char key ID (must be at least 17 bytes for null terminator)
 * @return 0 on success, -1 if key not found
 */
