#pragma once

/**
 * @file crypto/gpg.h
 * @ingroup crypto
 * @brief GPG agent interface for signing operations
 *
 * This header provides GPG agent integration for signing operations using
 * the Assuan protocol to communicate with gpg-agent.
 *
 * @warning GPG SUPPORT IS CURRENTLY DISABLED: This code exists but is not
 *          active in the current build. GPG-related functions may not work
 *          until GPG support is re-enabled. Use SSH agent or in-memory keys
 *          for signing operations instead.
 *
 * @note Assuan protocol: Uses the Assuan protocol to communicate with gpg-agent.
 *       Assuan is GPG's standard protocol for agent communication.
 *
 * @note Platform support:
 *       - Unix: Uses Unix domain sockets (AF_UNIX) to connect to gpg-agent
 *       - Windows: Uses named pipes (CreateFileA) to connect to GPG4Win's gpg-agent
 *
 * @note Agent detection: Uses gpgconf to find agent socket/pipe path.
 *       Falls back to default locations if gpgconf is unavailable.
 *
 * @note Signature format: GPG agent returns signatures as S-expressions.
 *       Function parses S-expressions to extract Ed25519 signature (R || S, 64 bytes).
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @name GPG Agent Connection
 * @{
 */

/**
 * @brief Connect to gpg-agent
 * @return Socket/pipe handle on success, -1 on error
 *
 * Connects to gpg-agent using platform-specific method:
 * - Unix: Unix domain socket (AF_UNIX, SOCK_STREAM)
 * - Windows: Named pipe (CreateFileA, GENERIC_READ | GENERIC_WRITE)
 *
 * @note Agent path: Uses gpgconf to find agent socket/pipe path:
 *       - Unix: `gpgconf --list-dirs agent-socket` or `~/.gnupg/S.gpg-agent`
 *       - Windows: `gpgconf --list-dirs agent-socket` or `%APPDATA%\gnupg\S.gpg-agent`
 *
 * @note Agent availability: Waits up to 5 seconds for agent to be available (Windows only).
 *       Returns error if agent is not available after timeout.
 *
 * @note Greeting: Reads initial greeting from agent and validates "OK" response.
 *       Sets loopback pinentry mode (Unix only) to avoid interactive prompts.
 *
 * @note Platform-specific return value:
 *       - Unix: Returns socket file descriptor (int)
 *       - Windows: Returns HANDLE cast to int (handle values are always even)
 *
 * @warning GPG support is currently disabled. This function may not work until
 *          GPG support is re-enabled.
 *
 * @warning Agent must be running. Function returns error if gpg-agent is not available.
 *          Use gpg_agent_is_available() to check availability first.
 *
 * @ingroup crypto
 */
int gpg_agent_connect(void);

/**
 * @brief Disconnect from gpg-agent
 * @param sock Socket/pipe handle from gpg_agent_connect()
 *
 * Disconnects from gpg-agent by sending "BYE" command and closing connection.
 *
 * @note Cleanup: Sends "BYE" command to agent before closing connection.
 *       This ensures agent properly cleans up resources.
 *
 * @note Platform-specific cleanup:
 *       - Unix: Calls close() on socket
 *       - Windows: Calls CloseHandle() on named pipe
 *
 * @note Safe to call with invalid handle (-1 or INVALID_HANDLE_VALUE).
 *       Function checks handle validity before operations.
 *
 * @warning GPG support is currently disabled. This function may not work until
 *          GPG support is re-enabled.
 *
 * @ingroup crypto
 */
void gpg_agent_disconnect(int sock);

/**
 * @brief Check if GPG agent is available
 * @return true if gpg-agent is running and accessible, false otherwise
 *
 * Checks if gpg-agent is running and accessible by attempting to connect.
 *
 * @note Connection test: Attempts to connect to agent socket/pipe.
 *       If connection succeeds, agent is available.
 *
 * @note Agent path: Uses same path discovery as gpg_agent_connect().
 *       May fail if agent path is incorrect or agent is not running.
 *
 * @warning GPG support is currently disabled. This function may not work until
 *          GPG support is re-enabled.
 *
 * @ingroup crypto
 */
bool gpg_agent_is_available(void);

/** @} */

/**
 * @name GPG Signing Operations
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
 * Signs a message using GPG agent via Assuan protocol.
 * Uses keygrip to identify the key in the agent's keyring.
 *
 * @note Protocol flow:
 *       1. Send "SIGKEY" command with keygrip
 *       2. Send "SETHASH" command with message hash
 *       3. Handle "INQUIRE TBSDATA" (if needed) and send message data
 *       4. Send "PKSIGN" command to request signature
 *       5. Parse S-expression response to extract Ed25519 signature
 *
 * @note Signature format: GPG agent returns signature as S-expression:
 *       `(7:sig-val(5:eddsa(1:r32:%<hex>)(1:s32:%<hex>)))`
 *       Function extracts R and S values and concatenates them (R || S, 64 bytes).
 *
 * @note Keygrip format: 40-character hexadecimal string identifying the key.
 *       Keygrip is SHA-1 hash of key material.
 *
 * @note Message data: If agent requests TBSDATA (to-be-signed data), function
 *       sends message as hex-encoded string using "D" command, then "END".
 *
 * @note Platform-specific handle:
 *       - Unix: Socket file descriptor
 *       - Windows: HANDLE cast to int
 *
 * @warning GPG support is currently disabled. This function may not work until
 *          GPG support is re-enabled.
 *
 * @warning Keygrip validation: Function does not validate keygrip format.
 *          Invalid keygrip may cause agent errors.
 *
 * @warning Signature buffer: Must be at least 64 bytes for Ed25519 signatures.
 *          Function does not validate buffer size (may overflow).
 *
 * @ingroup crypto
 */
int gpg_agent_sign(int sock, const char *keygrip, const uint8_t *message, size_t message_len, uint8_t *signature_out,
                   size_t *signature_len_out);

/** @} */

/**
 * @name GPG Key Retrieval
 * @{
 */

/**
 * @brief Get public key from GPG keyring by key ID
 * @param key_id GPG key ID (16-char hex string, e.g., "EDDAE1DA7360D7F4", must not be NULL)
 * @param public_key_out Output buffer for 32-byte Ed25519 public key (must not be NULL)
 * @param keygrip_out Output buffer for 40-char keygrip (optional, can be NULL)
 * @return 0 on success, -1 on error
 *
 * Retrieves Ed25519 public key from GPG keyring by key ID.
 * Shells out to `gpg --list-keys --with-keygrip --with-colons` to get key information.
 *
 * @note Key ID format: 16-character hexadecimal string (8 bytes, 16 hex chars).
 *       Example: "EDDAE1DA7360D7F4"
 *
 * @note Key ID validation: Function validates key_id to prevent command injection.
 *       - Checks for shell-safe characters (alphanumeric, hex only)
 *       - Validates hex characters (0-9, a-f, A-F)
 *       - Escapes key_id using single quotes for safe shell use
 *
 * @note GPG command: Executes `gpg --list-keys --with-keygrip --with-colons 0x<key_id>`
 *       - `--with-colons`: Machine-readable output format
 *       - `--with-keygrip`: Includes keygrip in output
 *       - `0x` prefix: Required for GPG key ID format
 *
 * @note Key extraction: Parses colon-separated output to find:
 *       - Public key: From `pub:` or `sub:` line (Ed25519 subkey)
 *       - Keygrip: From `grp:` line (40-char hex string)
 *
 * @note Platform-specific command:
 *       - Unix: `gpg --list-keys --with-keygrip --with-colons 0x%s 2>/dev/null`
 *       - Windows: `gpg --list-keys --with-keygrip --with-colons 0x%s 2>nul`
 *
 * @warning GPG support is currently disabled. This function may not work until
 *          GPG support is re-enabled.
 *
 * @warning Security: Key ID is validated and escaped to prevent command injection.
 *          Do NOT pass unvalidated key IDs to this function.
 *
 * @warning GPG dependency: Requires GPG to be installed and in PATH.
 *          Returns error with installation instructions if GPG is not found.
 *
 * @warning Key format: Only Ed25519 keys are supported. Other key types (RSA, ECDSA)
 *          will return error or may not work correctly.
 *
 * @ingroup crypto
 */
int gpg_get_public_key(const char *key_id, uint8_t *public_key_out, char *keygrip_out);

/** @} */

/**
 * @name GPG Signature Verification
 * @{
 */

/**
 * @brief Verify a GPG Ed25519 signature using libgcrypt
 * @param public_key 32-byte Ed25519 public key (must not be NULL)
 * @param message Message that was signed (must not be NULL)
 * @param message_len Message length (must be > 0)
 * @param signature 64-byte Ed25519 signature (R || S format, must not be NULL)
 * @return 0 on success (signature valid), -1 on error or invalid signature
 *
 * Verifies an Ed25519 signature using libgcrypt.
 * Used for verifying signatures from GPG-signed keys.
 *
 * @note Signature format: Ed25519 signature is 64 bytes (R || S format).
 *       R and S are both 32 bytes.
 *
 * @note Verification: Uses libgcrypt's Ed25519 verification function.
 *       Returns 0 if signature is valid, -1 if invalid or error occurred.
 *
 * @note Library dependency: Requires libgcrypt to be compiled with support.
 *       May not be available if HAVE_LIBGCRYPT is not defined.
 *
 * @note Constant-time: Verification uses constant-time comparison to prevent timing attacks.
 *
 * @warning GPG support is currently disabled. This function may not work until
 *          GPG support is re-enabled.
 *
 * @warning Library dependency: Requires libgcrypt to be installed and linked.
 *          Returns error if libgcrypt is not available.
 *
 * @warning Always check return value. Returns -1 for both invalid signature and errors.
 *          Check error state separately if needed.
 *
 * @ingroup crypto
 */
int gpg_verify_signature(const uint8_t *public_key, const uint8_t *message, size_t message_len,
                         const uint8_t *signature);

/** @} */
