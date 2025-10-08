#pragma once

#include "keys.h"
#include <stdbool.h>

/**
 * @brief Check if ssh-agent is running and available
 * @return true if ssh-agent is available, false otherwise
 */
bool ssh_agent_is_available(void);

/**
 * @brief Add a private key to ssh-agent
 * @param private_key The private key to add
 * @param key_path Original key file path (for reference)
 * @return 0 on success, -1 on failure
 */
int ssh_agent_add_key(const private_key_t *private_key, const char *key_path);

/**
 * @brief Check if a public key is already in ssh-agent
 * @param public_key The public key to check
 * @return true if key is in agent, false otherwise
 */
bool ssh_agent_has_key(const public_key_t *public_key);
