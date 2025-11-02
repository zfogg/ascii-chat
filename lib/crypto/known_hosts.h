#pragma once

/**
 * @file crypto/known_hosts.h
 * @ingroup crypto
 * @brief Known hosts management for MITM attack prevention
 *
 * This header provides known hosts management functionality similar to SSH's known_hosts.
 * Tracks server identity keys to detect man-in-the-middle attacks and key changes.
 *
 * Known hosts file format:
 * - Identity key entries: `<IP:port> x25519 <hex_key> [comment]`
 * - No-identity entries: `<IP:port> no-identity [comment]`
 *
 * @note File format examples:
 *       - IPv4: `192.0.2.1:8080 x25519 1234abcd... ascii-chat`
 *       - IPv6: `[2001:db8::1]:8080 x25519 1234abcd... ascii-chat`
 *       - No-identity: `192.0.2.1:8080 no-identity`
 *
 * @note No-identity servers: Servers without identity keys use "no-identity" entries.
 *       Cannot verify key (ephemeral keys change each connection) but can track server identity.
 *
 * @note Key comparison: Uses constant-time comparison (sodium_memcmp) to prevent timing attacks.
 *
 * @note MITM detection: Detects key mismatches by comparing received key with stored key.
 *       If key doesn't match, displays warning and prompts user for confirmation.
 *
 * @note Multiple entries: Can have multiple entries for same IP:port (e.g., key rotation).
 *       Function searches all entries and uses first matching key.
 *
 * @note Zero key handling: Special case for no-identity servers with zero keys.
 *       Zero key matches zero key (secure no-identity connection previously accepted).
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <stdint.h>
#include "../common.h"
#include <stdbool.h>

/**
 * @name Known Hosts Verification
 * @{
 */

/**
 * @brief Check if server key is in known_hosts
 * @param server_ip Server IP address (IPv4 or IPv6, must not be NULL)
 * @param port Server port number
 * @param server_key Server's Ed25519 public key (32 bytes, must not be NULL)
 * @return ASCIICHAT_OK if server not in known_hosts (first connection),
 *         positive value if key matches (connection verified),
 *         ERROR_CRYPTO_VERIFICATION if key mismatch (MITM warning)
 *
 * Checks if server key matches known hosts entry for the given IP:port.
 * Returns different values based on verification result.
 *
 * @note Return values:
 *       - ASCIICHAT_OK (0): Server not in known_hosts (first connection, needs verification)
 *       - Positive value (1): Key matches known_hosts (connection verified, safe to proceed)
 *       - ERROR_CRYPTO_VERIFICATION: Key mismatch (MITM attack or key rotation, needs user confirmation)
 *
 * @note File format: Searches for entries matching `<IP:port> x25519 <hex_key>`.
 *       Supports both IPv4 and IPv6 addresses with proper bracket notation.
 *
 * @note Multiple entries: If multiple entries exist for same IP:port, searches all entries
 *       and uses first matching key. Continues searching if key doesn't match.
 *
 * @note No-identity entries: Skips "no-identity" entries when checking identity keys.
 *       If server has identity key but entry is "no-identity", continues searching.
 *
 * @note Zero key handling: Special case for no-identity servers with zero keys.
 *       If both server key and stored key are zero, returns match (verified no-identity connection).
 *
 * @note Key comparison: Uses constant-time comparison (sodium_memcmp) to prevent timing attacks.
 *
 * @note File location: Uses `~/.ascii-chat/known_hosts` (or equivalent on Windows).
 *       Returns ASCIICHAT_OK if file doesn't exist (first connection).
 *
 * @warning This function should NOT be called for servers without identity keys.
 *          Use check_known_host_no_identity() for servers without identity keys.
 *
 * @warning Key mismatch: Returns ERROR_CRYPTO_VERIFICATION if key doesn't match.
 *          This indicates potential MITM attack or key rotation. User should verify.
 *
 * @ingroup crypto
 */
asciichat_error_t check_known_host(const char *server_ip, uint16_t port, const uint8_t server_key[32]);

/**
 * @brief Check known_hosts for servers without identity key (no-identity entries)
 * @param server_ip Server IP address (IPv4 or IPv6, must not be NULL)
 * @param port Server port number
 * @return ASCIICHAT_OK if server not in known_hosts (first connection),
 *         positive value if no-identity entry found (connection previously accepted),
 *         ERROR_CRYPTO_VERIFICATION if server previously had identity key but now has none
 *
 * Checks if server has "no-identity" entry in known_hosts.
 * Used for servers that don't have identity keys (ephemeral keys only).
 *
 * @note Return values:
 *       - ASCIICHAT_OK (0): Server not in known_hosts (first connection, needs verification)
 *       - Positive value (1): No-identity entry found (connection previously accepted)
 *       - ERROR_CRYPTO_VERIFICATION: Server previously had identity key but now has none (security concern)
 *
 * @note File format: Searches for entries matching `<IP:port> no-identity`.
 *       Does NOT verify keys (no keys to verify for no-identity servers).
 *
 * @note Purpose: This function is NOT for key verification (no keys to verify).
 *       Use check_known_host() for servers with identity keys.
 *
 * @note Security: Cannot verify server identity (no identity key to verify).
 *       Only tracks whether user previously accepted connection to this server.
 *
 * @warning This function should NOT be used for key verification.
 *          Use check_known_host() for servers with identity keys.
 *
 * @warning Security limitation: Cannot verify server identity (no keys to compare).
 *          Only provides tracking of previously accepted connections.
 *
 * @ingroup crypto
 */
asciichat_error_t check_known_host_no_identity(const char *server_ip, uint16_t port);

/** @} */

/**
 * @name Known Hosts Management
 * @{
 */

/**
 * @brief Add server to known_hosts
 * @param server_ip Server IP address (IPv4 or IPv6, must not be NULL)
 * @param port Server port number
 * @param server_key Server's Ed25519 public key (32 bytes, must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Adds server identity key to known_hosts file.
 * Creates file if it doesn't exist and creates directory if needed.
 *
 * @note File format: Adds entry as `<IP:port> x25519 <hex_key> [comment]`.
 *       Uses proper bracket notation for IPv6 addresses.
 *
 * @note File creation: Creates `~/.ascii-chat/known_hosts` if it doesn't exist.
 *       Creates `~/.ascii-chat/` directory if needed.
 *
 * @note File permissions: Sets file permissions to 0600 (Unix only).
 *       Windows does not have Unix-style permissions.
 *
 * @note Append mode: Appends entry to end of file (does not overwrite existing entries).
 *       Multiple entries for same IP:port are allowed (e.g., key rotation).
 *
 * @note Key format: Converts server key to hex string (64 hex chars) for storage.
 *       Key is stored as X25519 format (32 bytes).
 *
 * @warning File permissions: Sets restrictive permissions (0600) but function may fail
 *          if file was just created by fopen (chmod may fail). This is acceptable.
 *
 * @ingroup crypto
 */
asciichat_error_t add_known_host(const char *server_ip, uint16_t port, const uint8_t server_key[32]);

/**
 * @brief Remove server from known_hosts
 * @param server_ip Server IP address (IPv4 or IPv6, must not be NULL)
 * @param port Server port number
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Removes all entries for server IP:port from known_hosts file.
 * Removes both identity key entries and no-identity entries.
 *
 * @note Removal: Removes ALL entries matching IP:port (including multiple entries).
 *       File is rewritten with all matching entries removed.
 *
 * @note File format: Removes entries matching `<IP:port> x25519 ...` and `<IP:port> no-identity ...`.
 *       Uses proper bracket notation for IPv6 addresses.
 *
 * @note File preservation: Preserves all other entries and comments.
 *       Only removes entries matching the specified IP:port.
 *
 * @warning File is rewritten: Function reads entire file, removes matching entries, and rewrites.
 *          Original file is preserved as backup if possible.
 *
 * @ingroup crypto
 */
asciichat_error_t remove_known_host(const char *server_ip, uint16_t port);

/**
 * @brief Get known_hosts file path
 * @return Path to known_hosts file (never NULL, cached)
 *
 * Returns path to known_hosts file, expanding user directory if needed.
 * Path is cached after first call.
 *
 * @note File location: Returns `~/.ascii-chat/known_hosts` (or equivalent on Windows).
 *       Uses expand_path() to resolve user directory.
 *
 * @note Path caching: Path is cached after first call to avoid repeated expansion.
 *       Cache is freed by known_hosts_cleanup().
 *
 * @note Fallback: If path expansion fails, uses `/tmp/.ascii-chat/known_hosts` as fallback.
 *       Function always returns non-NULL path.
 *
 * @note Platform-specific:
 *       - Unix: `~/.ascii-chat/known_hosts`
 *       - Windows: `%APPDATA%\.ascii-chat\known_hosts`
 *
 * @warning Path may not exist: Function returns path even if file doesn't exist.
 *          File will be created when first entry is added.
 *
 * @ingroup crypto
 */
const char *get_known_hosts_path(void);

/** @} */

/**
 * @name User Interaction
 * @{
 */

/**
 * @brief Display MITM warning with key comparison and prompt user for confirmation
 * @param server_ip Server IP address (IPv4 or IPv6, must not be NULL)
 * @param port Server port number
 * @param expected_key Expected server key from known_hosts (32 bytes, must not be NULL)
 * @param received_key Received server key from connection (32 bytes, must not be NULL)
 * @return true if user accepts the risk and wants to continue, false otherwise
 *
 * Displays man-in-the-middle warning with key comparison and prompts user for confirmation.
 * Shows both expected and received keys in hex format for comparison.
 *
 * @note Warning display: Shows formatted warning message with:
 *       - Server IP:port
 *       - Expected key fingerprint (SHA256)
 *       - Received key fingerprint (SHA256)
 *       - Prompt for user confirmation
 *
 * @note Key fingerprints: Displays SHA256 fingerprints of both keys for easy comparison.
 *       Fingerprints are displayed in hex format (64 hex chars).
 *
 * @note User prompt: Prompts user to accept risk (continue) or abort connection.
 *       Returns true if user accepts, false if user aborts.
 *
 * @note Non-interactive mode: If not connected to TTY (snapshot mode), automatically
 *       accepts the connection (returns true). This allows automated connections.
 *
 * @note Security: Key mismatch indicates potential MITM attack or key rotation.
 *       User should verify keys before accepting.
 *
 * @warning Always check return value. If false, connection should be aborted.
 *
 * @warning MITM risk: Key mismatch may indicate man-in-the-middle attack.
 *          User should verify keys before accepting connection.
 *
 * @ingroup crypto
 */
bool display_mitm_warning(const char *server_ip, uint16_t port, const uint8_t expected_key[32],
                          const uint8_t received_key[32]);

/**
 * @brief Interactive prompt for unknown host - returns true if user wants to add, false to abort
 * @param server_ip Server IP address (IPv4 or IPv6, must not be NULL)
 * @param port Server port number
 * @param server_key Server's Ed25519 public key (32 bytes, must not be NULL)
 * @return true if user wants to add to known_hosts, false to abort
 *
 * Prompts user to add unknown host to known_hosts.
 * Displays server information and key fingerprint for user verification.
 *
 * @note Prompt display: Shows server IP:port and key fingerprint (SHA256).
 *       Prompts user to accept (add to known_hosts) or reject (abort connection).
 *
 * @note Key fingerprint: Displays SHA256 fingerprint of server key for verification.
 *       Fingerprint is displayed in hex format (64 hex chars).
 *
 * @note Non-interactive mode: If not connected to TTY (snapshot mode), automatically
 *       adds host to known_hosts (returns true). This allows automated connections.
 *
 * @note Security: User should verify key fingerprint before accepting.
 *       Only add host if key fingerprint matches expected value.
 *
 * @warning Always check return value. If false, connection should be aborted.
 *
 * @ingroup crypto
 */
bool prompt_unknown_host(const char *server_ip, uint16_t port, const uint8_t server_key[32]);

/**
 * @brief Interactive prompt for unknown host without identity key - returns true if user wants to continue, false to
 * abort
 * @param server_ip Server IP address (IPv4 or IPv6, must not be NULL)
 * @param port Server port number
 * @return true if user wants to continue (accepts no-identity connection), false to abort
 *
 * Prompts user to accept unknown host without identity key.
 * Displays server information and warns that identity cannot be verified.
 *
 * @note Prompt display: Shows server IP:port and warning that identity cannot be verified.
 *       Prompts user to accept (continue connection) or reject (abort connection).
 *
 * @note Security warning: Warns that server identity cannot be verified (no identity key).
 *       Connection is vulnerable to MITM attacks without identity verification.
 *
 * @note Non-interactive mode: If not connected to TTY (snapshot mode), automatically
 *       accepts connection (returns true). This allows automated connections.
 *
 * @note No-identity entries: If user accepts, adds "no-identity" entry to known_hosts.
 *       This tracks that user previously accepted this server without identity verification.
 *
 * @warning Security limitation: Cannot verify server identity (no keys to compare).
 *          Connection is vulnerable to MITM attacks.
 *
 * @warning Always check return value. If false, connection should be aborted.
 *
 * @ingroup crypto
 */
bool prompt_unknown_host_no_identity(const char *server_ip, uint16_t port);

/** @} */

/**
 * @name Key Fingerprinting
 * @{
 */

/**
 * @brief Compute SHA256 fingerprint of Ed25519 key for display
 * @param key Ed25519 public key (32 bytes, must not be NULL)
 * @param fingerprint Output buffer for fingerprint (65 bytes including null terminator)
 *
 * Computes SHA256 fingerprint of Ed25519 public key for display.
 * Fingerprint is displayed in hex format (64 hex chars + null terminator).
 *
 * @note Fingerprint format: SHA256 hash of key, displayed as 64 hex characters.
 *       Example: "a1b2c3d4e5f6..."
 *
 * @note Output format: Fingerprint is stored as null-terminated hex string (65 bytes total).
 *       First 64 bytes are hex characters, 65th byte is null terminator.
 *
 * @note Key format: Accepts Ed25519 public key (32 bytes).
 *       Fingerprint is computed over raw key bytes.
 *
 * @note Use case: Used for displaying key fingerprints in warnings and prompts.
 *       Helps users verify keys visually.
 *
 * @warning Output buffer must be at least 65 bytes (64 hex chars + null terminator).
 *          Function does not validate buffer size (may overflow).
 *
 * @ingroup crypto
 */
void compute_key_fingerprint(const uint8_t key[32], char fingerprint[65]);

/** @} */

/**
 * @name Cleanup
 * @{
 */

/**
 * @brief Cleanup function to free cached known_hosts path
 *
 * Frees cached known_hosts file path.
 * Should be called at program shutdown to clean up resources.
 *
 * @note Path caching: Path is cached after first call to get_known_hosts_path().
 *       This function frees the cached path.
 *
 * @note Safe to call multiple times: Function checks if cache exists before freeing.
 *       Safe to call even if cache was never allocated.
 *
 * @ingroup crypto
 */
void known_hosts_cleanup(void);

/** @} */
