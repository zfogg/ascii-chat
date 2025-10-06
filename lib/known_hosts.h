#pragma once

#include <stdint.h>
#include <stdbool.h>

// Check if server key is in known_hosts
// Returns:
//   1 = key matches (all good)
//   0 = server not in known_hosts (first connection)
//  -1 = key mismatch (MITM warning!)
int check_known_host(const char *hostname, uint16_t port, const uint8_t server_key[32]);

// Add server to known_hosts
int add_known_host(const char *hostname, uint16_t port, const uint8_t server_key[32]);

// Remove server from known_hosts
int remove_known_host(const char *hostname, uint16_t port);

// Get known_hosts file path
const char *get_known_hosts_path(void);

// Display MITM warning with key comparison
void display_mitm_warning(const uint8_t expected_key[32], const uint8_t received_key[32]);
