#pragma once

#include <stdint.h>
#include "../common.h"
#include <stdbool.h>

// Check if server key is in known_hosts
// Returns:
//   1 = key matches (all good)
//   0 = server not in known_hosts (first connection)
//  -1 = key mismatch (MITM warning!)
// Note: For servers without identity keys, this function should not be called
// as ephemeral keys change on every connection and cannot be verified this way
asciichat_error_t check_known_host(const char *server_ip, uint16_t port, const uint8_t server_key[32]);

// Check known_hosts for servers without identity key (no-identity entries)
// Returns:
//   1 = known host (no-identity entry found)
//   0 = unknown host (first connection)
//  -1 = error (server previously had identity key but now has none)
// Note: This function should NOT be used for key verification - use check_known_host instead
asciichat_error_t check_known_host_no_identity(const char *server_ip, uint16_t port);

// Add server to known_hosts
asciichat_error_t add_known_host(const char *server_ip, uint16_t port, const uint8_t server_key[32]);

// Remove server from known_hosts
asciichat_error_t remove_known_host(const char *server_ip, uint16_t port);

// Get known_hosts file path
const char *get_known_hosts_path(void);

// Display MITM warning with key comparison and prompt user for confirmation
// Returns true if user accepts the risk and wants to continue, false otherwise
bool display_mitm_warning(const char *server_ip, uint16_t port, const uint8_t expected_key[32],
                          const uint8_t received_key[32]);

// Interactive prompt for unknown host - returns true if user wants to add, false to abort
bool prompt_unknown_host(const char *server_ip, uint16_t port, const uint8_t server_key[32]);

// Interactive prompt for unknown host without identity key - returns true if user wants to continue, false to abort
bool prompt_unknown_host_no_identity(const char *server_ip, uint16_t port);

// Compute SHA256 fingerprint of Ed25519 key for display
void compute_key_fingerprint(const uint8_t key[32], char fingerprint[65]);
