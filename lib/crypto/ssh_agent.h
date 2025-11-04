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
#include "keys/types.h"
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
 * Adds private key to SSH agent using the SSH agent protocol.
 *
 * Message format:
 *   uint32: message length
 *   byte:   SSH2_AGENTC_ADD_IDENTITY (17)
 *   string: key type ("ssh-ed25519")
 *   string: public key (32 bytes)
 *   string: private key (64 bytes)
 *   string: comment (key path or empty)
 *
 * @note Agent requirement: SSH agent must be running and accessible.
 *       Function returns error if agent is not available.
 *
 * @note Key format: Only Ed25519 keys are supported.
 *       Returns error if key type is not KEY_TYPE_ED25519.
 *
 * @note Key addition: Uses SSH agent protocol directly via `lib/platform/pipe.h` abstraction:
 *       - Connects to agent via Unix domain socket (POSIX) or named pipe (Windows)
 *       - Sends SSH2_AGENTC_ADD_IDENTITY (17) message with key material
 *       - Receives SSH_AGENT_SUCCESS (6) response on success
 *       - No temporary files or external commands required
 *
 * @note Platform abstraction: Uses `lib/platform/pipe.h` for cross-platform communication:
 *       - POSIX: Unix domain socket via `SSH_AUTH_SOCK` environment variable
 *       - Windows: Named pipe via `SSH_AUTH_SOCK` or default `\\.\pipe\openssh-ssh-agent`
 *
 * @note Idempotent: SSH agent protocol is idempotent - adding same key multiple times is safe.
 *       Key is not duplicated in agent.
 *
 * @note Key path: key_path is only used for logging/reference in agent comment field.
 *       No temporary files are created.
 *
 * @warning Agent requirement: SSH agent must be running and accessible.
 *          Returns error if agent connection fails (agent not running, wrong path, etc.).
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
 * Checks if public key is already in SSH agent by listing agent keys using SSH agent protocol.
 *
 * @note Agent requirement: SSH agent must be running and accessible.
 *       Returns false if agent is not available.
 *
 * @note Key listing: Uses SSH agent protocol directly via `lib/platform/pipe.h` abstraction:
 *       - Connects to agent via Unix domain socket (POSIX) or named pipe (Windows)
 *       - Sends SSH2_AGENTC_REQUEST_IDENTITIES (11) message
 *       - Receives SSH2_AGENT_IDENTITIES_ANSWER (12) response with key list
 *       - Parses response to find matching Ed25519 public key (32-byte comparison)
 *
 * @note Key matching: Compares raw Ed25519 public keys (32 bytes) directly.
 *       No fingerprint computation required - direct byte comparison.
 *
 * @note Platform abstraction: Uses `lib/platform/pipe.h` for cross-platform communication:
 *       - POSIX: Unix domain socket via `SSH_AUTH_SOCK` environment variable
 *       - Windows: Named pipe via `SSH_AUTH_SOCK` or default `\\.\pipe\openssh-ssh-agent`
 *
 * @warning Agent requirement: SSH agent must be running and accessible.
 *          Returns false if agent connection fails (agent not running, wrong path, etc.).
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
