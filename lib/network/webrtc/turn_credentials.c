/**
 * @file turn_credentials.c
 * @brief TURN server credential generation implementation
 */

#include "network/webrtc/turn_credentials.h"
#include "asciichat_errno.h"
#include "common.h"
#include "log/logging.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/**
 * @brief Base64 encoding table (RFC 4648)
 */
static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * @brief Encode binary data to base64
 *
 * @param input Binary data to encode
 * @param input_len Length of input data
 * @param output Output buffer (must be at least ((input_len + 2) / 3) * 4 + 1 bytes)
 * @param output_size Size of output buffer
 * @return Number of bytes written to output (excluding null terminator), or 0 on error
 */
static size_t base64_encode(const uint8_t *input, size_t input_len, char *output, size_t output_size) {
  if (!input || !output || input_len == 0) {
    return 0;
  }

  // Calculate required output size: ((n + 2) / 3) * 4 + 1 for null terminator
  size_t required_size = ((input_len + 2) / 3) * 4 + 1;
  if (output_size < required_size) {
    return 0;
  }

  size_t i = 0;
  size_t j = 0;

  // Process input in 3-byte chunks
  while (i + 2 < input_len) {
    uint32_t triple = ((uint32_t)input[i] << 16) | ((uint32_t)input[i + 1] << 8) | (uint32_t)input[i + 2];

    output[j++] = base64_table[(triple >> 18) & 0x3F];
    output[j++] = base64_table[(triple >> 12) & 0x3F];
    output[j++] = base64_table[(triple >> 6) & 0x3F];
    output[j++] = base64_table[triple & 0x3F];

    i += 3;
  }

  // Handle remaining bytes (1 or 2)
  if (i < input_len) {
    uint32_t triple = (uint32_t)input[i] << 16;
    if (i + 1 < input_len) {
      triple |= (uint32_t)input[i + 1] << 8;
    }

    output[j++] = base64_table[(triple >> 18) & 0x3F];
    output[j++] = base64_table[(triple >> 12) & 0x3F];

    if (i + 1 < input_len) {
      output[j++] = base64_table[(triple >> 6) & 0x3F];
    } else {
      output[j++] = '=';
    }
    output[j++] = '=';
  }

  output[j] = '\0';
  return j;
}

/**
 * @brief Compute HMAC-SHA1 of data using a secret key
 *
 * @param data Data to authenticate
 * @param data_len Length of data
 * @param secret Secret key
 * @param secret_len Length of secret
 * @param output Output buffer (must be at least 20 bytes for SHA1)
 * @param output_len Pointer to store output length
 * @return ASCIICHAT_OK on success, error code on failure
 */
static asciichat_error_t hmac_sha1(const uint8_t *data, size_t data_len, const uint8_t *secret, size_t secret_len,
                                   uint8_t *output, unsigned int *output_len) {
  if (!data || !secret || !output || !output_len) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "HMAC-SHA1: NULL parameter");
  }

  // OpenSSL 3.0+ uses EVP interface, older versions use HMAC_* functions
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
  EVP_MAC *mac = EVP_MAC_fetch(NULL, "HMAC", NULL);
  if (!mac) {
    return SET_ERRNO(ERROR_CRYPTO, "HMAC-SHA1: Failed to fetch HMAC algorithm");
  }

  EVP_MAC_CTX *ctx = EVP_MAC_CTX_new(mac);
  if (!ctx) {
    EVP_MAC_free(mac);
    return SET_ERRNO(ERROR_CRYPTO, "HMAC-SHA1: Failed to create context");
  }

  // Set digest algorithm to SHA1
  OSSL_PARAM params[] = {OSSL_PARAM_construct_utf8_string("digest", "SHA1", 0), OSSL_PARAM_construct_end()};

  if (EVP_MAC_init(ctx, secret, secret_len, params) != 1) {
    EVP_MAC_CTX_free(ctx);
    EVP_MAC_free(mac);
    return SET_ERRNO(ERROR_CRYPTO, "HMAC-SHA1: Failed to initialize");
  }

  if (EVP_MAC_update(ctx, data, data_len) != 1) {
    EVP_MAC_CTX_free(ctx);
    EVP_MAC_free(mac);
    return SET_ERRNO(ERROR_CRYPTO, "HMAC-SHA1: Failed to update");
  }

  size_t out_len = 0;
  if (EVP_MAC_final(ctx, output, &out_len, 20) != 1) {
    EVP_MAC_CTX_free(ctx);
    EVP_MAC_free(mac);
    return SET_ERRNO(ERROR_CRYPTO, "HMAC-SHA1: Failed to finalize");
  }

  *output_len = (unsigned int)out_len;

  EVP_MAC_CTX_free(ctx);
  EVP_MAC_free(mac);
#else
  // OpenSSL 1.x compatibility
  unsigned char *result = HMAC(EVP_sha1(), secret, (int)secret_len, data, data_len, output, output_len);
  if (!result) {
    return SET_ERRNO(ERROR_CRYPTO, "HMAC-SHA1: Failed to compute HMAC");
  }
#endif

  return ASCIICHAT_OK;
}

asciichat_error_t turn_generate_credentials(const char *session_id, const char *secret, uint32_t validity_seconds,
                                            turn_credentials_t *out_credentials) {
  if (!session_id || !secret || !out_credentials) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "TURN credentials: NULL parameter");
  }

  if (validity_seconds == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "TURN credentials: validity_seconds must be > 0");
  }

  // Calculate expiration timestamp
  time_t now = time(NULL);
  time_t expires_at = now + (time_t)validity_seconds;

  // Format username: "{timestamp}:{session_id}"
  int username_len = safe_snprintf(out_credentials->username, sizeof(out_credentials->username), "%ld:%s",
                                   (long)expires_at, session_id);
  if (username_len < 0 || (size_t)username_len >= sizeof(out_credentials->username)) {
    return SET_ERRNO(ERROR_BUFFER_OVERFLOW, "TURN credentials: username too long");
  }

  // Compute HMAC-SHA1(secret, username)
  uint8_t hmac_result[20]; // SHA1 produces 20 bytes
  unsigned int hmac_len = 0;

  asciichat_error_t result = hmac_sha1((const uint8_t *)out_credentials->username, (size_t)username_len,
                                       (const uint8_t *)secret, strlen(secret), hmac_result, &hmac_len);
  if (result != ASCIICHAT_OK) {
    return result;
  }

  if (hmac_len != 20) {
    return SET_ERRNO(ERROR_CRYPTO, "TURN credentials: unexpected HMAC length %u (expected 20)", hmac_len);
  }

  // Base64-encode the HMAC to get the password
  size_t encoded_len =
      base64_encode(hmac_result, hmac_len, out_credentials->password, sizeof(out_credentials->password));
  if (encoded_len == 0) {
    return SET_ERRNO(ERROR_BUFFER_OVERFLOW, "TURN credentials: password encoding failed");
  }

  out_credentials->expires_at = expires_at;

  log_debug("Generated TURN credentials: username=%s, expires_at=%ld", out_credentials->username, (long)expires_at);

  return ASCIICHAT_OK;
}

bool turn_credentials_expired(const turn_credentials_t *credentials) {
  if (!credentials) {
    return true;
  }

  time_t now = time(NULL);
  return now >= credentials->expires_at;
}
