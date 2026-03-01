/**
 * @file util/atomic_helpers.h
 * @brief ⚛️ Atomic operations and synchronization helpers
 * @ingroup util
 *
 * Provides convenience macros and inline functions for common atomic
 * operations patterns used throughout the codebase.
 *
 * These helpers reduce code duplication for patterns like:
 * - Checking shutdown flags
 * - Safe atomic loads/stores
 * - Compare-and-swap operations with error handling
 *
 * All operations are thread-safe using C11 atomics.
 *
 * Usage:
 * @code
 * atomic_t g_should_exit = false;
 *
 * // Check shutdown flag
 * if (SHOULD_EXIT()) {
 *     break;
 * }
 *
 * // Safe atomic load
 * uint32_t client_id = ATOMIC_LOAD_UINT32(&client->client_id);
 *
 * // Compare and swap with status checking
 * bool was_active = ATOMIC_CAS_BOOL(&client->active, true, false);
 * @endcode
 */

#pragma once

#include <ascii-chat/atomic.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * Check if the global server should exit.
 * Requires g_should_exit to be declared as atomic_t in the compilation unit.
 *
 * @return true if shutdown is requested, false otherwise
 *
 * Usage:
 * @code
 * extern atomic_t g_should_exit;
 *
 * while (!SHOULD_EXIT()) {
 *     process_event();
 * }
 * @endcode
 *
 * @note This assumes g_should_exit is declared in the same file or extern declared.
 */
#define SHOULD_EXIT() (atomic_load(&g_should_exit))

/**
 * Check if the global client should exit.
 * Requires g_should_exit to be declared as atomic_t in the compilation unit.
 *
 * @return true if shutdown is requested, false otherwise
 *
 * Usage:
 * @code
 * extern atomic_t g_should_exit;
 *
 * while (!CLIENT_SHOULD_EXIT()) {
 *     process_client_event();
 * }
 * @endcode
 */
#define CLIENT_SHOULD_EXIT() (atomic_load(&g_should_exit))

/**
 * Safely load a boolean atomic value.
 *
 * @param ptr Pointer to atomic_t variable
 * @return Current value of the atomic variable
 *
 * Usage:
 * @code
 * bool is_active = ATOMIC_LOAD_BOOL(&client->active);
 * @endcode
 */
#define ATOMIC_LOAD_BOOL(ptr) atomic_load((atomic_t *)(ptr))

/**
 * Safely load an unsigned 32-bit atomic value.
 *
 * @param ptr Pointer to atomic_uint32_t variable
 * @return Current value of the atomic variable
 *
 * Usage:
 * @code
 * uint32_t client_id = ATOMIC_LOAD_UINT32(&client->client_id);
 * @endcode
 */
#define ATOMIC_LOAD_UINT32(ptr) atomic_load((atomic_uint32_t *)(ptr))

/**
 * Safely load an unsigned 64-bit atomic value.
 *
 * @param ptr Pointer to atomic_uint64_t variable
 * @return Current value of the atomic variable
 *
 * Usage:
 * @code
 * uint64_t timestamp = ATOMIC_LOAD_UINT64(&client->last_activity);
 * @endcode
 */
#define ATOMIC_LOAD_UINT64(ptr) atomic_load((atomic_uint64_t *)(ptr))

/**
 * Safely store a boolean atomic value.
 *
 * @param ptr Pointer to atomic_t variable
 * @param value Value to store
 *
 * Usage:
 * @code
 * ATOMIC_STORE_BOOL(&client->active, true);
 * @endcode
 */
#define ATOMIC_STORE_BOOL(ptr, value) atomic_store((atomic_t *)(ptr), (value))

/**
 * Safely store an unsigned 32-bit atomic value.
 *
 * @param ptr Pointer to atomic_uint32_t variable
 * @param value Value to store
 *
 * Usage:
 * @code
 * ATOMIC_STORE_UINT32(&client->client_id, 42);
 * @endcode
 */
#define ATOMIC_STORE_UINT32(ptr, value) atomic_store((atomic_uint32_t *)(ptr), (value))

/**
 * Safely store an unsigned 64-bit atomic value.
 *
 * @param ptr Pointer to atomic_uint64_t variable
 * @param value Value to store
 *
 * Usage:
 * @code
 * ATOMIC_STORE_UINT64(&client->last_activity, get_time_ms());
 * @endcode
 */
#define ATOMIC_STORE_UINT64(ptr, value) atomic_store((atomic_uint64_t *)(ptr), (value))

/**
 * Compare and swap operation for boolean atomics.
 * Atomically compares the value at ptr with expected, and if equal,
 * stores new_value and returns true. Otherwise returns false.
 *
 * @param ptr Pointer to atomic_t variable
 * @param expected Expected value
 * @param new_value New value to store if comparison succeeds
 * @return true if swap succeeded (value was equal to expected), false otherwise
 *
 * Usage:
 * @code
 * // Try to set client as active (was inactive)
 * if (ATOMIC_CAS_BOOL(&client->active, false, true)) {
 *     log_debug("Client activated");
 * } else {
 *     log_debug("Client already active");
 * }
 * @endcode
 */
#define ATOMIC_CAS_BOOL(ptr, expected, new_value)                                                                      \
  atomic_compare_exchange_strong((atomic_t *)(ptr), &(expected), (new_value))

/**
 * Compare and swap operation for unsigned 32-bit atomics.
 *
 * @param ptr Pointer to atomic_uint32_t variable
 * @param expected Expected value (passed by reference)
 * @param new_value New value to store if comparison succeeds
 * @return true if swap succeeded, false otherwise
 *
 * Usage:
 * @code
 * uint32_t expected = current_id;
 * if (ATOMIC_CAS_UINT32(&g_next_client_id, &expected, current_id + 1)) {
 *     log_debug("Claimed client ID %u", expected);
 * }
 * @endcode
 */
#define ATOMIC_CAS_UINT32(ptr, expected, new_value)                                                                    \
  atomic_compare_exchange_strong((atomic_uint32_t *)(ptr), (expected), (new_value))

/**
 * Atomically add to an unsigned 32-bit value.
 *
 * @param ptr Pointer to atomic_uint32_t variable
 * @param delta Value to add
 * @return Previous value of the atomic variable
 *
 * Usage:
 * @code
 * uint32_t previous = ATOMIC_ADD_UINT32(&g_frame_counter, 1);
 * log_debug("Frame count before: %u", previous);
 * @endcode
 */
#define ATOMIC_ADD_UINT32(ptr, delta) atomic_fetch_add((atomic_uint32_t *)(ptr), (delta))

/**
 * Atomically subtract from an unsigned 32-bit value.
 *
 * @param ptr Pointer to atomic_uint32_t variable
 * @param delta Value to subtract
 * @return Previous value of the atomic variable
 *
 * Usage:
 * @code
 * uint32_t previous = ATOMIC_SUB_UINT32(&g_active_clients, 1);
 * log_debug("Active clients before: %u", previous);
 * @endcode
 */
#define ATOMIC_SUB_UINT32(ptr, delta) atomic_fetch_sub((atomic_uint32_t *)(ptr), (delta))

#endif
