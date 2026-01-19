/**
 * @file crypto/gpg/openpgp.c
 * @brief OpenPGP (RFC 4880) packet format parser implementation
 * @ingroup crypto
 */

#include "openpgp.h"
#include "../../common.h"
#include "../../asciichat_errno.h"
#include "../../log/logging.h"
#include "../../platform/question.h"
#include <string.h>
#include <stdlib.h>
#include <sodium.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

// =============================================================================
// Base64 Decoding for PGP Armor
// =============================================================================

asciichat_error_t openpgp_base64_decode(const char *base64, size_t base64_len, uint8_t **binary_out,
                                        size_t *binary_len) {
  if (!base64 || !binary_out || !binary_len) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for base64 decode");
  }

  // Remove whitespace from base64 input (PGP armor has newlines)
  char *clean_base64 = SAFE_MALLOC(base64_len + 1, char *);
  char *clean_ptr = clean_base64;
  for (size_t i = 0; i < base64_len; i++) {
    if (base64[i] != '\n' && base64[i] != '\r' && base64[i] != ' ' && base64[i] != '\t') {
      *clean_ptr++ = base64[i];
    }
  }
  *clean_ptr = '\0';
  size_t clean_len = (size_t)(clean_ptr - clean_base64);

  // Allocate max possible output size
  *binary_out = SAFE_MALLOC(clean_len, uint8_t *);

  const char *end;
  int result = sodium_base642bin(*binary_out, clean_len, clean_base64, clean_len, NULL, binary_len, &end,
                                 sodium_base64_VARIANT_ORIGINAL);

  SAFE_FREE(clean_base64);

  if (result != 0) {
    SAFE_FREE(*binary_out);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to decode base64 PGP armored data");
  }

  return ASCIICHAT_OK;
}

// =============================================================================
// OpenPGP Packet Header Parsing
// =============================================================================

asciichat_error_t openpgp_parse_packet_header(const uint8_t *data, size_t data_len, openpgp_packet_header_t *header) {
  if (!data || !header || data_len == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for packet header parsing");
  }

  memset(header, 0, sizeof(openpgp_packet_header_t));

  uint8_t ctb = data[0]; // Cipher Type Byte

  // Check if bit 7 is set (all packets must have bit 7 = 1)
  if ((ctb & 0x80) == 0) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Invalid OpenPGP packet: bit 7 not set in CTB");
  }

  // Check if new format (bit 6 = 1) or old format (bit 6 = 0)
  if (ctb & 0x40) {
    // New format: bits 5-0 = tag
    header->new_format = true;
    header->tag = ctb & 0x3F;

    if (data_len < 2) {
      return SET_ERRNO(ERROR_CRYPTO_KEY, "Insufficient data for new format packet header");
    }

    uint8_t len_byte = data[1];

    if (len_byte < 192) {
      // One-octet length
      header->length = len_byte;
      header->header_len = 2;
    } else if (len_byte < 224) {
      // Two-octet length
      if (data_len < 3) {
        return SET_ERRNO(ERROR_CRYPTO_KEY, "Insufficient data for two-octet length");
      }
      header->length = ((len_byte - 192) << 8) + data[2] + 192;
      header->header_len = 3;
    } else if (len_byte == 255) {
      // Five-octet length
      if (data_len < 6) {
        return SET_ERRNO(ERROR_CRYPTO_KEY, "Insufficient data for five-octet length");
      }
      header->length = ((size_t)data[2] << 24) | ((size_t)data[3] << 16) | ((size_t)data[4] << 8) | data[5];
      header->header_len = 6;
    } else {
      // Partial body length (not supported for our use case)
      return SET_ERRNO(ERROR_CRYPTO_KEY, "Partial body length not supported");
    }
  } else {
    // Old format: bits 5-2 = tag, bits 1-0 = length type
    header->new_format = false;
    header->tag = (ctb >> 2) & 0x0F;
    uint8_t length_type = ctb & 0x03;

    switch (length_type) {
    case 0: // One-octet length
      if (data_len < 2) {
        return SET_ERRNO(ERROR_CRYPTO_KEY, "Insufficient data for one-octet length");
      }
      header->length = data[1];
      header->header_len = 2;
      break;

    case 1: // Two-octet length
      if (data_len < 3) {
        return SET_ERRNO(ERROR_CRYPTO_KEY, "Insufficient data for two-octet length");
      }
      header->length = ((size_t)data[1] << 8) | data[2];
      header->header_len = 3;
      break;

    case 2: // Four-octet length
      if (data_len < 5) {
        return SET_ERRNO(ERROR_CRYPTO_KEY, "Insufficient data for four-octet length");
      }
      header->length = ((size_t)data[1] << 24) | ((size_t)data[2] << 16) | ((size_t)data[3] << 8) | data[4];
      header->header_len = 5;
      break;

    case 3: // Indeterminate length (not supported)
      return SET_ERRNO(ERROR_CRYPTO_KEY, "Indeterminate length not supported");

    default:
      return SET_ERRNO(ERROR_CRYPTO_KEY, "Invalid length type: %u", length_type);
    }
  }

  log_debug("OpenPGP packet: tag=%u, length=%zu, header_len=%zu, new_format=%d", header->tag, header->length,
            header->header_len, header->new_format);

  return ASCIICHAT_OK;
}

// =============================================================================
// MPI (Multi-Precision Integer) Parsing
// =============================================================================

asciichat_error_t openpgp_extract_ed25519_from_mpi(const uint8_t *mpi, size_t mpi_len, uint8_t ed25519_pk[32]) {
  if (!mpi || !ed25519_pk || mpi_len < 35) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for MPI extraction (need at least 35 bytes)");
  }

  // MPI format:
  // - 2 bytes: bit count (big-endian)
  // - 1 byte: 0x40 prefix (Ed25519 marker)
  // - 32 bytes: Ed25519 public key

  uint16_t bit_count = ((uint16_t)mpi[0] << 8) | mpi[1];
  log_debug("MPI bit count: %u", bit_count);

  // Ed25519 with 0x40 prefix is typically 263 bits (0x0107)
  if (bit_count < 256 || bit_count > 270) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Unexpected MPI bit count for Ed25519: %u (expected ~263)", bit_count);
  }

  // Check for 0x40 prefix byte
  if (mpi[2] != 0x40) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Missing 0x40 prefix byte in Ed25519 MPI (found 0x%02x)", mpi[2]);
  }

  // Extract 32-byte Ed25519 public key
  memcpy(ed25519_pk, mpi + 3, 32);

  return ASCIICHAT_OK;
}

// =============================================================================
// Public Key Packet Parsing
// =============================================================================

asciichat_error_t openpgp_parse_public_key_packet(const uint8_t *packet_body, size_t body_len,
                                                  openpgp_public_key_t *pubkey) {
  if (!packet_body || !pubkey || body_len < 6) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for public key packet parsing");
  }

  memset(pubkey, 0, sizeof(openpgp_public_key_t));

  size_t offset = 0;

  // Version (1 byte, must be 4)
  pubkey->version = packet_body[offset++];
  if (pubkey->version != 4) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Unsupported OpenPGP public key version: %u (only version 4 supported)",
                     pubkey->version);
  }

  // Creation time (4 bytes, big-endian Unix timestamp)
  pubkey->created = ((uint32_t)packet_body[offset] << 24) | ((uint32_t)packet_body[offset + 1] << 16) |
                    ((uint32_t)packet_body[offset + 2] << 8) | packet_body[offset + 3];
  offset += 4;

  // Algorithm (1 byte)
  pubkey->algorithm = packet_body[offset++];

  log_debug("Public key packet: version=%u, created=%u, algorithm=%u", pubkey->version, pubkey->created,
            pubkey->algorithm);

  // Only support EdDSA (algorithm 22)
  if (pubkey->algorithm != OPENPGP_ALGO_EDDSA) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Unsupported public key algorithm: %u (only EdDSA/22 supported)",
                     pubkey->algorithm);
  }

  // EdDSA (Ed25519) keys have a special encoding:
  // - OID for the curve (variable length)
  // - 0x40 prefix byte
  // - 32 bytes of Ed25519 public key
  //
  // We search for the 0x40 prefix and extract the following 32 bytes

  // Search for 0x40 prefix byte in the remaining packet data
  bool found_prefix = false;
  size_t key_offset = 0;

  for (size_t i = offset; i < body_len - 32; i++) {
    if (packet_body[i] == 0x40) {
      key_offset = i + 1; // Point to first byte after 0x40
      found_prefix = true;
      log_debug("Found Ed25519 key prefix 0x40 at offset %zu", i);
      break;
    }
  }

  if (!found_prefix) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Ed25519 public key prefix (0x40) not found in packet");
  }

  if (key_offset + 32 > body_len) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Insufficient data for Ed25519 public key (need 32 bytes after 0x40 prefix)");
  }

  // Extract the 32-byte Ed25519 public key
  memcpy(pubkey->pubkey, packet_body + key_offset, 32);

  log_debug("Extracted Ed25519 public key (first 8 bytes): %02x%02x%02x%02x%02x%02x%02x%02x", pubkey->pubkey[0],
            pubkey->pubkey[1], pubkey->pubkey[2], pubkey->pubkey[3], pubkey->pubkey[4], pubkey->pubkey[5],
            pubkey->pubkey[6], pubkey->pubkey[7]);

  // Calculate Key ID (last 8 bytes of SHA-1 fingerprint)
  // For now, we'll skip fingerprint calculation and just extract from packet if available
  // The keyid is typically at a fixed offset for Ed25519 keys
  // We'll compute it properly by hashing the public key material

  // For version 4 keys, fingerprint = SHA-1(0x99 || length || packet_body)
  // Key ID = last 8 bytes of fingerprint
  // For simplicity, we'll set keyid to 0 for now (not critical for our use case)
  pubkey->keyid = 0;

  log_debug("Extracted Ed25519 public key (first 8 bytes): %02x%02x%02x%02x%02x%02x%02x%02x", pubkey->pubkey[0],
            pubkey->pubkey[1], pubkey->pubkey[2], pubkey->pubkey[3], pubkey->pubkey[4], pubkey->pubkey[5],
            pubkey->pubkey[6], pubkey->pubkey[7]);

  return ASCIICHAT_OK;
}

// =============================================================================
// PGP Armored Format Parsing
// =============================================================================

asciichat_error_t openpgp_parse_armored_pubkey(const char *armored_text, uint8_t ed25519_pk[32]) {
  if (!armored_text || !ed25519_pk) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for armored pubkey parsing");
  }

  // Find the BEGIN marker
  const char *begin = strstr(armored_text, "-----BEGIN PGP PUBLIC KEY BLOCK-----");
  if (!begin) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Missing PGP PUBLIC KEY BLOCK BEGIN marker");
  }

  // Skip to the end of the BEGIN line
  const char *base64_start = strchr(begin, '\n');
  if (!base64_start) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Invalid PGP armored format: no newline after BEGIN marker");
  }
  base64_start++; // Skip the newline

  // Find the END marker
  const char *end = strstr(base64_start, "-----END PGP PUBLIC KEY BLOCK-----");
  if (!end) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Missing PGP PUBLIC KEY BLOCK END marker");
  }

  // Find the checksum line (starts with '=') and exclude it
  const char *base64_end = end;
  const char *checksum = base64_end;
  while (checksum > base64_start && *checksum != '=') {
    checksum--;
  }
  if (*checksum == '=') {
    // Move back to before the checksum line
    while (checksum > base64_start && (checksum[-1] == '\n' || checksum[-1] == '\r')) {
      checksum--;
    }
    base64_end = checksum;
  }

  size_t base64_len = (size_t)(base64_end - base64_start);

  log_debug("Extracting base64 data from PGP armor (%zu bytes)", base64_len);

  // Decode base64 to binary OpenPGP packets
  uint8_t *binary_data;
  size_t binary_len;
  asciichat_error_t decode_result = openpgp_base64_decode(base64_start, base64_len, &binary_data, &binary_len);
  if (decode_result != ASCIICHAT_OK) {
    return decode_result;
  }

  log_debug("Decoded %zu bytes of OpenPGP packet data", binary_len);

  // Parse OpenPGP packets to find the public key packet (tag 6)
  size_t offset = 0;
  bool found_pubkey = false;

  while (offset < binary_len) {
    openpgp_packet_header_t header;
    asciichat_error_t header_result = openpgp_parse_packet_header(binary_data + offset, binary_len - offset, &header);
    if (header_result != ASCIICHAT_OK) {
      SAFE_FREE(binary_data);
      return header_result;
    }

    log_debug("Packet at offset %zu: tag=%u, length=%zu", offset, header.tag, header.length);

    // Check if this is a public key packet (tag 6)
    if (header.tag == OPENPGP_TAG_PUBLIC_KEY) {
      openpgp_public_key_t pubkey;
      asciichat_error_t parse_result =
          openpgp_parse_public_key_packet(binary_data + offset + header.header_len, header.length, &pubkey);

      if (parse_result == ASCIICHAT_OK) {
        memcpy(ed25519_pk, pubkey.pubkey, 32);
        found_pubkey = true;
        log_debug("Extracted Ed25519 public key from OpenPGP armored block");
        break;
      } else {
        // Not an Ed25519 key, try next packet
        log_debug("Skipping non-Ed25519 public key packet");
      }
    }

    // Move to next packet
    offset += header.header_len + header.length;
  }

  SAFE_FREE(binary_data);

  if (!found_pubkey) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "No Ed25519 public key found in PGP armored block");
  }

  return ASCIICHAT_OK;
}

// =============================================================================
// Secret Key Packet Parsing
// =============================================================================

asciichat_error_t openpgp_parse_secret_key_packet(const uint8_t *packet_body, size_t body_len,
                                                  openpgp_secret_key_t *seckey) {
  if (!packet_body || !seckey || body_len < 6) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for secret key packet parsing");
  }

  memset(seckey, 0, sizeof(openpgp_secret_key_t));

  size_t offset = 0;

  // Parse public key portion (same as public key packet)
  // Version (1 byte, must be 4)
  seckey->version = packet_body[offset++];
  if (seckey->version != 4) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Unsupported OpenPGP secret key version: %u (only version 4 supported)",
                     seckey->version);
  }

  // Creation time (4 bytes, big-endian Unix timestamp)
  seckey->created = ((uint32_t)packet_body[offset] << 24) | ((uint32_t)packet_body[offset + 1] << 16) |
                    ((uint32_t)packet_body[offset + 2] << 8) | packet_body[offset + 3];
  offset += 4;

  // Algorithm (1 byte)
  seckey->algorithm = packet_body[offset++];

  log_debug("Secret key packet: version=%u, created=%u, algorithm=%u", seckey->version, seckey->created,
            seckey->algorithm);

  // Only support EdDSA (algorithm 22)
  if (seckey->algorithm != OPENPGP_ALGO_EDDSA) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Unsupported secret key algorithm: %u (only EdDSA/22 supported)",
                     seckey->algorithm);
  }

  // EdDSA public key: OID + 0x40 prefix + 32 bytes of Ed25519 public key
  // Search for 0x40 prefix byte
  bool found_prefix = false;
  size_t pubkey_offset = 0;

  for (size_t i = offset; i < body_len - 32; i++) {
    if (packet_body[i] == 0x40) {
      pubkey_offset = i + 1;
      found_prefix = true;
      log_debug("Found Ed25519 public key prefix 0x40 at offset %zu", i);
      break;
    }
  }

  if (!found_prefix) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Ed25519 public key prefix (0x40) not found in secret key packet");
  }

  if (pubkey_offset + 32 > body_len) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Insufficient data for Ed25519 public key (need 32 bytes after 0x40 prefix)");
  }

  // Extract the 32-byte Ed25519 public key
  memcpy(seckey->pubkey, packet_body + pubkey_offset, 32);

  log_debug("Extracted Ed25519 public key (first 8 bytes): %02x%02x%02x%02x%02x%02x%02x%02x", seckey->pubkey[0],
            seckey->pubkey[1], seckey->pubkey[2], seckey->pubkey[3], seckey->pubkey[4], seckey->pubkey[5],
            seckey->pubkey[6], seckey->pubkey[7]);

  // Move offset past public key material
  offset = pubkey_offset + 32;

  // S2K usage byte (1 byte)
  // 0x00 = secret key is not encrypted
  // 0xFE or 0xFF = secret key is encrypted with S2K
  if (offset >= body_len) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Missing S2K usage byte in secret key packet");
  }

  uint8_t s2k_usage = packet_body[offset++];
  log_debug("S2K usage byte: 0x%02x", s2k_usage);

  if (s2k_usage != 0x00) {
    seckey->is_encrypted = true;
    log_debug("Detected encrypted secret key (S2K usage = 0x%02x)", s2k_usage);
    // Don't parse encrypted key material here - caller will need to decrypt with gpg
    return ASCIICHAT_OK;
  }

  seckey->is_encrypted = false;

  // For unencrypted keys (S2K usage = 0x00), secret key material follows directly
  // For Ed25519: 32 bytes of secret key
  if (offset + 32 > body_len) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Insufficient data for Ed25519 secret key (need 32 bytes)");
  }

  // Extract the 32-byte Ed25519 secret key
  memcpy(seckey->seckey, packet_body + offset, 32);

  log_debug("Extracted Ed25519 secret key (first 8 bytes): %02x%02x%02x%02x%02x%02x%02x%02x", seckey->seckey[0],
            seckey->seckey[1], seckey->seckey[2], seckey->seckey[3], seckey->seckey[4], seckey->seckey[5],
            seckey->seckey[6], seckey->seckey[7]);

  return ASCIICHAT_OK;
}

/**
 * @brief Decrypt GPG armored secret key using gpg binary
 * @param armored_text Encrypted GPG armored text
 * @param decrypted_out Output buffer for decrypted armored text (caller must free)
 * @return ASCIICHAT_OK on success, error code on failure
 */
static asciichat_error_t openpgp_decrypt_with_gpg(const char *armored_text, char **decrypted_out) {
  if (!armored_text || !decrypted_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for GPG decryption");
  }

  // Get passphrase from environment variable or interactive prompt
  const char *passphrase = SAFE_GETENV("ASCII_CHAT_KEY_PASSWORD");
  char passphrase_buffer[512] = {0};

  if (!passphrase) {
    // No environment variable - try interactive prompt
    if (platform_is_interactive()) {
      log_info("GPG key is encrypted - prompting for passphrase");
      if (platform_prompt_question("Enter passphrase for GPG key", passphrase_buffer, sizeof(passphrase_buffer),
                                   PROMPT_OPTS_PASSWORD) != 0) {
        return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to read passphrase for encrypted GPG key");
      }
      passphrase = passphrase_buffer;
    } else {
      return SET_ERRNO(ERROR_CRYPTO_KEY,
                       "Encrypted GPG key requires passphrase. Set ASCII_CHAT_KEY_PASSWORD environment variable or "
                       "run interactively.");
    }
  }

  // Create temporary files for input and output
  char input_path[] = "/tmp/ascii-chat-gpg-XXXXXX";
  int input_fd = mkstemp(input_path);
  if (input_fd < 0) {
    sodium_memzero(passphrase_buffer, sizeof(passphrase_buffer));
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to create temporary file for GPG decryption");
  }

  // Write armored key to temp file
  size_t armored_len = strlen(armored_text);
  ssize_t written = write(input_fd, armored_text, armored_len);
  close(input_fd);

  if (written != (ssize_t)armored_len) {
    unlink(input_path);
    sodium_memzero(passphrase_buffer, sizeof(passphrase_buffer));
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to write armored key to temp file");
  }

  // Create output file for decrypted key
  char output_path[] = "/tmp/ascii-chat-gpg-out-XXXXXX";
  int output_fd = mkstemp(output_path);
  if (output_fd < 0) {
    unlink(input_path);
    sodium_memzero(passphrase_buffer, sizeof(passphrase_buffer));
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to create temporary output file");
  }
  close(output_fd);

  // Build gpg command to import, decrypt, and re-export as unencrypted
  // We need to:
  // 1. Import the key to gpg keyring
  // 2. Export it unencrypted with the passphrase
  char command[4096];
  safe_snprintf(command, sizeof(command),
                "gpg --batch --import '%s' 2>/dev/null && "
                "KEY_FPR=$(gpg --list-secret-keys --with-colons 2>/dev/null | grep '^fpr' | head -1 | cut -d: -f10) && "
                "gpg --batch --pinentry-mode loopback --passphrase '%s' --armor --export-secret-keys --export-options "
                "export-minimal,no-export-attributes \"$KEY_FPR\" > '%s' 2>/dev/null",
                input_path, passphrase, output_path);

  int status = system(command);

  if (status != 0) {
    unlink(input_path);
    unlink(output_path);
    // Clean up the imported key
    system("gpg --batch --yes --delete-secret-and-public-keys $(gpg --list-secret-keys --with-colons 2>/dev/null | "
           "grep '^fpr' | tail -1 | cut -d: -f10) 2>/dev/null");
    sodium_memzero(passphrase_buffer, sizeof(passphrase_buffer));
    return SET_ERRNO(ERROR_CRYPTO_KEY, "GPG decryption failed. Check passphrase and key format.");
  }

  // Read the decrypted output
  FILE *output_file = platform_fopen(output_path, "r");
  if (!output_file) {
    unlink(input_path);
    unlink(output_path);
    sodium_memzero(passphrase_buffer, sizeof(passphrase_buffer));
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to read decrypted GPG output");
  }

  fseek(output_file, 0, SEEK_END);
  long output_size = ftell(output_file);
  fseek(output_file, 0, SEEK_SET);

  if (output_size <= 0 || output_size > 1024 * 1024) {
    fclose(output_file);
    unlink(input_path);
    unlink(output_path);
    sodium_memzero(passphrase_buffer, sizeof(passphrase_buffer));
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Invalid decrypted GPG output size: %ld bytes", output_size);
  }

  *decrypted_out = SAFE_MALLOC((size_t)output_size + 1, char *);
  size_t bytes_read = fread(*decrypted_out, 1, (size_t)output_size, output_file);
  (*decrypted_out)[bytes_read] = '\0';
  fclose(output_file);

  // Clean up temporary files and imported key
  unlink(input_path);
  unlink(output_path);
  system("gpg --batch --yes --delete-secret-and-public-keys $(gpg --list-secret-keys --with-colons 2>/dev/null | "
         "grep '^fpr' | tail -1 | cut -d: -f10) 2>/dev/null");

  // Securely erase passphrase from memory
  sodium_memzero(passphrase_buffer, sizeof(passphrase_buffer));

  log_debug("Successfully decrypted GPG key using passphrase");
  return ASCIICHAT_OK;
}

asciichat_error_t openpgp_parse_armored_seckey(const char *armored_text, uint8_t ed25519_pk[32],
                                               uint8_t ed25519_sk[32]) {
  if (!armored_text || !ed25519_pk || !ed25519_sk) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for armored seckey parsing");
  }

  // Find the BEGIN marker (try both "PRIVATE KEY" and "SECRET KEY" formats)
  const char *begin = strstr(armored_text, "-----BEGIN PGP PRIVATE KEY BLOCK-----");
  if (!begin) {
    begin = strstr(armored_text, "-----BEGIN PGP SECRET KEY BLOCK-----");
  }
  if (!begin) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Missing PGP PRIVATE/SECRET KEY BLOCK BEGIN marker");
  }

  // Skip to the end of the BEGIN line
  const char *base64_start = strchr(begin, '\n');
  if (!base64_start) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Invalid PGP armored format: no newline after BEGIN marker");
  }
  base64_start++; // Skip the newline

  // Find the END marker (try both formats)
  const char *end = strstr(base64_start, "-----END PGP PRIVATE KEY BLOCK-----");
  if (!end) {
    end = strstr(base64_start, "-----END PGP SECRET KEY BLOCK-----");
  }
  if (!end) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Missing PGP PRIVATE/SECRET KEY BLOCK END marker");
  }

  // Find the checksum line (starts with '=') and exclude it
  const char *base64_end = end;
  const char *checksum = base64_end;
  while (checksum > base64_start && *checksum != '=') {
    checksum--;
  }
  if (*checksum == '=') {
    // Move back to before the checksum line
    while (checksum > base64_start && (checksum[-1] == '\n' || checksum[-1] == '\r')) {
      checksum--;
    }
    base64_end = checksum;
  }

  size_t base64_len = (size_t)(base64_end - base64_start);

  log_debug("Extracting base64 data from PGP secret key armor (%zu bytes)", base64_len);

  // Decode base64 to binary OpenPGP packets
  uint8_t *binary_data;
  size_t binary_len;
  asciichat_error_t decode_result = openpgp_base64_decode(base64_start, base64_len, &binary_data, &binary_len);
  if (decode_result != ASCIICHAT_OK) {
    return decode_result;
  }

  log_debug("Decoded %zu bytes of OpenPGP secret key packet data", binary_len);

  // Parse OpenPGP packets to find the secret key packet (tag 5)
  size_t offset = 0;
  bool found_seckey = false;

  while (offset < binary_len) {
    openpgp_packet_header_t header;
    asciichat_error_t header_result = openpgp_parse_packet_header(binary_data + offset, binary_len - offset, &header);
    if (header_result != ASCIICHAT_OK) {
      SAFE_FREE(binary_data);
      return header_result;
    }

    log_debug("Packet at offset %zu: tag=%u, length=%zu", offset, header.tag, header.length);

    // Check if this is a secret key packet (tag 5)
    if (header.tag == OPENPGP_TAG_SECRET_KEY) {
      openpgp_secret_key_t seckey;
      asciichat_error_t parse_result =
          openpgp_parse_secret_key_packet(binary_data + offset + header.header_len, header.length, &seckey);

      if (parse_result == ASCIICHAT_OK) {
        // Check if key is encrypted
        if (seckey.is_encrypted) {
          SAFE_FREE(binary_data);
          log_debug("Detected encrypted GPG key, attempting to decrypt with passphrase");

          // Decrypt the key using gpg binary
          char *decrypted_text = NULL;
          asciichat_error_t decrypt_result = openpgp_decrypt_with_gpg(armored_text, &decrypted_text);
          if (decrypt_result != ASCIICHAT_OK) {
            return decrypt_result;
          }

          // Recursively parse the decrypted key
          asciichat_error_t recursive_result = openpgp_parse_armored_seckey(decrypted_text, ed25519_pk, ed25519_sk);
          SAFE_FREE(decrypted_text);
          return recursive_result;
        }

        // Unencrypted key - extract directly
        memcpy(ed25519_pk, seckey.pubkey, 32);
        memcpy(ed25519_sk, seckey.seckey, 32);
        found_seckey = true;
        log_debug("Extracted Ed25519 keypair from OpenPGP armored secret key block");
        break;
      } else {
        // Not an Ed25519 key, try next packet
        log_debug("Skipping non-Ed25519 secret key packet");
      }
    }

    // Move to next packet
    offset += header.header_len + header.length;
  }

  SAFE_FREE(binary_data);

  if (!found_seckey) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "No Ed25519 secret key found in PGP armored block");
  }

  return ASCIICHAT_OK;
}
