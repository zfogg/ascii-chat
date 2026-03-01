/**
 * @file atomic.h
 * @brief ⚛️ Atomic operations abstraction layer with debug tracking
 * @ingroup util
 * @date March 2026
 *
 * Provides a platform-independent wrapper for atomic operations similar to
 * mutex_t, rwlock_t, and cond_t abstractions. Supports named registration,
 * debug tracking, and integration with --sync-state output.
 *
 * Design principles:
 * - No recursion (atomics never depend on other atomics)
 * - No mutexes in implementation (uses raw C11 atomics internally)
 * - Named registration via named.c (NAMED_REGISTER_ATOMIC macro)
 * - Debug-only timing and count tracking
 * - Exchange operations for atomic min/max patterns (avoids TOCTOU races)
 *
 * Supported types via typed macros:
 * - bool: atomic_load_bool, atomic_store_bool, atomic_cas_bool, atomic_exchange_bool
 * - int: atomic_load_int, atomic_store_int, atomic_fetch_add_int, atomic_fetch_sub_int
 * - uint64_t: atomic_load_u64, atomic_store_u64, atomic_fetch_add_u64, atomic_fetch_sub_u64, atomic_exchange_u64
 * - Pointers: atomic_ptr_load, atomic_ptr_store, atomic_ptr_cas, atomic_ptr_exchange
 *
 * Usage:
 * @code
 * // Declare a named atomic
 * atomic_t g_should_exit = ATOMIC_INIT(false, "g_should_exit");
 *
 * // Load/store operations
 * bool value = atomic_load_bool(&g_should_exit);
 * atomic_store_bool(&g_should_exit, true);
 *
 * // In release builds: zero overhead, calls _impl functions directly
 * // In debug builds: includes registration and timing hooks
 * @endcode
 */

#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Core Atomic Types
// ============================================================================

/**
 * @brief Atomic value wrapper for integral/boolean types
 * @ingroup util
 *
 * Wraps C11 _Atomic(uint64_t) with debug tracking fields.
 * All values (bool, int, uint64_t, size_t) fit in uint64_t on 64-bit platforms.
 *
 * The name is registered separately via NAMED_REGISTER_ATOMIC macro,
 * stored in named.c registry (not in this struct).
 *
 * Usage:
 * @code
 * // Global declaration
 * atomic_t g_counter = {0};
 *
 * // Register name for debug tracking (in main()):
 * NAMED_REGISTER_ATOMIC(&g_counter, "g_counter");
 *
 * // Use:
 * atomic_store_u64(&g_counter, 0);
 * uint64_t val = atomic_load_u64(&g_counter);
 * @endcode
 */
typedef struct {
    _Atomic(uint64_t) impl;  ///< Underlying C11 atomic value
#ifndef NDEBUG
    uint64_t          last_store_time_ns;
    uint64_t          last_load_time_ns;
    uint64_t          store_count;
    uint64_t          load_count;
    uint64_t          cas_count;
    uint64_t          cas_success_count;
    uint64_t          fetch_count;
#endif
} atomic_t;

/**
 * @brief Atomic pointer wrapper
 * @ingroup util
 *
 * Wraps C11 _Atomic(void *) with debug tracking fields.
 * The name is registered separately via NAMED_REGISTER_ATOMIC_PTR,
 * stored in named.c registry (not in this struct).
 *
 * Usage:
 * @code
 * _Atomic(void *) g_ptr = {0};
 *
 * NAMED_REGISTER_ATOMIC_PTR(&g_ptr, "g_ptr");
 *
 * atomic_ptr_store(&g_ptr, some_pointer);
 * @endcode
 */
typedef struct {
    _Atomic(void *)   impl;  ///< Underlying C11 atomic pointer
#ifndef NDEBUG
    uint64_t          last_store_time_ns;
    uint64_t          last_load_time_ns;
    uint64_t          store_count;
    uint64_t          load_count;
    uint64_t          cas_count;
    uint64_t          cas_success_count;
    uint64_t          exchange_count;
#endif
} atomic_ptr_t;


// ============================================================================
// bool Operations
// ============================================================================

/**
 * @brief Atomically load a boolean value
 * @param a Pointer to atomic_t
 * @return Current boolean value
 */
#ifndef NDEBUG
bool atomic_load_bool(atomic_t *a);
#else
#define atomic_load_bool(a) ((bool)atomic_load_bool_impl(a))
#endif

bool atomic_load_bool_impl(const atomic_t *a);

/**
 * @brief Atomically store a boolean value
 * @param a Pointer to atomic_t
 * @param value Value to store
 */
#ifndef NDEBUG
void atomic_store_bool(atomic_t *a, bool value);
#else
#define atomic_store_bool(a, value) atomic_store_bool_impl((a), (uint64_t)(value))
#endif

void atomic_store_bool_impl(atomic_t *a, uint64_t value);

/**
 * @brief Atomically compare-and-swap a boolean
 * @param a Pointer to atomic_t
 * @param expected Expected current value (passed by reference)
 * @param new_value Value to store if comparison succeeds
 * @return true if swap succeeded, false otherwise
 */
#ifndef NDEBUG
bool atomic_cas_bool(atomic_t *a, bool *expected, bool new_value);
#else
#define atomic_cas_bool(a, expected, new_value) \
    atomic_cas_bool_impl((a), (uint64_t *)(expected), (uint64_t)(new_value))
#endif

bool atomic_cas_bool_impl(atomic_t *a, uint64_t *expected, uint64_t new_value);

/**
 * @brief Atomically exchange a boolean and return the old value
 * @param a Pointer to atomic_t
 * @param new_value Boolean value to store
 * @return Previous boolean value
 *
 * Atomically swaps the new value for the old value and returns the old value.
 * Avoids TOCTOU race conditions present in load+check+cas patterns.
 */
#ifndef NDEBUG
bool atomic_exchange_bool(atomic_t *a, bool new_value);
#else
#define atomic_exchange_bool(a, new_value) \
    atomic_exchange_bool_impl((a), (new_value))
#endif

bool atomic_exchange_bool_impl(atomic_t *a, bool new_value);

// ============================================================================
// int Operations
// ============================================================================

/**
 * @brief Atomically load an int value
 * @param a Pointer to atomic_t
 * @return Current int value
 */
#ifndef NDEBUG
int atomic_load_int(atomic_t *a);
#else
#define atomic_load_int(a) ((int)atomic_load_int_impl(a))
#endif

int atomic_load_int_impl(const atomic_t *a);

/**
 * @brief Atomically store an int value
 * @param a Pointer to atomic_t
 * @param value Value to store
 */
#ifndef NDEBUG
void atomic_store_int(atomic_t *a, int value);
#else
#define atomic_store_int(a, value) atomic_store_int_impl((a), (uint64_t)(int32_t)(value))
#endif

void atomic_store_int_impl(atomic_t *a, uint64_t value);

/**
 * @brief Atomically add to an int and return the previous value
 * @param a Pointer to atomic_t
 * @param delta Value to add
 * @return Previous value before the add
 */
#ifndef NDEBUG
int atomic_fetch_add_int(atomic_t *a, int delta);
#else
#define atomic_fetch_add_int(a, delta) \
    ((int)atomic_fetch_add_int_impl((a), (int64_t)(delta)))
#endif

int atomic_fetch_add_int_impl(atomic_t *a, int64_t delta);

/**
 * @brief Atomically subtract from an int and return the previous value
 * @param a Pointer to atomic_t
 * @param delta Value to subtract
 * @return Previous value before the subtract
 */
#ifndef NDEBUG
int atomic_fetch_sub_int(atomic_t *a, int delta);
#else
#define atomic_fetch_sub_int(a, delta) \
    ((int)atomic_fetch_sub_int_impl((a), (int64_t)(delta)))
#endif

int atomic_fetch_sub_int_impl(atomic_t *a, int64_t delta);

/**
 * @brief Atomically compare-and-swap an int
 * @param a Pointer to atomic_t
 * @param expected Expected current value (passed by reference)
 * @param new_value Value to store if comparison succeeds
 * @return true if swap succeeded, false otherwise
 */
#ifndef NDEBUG
bool atomic_cas_int(atomic_t *a, int *expected, int new_value);
#else
#define atomic_cas_int(a, expected, new_value) \
    atomic_cas_int_impl((a), (int64_t *)(expected), (int64_t)(new_value))
#endif

bool atomic_cas_int_impl(atomic_t *a, int64_t *expected, int64_t new_value);

// ============================================================================
// uint64_t Operations
// ============================================================================

/**
 * @brief Atomically load a uint64_t value
 * @param a Pointer to atomic_t
 * @return Current uint64_t value
 */
#ifndef NDEBUG
uint64_t atomic_load_u64(atomic_t *a);
#else
#define atomic_load_u64(a) atomic_load_u64_impl(a)
#endif

uint64_t atomic_load_u64_impl(const atomic_t *a);

/**
 * @brief Atomically store a uint64_t value
 * @param a Pointer to atomic_t
 * @param value Value to store
 */
#ifndef NDEBUG
void atomic_store_u64(atomic_t *a, uint64_t value);
#else
#define atomic_store_u64(a, value) atomic_store_u64_impl((a), (value))
#endif

void atomic_store_u64_impl(atomic_t *a, uint64_t value);

/**
 * @brief Atomically add to a uint64_t and return the previous value
 * @param a Pointer to atomic_t
 * @param delta Value to add
 * @return Previous value before the add
 */
#ifndef NDEBUG
uint64_t atomic_fetch_add_u64(atomic_t *a, uint64_t delta);
#else
#define atomic_fetch_add_u64(a, delta) atomic_fetch_add_u64_impl((a), (delta))
#endif

uint64_t atomic_fetch_add_u64_impl(atomic_t *a, uint64_t delta);

/**
 * @brief Atomically subtract from a uint64_t and return the previous value
 * @param a Pointer to atomic_t
 * @param delta Value to subtract
 * @return Previous value before the subtract
 */
#ifndef NDEBUG
uint64_t atomic_fetch_sub_u64(atomic_t *a, uint64_t delta);
#else
#define atomic_fetch_sub_u64(a, delta) atomic_fetch_sub_u64_impl((a), (delta))
#endif

uint64_t atomic_fetch_sub_u64_impl(atomic_t *a, uint64_t delta);

/**
 * @brief Atomically compare-and-swap a uint64_t
 * @param a Pointer to atomic_t
 * @param expected Expected current value (passed by reference)
 * @param new_value Value to store if comparison succeeds
 * @return true if swap succeeded, false otherwise
 */
#ifndef NDEBUG
bool atomic_cas_u64(atomic_t *a, uint64_t *expected, uint64_t new_value);
#else
#define atomic_cas_u64(a, expected, new_value) \
    atomic_cas_u64_impl((a), (expected), (new_value))
#endif

bool atomic_cas_u64_impl(atomic_t *a, uint64_t *expected, uint64_t new_value);

/**
 * @brief Atomically exchange a uint64_t and return the old value
 * @param a Pointer to atomic_t
 * @param new_value Value to store
 * @return Previous uint64_t value
 *
 * Used for atomic min/max updates and other exchange patterns where you need
 * the old value atomically. Avoids TOCTOU race conditions present in load+check+cas.
 *
 * Example (atomic minimum):
 * @code
 * uint64_t old = atomic_exchange_u64(&stats->min_val, 0);
 * // old is the previous value, atomically
 * @endcode
 */
#ifndef NDEBUG
uint64_t atomic_exchange_u64(atomic_t *a, uint64_t new_value);
#else
#define atomic_exchange_u64(a, new_value) \
    atomic_exchange_u64_impl((a), (new_value))
#endif

uint64_t atomic_exchange_u64_impl(atomic_t *a, uint64_t new_value);

// ============================================================================
// Pointer Operations
// ============================================================================

/**
 * @brief Atomically load a pointer
 * @param a Pointer to atomic_ptr_t
 * @return Current pointer value
 */
#ifndef NDEBUG
void *atomic_ptr_load(atomic_ptr_t *a);
#else
#define atomic_ptr_load(a) atomic_ptr_load_impl(a)
#endif

void *atomic_ptr_load_impl(const atomic_ptr_t *a);

/**
 * @brief Atomically store a pointer
 * @param a Pointer to atomic_ptr_t
 * @param value Pointer value to store
 */
#ifndef NDEBUG
void atomic_ptr_store(atomic_ptr_t *a, void *value);
#else
#define atomic_ptr_store(a, value) atomic_ptr_store_impl((a), (value))
#endif

void atomic_ptr_store_impl(atomic_ptr_t *a, void *value);

/**
 * @brief Atomically compare-and-swap a pointer
 * @param a Pointer to atomic_ptr_t
 * @param expected Expected current pointer (passed by reference)
 * @param new_value Pointer value to store if comparison succeeds
 * @return true if swap succeeded, false otherwise
 */
#ifndef NDEBUG
bool atomic_ptr_cas(atomic_ptr_t *a, void **expected, void *new_value);
#else
#define atomic_ptr_cas(a, expected, new_value) \
    atomic_ptr_cas_impl((a), (expected), (new_value))
#endif

bool atomic_ptr_cas_impl(atomic_ptr_t *a, void **expected, void *new_value);

/**
 * @brief Atomically exchange a pointer and return the old value
 * @param a Pointer to atomic_ptr_t
 * @param new_value Pointer value to store
 * @return Previous pointer value
 */
#ifndef NDEBUG
void *atomic_ptr_exchange(atomic_ptr_t *a, void *new_value);
#else
#define atomic_ptr_exchange(a, new_value) \
    atomic_ptr_exchange_impl((a), (new_value))
#endif

void *atomic_ptr_exchange_impl(atomic_ptr_t *a, void *new_value);

/**
 * @brief Atomically exchange a bool and return the old value
 * @param a Pointer to atomic_t
 * @param new_value Bool value to store
 * @return Previous bool value
 */
/**
 * @brief Atomically exchange an int and return the old value
 * @param a Pointer to atomic_t
 * @param new_value Int value to store
 * @return Previous int value
 */
#ifndef NDEBUG
int atomic_exchange_int(atomic_t *a, int new_value);
#else
#define atomic_exchange_int(a, new_value) \
    atomic_exchange_int_impl((a), (new_value))
#endif

int atomic_exchange_int_impl(atomic_t *a, int new_value);

/**
 * @brief Atomically exchange a uint64_t and return the old value
 * @param a Pointer to atomic_t
 * @param new_value Uint64_t value to store
 * @return Previous uint64_t value
 */
#ifndef NDEBUG
uint64_t atomic_exchange_u64(atomic_t *a, uint64_t new_value);
#else
#define atomic_exchange_u64(a, new_value) \
    atomic_exchange_u64_impl((a), (new_value))
#endif

uint64_t atomic_exchange_u64_impl(atomic_t *a, uint64_t new_value);

// ============================================================================
// Debug Hooks (called by _impl functions in debug builds)
// ============================================================================

#ifndef NDEBUG
void atomic_on_load(atomic_t *a);
void atomic_on_store(atomic_t *a);
void atomic_on_cas(atomic_t *a, bool success);
void atomic_on_fetch(atomic_t *a);

void atomic_ptr_on_load(atomic_ptr_t *a);
void atomic_ptr_on_store(atomic_ptr_t *a);
void atomic_ptr_on_cas(atomic_ptr_t *a, bool success);
void atomic_ptr_on_exchange(atomic_ptr_t *a);
#endif

// ============================================================================
// Convenience Macros for Atomic Declaration and Registration
// ============================================================================

/**
 * @brief Register an atomic variable for debug tracking with automatic name generation
 *
 * Uses stringification to avoid repeating the variable name. Simplifies the
 * declaration + registration pattern.
 *
 * Usage:
 * @code
 * // In header:
 * extern atomic_t g_counter;
 *
 * // In source:
 * atomic_t g_counter = {0};
 *
 * // In main/init:
 * ATOMIC_REGISTER_AUTO(g_counter);
 * @endcode
 */
#define ATOMIC_REGISTER_AUTO(name) NAMED_REGISTER_ATOMIC(&(name), #name)

/**
 * @brief Register an atomic pointer variable for debug tracking with automatic name generation
 *
 * Same as ATOMIC_REGISTER_AUTO but for _Atomic(void *) variables.
 *
 * Usage:
 * @code
 * _Atomic(void *) g_buffer = {0};
 * ATOMIC_PTR_REGISTER_AUTO(g_buffer);
 * @endcode
 */
#define ATOMIC_PTR_REGISTER_AUTO(name) NAMED_REGISTER_ATOMIC_PTR(&(name), #name)

// ============================================================================
// Automatic Named Initialization & Registration Macros
// ============================================================================

/**
 * @brief Initialize an atomic_t with automatic named registration
 *
 * In debug builds, automatically registers the atomic with named.c using
 * a constructor function. In release builds, just initializes to {0}.
 *
 * Usage:
 * @code
 * // Declare with automatic registration:
 * static atomic_t g_counter ATOMIC_INIT_AUTO("g_counter");
 *
 * // Or simpler - use the variable name directly:
 * static atomic_t g_counter ATOMIC_INIT_AUTO();
 * @endcode
 *
 * Note: Currently requires GCC/Clang constructor attribute. For other compilers,
 * use ATOMIC_REGISTER_AUTO(variable_name) separately.
 */
#ifndef NDEBUG
#define ATOMIC_INIT_AUTO(...) = _atomic_init_auto(__VA_ARGS__)
#else
#define ATOMIC_INIT_AUTO(...) = {0}
#endif

/**
 * @brief Initialize an atomic_ptr_t with automatic named registration
 *
 * Same as ATOMIC_INIT_AUTO but for atomic_ptr_t.
 */
#ifndef NDEBUG
#define ATOMIC_PTR_INIT_AUTO(...) = _atomic_ptr_init_auto(__VA_ARGS__)
#else
#define ATOMIC_PTR_INIT_AUTO(...) = {0}
#endif

// Actual init functions (defined below - use a workaround pattern)
// These are helpers that work with static initialization
#define _atomic_init_auto(name) {0}; \
  static void __attribute__((constructor, used)) _atomic_reg_##name(void) { \
    NAMED_REGISTER_ATOMIC(&name, #name); \
  } typedef int _atomic_assert_##name

#define _atomic_ptr_init_auto(name) {0}; \
  static void __attribute__((constructor, used)) _atomic_ptr_reg_##name(void) { \
    NAMED_REGISTER_ATOMIC_PTR(&name, #name); \
  } typedef int _atomic_ptr_assert_##name

// ============================================================================
// Debug Initialization/Shutdown
// ============================================================================

#ifndef NDEBUG
void debug_atomic_init(void);
void debug_atomic_shutdown(void);
bool debug_atomic_is_initialized(void);
void debug_atomic_print_state(void);
#else
#define debug_atomic_init()           ((void)0)
#define debug_atomic_shutdown()       ((void)0)
#define debug_atomic_is_initialized() (true)
#define debug_atomic_print_state()    ((void)0)
#endif

#ifdef __cplusplus
}
#endif
