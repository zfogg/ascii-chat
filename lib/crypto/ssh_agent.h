#pragma once

/**
 * @file crypto/ssh_agent.h
 * @ingroup crypto
 * @brief SSH agent interface for signing operations
 *
 * This header provides SSH agent integration for signing operations.
 * Allows keys to stay in SSH agent (not loaded into memory) for better security.
 *
 * @note SSH agent: Uses SSH agent protocol to communicate with ssh-agent.
 *       Keys stay in agent and are never loaded into application memory.
 *
 * @note Platform support:
 *       - Unix: Uses SSH_AUTH_SOCK environment variable (Unix domain socket)
 *       - Windows: Uses SSH_AUTH_SOCK environment variable (named pipe)
 *
 * @note Key format: Only Ed25519 keys are supported.
 *       RSA/ECDSA keys are NOT supported.
 *
 * @note Agent detection: Checks SSH_AUTH_SOCK environment variable.
 *       On Unix, also verifies socket is accessible.
 *       On Windows, only checks environment variable (named pipes handled differently).
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include "common.h"
#include "keys/keys.h"
#include <stdbool.h>

/**
 * @name SSH Agent Detection
 * @{
 */

/**
 * @brief Check if ssh-agent is running and available
 * @return true if ssh-agent is available, false otherwise
 *
 * Checks if SSH agent is running and accessible by verifying SSH_AUTH_SOCK environment variable.
 *
 * @note Agent detection:
 *       - Checks SSH_AUTH_SOCK environment variable is set
 *       - On Unix: Verifies socket exists and is accessible (access() with W_OK)
 *       - On Windows: Only checks environment variable (named pipe accessibility checked at connection time)
 *
 * @note Platform differences:
 *       - Unix: Uses Unix domain socket (AF_UNIX) - can verify socket exists
 *       - Windows: Uses named pipe - can't use access() on named pipes
 *
 * @note Agent location:
 *       - Unix: SSH_AUTH_SOCK points to Unix domain socket (e.g., `/tmp/ssh-XXXXXXXX/agent.XXXXXX`)
 *       - Windows: SSH_AUTH_SOCK points to named pipe (e.g., `\\.\pipe\openssh-ssh-agent`)
 *
 * @warning Agent may not be running: Function checks environment variable but doesn't verify agent is actually running.
 *          Connection may fail later if agent is not running.
 *
 * @warning Windows limitation: Cannot verify named pipe accessibility without attempting connection.
 *          Function may return true even if agent is not running (connection will fail later).
 *
 * @ingroup crypto
 */
bool ssh_agent_is_available(void);

/** @} */

/**
 * @name SSH Agent Key Management
 * @{
 */

/**
 * @brief Add a private key to ssh-agent
 * @param private_key Private key to add (must not be NULL)
 * @param key_path Original key file path (for reference, can be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Adds private key to SSH agent by creating temporary key file and using `ssh-add`.
 * Key is added to agent and stays in agent (not loaded into application memory).
 *
 * @note Agent requirement: SSH agent must be running and accessible.
 *       Function returns error if agent is not available.
 *
 * @note Key format: Only Ed25519 keys are supported.
 *       Returns error if key type is not KEY_TYPE_ED25519.
 *
 * @note Key addition: Uses `ssh-add` command to add key to agent:
 *       - Creates temporary key file with restrictive permissions (0600)
 *       - Writes OpenSSH private key format to temporary file
 *       - Executes `ssh-add <tmpfile>` to add key to agent
 *       - Deletes temporary file after adding key
 *
 * @note Temporary file:
 *       - Unix: `/tmp/ascii-chat-key-XXXXXX` (mkstemp)
 *       - Windows: `%TEMP%\ascii-chat-key-XXXXXX` (GetTempPathA, _mktemp_s)
 *
 * @note File permissions: Sets restrictive permissions (0600) on temporary key file.
 *       Windows doesn't have Unix-style permissions (uses _S_IREAD | _S_IWRITE).
 *
 * @note Idempotent: `ssh-add` is idempotent - adding same key multiple times is safe.
 *       Key is not duplicated in agent.
 *
 * @note Key path: key_path is only used for logging/reference.
 *       Function creates temporary file regardless of original key path.
 *
 * @warning Key security: Temporary key file contains private key material.
 *          File is created with restrictive permissions and deleted immediately after use.
 *
 * @warning Agent dependency: Requires `ssh-add` to be installed and in PATH.
 *          Returns error with installation instructions if `ssh-add` is not found.
 *
 * @warning Key format: Only Ed25519 keys are supported. Other key types will return error.
 *
 * @ingroup crypto
 */
asciichat_error_t ssh_agent_add_key(const private_key_t *private_key, const char *key_path);

/**
 * @brief Check if a public key is already in ssh-agent
 * @param public_key Public key to check (must not be NULL)
 * @return true if key is in agent, false otherwise
 *
 * Checks if public key is already in SSH agent by listing agent keys.
 * Uses `ssh-add -l` to list keys and compares fingerprints.
 *
 * @note Agent requirement: SSH agent must be running and accessible.
 *       Returns false if agent is not available.
 *
 * @note Key listing: Uses `ssh-add -l` to list keys in agent.
 *       Parses output to find matching key fingerprint.
 *
 * @note Key fingerprint: Compares SHA256 fingerprints of keys.
 *       Format: `256 SHA256:fingerprint comment (ED25519)`
 *
 * @note Current implementation: Function always returns false (exact matching not implemented).
 *       `ssh-add` is idempotent anyway, so duplicate adds are safe.
 *
 * @note Platform-specific command:
 *       - Unix: `ssh-add -l 2>/dev/null`
 *       - Windows: `ssh-add -l 2>nul`
 *
 * @warning Function currently always returns false: Exact fingerprint matching is not implemented.
 *          `ssh-add` is idempotent, so duplicate adds are safe.
 *
 * @warning Agent dependency: Requires `ssh-add` to be installed and in PATH.
 *          Returns false if `ssh-add` is not found.
 *
 * @ingroup crypto
 */
bool ssh_agent_has_key(const public_key_t *public_key);

/**
 * @brief Retrieve a private key from ssh-agent by matching public key
 * @param public_key Public key to match (must not be NULL)
 * @param key_out Output private key structure (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Retrieves private key from SSH agent by sending SSH2_AGENTC_SIGN_REQUEST.
 * This doesn't actually retrieve the private key material - instead it proves
 * the key exists in the agent by attempting a signature operation.
 *
 * @note Agent requirement: SSH agent must be running and accessible.
 *       Returns error if agent is not available.
 *
 * @note Key format: Only Ed25519 keys are supported.
 *
 * @note Security: Private key never leaves ssh-agent.
 *       This function only verifies the key exists by matching the public key.
 *
 * @ingroup crypto
 */
asciichat_error_t ssh_agent_get_key(const public_key_t *public_key, private_key_t *key_out);

/** @} */
