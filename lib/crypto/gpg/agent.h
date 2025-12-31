#pragma once

/**
 * @file crypto/gpg/agent.h
 * @brief GPG agent connection and communication interface
 * @ingroup crypto
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Connect to gpg-agent
 * @return Socket/pipe handle on success, -1 on error
 */
int gpg_agent_connect(void);

/**
 * @brief Disconnect from gpg-agent
 * @param sock Socket/pipe handle from gpg_agent_connect()
 */
void gpg_agent_disconnect(int sock);

/**
 * @brief Check if GPG agent is available
 * @return true if gpg-agent is running and accessible, false otherwise
 */
bool gpg_agent_is_available(void);

/**
 * @brief Sign a message using GPG agent
 * @param sock Socket/pipe handle from gpg_agent_connect()
 * @param keygrip GPG keygrip (40-char hex string)
 * @param message Message to sign
 * @param message_len Message length
 * @param signature_out Output buffer for signature (must be >= 64 bytes)
 * @param signature_len_out Output parameter for signature length
 * @return 0 on success, -1 on error
 */
int gpg_agent_sign(int sock, const char *keygrip, const uint8_t *message, size_t message_len, uint8_t *signature_out,
                   size_t *signature_len_out);
