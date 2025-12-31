#pragma once

/**
 * @file crypto/gpg/agent.h
 * @brief GPG agent connection and communication interface
 * @ingroup crypto
 * @addtogroup crypto
 * @{
 *
 * This header provides GPG agent (gpg-agent) integration for signing operations
 * with GPG keys. Allows private keys to stay in GPG agent without being loaded
 * into application memory.
 *
 * @note GPG agent protocol: Implements Assuan protocol for communicating with gpg-agent.
 *       Keys stay in agent and are never loaded into application memory.
 *
 * @note Platform support:
 *       - Unix: Uses GPG_AGENT_INFO or connects to standard socket (~/.gnupg/S.gpg-agent)
 *       - Windows: Connects to standard named pipe (Gpg4win installs gpg-agent as service)
 *
 * @note Key format: Only Ed25519 GPG keys are supported.
 *       RSA/ECDSA GPG keys are NOT supported.
 *
 * @note Keygrip: GPG uses keygrips (40-char hex strings) to identify keys in the agent.
 *       Keygrips are computed from public key material and are stable identifiers.
 *
 * @note Agent detection: Checks for agent socket/pipe existence and accessibility.
 *       On Unix, verifies socket is accessible. On Windows, checks for named pipe.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @name GPG Agent Connection Management
 * @{
 */

/**
 * @brief Connect to gpg-agent
 * @return Socket/pipe handle on success, -1 on error
 *
 * Establishes connection to GPG agent using Assuan protocol.
 * Connects to agent socket/pipe and performs initial handshake.
 *
 * @note Connection method:
 *       - Unix: Connects to Unix domain socket (~/.gnupg/S.gpg-agent or GPG_AGENT_INFO)
 *       - Windows: Connects to named pipe (\\.\pipe\gpg-agent or from GPG_AGENT_INFO)
 *
 * @note Socket location:
 *       - Unix: $GPG_AGENT_INFO or ~/.gnupg/S.gpg-agent
 *       - Windows: Named pipe from GPG_AGENT_INFO or default pipe
 *
 * @note Assuan protocol: After connection, sends initial commands:
 *       - Receives "OK Pleased to meet you" greeting
 *       - No additional initialization needed for signing operations
 *
 * @note Connection ownership: Caller must call gpg_agent_disconnect() when done.
 *       Failing to disconnect will leak socket handles.
 *
 * @warning Agent requirement: GPG agent must be running and accessible.
 *          Returns -1 if agent is not available (not running, wrong path, etc.).
 *
 * @warning Socket cleanup: Must call gpg_agent_disconnect() to close socket.
 *          Failing to do so will leak file descriptors/handles.
 *
 * @ingroup crypto
 */
int gpg_agent_connect(void);

/**
 * @brief Disconnect from gpg-agent
 * @param sock Socket/pipe handle from gpg_agent_connect()
 *
 * Closes connection to GPG agent and releases socket resources.
 * Safe to call with invalid socket (does nothing if sock < 0).
 *
 * @note Socket closure: Closes socket handle using platform-specific close function.
 *       On Unix, uses close(). On Windows, uses CloseHandle().
 *
 * @note Assuan protocol: No goodbye message needed - just closes socket.
 *       GPG agent handles disconnections gracefully.
 *
 * @note Safe disconnect: Function validates socket handle before closing.
 *       Safe to call with -1 or invalid handles.
 *
 * @warning Always call this after gpg_agent_connect() to avoid resource leaks.
 *
 * @ingroup crypto
 */
void gpg_agent_disconnect(int sock);

/**
 * @brief Check if GPG agent is available
 * @return true if gpg-agent is running and accessible, false otherwise
 *
 * Checks if GPG agent is running by attempting to connect and immediately disconnecting.
 * Uses gpg_agent_connect() internally.
 *
 * @note Agent detection: Attempts actual connection to verify agent is running.
 *       Returns false if connection fails for any reason.
 *
 * @note Platform differences:
 *       - Unix: Checks for socket existence and accessibility (~/.gnupg/S.gpg-agent)
 *       - Windows: Attempts to connect to named pipe
 *
 * @note Connection test: Creates temporary connection and closes it immediately.
 *       Does not leave connection open.
 *
 * @note Performance: Involves actual socket connection - may be slow if agent is not running.
 *       Consider caching result if calling frequently.
 *
 * @warning Agent may not be running: Function returns false if agent is not running,
 *          socket/pipe doesn't exist, or permissions prevent access.
 *
 * @ingroup crypto
 */
bool gpg_agent_is_available(void);

/** @} */

/**
 * @name GPG Agent Signing Operations
 * @{
 */

/**
 * @brief Sign a message using GPG agent
 * @param sock Socket/pipe handle from gpg_agent_connect() (must be valid)
 * @param keygrip GPG keygrip (40-char hex string, must not be NULL)
 * @param message Message to sign (must not be NULL)
 * @param message_len Message length (must be > 0)
 * @param signature_out Output buffer for signature (must be >= 64 bytes for Ed25519)
 * @param signature_len_out Output parameter for signature length (must not be NULL)
 * @return 0 on success, -1 on error
 *
 * Signs message using GPG agent Assuan protocol PKSIGN command.
 * Key stays in GPG agent - private key never enters application memory.
 *
 * @note Assuan protocol commands:
 *       1. RESET - Clear any previous state
 *       2. SIGKEY <keygrip> - Select key to use for signing
 *       3. SETHASH --hash=sha256 <hex_hash> - Set message hash (SHA-256 of message)
 *       4. PKSIGN - Perform signature operation
 *
 * @note Key selection: Uses keygrip to identify key in agent.
 *       Keygrip is 40-char hex string computed from public key material.
 *
 * @note Hash algorithm: Uses SHA-256 hash of message for signing.
 *       GPG agent expects hex-encoded hash, not raw message.
 *
 * @note Signature format: Returns raw Ed25519 signature (64 bytes).
 *       Format: R || S (32 bytes each) for Ed25519.
 *
 * @note Error handling: Returns -1 on any error:
 *       - Agent connection lost
 *       - Key not found in agent (wrong keygrip)
 *       - Signature operation failed
 *       - Protocol error (malformed response)
 *
 * @note Connection requirement: Requires active connection from gpg_agent_connect().
 *       Does not create or close connection - caller manages connection lifecycle.
 *
 * @warning Agent connection: Requires valid socket from gpg_agent_connect().
 *          Returns -1 if socket is invalid or connection is closed.
 *
 * @warning Key availability: Key must be in GPG agent and identified by keygrip.
 *          Returns -1 if key not found or keygrip is invalid.
 *
 * @warning Buffer size: signature_out must be at least 64 bytes for Ed25519.
 *          Function writes up to 64 bytes and sets signature_len_out accordingly.
 *
 * @ingroup crypto
 */
int gpg_agent_sign(int sock, const char *keygrip, const uint8_t *message, size_t message_len, uint8_t *signature_out,
                   size_t *signature_len_out);

/** @} */

/** @} */ /* crypto */
