/**
 * @file symbols.h
 * @brief Symbol resolution cache for backtrace addresses
 *
 * Provides cached symbol resolution using addr2line to avoid expensive
 * subprocess spawns on every backtrace. Uses hashtable for O(1) lookups.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2025
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Initialize the symbol cache
 * @return 0 on success, -1 on failure
 */
int symbol_cache_init(void);

/**
 * @brief Clean up the symbol cache and free all resources
 */
void symbol_cache_cleanup(void);

/**
 * @brief Look up a symbol for a given address
 * @param addr Address to resolve
 * @return Cached symbol string, or NULL if not in cache
 * @note Returned string is owned by cache - do not free
 */
const char *symbol_cache_lookup(void *addr);

/**
 * @brief Insert a symbol into the cache
 * @param addr Address to cache
 * @param symbol Symbol string to cache (will be copied)
 * @return true on success, false on failure
 */
bool symbol_cache_insert(void *addr, const char *symbol);

/**
 * @brief Get cache statistics
 * @param hits_out Pointer to receive hit count (can be NULL)
 * @param misses_out Pointer to receive miss count (can be NULL)
 * @param entries_out Pointer to receive entry count (can be NULL)
 */
void symbol_cache_get_stats(uint64_t *hits_out, uint64_t *misses_out, size_t *entries_out);

/**
 * @brief Print cache statistics to log
 */
void symbol_cache_print_stats(void);

/**
 * @brief Resolve multiple addresses using addr2line and cache results
 * @param buffer Array of addresses to resolve
 * @param size Number of addresses
 * @return Array of symbol strings (caller must free with symbol_cache_free_symbols)
 */
char **symbol_cache_resolve_batch(void *const *buffer, int size);

/**
 * @brief Free symbol array returned by symbol_cache_resolve_batch
 * @param symbols Array of symbol strings
 */
void symbol_cache_free_symbols(char **symbols);
