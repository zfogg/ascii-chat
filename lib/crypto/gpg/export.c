/**
 * @file crypto/gpg/export.c
 * @ingroup crypto
 * @brief GPG public key export implementation
 */

#include "export.h"
#include "agent.h"
#include "../keys.h"
#include "common.h"
#include "util/string.h"
#include "util/validation.h"
#include "log/logging.h"
#include "platform/system.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef _WIN32
#define SAFE_POPEN _popen
#define SAFE_PCLOSE _pclose
#else
#define SAFE_POPEN popen
#define SAFE_PCLOSE pclose
#endif

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
