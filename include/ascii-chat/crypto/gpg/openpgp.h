#pragma once

/**
 * @file crypto/gpg/openpgp.h
 * @brief OpenPGP (RFC 4880) packet format parser
 * @ingroup crypto
 * @addtogroup crypto
 * @{
 *
 * Implements parsing of OpenPGP packet format (RFC 4880) for extracting
 * Ed25519 public keys from PGP armored key blocks.
 *
 * Supported features:
 * - PGP armored format (-----BEGIN PGP PUBLIC KEY BLOCK-----)
 * - Base64 decoding of armored data
 * - OpenPGP packet header parsing (old and new formats)
 * - Public Key Packet (tag 6) parsing
 * - Ed25519 (algorithm 22) key extraction
 *
 * @note Limitations:
 * - Only supports Ed25519 keys (algorithm 22)
 * - Only parses public key packets (tag 6)
 * - Does not verify signatures or checksums
 * - Does not support encrypted keys
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include "../../common.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @name OpenPGP Constants
 * @{
 */

/** @brief OpenPGP packet tag for Public Key Packet */
#define OPENPGP_TAG_PUBLIC_KEY 6

/** @brief OpenPGP packet tag for Secret Key Packet */
#define OPENPGP_TAG_SECRET_KEY 5

/** @brief OpenPGP packet tag for User ID Packet */
#define OPENPGP_TAG_USER_ID 13

/** @brief OpenPGP packet tag for Signature Packet */
#define OPENPGP_TAG_SIGNATURE 2

/** @brief OpenPGP algorithm ID for EdDSA (Ed25519) */
#define OPENPGP_ALGO_EDDSA 22

/** @brief OpenPGP algorithm ID for ECDH (Curve25519) */
#define OPENPGP_ALGO_ECDH 18

/** @} */

/**
 * @name OpenPGP Packet Header
 * @{
 */

/**
 * @brief OpenPGP packet header information
 *
 * Represents the parsed header of an OpenPGP packet, including
 * tag, length, and offset information.
 */
typedef struct {
  uint8_t tag;       ///< Packet tag (type identifier)
  size_t length;     ///< Packet body length
  size_t header_len; ///< Header length (bytes consumed by header)
  bool new_format;   ///< True if new format, false if old format
} openpgp_packet_header_t;

/** @} */

/**
 * @name OpenPGP Public Key Packet
 * @{
 */

/**
 * @brief OpenPGP public key packet data
 *
 * Represents the parsed data from a Public Key Packet (tag 6),
 * containing algorithm, creation time, and key material.
 */
typedef struct {
  uint8_t version;    ///< Packet version (should be 4)
  uint32_t created;   ///< Creation timestamp (Unix epoch)
  uint8_t algorithm;  ///< Public key algorithm (22 = EdDSA)
  uint8_t pubkey[32]; ///< Ed25519 public key (32 bytes)
  uint64_t keyid;     ///< OpenPGP Key ID (last 8 bytes of fingerprint)
} openpgp_public_key_t;

/** @} */

/**
 * @name OpenPGP Secret Key Packet
 * @{
 */

/**
 * @brief OpenPGP secret key packet data
 *
 * Represents the parsed data from a Secret Key Packet (tag 5),
 * containing algorithm, creation time, and both public and secret key material.
 */
typedef struct {
  uint8_t version;    ///< Packet version (should be 4)
  uint32_t created;   ///< Creation timestamp (Unix epoch)
  uint8_t algorithm;  ///< Public key algorithm (22 = EdDSA)
  uint8_t pubkey[32]; ///< Ed25519 public key (32 bytes)
  uint8_t seckey[32]; ///< Ed25519 secret key (32 bytes)
  uint64_t keyid;     ///< OpenPGP Key ID (last 8 bytes of fingerprint)
  bool is_encrypted;  ///< True if secret key material is encrypted
} openpgp_secret_key_t;

/** @} */

/**
 * @name PGP Armored Format Parsing
 * @{
 */

/**
 * @brief Parse PGP armored key block and extract Ed25519 public key
 * @param armored_text PGP armored text (-----BEGIN PGP PUBLIC KEY BLOCK-----)
 * @param ed25519_pk Output buffer for Ed25519 public key (32 bytes)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Parses a complete PGP armored key block:
 * 1. Extracts base64 data between BEGIN/END markers
 * 2. Decodes base64 to binary OpenPGP packets
 * 3. Parses packet headers to find public key packet (tag 6)
 * 4. Extracts Ed25519 public key from packet body
 *
 * @note Only supports Ed25519 keys (algorithm 22)
 * @note Ignores signatures, user IDs, and other packet types
 * @note Does not verify checksums or signatures
 *
 * Example armored format:
 * ```
 * -----BEGIN PGP PUBLIC KEY BLOCK-----
 *
 * mDMEaWxCORYJKwYBBAHaRw8BAQdAOaykIMyaQi8CBTNiF9o/Nbm6L5DwR9h1maS3
 * yqG5PFO0MmFzY2lpLWNoYXQgRGlzY292ZXJ5IFNlcnZpY2UgPGFjZHNAYXNjaWkt
 * ...
 * =+ncm
 * -----END PGP PUBLIC KEY BLOCK-----
 * ```
 *
 * @ingroup crypto
 */
asciichat_error_t openpgp_parse_armored_pubkey(const char *armored_text, uint8_t ed25519_pk[32]);

/**
 * @brief Parse PGP armored secret key block and extract Ed25519 keypair
 * @param armored_text PGP armored text (-----BEGIN PGP PRIVATE KEY BLOCK-----)
 * @param ed25519_pk Output buffer for Ed25519 public key (32 bytes)
 * @param ed25519_sk Output buffer for Ed25519 secret key (32 bytes)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Parses a complete PGP armored secret key block:
 * 1. Extracts base64 data between BEGIN/END markers
 * 2. Decodes base64 to binary OpenPGP packets
 * 3. Parses packet headers to find secret key packet (tag 5)
 * 4. Extracts Ed25519 public and secret keys from packet body
 *
 * @note Only supports Ed25519 keys (algorithm 22)
 * @note Only supports unencrypted secret keys (S2K usage = 0)
 * @note Ignores signatures, user IDs, and other packet types
 * @note Does not verify checksums or signatures
 *
 * Example armored format:
 * ```
 * -----BEGIN PGP PRIVATE KEY BLOCK-----
 *
 * lIYEaWxCORYJKwYBBAHaRw8BAQdAOaykIMyaQi8CBTNiF9o/Nbm6L5DwR9h1maS3
 * yqG5PFMAAQDm8...
 * =abcd
 * -----END PGP PRIVATE KEY BLOCK-----
 * ```
 *
 * @ingroup crypto
 */
asciichat_error_t openpgp_parse_armored_seckey(const char *armored_text, uint8_t ed25519_pk[32],
                                               uint8_t ed25519_sk[32]);

/** @} */

/**
 * @name OpenPGP Packet Parsing
 * @{
 */

/**
 * @brief Parse OpenPGP packet header
 * @param data Packet data (starts with packet header byte)
 * @param data_len Length of packet data
 * @param header Output parameter for parsed header
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Parses OpenPGP packet header (old or new format):
 * - Old format: bit 7 = 1, bit 6 = 0, bits 5-2 = tag, bits 1-0 = length type
 * - New format: bit 7 = 1, bit 6 = 1, bits 5-0 = tag
 *
 * RFC 4880 Section 4.2: Packet Headers
 *
 * @ingroup crypto
 */
asciichat_error_t openpgp_parse_packet_header(const uint8_t *data, size_t data_len, openpgp_packet_header_t *header);

/**
 * @brief Parse OpenPGP Public Key Packet (tag 6)
 * @param packet_body Packet body data (after header)
 * @param body_len Length of packet body
 * @param pubkey Output parameter for parsed public key
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Parses Public Key Packet (tag 6) body:
 * - Version (1 byte, must be 4)
 * - Creation time (4 bytes, Unix timestamp)
 * - Algorithm (1 byte, 22 = EdDSA)
 * - Public key material (MPI format for Ed25519)
 *
 * RFC 4880 Section 5.5.2: Public-Key Packet Formats
 *
 * @note Only supports version 4 packets
 * @note Only supports EdDSA (algorithm 22)
 *
 * @ingroup crypto
 */
asciichat_error_t openpgp_parse_public_key_packet(const uint8_t *packet_body, size_t body_len,
                                                  openpgp_public_key_t *pubkey);

/**
 * @brief Parse OpenPGP Secret Key Packet (tag 5)
 * @param packet_body Packet body data (after header)
 * @param body_len Length of packet body
 * @param seckey Output parameter for parsed secret key
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Parses Secret Key Packet (tag 5) body:
 * - Version (1 byte, must be 4)
 * - Creation time (4 bytes, Unix timestamp)
 * - Algorithm (1 byte, 22 = EdDSA)
 * - Public key material (MPI format for Ed25519)
 * - S2K usage (1 byte, must be 0 for unencrypted)
 * - Secret key material (32 bytes for Ed25519)
 *
 * RFC 4880 Section 5.5.3: Secret-Key Packet Formats
 *
 * @note Only supports version 4 packets
 * @note Only supports EdDSA (algorithm 22)
 * @note Only supports unencrypted secret keys (S2K usage = 0)
 *
 * @ingroup crypto
 */
asciichat_error_t openpgp_parse_secret_key_packet(const uint8_t *packet_body, size_t body_len,
                                                  openpgp_secret_key_t *seckey);

/**
 * @brief Extract Ed25519 public key from MPI-encoded data
 * @param mpi MPI-encoded public key data
 * @param mpi_len Length of MPI data
 * @param ed25519_pk Output buffer for Ed25519 public key (32 bytes)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Extracts Ed25519 public key from OpenPGP MPI (Multi-Precision Integer) format:
 * - 2 bytes: bit count (should be ~263 bits for Ed25519 with prefix)
 * - 1 byte: 0x40 prefix byte (Ed25519 marker)
 * - 32 bytes: Ed25519 public key
 *
 * RFC 4880 Section 3.2: Multiprecision Integers
 *
 * @ingroup crypto
 */
asciichat_error_t openpgp_extract_ed25519_from_mpi(const uint8_t *mpi, size_t mpi_len, uint8_t ed25519_pk[32]);

/** @} */

/**
 * @name Base64 Decoding for PGP Armor
 * @{
 */

/**
 * @brief Decode PGP armored base64 data
 * @param base64 Base64-encoded string
 * @param base64_len Length of base64 string
 * @param binary_out Output buffer for decoded binary data (allocated by function)
 * @param binary_len Output parameter for decoded data length
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Decodes base64 data from PGP armored format:
 * - Removes whitespace (newlines, spaces, tabs)
 * - Decodes using libsodium's base64 decoder
 * - Allocates output buffer (caller must free)
 *
 * @note Caller must free *binary_out using SAFE_FREE()
 *
 * @ingroup crypto
 */
asciichat_error_t openpgp_base64_decode(const char *base64, size_t base64_len, uint8_t **binary_out,
                                        size_t *binary_len);

/** @} */

/** @} */
