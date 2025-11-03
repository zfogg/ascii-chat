/**
 * @file platform/symbols.h
 * @ingroup platform
 * @brief Symbol Resolution Cache for Backtrace Addresses
 *
 * This header provides a high-performance symbol resolution cache system for
 * converting backtrace addresses to human-readable symbol names. The system
 * caches resolved symbols to avoid expensive addr2line subprocess spawns on
 * every backtrace operation.
 *
 * CORE FEATURES:
 * ==============
 * - Cached symbol resolution using hashtable for O(1) lookups
 * - Batch address resolution using addr2line subprocess
 * - Automatic caching of resolved symbols for future lookups
 * - Comprehensive statistics tracking (hits, misses, entries)
 * - Thread-safe operations for concurrent backtrace processing
 * - Memory-efficient symbol string storage
 *
 * PERFORMANCE BENEFITS:
 * =====================
 * Symbol resolution without caching requires:
 * - Fork/exec subprocess for each address (expensive)
 * - addr2line parsing and symbol lookup (slow)
 * - File I/O for debug information (disk-bound)
 *
 * With caching:
 * - First resolution: Same cost as uncached (cache miss)
 * - Subsequent lookups: O(1) hashtable lookup (fast)
 * - Batch resolution: Single subprocess for multiple addresses
 * - Eliminates redundant addr2line invocations
 *
 * ARCHITECTURE:
 * =============
 * The symbol cache uses a hashtable to store resolved symbols:
 * - Key: Memory address (void*)
 * - Value: Symbol string (allocated, owned by cache)
 * - Lookup: O(1) average case using address as key
 * - Insertion: O(1) average case for new symbols
 *
 * BATCH RESOLUTION:
 * ================
 * For efficient backtrace processing:
 * - Collect all addresses from backtrace
 * - Check cache for each address (fast)
 * - Resolve uncached addresses in single addr2line invocation
 * - Cache all resolved symbols for future use
 *
 * STATISTICS TRACKING:
 * ====================
 * The system tracks:
 * - Cache hits: Successful lookups from cache
 * - Cache misses: Addresses not in cache (required resolution)
 * - Entry count: Number of cached symbols
 *
 * These statistics enable performance monitoring and cache efficiency analysis.
 *
 * THREAD SAFETY:
 * ==============
 * - All cache operations are thread-safe
 * - Concurrent lookups and insertions are supported
 * - Statistics are updated atomically
 * - Batch resolution is safe for concurrent use
 *
 * @note The symbol cache significantly improves backtrace performance by
 *       avoiding repeated addr2line subprocess spawns for the same addresses.
 * @note Cached symbol strings are owned by the cache and should not be freed
 *       by callers. The cache manages memory automatically.
 * @note addr2line must be available in PATH for batch resolution to work.
 * @note Debug symbols must be available in the executable for resolution.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "../common.h"

/* ============================================================================
 * Symbol Cache Lifecycle
 * @{
 */

/**
 * @brief Initialize the symbol cache
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Initializes the global symbol cache system. Creates the hashtable and
 * initializes statistics counters. Must be called before using any other
 * symbol cache functions.
 *
 * @note Idempotent: Safe to call multiple times (no-op after first call).
 * @note Thread-safe: Can be called from any thread during initialization.
 *
 * @ingroup platform
 */
asciichat_error_t symbol_cache_init(void);

/**
 * @brief Clean up the symbol cache and free all resources
 *
 * Destroys the symbol cache and frees all cached symbols and internal
 * structures. Should be called at application shutdown after all backtraces
 * are complete.
 *
 * @note Safe to call multiple times (no-op after first call).
 * @note All cached symbol strings are freed automatically.
 *
 * @ingroup platform
 */
void symbol_cache_cleanup(void);

/** @} */

/* ============================================================================
 * Symbol Cache Operations
 * @{
 */

/**
 * @brief Look up a symbol for a given address
 * @param addr Address to resolve (must not be NULL)
 * @return Cached symbol string, or NULL if not in cache
 *
 * Performs a hashtable lookup to find the cached symbol for the given address.
 * Returns NULL if the address is not in the cache (cache miss). Use
 * symbol_cache_resolve_batch() to resolve uncached addresses.
 *
 * @note This is a fast O(1) lookup operation (hashtable lookup).
 * @note Returned string is owned by the cache and should not be freed.
 * @note String remains valid until cache is cleaned up.
 * @note Thread-safe: Can be called from multiple threads simultaneously.
 *
 * @note For batch resolution, use symbol_cache_resolve_batch() which handles
 *       both cache lookup and addr2line resolution automatically.
 *
 * @ingroup platform
 */
const char *symbol_cache_lookup(void *addr);

/**
 * @brief Insert a symbol into the cache
 * @param addr Address to cache (must not be NULL)
 * @param symbol Symbol string to cache (must not be NULL, will be copied)
 * @return true on success, false on failure (memory allocation error)
 *
 * Inserts a symbol into the cache for future lookups. The symbol string is
 * copied and owned by the cache. If the address already exists in the cache,
 * the symbol is updated.
 *
 * @note Symbol string is copied, so caller can free the original string.
 * @note Thread-safe: Can be called from multiple threads simultaneously.
 * @note Statistics are updated (entry count, etc.).
 *
 * @warning Memory allocation failures return false. Check return value.
 *
 * @ingroup platform
 */
bool symbol_cache_insert(void *addr, const char *symbol);

/** @} */

/* ============================================================================
 * Symbol Cache Statistics
 * @{
 */

/**
 * @brief Get cache statistics
 * @param hits_out Pointer to receive hit count (can be NULL)
 * @param misses_out Pointer to receive miss count (can be NULL)
 * @param entries_out Pointer to receive entry count (can be NULL)
 *
 * Retrieves cumulative statistics from the symbol cache. Useful for
 * performance monitoring and cache efficiency analysis.
 *
 * @note All counters are cumulative since cache initialization.
 * @note Thread-safe: Can be called from any thread.
 *
 * @ingroup platform
 */
void symbol_cache_get_stats(uint64_t *hits_out, uint64_t *misses_out, size_t *entries_out);

/**
 * @brief Print cache statistics to logging system
 *
 * Logs detailed statistics from the symbol cache including hit rate,
 * miss count, and entry count. Useful for periodic performance monitoring.
 *
 * @note Requires logging system to be initialized.
 * @note Statistics are formatted and logged at INFO level.
 *
 * @ingroup platform
 */
void symbol_cache_print_stats(void);

/** @} */

/* ============================================================================
 * Batch Symbol Resolution
 * @{
 */

/**
 * @brief Resolve multiple addresses using addr2line and cache results
 * @param buffer Array of addresses to resolve (must not be NULL)
 * @param size Number of addresses in buffer (must be > 0)
 * @return Array of symbol strings (caller must free with symbol_cache_free_symbols), or NULL on error
 *
 * Resolves multiple addresses to symbol names using addr2line. For each address:
 * 1. Checks cache first (fast O(1) lookup)
 * 2. If not cached, resolves using addr2line subprocess
 * 3. Caches resolved symbol for future lookups
 * 4. Returns symbol string in output array
 *
 * This function is optimized for batch backtrace processing:
 * - Checks cache for all addresses first (fast)
 * - Resolves only uncached addresses in single addr2line invocation
 * - Caches all resolved symbols automatically
 *
 * @note Returned array has size elements (one per input address).
 * @note Array elements are NULL if symbol resolution failed for that address.
 * @note Array elements point to cached symbol strings (owned by cache).
 * @note Call symbol_cache_free_symbols() to free the array (not the strings).
 *
 * @note addr2line must be available in PATH for resolution to work.
 * @note Debug symbols must be available in the executable.
 *
 * @warning Caller must free the returned array using symbol_cache_free_symbols().
 *          Do NOT free individual strings (owned by cache).
 *
 * @ingroup platform
 */
char **symbol_cache_resolve_batch(void *const *buffer, int size);

/**
 * @brief Free symbol array returned by symbol_cache_resolve_batch
 * @param symbols Array of symbol strings (can be NULL)
 *
 * Frees the array structure returned by symbol_cache_resolve_batch().
 * This only frees the array itself, not the individual symbol strings
 * (which are owned by the cache).
 *
 * @note Safe to call with NULL pointer (no-op).
 * @note This function only frees the array structure, not cached symbol strings.
 *
 * @ingroup platform
 */
void symbol_cache_free_symbols(char **symbols);

/** @} */
