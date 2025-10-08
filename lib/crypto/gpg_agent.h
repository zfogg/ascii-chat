#ifndef GPG_AGENT_H
#define GPG_AGENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * GPG agent interface for signing operations
 * Uses the Assuan protocol to communicate with gpg-agent
 */

/**
 * Connect to gpg-agent
 * @return socket fd on success, -1 on error
 */
int gpg_agent_connect(void);

/**
 * Disconnect from gpg-agent
 * @param sock Socket fd from gpg_agent_connect
 */
void gpg_agent_disconnect(int sock);

/**
 * Sign a message using GPG agent
 * @param sock Socket fd from gpg_agent_connect
 * @param keygrip GPG keygrip (40-char hex string)
 * @param message Message to sign
 * @param message_len Message length
 * @param signature_out Output buffer for signature (must be >= 64 bytes for Ed25519)
 * @param signature_len_out Output signature length
 * @return 0 on success, -1 on error
 */
int gpg_agent_sign(int sock, const char *keygrip, const uint8_t *message, size_t message_len, uint8_t *signature_out,
                   size_t *signature_len_out);

/**
 * Get public key from GPG keyring by key ID
 * @param key_id GPG key ID (16-char hex string, e.g., "EDDAE1DA7360D7F4")
 * @param public_key_out Output buffer for 32-byte Ed25519 public key
 * @param keygrip_out Output buffer for 40-char keygrip (optional, can be NULL)
 * @return 0 on success, -1 on error
 */
int gpg_get_public_key(const char *key_id, uint8_t *public_key_out, char *keygrip_out);

/**
 * Check if GPG agent is available
 * @return true if gpg-agent is running and accessible
 */
bool gpg_agent_is_available(void);

#endif // GPG_AGENT_H
