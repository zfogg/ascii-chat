#pragma once

/**
 * @file crypto/gpg.h
 * @brief GPG agent interface for signing operations
 * @ingroup crypto
 * @addtogroup crypto
 * @{
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

/**
 * @brief Sign a message using GPG key (via gpg --detach-sign)
 * @param key_id GPG key ID (e.g., "7FE90A79F2E80ED3")
 * @param message Message to sign (must not be NULL)
 * @param message_len Message length (must be > 0)
 * @param signature_out Output buffer for signature (must provide at least 512 bytes)
 * @param signature_len_out Actual signature length written
 * @return 0 on success, -1 on error
 *
 * Creates a GPG detached signature by calling `gpg --detach-sign`.
 * Uses gpg-agent internally, so no passphrase prompt if key is cached.
 *
 * @note Signature format: Returns OpenPGP packet format signature (~119 bytes for Ed25519).
 *       This is compatible with `gpg --verify` for verification.
 *
 * @note GPG Agent: This function uses `gpg --detach-sign` which uses gpg-agent internally.
 *       If the key is unlocked in gpg-agent, no passphrase prompt will appear.
 *
 * @note Process safety: Uses process-specific temp files to support concurrent signing.
 *
 * @warning Buffer size: Caller must provide at least 512 bytes in signature_out buffer.
 *          Typical Ed25519 signature is ~119 bytes in OpenPGP format.
 *
 * @warning GPG dependency: Requires GPG to be installed and in PATH.
 *
 * @ingroup crypto
 */
int gpg_sign_with_key(const char *key_id, const uint8_t *message, size_t message_len, uint8_t *signature_out,
                      size_t *signature_len_out);

/**
 * @brief Sign message using gpg --detach-sign and extract raw Ed25519 signature
 * @param key_id GPG key ID (16-char hex string)
 * @param message Message to sign
 * @param message_len Message length
 * @param signature_out Output buffer for 64-byte Ed25519 signature (must not be NULL)
 * @return 0 on success, -1 on error
 *
 * Fallback function used when GPG agent is not available.
 * Uses `gpg --detach-sign` to create an OpenPGP signature packet,
 * then parses the packet to extract the raw 64-byte Ed25519 signature.
 *
 * @note This function is used as a fallback when GPG agent connection fails.
 *       It allows signing operations to work even when gpg-agent is not running.
 *
 * @warning Requires GPG binary in PATH and key must be unlocked or have no passphrase.
 *
 * @ingroup crypto
 */
int gpg_sign_detached_ed25519(const char *key_id, const uint8_t *message, size_t message_len,
                              uint8_t signature_out[64]);

/**
 * @brief Verify GPG signature using gpg --verify
 * @param key_id GPG key ID (16-char hex string) to use for verification
 * @param message Message that was signed
 * @param message_len Message length
 * @param signature 64-byte Ed25519 signature (raw R||S format)
 * @return 0 on success (signature valid), -1 on error or invalid signature
 *
 * Fallback verification function that uses `gpg --verify` command.
 * Takes a raw 64-byte Ed25519 signature, reconstructs the OpenPGP signature packet,
 * and verifies it using GPG.
 *
 * @note This is the counterpart to gpg_sign_detached_ed25519().
 * @ingroup crypto
 */
int gpg_verify_detached_ed25519(const char *key_id, const uint8_t *message, size_t message_len,
                                const uint8_t signature[64]);

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

/**
 * @brief Verify a GPG signature using gpg --verify binary
 * @param signature GPG signature in OpenPGP packet format (must not be NULL)
 * @param signature_len Signature length (typically ~119 bytes for Ed25519, max 512 bytes)
 * @param message Message that was signed (must not be NULL)
 * @param message_len Message length (must be > 0)
 * @param expected_key_id Expected GPG key ID (16-char hex, optional, can be NULL)
 * @return 0 on success (signature valid), -1 on error or invalid signature
 *
 * Verifies a GPG signature by calling `gpg --verify` binary.
 * This approach uses GPG's internal verification which handles OpenPGP packet format.
 *
 * @note Workaround for issue #92: This function works with signatures created by
 *       `gpg --detach-sign` which uses gpg-agent internally and creates proper
 *       OpenPGP packet format signatures.
 *
 * @note Same approach as Git: Git uses `gpg --verify` for commit signature verification
 *       instead of calling libgcrypt API directly.
 *
 * @note Signature format: Expects OpenPGP packet format signature (not raw 64-byte Ed25519).
 *       Typical Ed25519 detached signature is ~119 bytes in OpenPGP format.
 *       Function writes this to temporary file for GPG verification.
 *
 * @note Verification flow:
 *       1. Write signature to /tmp/asciichat_sig_<PID>_XXXXXX (Unix) or %TEMP%\asc_sig_<PID>_*.tmp (Windows)
 *       2. Write message to /tmp/asciichat_msg_<PID>_XXXXXX (Unix) or %TEMP%\asc_msg_<PID>_*.tmp (Windows)
 *       3. Call `gpg --verify /tmp/sig /tmp/msg`
 *       4. Parse GPG output for "Good signature"
 *       5. Verify key ID matches expected_key_id (if provided)
 *       6. Cleanup temp files
 *
 * @note Process safety: Temp files include process ID (PID) in their names to ensure concurrent
 *       ascii-chat processes can run GPG verification simultaneously without conflicts.
 *
 * @note Output parsing: Looks for "Good signature" in GPG output and verifies exit code is 0.
 *       If expected_key_id is provided, also checks that GPG output contains the key ID.
 *
 * @note Performance: ~10-50ms overhead per verification due to shell execution and temp file I/O.
 *       Acceptable for authentication (infrequent) but not suitable for per-packet verification.
 *
 * @note Security considerations:
 *       - Uses mkstemp() to create temp files with mode 0600 (owner-only)
 *       - Signature/message written to temp files, not passed as shell arguments (no command injection)
 *       - Temp files cleaned up even on error (RAII pattern)
 *       - Validates GPG exit code and output parsing
 *
 * @warning Requires GPG binary: Function shells out to `gpg --verify`.
 *          Returns error if GPG is not installed or not in PATH.
 *
 * @warning Temp file I/O: Creates temporary files in /tmp.
 *          May fail if /tmp is full or not writable.
 *
 * @warning Platform-specific: Uses mkstemp() (Unix) or equivalent (Windows).
 *          Temp file paths differ by platform.
 *
 * @ingroup crypto
 */
int gpg_verify_signature_with_binary(const uint8_t *signature, size_t signature_len, const uint8_t *message,
                                     size_t message_len, const char *expected_key_id);

/** @} */
