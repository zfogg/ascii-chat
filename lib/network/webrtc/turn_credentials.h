/**
 * @file turn_credentials.h
 * @brief TURN server credential generation for WebRTC
 *
 * Implements time-limited HMAC-based TURN authentication as specified in
 * RFC 5389 (STUN) and RFC 5766 (TURN).
 *
 * Credential format:
 * - Username: "{timestamp}:{session_id}"
 * - Password: base64(HMAC-SHA1(secret, username))
 *
 * The timestamp provides time-limited credentials that expire after a
 * configurable duration (default 24 hours).
 */

#ifndef ASCIICHAT_NETWORKING_WEBRTC_TURN_CREDENTIALS_H
#define ASCIICHAT_NETWORKING_WEBRTC_TURN_CREDENTIALS_H

#include "common/error_codes.h"

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/**
 * @brief TURN server credentials (username + password)
 */
typedef struct {
  char username[128]; /**< Format: "timestamp:session_id" */
  char password[128]; /**< Base64-encoded HMAC-SHA1(secret, username) */
  time_t expires_at;  /**< Unix timestamp when credentials expire */
} turn_credentials_t;

/**
 * @brief Generate time-limited TURN credentials
 *
 * Creates TURN authentication credentials using HMAC-SHA1 with a shared secret.
 * The username includes a timestamp for automatic expiration.
 *
 * @param session_id Session identifier (e.g., "swift-river-mountain")
 * @param secret Shared secret configured on TURN server
 * @param validity_seconds How long credentials remain valid (default: 86400 = 24 hours)
 * @param out_credentials Output credentials structure
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note The secret must match the static-auth-secret configured on the TURN server
 *
 * Example:
 * @code
 * turn_credentials_t creds;
 * asciichat_error_t result = turn_generate_credentials(
 *     "swift-river-mountain",
 *     "my-turn-secret",
 *     86400,  // 24 hours
 *     &creds
 * );
 * if (result == ASCIICHAT_OK) {
 *     printf("Username: %s\n", creds.username);
 *     printf("Password: %s\n", creds.password);
 * }
 * @endcode
 */
asciichat_error_t turn_generate_credentials(const char *session_id, const char *secret, uint32_t validity_seconds,
                                            turn_credentials_t *out_credentials);

/**
 * @brief Check if TURN credentials have expired
 *
 * @param credentials Credentials to check
 * @return true if expired, false if still valid
 */
bool turn_credentials_expired(const turn_credentials_t *credentials);

#endif // ASCIICHAT_NETWORKING_WEBRTC_TURN_CREDENTIALS_H
