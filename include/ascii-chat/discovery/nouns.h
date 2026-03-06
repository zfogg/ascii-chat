/**
 * @file discovery/nouns.h
 * @ingroup discovery
 * @brief Wordlist and utilities for generating friendly session and client names
 *
 * Provides a curated list of 5000 English nouns for:
 * - Session identifiers: "adjective-noun-noun" format in ACDS
 * - Client display names: "noun.counter (transport:port)" format on servers
 *
 * Two distinct generation functions serve different use cases:
 * - `generate_client_id()` - Simple noun-only ID for internal identification
 * - `generate_client_name()` - Full formatted name for user-facing displays
 */

#ifndef ASCII_CHAT_DISCOVERY_NOUNS_H
#define ASCII_CHAT_DISCOVERY_NOUNS_H

#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Array of English nouns for session and client naming
 *
 * A curated list of 5000 nouns selected for:
 * - Easy pronunciation and memorability
 * - Neutral tone suitable for formal and casual contexts
 * - Variety (objects, animals, places, concepts, etc.)
 *
 * Used for both:
 * 1. Session identifiers (2 nouns + 1 adjective = "adjective-noun-noun")
 * 2. Client display names (1 noun + counter + transport info)
 *
 * Example values: "abandon", "abbey", "ability", "absent", "absolute", ..., "zoom", "zoo"
 *
 * @see nouns_count
 * @see generate_client_id()
 * @see generate_client_name()
 * @see include/ascii-chat/discovery/adjectives.h for the adjective wordlist
 */
extern const char *nouns[];

/**
 * @brief Count of nouns in the nouns array
 *
 * Always equals 5000. Used for bounds checking and random selection.
 *
 * @see nouns
 */
extern const size_t nouns_count;

/**
 * @brief Generate a unique client ID (noun only, no counter or port)
 *
 * Returns just the noun part without counters or transport information. Used for internal
 * client identification in thread names, buffer pools, event logs, etc.
 *
 * @param buffer Output buffer for the generated ID
 * @param buffer_size Size of the output buffer (minimum recommended: 32 bytes)
 * @return 0 on success, -1 on error (invalid args or buffer too small)
 *
 * @note Does not guarantee uniqueness. For user-facing names, use generate_client_name() instead.
 *
 * @example
 * ```c
 * char id[32];
 * if (generate_client_id(id, sizeof(id)) == 0) {
 *     printf("Client ID: %s\n", id);  // Output: "Client ID: mountain"
 * }
 * ```
 *
 * @see generate_client_name() for client display names with uniqueness guarantees
 */
int generate_client_id(char *buffer, size_t buffer_size);

/**
 * @brief Generate a unique display name for a connected client
 *
 * Generates a name in the format "noun.counter (transport:port)" ensuring uniqueness
 * by checking against existing clients in the provided hash table.
 *
 * Format examples:
 * - "mountain.0 (tcp:48036)" - TCP client
 * - "tiger.1 (webrtc:52441)" - WebRTC client
 * - "ocean.0 (tcp:27224)" - Another TCP client
 *
 * If collision occurs (name already exists), increments counter and tries again.
 * Falls back to numeric-only format "client_N (transport:port)" if unable to find
 * a unique noun-based name after 100 attempts.
 *
 * @param buffer Output buffer for the generated name
 * @param buffer_size Size of the output buffer (minimum recommended: 64 bytes)
 * @param existing_clients_hash Hash table of existing client_info_t structures (may be NULL)
 * @param port Port number for the client connection
 * @param is_tcp True for TCP transport, false for WebRTC
 * @return 0 on success, -1 on error (invalid args or buffer too small)
 *
 * @pre Caller MUST hold a write lock on g_client_manager_rwlock before calling.
 * This function iterates through existing_clients_hash unsafely and requires exclusive access.
 *
 * @note Uniqueness is only guaranteed if existing_clients_hash contains all currently
 * connected clients and is not modified during the call.
 *
 * @example
 * ```c
 * char name[64];
 * // Assuming locked write access to g_client_manager_rwlock
 * if (generate_client_name(name, sizeof(name), existing_clients, 48036, true) == 0) {
 *     printf("Client display name: %s\n", name);  // "Client display name: mountain.0 (tcp:48036)"
 * }
 * ```
 *
 * @see generate_client_id() for simple internal identification (no uniqueness check)
 * @see lib/network/client.h for client_info_t structure
 */
int generate_client_name(char *buffer, size_t buffer_size, void *existing_clients_hash, int port, bool is_tcp);

#endif // ASCII_CHAT_DISCOVERY_NOUNS_H
