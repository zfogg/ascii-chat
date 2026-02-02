/**
 * @file crypto_openpgp_test.c
 * @brief Unit tests for OpenPGP packet parser (RFC 4880)
 *
 * Tests both old and new format packet headers, Ed25519 key extraction,
 * and full PGP armored key block parsing.
 */

#include <criterion/criterion.h>
#include <criterion/redirect.h>
#include <string.h>

#include "../../../lib/crypto/gpg/openpgp.h"
#include "../../../lib/common.h"

// =============================================================================
// Test Fixtures
// =============================================================================

/**
 * Old format Public Key Packet (tag 6) with Ed25519 key
 *
 * Packet structure:
 *   ctb: 0x98 (old format, tag 6, one-octet length)
 *   length: 0x33 (51 bytes)
 *   version: 4
 *   created: 0x69640B39 (2026-01-18)
 *   algorithm: 22 (EdDSA)
 *   OID: 092B06010401DA470F01 (Ed25519)
 *   prefix: 0x40
 *   key: 32 bytes of Ed25519 public key
 */
static const uint8_t OLD_FORMAT_PUBKEY_PACKET[] = {0x98, 0x33,             // Old format header: tag 6, length 51
                                                   0x04,                   // Version 4
                                                   0x69, 0x64, 0x0B, 0x39, // Created timestamp
                                                   0x16,                   // Algorithm 22 (EdDSA)
                                                   // OID for Ed25519 (variable length)
                                                   0x09, 0x2B, 0x06, 0x01, 0x04, 0x01, 0xDA, 0x47, 0x0F, 0x01,
                                                   // Ed25519 key: 0x40 prefix + 32 bytes
                                                   0x40, 0x39, 0xAC, 0xA4, 0x20, 0xCC, 0x9A, 0x42, 0x2F, 0x02, 0x05,
                                                   0x33, 0x62, 0x17, 0xDA, 0x3F, 0x35, 0xB9, 0xBA, 0x2F, 0x90, 0xF0,
                                                   0x47, 0xD8, 0x75, 0x99, 0xA4, 0xB7, 0xCA, 0xA1, 0xB9, 0x3C, 0x53};

/**
 * New format Public Key Packet (tag 6) with Ed25519 key
 *
 * Packet structure:
 *   ctb: 0xC6 (new format, tag 6)
 *   length: 0x33 (51 bytes, one-octet encoding)
 *   (rest identical to old format)
 */
static const uint8_t NEW_FORMAT_PUBKEY_PACKET[] = {0xC6, 0x33,             // New format header: tag 6, length 51
                                                   0x04,                   // Version 4
                                                   0x69, 0x64, 0x0B, 0x39, // Created timestamp
                                                   0x16,                   // Algorithm 22 (EdDSA)
                                                   // OID for Ed25519
                                                   0x09, 0x2B, 0x06, 0x01, 0x04, 0x01, 0xDA, 0x47, 0x0F, 0x01,
                                                   // Ed25519 key: 0x40 prefix + 32 bytes
                                                   0x40, 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
                                                   0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x01,
                                                   0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0xFE, 0xDC, 0xBA, 0x98};

/**
 * Expected Ed25519 public key from OLD_FORMAT_PUBKEY_PACKET
 */
static const uint8_t EXPECTED_OLD_FORMAT_KEY[32] = {0x39, 0xAC, 0xA4, 0x20, 0xCC, 0x9A, 0x42, 0x2F, 0x02, 0x05, 0x33,
                                                    0x62, 0x17, 0xDA, 0x3F, 0x35, 0xB9, 0xBA, 0x2F, 0x90, 0xF0, 0x47,
                                                    0xD8, 0x75, 0x99, 0xA4, 0xB7, 0xCA, 0xA1, 0xB9, 0x3C, 0x53};

/**
 * Expected Ed25519 public key from NEW_FORMAT_PUBKEY_PACKET
 */
static const uint8_t EXPECTED_NEW_FORMAT_KEY[32] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                                                    0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x01, 0x23,
                                                    0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0xFE, 0xDC, 0xBA, 0x98};

// =============================================================================
// Packet Header Parsing Tests
// =============================================================================

Test(openpgp, parse_old_format_packet_header) {
  openpgp_packet_header_t header;
  asciichat_error_t result =
      openpgp_parse_packet_header(OLD_FORMAT_PUBKEY_PACKET, sizeof(OLD_FORMAT_PUBKEY_PACKET), &header);

  cr_assert_eq(result, ASCIICHAT_OK, "Failed to parse old format packet header");
  cr_assert_eq(header.new_format, false, "Should detect old format");
  cr_assert_eq(header.tag, 6, "Should extract tag 6 (Public Key Packet)");
  cr_assert_eq(header.length, 51, "Should extract length 51");
  cr_assert_eq(header.header_len, 2, "Old format one-octet length header is 2 bytes");
}

Test(openpgp, parse_new_format_packet_header) {
  openpgp_packet_header_t header;
  asciichat_error_t result =
      openpgp_parse_packet_header(NEW_FORMAT_PUBKEY_PACKET, sizeof(NEW_FORMAT_PUBKEY_PACKET), &header);

  cr_assert_eq(result, ASCIICHAT_OK, "Failed to parse new format packet header");
  cr_assert_eq(header.new_format, true, "Should detect new format");
  cr_assert_eq(header.tag, 6, "Should extract tag 6 (Public Key Packet)");
  cr_assert_eq(header.length, 51, "Should extract length 51");
  cr_assert_eq(header.header_len, 2, "New format one-octet length header is 2 bytes");
}

Test(openpgp, parse_new_format_two_octet_length) {
  // New format packet with two-octet length encoding (192-8383 bytes)
  // ctb=0xC6 (tag 6), len1=0xC0 (192), len2=0x00 → length = 192
  const uint8_t packet[] = {0xC6, 0xC0, 0x00};
  openpgp_packet_header_t header;

  asciichat_error_t result = openpgp_parse_packet_header(packet, sizeof(packet), &header);

  cr_assert_eq(result, ASCIICHAT_OK, "Failed to parse two-octet length");
  cr_assert_eq(header.new_format, true);
  cr_assert_eq(header.tag, 6);
  cr_assert_eq(header.length, 192, "Should decode two-octet length");
  cr_assert_eq(header.header_len, 3);
}

Test(openpgp, parse_new_format_five_octet_length) {
  // New format packet with five-octet length encoding (>8383 bytes)
  // ctb=0xC6 (tag 6), len=0xFF, followed by 4 bytes: 0x00001000 = 4096
  const uint8_t packet[] = {0xC6, 0xFF, 0x00, 0x00, 0x10, 0x00};
  openpgp_packet_header_t header;

  asciichat_error_t result = openpgp_parse_packet_header(packet, sizeof(packet), &header);

  cr_assert_eq(result, ASCIICHAT_OK, "Failed to parse five-octet length");
  cr_assert_eq(header.new_format, true);
  cr_assert_eq(header.tag, 6);
  cr_assert_eq(header.length, 4096, "Should decode five-octet length");
  cr_assert_eq(header.header_len, 6);
}

Test(openpgp, parse_old_format_two_octet_length) {
  // Old format packet: ctb=0x99 (tag 6, length type 1), length=0x0100 = 256
  const uint8_t packet[] = {0x99, 0x01, 0x00};
  openpgp_packet_header_t header;

  asciichat_error_t result = openpgp_parse_packet_header(packet, sizeof(packet), &header);

  cr_assert_eq(result, ASCIICHAT_OK, "Failed to parse old format two-octet length");
  cr_assert_eq(header.new_format, false);
  cr_assert_eq(header.tag, 6);
  cr_assert_eq(header.length, 256);
  cr_assert_eq(header.header_len, 3);
}

Test(openpgp, parse_old_format_four_octet_length) {
  // Old format packet: ctb=0x9A (tag 6, length type 2), length=0x00010000 = 65536
  const uint8_t packet[] = {0x9A, 0x00, 0x01, 0x00, 0x00};
  openpgp_packet_header_t header;

  asciichat_error_t result = openpgp_parse_packet_header(packet, sizeof(packet), &header);

  cr_assert_eq(result, ASCIICHAT_OK, "Failed to parse old format four-octet length");
  cr_assert_eq(header.new_format, false);
  cr_assert_eq(header.tag, 6);
  cr_assert_eq(header.length, 65536);
  cr_assert_eq(header.header_len, 5);
}

Test(openpgp, reject_invalid_packet_bit7_not_set) {
  // Invalid packet: bit 7 not set (0x40 instead of 0xC0)
  const uint8_t packet[] = {0x40, 0x00};
  openpgp_packet_header_t header;

  asciichat_error_t result = openpgp_parse_packet_header(packet, sizeof(packet), &header);

  cr_assert_neq(result, ASCIICHAT_OK, "Should reject packet with bit 7 not set");
}

// =============================================================================
// Public Key Packet Parsing Tests
// =============================================================================

Test(openpgp, parse_old_format_pubkey_packet) {
  // Skip 2-byte header to get packet body
  const uint8_t *packet_body = OLD_FORMAT_PUBKEY_PACKET + 2;
  size_t body_len = 51;

  openpgp_public_key_t pubkey;
  asciichat_error_t result = openpgp_parse_public_key_packet(packet_body, body_len, &pubkey);

  cr_assert_eq(result, ASCIICHAT_OK, "Failed to parse old format public key packet");
  cr_assert_eq(pubkey.version, 4, "Should extract version 4");
  cr_assert_eq(pubkey.algorithm, 22, "Should extract EdDSA algorithm");
  cr_assert(memcmp(pubkey.pubkey, EXPECTED_OLD_FORMAT_KEY, 32) == 0, "Should extract correct Ed25519 public key");
}

Test(openpgp, parse_new_format_pubkey_packet) {
  // Skip 2-byte header to get packet body
  const uint8_t *packet_body = NEW_FORMAT_PUBKEY_PACKET + 2;
  size_t body_len = 51;

  openpgp_public_key_t pubkey;
  asciichat_error_t result = openpgp_parse_public_key_packet(packet_body, body_len, &pubkey);

  cr_assert_eq(result, ASCIICHAT_OK, "Failed to parse new format public key packet");
  cr_assert_eq(pubkey.version, 4, "Should extract version 4");
  cr_assert_eq(pubkey.algorithm, 22, "Should extract EdDSA algorithm");
  cr_assert(memcmp(pubkey.pubkey, EXPECTED_NEW_FORMAT_KEY, 32) == 0, "Should extract correct Ed25519 public key");
}

Test(openpgp, reject_non_eddsa_algorithm) {
  // Create packet with RSA algorithm (1) instead of EdDSA (22)
  uint8_t packet_body[49]; // 51 byte packet - 2 byte header
  memcpy(packet_body, OLD_FORMAT_PUBKEY_PACKET + 2, 49);
  packet_body[5] = 1; // Change algorithm from 22 to 1 (RSA)

  openpgp_public_key_t pubkey;
  asciichat_error_t result = openpgp_parse_public_key_packet(packet_body, 49, &pubkey);

  cr_assert_neq(result, ASCIICHAT_OK, "Should reject non-EdDSA algorithm");
}

Test(openpgp, reject_missing_0x40_prefix) {
  // Create packet without 0x40 prefix
  uint8_t packet_body[49]; // 51 byte packet - 2 byte header
  memcpy(packet_body, OLD_FORMAT_PUBKEY_PACKET + 2, 49);
  // Overwrite 0x40 prefix with something else
  for (size_t i = 6; i < 49; i++) {
    if (packet_body[i] == 0x40) {
      packet_body[i] = 0x00;
    }
  }

  openpgp_public_key_t pubkey;
  asciichat_error_t result = openpgp_parse_public_key_packet(packet_body, 49, &pubkey);

  cr_assert_neq(result, ASCIICHAT_OK, "Should reject packet without 0x40 prefix");
}

// =============================================================================
// Base64 Decoding Tests
// =============================================================================

Test(openpgp, decode_base64_with_newlines) {
  const char *base64_with_newlines = "bURFYVd4Q09SWUpLd1lCQkFIYVJ3OEJBUWR\n"
                                     "BT2F5a0lNeWFRaThDQlROaUY5by9OYm02\n"
                                     "TDVEd1I5aDFtYVMzeXFHNVBGTzBNbUZ6WT\n"
                                     "JscExXTm9ZWFFnUkdsell\n";

  uint8_t *binary_out;
  size_t binary_len;
  asciichat_error_t result =
      openpgp_base64_decode(base64_with_newlines, strlen(base64_with_newlines), &binary_out, &binary_len);

  cr_assert_eq(result, ASCIICHAT_OK, "Failed to decode base64 with newlines");
  cr_assert_neq(binary_out, NULL, "Should allocate output buffer");
  cr_assert_gt(binary_len, 0, "Should decode to non-zero length");

  SAFE_FREE(binary_out);
}

Test(openpgp, reject_invalid_base64) {
  const char *invalid_base64 = "This is not valid base64!!!";

  uint8_t *binary_out;
  size_t binary_len;
  asciichat_error_t result = openpgp_base64_decode(invalid_base64, strlen(invalid_base64), &binary_out, &binary_len);

  cr_assert_neq(result, ASCIICHAT_OK, "Should reject invalid base64");
}

// =============================================================================
// Full Armored Key Parsing Test
// =============================================================================

Test(openpgp, parse_full_armored_key_old_format) {
  // Minimal PGP armored key with old format packet
  const char *armored_key = "-----BEGIN PGP PUBLIC KEY BLOCK-----\n"
                            "\n"
                            "mDMEaWxCORYJKwYBBAHaRw8BAQdAOaykIMyaQi8CBTNiF9o/Nbm6L5DwR9h1maS3\n"
                            "yqG5PFO0MmFzY2lpLWNoYXQgRGlzY292ZXJ5IFNlcnZpY2UgPGFjZHNAYXNjaWkt\n"
                            "Y2hhdC5jb20+iJMEExYKADsWIQQKrn1n1zRpWXTDbO7DgNoIrxg1uQUCaWxCOQIb\n"
                            "AwULCQgHAgIiAgYVCgkICwIEFgIDAQIeBwIXgAAKCRDDgNoIrxg1uWGGAP9aQNW9\n"
                            "A+8k2sZqV8r5lWCdGFfELfCDd1lC5l42ufpbxwEAuP0VLCukPJcXH5IWKo2jNXY5\n"
                            "bLGfRfU3EpLVP6hhYga4MwRpbEI5EgkrBgEEAdpHDwEBB0COLu0d43K+GFUvfqbk\n"
                            "AWjJe3rsmXOXL0iJhVCCyMjDW4j1BBgWCgAmFiEECq59Z9c0aVl0w2zuw4DaCK8Y\n"
                            "NbkFAmlsQjkCGwIFCQHhM4AAgQkQw4DaCK8YNbl2IAQZEgoAHRYhBPaYPh6ZTTho\n"
                            "g9+HFx/C3XBKL0fpBQJpbEI5AAoJEB/C3XBKL0fpBGABAKIrVqeVCcFRuVBwFn2O\n"
                            "P+9XzSYf3eLlvfR2wpoWdmK4AQDrT3vWPRnZp3dqEhqWGC+sWN0K2Fq7q0m5K0TL\n"
                            "8T+gBfXVAQCqcYp9Q2Fh7vN8p1KtNk5vB3q8j7Y5F1mMFUdD4P6aDwEA1tGx0Qd5\n"
                            "pT0B8mNZhHVvT0F2q9YD5y8p3Br3vE0zLwU=\n"
                            "=+ncm\n"
                            "-----END PGP PUBLIC KEY BLOCK-----\n";

  uint8_t ed25519_pk[32];
  asciichat_error_t result = openpgp_parse_armored_pubkey(armored_key, ed25519_pk);

  cr_assert_eq(result, ASCIICHAT_OK, "Failed to parse full armored key");
  cr_assert(memcmp(ed25519_pk, EXPECTED_OLD_FORMAT_KEY, 32) == 0,
            "Should extract correct Ed25519 public key from armored block");
}

Test(openpgp, reject_missing_begin_marker) {
  const char *armored_key = "mDMEaWxCORYJKwYBBAHaRw8BAQdAOaykIMyaQi8CBTNiF9o/Nbm6L5DwR9h1maS3\n"
                            "-----END PGP PUBLIC KEY BLOCK-----\n";

  uint8_t ed25519_pk[32];
  asciichat_error_t result = openpgp_parse_armored_pubkey(armored_key, ed25519_pk);

  cr_assert_neq(result, ASCIICHAT_OK, "Should reject key without BEGIN marker");
}

Test(openpgp, reject_missing_end_marker) {
  const char *armored_key = "-----BEGIN PGP PUBLIC KEY BLOCK-----\n"
                            "mDMEaWxCORYJKwYBBAHaRw8BAQdAOaykIMyaQi8CBTNiF9o/Nbm6L5DwR9h1maS3\n";

  uint8_t ed25519_pk[32];
  asciichat_error_t result = openpgp_parse_armored_pubkey(armored_key, ed25519_pk);

  cr_assert_neq(result, ASCIICHAT_OK, "Should reject key without END marker");
}
