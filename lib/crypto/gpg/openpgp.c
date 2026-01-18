/**
 * @file crypto/gpg/openpgp.c
 * @brief OpenPGP (RFC 4880) packet format parser implementation
 * @ingroup crypto
 */

#include "openpgp.h"
#include "../../common.h"
#include "../../asciichat_errno.h"
#include "../../log/logging.h"
#include <string.h>
#include <stdlib.h>
#include <sodium.h>

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
        log_info("Extracted Ed25519 public key from OpenPGP armored block");
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
