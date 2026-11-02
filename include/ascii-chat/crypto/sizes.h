#pragma once

/* Existing buffer budgets in bytes, including NUL for text storage. */
#define GPG_SIGNATURE_PACKET_CAPACITY 512

#include <ascii-chat/common/limits.h>

/** Cryptographic storage and wire widths; usable without crypto context types or libsodium. */

/**
 * @name Password Requirements
 * @{
 */

/** @brief Minimum password length (8 characters) */
#define MIN_PASSWORD_LENGTH 8
/** @brief Maximum password length (256 characters) */
#define MAX_PASSWORD_LENGTH 256

/** @} */

/**
 * @name Algorithm-Specific Key Sizes
 * @{
 */

/** @brief X25519 key size in bytes */
#define X25519_KEY_SIZE 32
/** @brief Ed25519 public key size in bytes */
#define ED25519_PUBLIC_KEY_SIZE 32
/** @brief Ed25519 private key size (seed + public) in bytes */
#define ED25519_PRIVATE_KEY_SIZE 64
/** @brief Ed25519 signature size in bytes */
#define ED25519_SIGNATURE_SIZE 64
/** @brief XSalsa20 nonce size in bytes */
#define XSALSA20_NONCE_SIZE 24
/** @brief Poly1305 MAC size in bytes */
#define POLY1305_MAC_SIZE 16
/** @brief HMAC-SHA256 output size in bytes */
#define HMAC_SHA256_SIZE 32
/** Fixed HMAC key width used by the authentication API. */
#define HMAC_SHA256_KEY_SIZE 32
/** @brief Argon2id salt size in bytes */
#define ARGON2ID_SALT_SIZE 32
/** @brief Secretbox key size in bytes */
#define SECRETBOX_KEY_SIZE 32
/** @brief AES-256 key size in bytes */
#define AES256_KEY_SIZE 32
/** @brief AES initialization vector (IV) size in bytes */
#define AES_IV_SIZE 16
/** @brief AES-256 key + IV derived size in bytes (for bcrypt_pbkdf) */
#define AES256_DERIVED_SIZE (AES256_KEY_SIZE + AES_IV_SIZE)

/** @} */

/**
 * @name Abstracted Cryptographic Constants
 * @{
 *
 * These constants abstract the underlying algorithms to allow future changes.
 */

/** @brief Public key size (X25519) */
#define CRYPTO_PUBLIC_KEY_SIZE X25519_KEY_SIZE
/** @brief Private key size (X25519) */
#define CRYPTO_PRIVATE_KEY_SIZE X25519_KEY_SIZE
/** @brief Shared key size (X25519) */
#define CRYPTO_SHARED_KEY_SIZE X25519_KEY_SIZE
/** @brief Ed25519 public key size */
#define CRYPTO_ED25519_PUBLIC_KEY_SIZE ED25519_PUBLIC_KEY_SIZE
/** @brief Ed25519 private key size */
#define CRYPTO_ED25519_PRIVATE_KEY_SIZE ED25519_PRIVATE_KEY_SIZE
/** @brief Ed25519 signature size */
#define CRYPTO_ED25519_SIGNATURE_SIZE ED25519_SIGNATURE_SIZE
/** @brief Nonce size (XSalsa20) */
#define CRYPTO_NONCE_SIZE XSALSA20_NONCE_SIZE
/** @brief Salt size (Argon2id) */
#define CRYPTO_SALT_SIZE ARGON2ID_SALT_SIZE
/** @brief Encryption key size (XSalsa20-Poly1305) */
#define CRYPTO_ENCRYPTION_KEY_SIZE SECRETBOX_KEY_SIZE
/** @brief MAC size (Poly1305) */
#define CRYPTO_MAC_SIZE POLY1305_MAC_SIZE
/** @brief HMAC size (HMAC-SHA256) */
#define CRYPTO_HMAC_SIZE HMAC_SHA256_SIZE

/** @} */

/**
 * @name Authentication Packet Sizes
 * @{
 */

/** @brief HMAC size in authentication packets (32 bytes) */
#define AUTH_HMAC_SIZE 32
/** @brief Challenge nonce size (32 bytes) */
#define AUTH_CHALLENGE_SIZE 32
/** @brief Combined authentication data size (HMAC + challenge, 64 bytes) */
#define AUTH_COMBINED_SIZE (AUTH_HMAC_SIZE + AUTH_CHALLENGE_SIZE)
/** @brief Ed25519 signature size (64 bytes) */
#define AUTH_SIGNATURE_SIZE 64
/** @brief Combined signature + challenge size (96 bytes) */
#define AUTH_SIGNATURE_COMBINED_SIZE (AUTH_SIGNATURE_SIZE + AUTH_CHALLENGE_SIZE)

/** @} */

/**
 * @name Authentication Challenge Packet Structure
 * @{
 */

/** @brief Authentication flags size (1 byte) */
#define AUTH_CHALLENGE_FLAGS_SIZE 1
/** @brief Complete authentication challenge packet size (1 + 32 = 33 bytes) */
#define AUTH_CHALLENGE_PACKET_SIZE (AUTH_CHALLENGE_FLAGS_SIZE + AUTH_CHALLENGE_SIZE)

/** @} */

/**
 * @name Authentication Response Packet Sizes
 * @{
 */

/** @brief Password-based authentication response size (HMAC + challenge, 64 bytes) */
#define AUTH_RESPONSE_PASSWORD_SIZE (AUTH_HMAC_SIZE + AUTH_CHALLENGE_SIZE)
/** @brief Signature-based authentication response size (signature + challenge, 96 bytes) */
#define AUTH_RESPONSE_SIGNATURE_SIZE (AUTH_SIGNATURE_SIZE + AUTH_CHALLENGE_SIZE)

/** @} */

/** @brief Server authentication response size (32 bytes) */
#define SERVER_AUTH_RESPONSE_SIZE AUTH_HMAC_SIZE

/**
 * @name Packet Size Limits
 * @{
 */

/** @brief Maximum AUTH_FAILED packet size (256 bytes) */
#define MAX_AUTH_FAILED_PACKET_SIZE 256
/** @brief Maximum encrypted packet size (64KB) */
#define MAX_ENCRYPTED_PACKET_SIZE 65536

/** @} */

/**
 * @name Buffer Sizes
 * @{
 */

/** @brief Hex string size for 32-byte values (64 hex chars + null terminator) */
#define HEX_STRING_SIZE_32 (32 * 2 + 1)
/** @brief Hex string size for 64-byte values (128 hex chars + null terminator) */
#define HEX_STRING_SIZE_64 (64 * 2 + 1)
/** @brief Password input buffer size (256 bytes) */
#define PASSWORD_BUFFER_SIZE 256
/** @brief Zero key array size (32 bytes, used for no-identity entries) */
#define ZERO_KEY_SIZE X25519_KEY_SIZE

/** @} */

/** Raw Ed25519 seeds used by OpenPGP, before expansion to a secret key. */
#define ED25519_SEED_SIZE 32
/** OpenPGP v4 fingerprint and long key ID widths. */
#define GPG_FINGERPRINT_SIZE 20
#define GPG_FINGERPRINT_HEX_BUFFER_SIZE (GPG_FINGERPRINT_SIZE * 2 + 1)
#define GPG_KEY_ID_SIZE 8
#define GPG_KEY_ID_HEX_BUFFER_SIZE (GPG_KEY_ID_SIZE * 2 + 1)
/** Existing text storage budgets, including NUL. */
#define CRYPTO_KEY_COMMENT_BUFFER_SIZE 256
#define GPG_KEYGRIP_BUFFER_SIZE 64
