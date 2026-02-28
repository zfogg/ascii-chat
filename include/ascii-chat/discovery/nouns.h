/**
 * @file discovery/nouns.h
 * @ingroup discovery
 * @brief Wordlist for generating friendly names (for clients, servers, etc.)
 */

#ifndef ASCII_CHAT_DISCOVERY_NOUNS_H
#define ASCII_CHAT_DISCOVERY_NOUNS_H

#include <stddef.h>

/**
 * @brief Array of English nouns for friendly name generation
 */
extern const char *nouns[];
extern const size_t nouns_count;

/**
 * @brief Generate a unique client name from the nouns wordlist
 *
 * Generates a name in the format "noun.N (transport:port)" where N is a counter to ensure uniqueness.
 * Examples: "mountain.0 (tcp:48036)", "tiger.1 (webrtc:52441)"
 * IMPORTANT: Caller MUST hold write lock on g_client_manager_rwlock before calling.
 *
 * @param buffer Output buffer for the generated name
 * @param buffer_size Size of the output buffer
 * @param existing_clients Hash table of existing clients to check for name collisions
 * @param port Port number for the client connection
 * @param is_tcp True for TCP clients, false for WebRTC
 * @return 0 on success, -1 on error
 */
int generate_client_name(char *buffer, size_t buffer_size, void *existing_clients_hash, int port, bool is_tcp);

#endif // ASCII_CHAT_DISCOVERY_NOUNS_H
