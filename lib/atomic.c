/**
 * @file atomic.c
 * @brief Implementation of atomic operations abstraction
 * @ingroup util
 * @date March 2026
 *
 * Provides _impl functions for atomic operations. In debug builds, these
 * functions are wrapped by the typed macros to include debug hooks.
 * In release builds, the macros call these directly for zero overhead.
 *
 * Design: Uses raw C11 atomic operations internally, never calls back
 * into the atomic_t system (no recursion). Debug hooks are called
 * after the atomic operation completes.
 */

#include <stdatomic.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/debug/atomic.h>


// ============================================================================
// bool Implementation Functions
// ============================================================================

bool atomic_load_bool_impl(const atomic_t *a) {
    if (!a) return false;
    return (bool)atomic_load((const _Atomic(uint64_t) *)&a->impl);
}

void atomic_store_bool_impl(atomic_t *a, uint64_t value) {
    if (!a) return;
    atomic_store((_Atomic(uint64_t) *)&a->impl, value);
}

bool atomic_cas_bool_impl(atomic_t *a, uint64_t *expected, uint64_t new_value) {
    if (!a || !expected) return false;
    return atomic_compare_exchange_strong((_Atomic(uint64_t) *)&a->impl, expected, new_value);
}

// ============================================================================
// int Implementation Functions
// ============================================================================

int atomic_load_int_impl(const atomic_t *a) {
    if (!a) return 0;
    return (int)(int32_t)atomic_load((const _Atomic(uint64_t) *)&a->impl);
}

void atomic_store_int_impl(atomic_t *a, uint64_t value) {
    if (!a) return;
    atomic_store((_Atomic(uint64_t) *)&a->impl, value);
}

int atomic_fetch_add_int_impl(atomic_t *a, int64_t delta) {
    if (!a) return 0;
    return (int)(int32_t)atomic_fetch_add((_Atomic(uint64_t) *)&a->impl, (uint64_t)delta);
}

int atomic_fetch_sub_int_impl(atomic_t *a, int64_t delta) {
    if (!a) return 0;
    return (int)(int32_t)atomic_fetch_sub((_Atomic(uint64_t) *)&a->impl, (uint64_t)delta);
}

// ============================================================================
// uint64_t Implementation Functions
// ============================================================================

uint64_t atomic_load_u64_impl(const atomic_t *a) {
    if (!a) return 0;
    return atomic_load((const _Atomic(uint64_t) *)&a->impl);
}

void atomic_store_u64_impl(atomic_t *a, uint64_t value) {
    if (!a) return;
    atomic_store((_Atomic(uint64_t) *)&a->impl, value);
}

uint64_t atomic_fetch_add_u64_impl(atomic_t *a, uint64_t delta) {
    if (!a) return 0;
    return atomic_fetch_add((_Atomic(uint64_t) *)&a->impl, delta);
}

uint64_t atomic_fetch_sub_u64_impl(atomic_t *a, uint64_t delta) {
    if (!a) return 0;
    return atomic_fetch_sub((_Atomic(uint64_t) *)&a->impl, delta);
}

// ============================================================================
// Pointer Implementation Functions
// ============================================================================

void *atomic_ptr_load_impl(const _Atomic(void *) *a) {
    if (!a) return NULL;
    return atomic_load(a);
}

void atomic_ptr_store_impl(_Atomic(void *) *a, void *value) {
    if (!a) return;
    atomic_store(a, value);
}

bool atomic_ptr_cas_impl(_Atomic(void *) *a, void **expected, void *new_value) {
    if (!a || !expected) return false;
    return atomic_compare_exchange_strong(a, expected, new_value);
}

void *atomic_ptr_exchange_impl(_Atomic(void *) *a, void *new_value) {
    if (!a) return NULL;
    return atomic_exchange(a, new_value);
}

// ============================================================================
// Debug Build: Wrapper Functions with Hooks
// ============================================================================

#ifndef NDEBUG

bool atomic_load_bool(atomic_t *a) {
    if (!a) return false;
    bool result = (bool)atomic_load((const _Atomic(uint64_t) *)&a->impl);
    atomic_on_load(a);
    return result;
}

void atomic_store_bool(atomic_t *a, bool value) {
    if (!a) return;
    atomic_store((_Atomic(uint64_t) *)&a->impl, (uint64_t)value);
    atomic_on_store(a);
}

bool atomic_cas_bool(atomic_t *a, bool *expected, bool new_value) {
    if (!a || !expected) return false;
    uint64_t exp = (uint64_t)*expected;
    bool success = atomic_compare_exchange_strong((_Atomic(uint64_t) *)&a->impl, &exp, (uint64_t)new_value);
    *expected = (bool)exp;
    atomic_on_cas(a, success);
    return success;
}

int atomic_load_int(atomic_t *a) {
    if (!a) return 0;
    int result = (int)(int32_t)atomic_load((const _Atomic(uint64_t) *)&a->impl);
    atomic_on_load(a);
    return result;
}

void atomic_store_int(atomic_t *a, int value) {
    if (!a) return;
    atomic_store((_Atomic(uint64_t) *)&a->impl, (uint64_t)(int32_t)value);
    atomic_on_store(a);
}

int atomic_fetch_add_int(atomic_t *a, int delta) {
    if (!a) return 0;
    int result = (int)(int32_t)atomic_fetch_add((_Atomic(uint64_t) *)&a->impl, (uint64_t)(int64_t)delta);
    atomic_on_fetch(a);
    return result;
}

int atomic_fetch_sub_int(atomic_t *a, int delta) {
    if (!a) return 0;
    int result = (int)(int32_t)atomic_fetch_sub((_Atomic(uint64_t) *)&a->impl, (uint64_t)(int64_t)delta);
    atomic_on_fetch(a);
    return result;
}

uint64_t atomic_load_u64(atomic_t *a) {
    if (!a) return 0;
    uint64_t result = atomic_load((const _Atomic(uint64_t) *)&a->impl);
    atomic_on_load(a);
    return result;
}

void atomic_store_u64(atomic_t *a, uint64_t value) {
    if (!a) return;
    atomic_store((_Atomic(uint64_t) *)&a->impl, value);
    atomic_on_store(a);
}

uint64_t atomic_fetch_add_u64(atomic_t *a, uint64_t delta) {
    if (!a) return 0;
    uint64_t result = atomic_fetch_add((_Atomic(uint64_t) *)&a->impl, delta);
    atomic_on_fetch(a);
    return result;
}

uint64_t atomic_fetch_sub_u64(atomic_t *a, uint64_t delta) {
    if (!a) return 0;
    uint64_t result = atomic_fetch_sub((_Atomic(uint64_t) *)&a->impl, delta);
    atomic_on_fetch(a);
    return result;
}

void *atomic_ptr_load(_Atomic(void *) *a) {
    if (!a) return NULL;
    void *result = atomic_load(a);
    // atomic_ptr_on_load(a);  // Debug hook would need atomic_ptr_t *, not raw _Atomic
    return result;
}

void atomic_ptr_store(_Atomic(void *) *a, void *value) {
    if (!a) return;
    atomic_store(a, value);
    // atomic_ptr_on_store(a);  // Debug hook would need atomic_ptr_t *
}

bool atomic_ptr_cas(_Atomic(void *) *a, void **expected, void *new_value) {
    if (!a || !expected) return false;
    bool success = atomic_compare_exchange_strong(a, expected, new_value);
    // atomic_ptr_on_cas(a, success);  // Debug hook would need atomic_ptr_t *
    return success;
}

void *atomic_ptr_exchange(_Atomic(void *) *a, void *new_value) {
    if (!a) return NULL;
    void *result = atomic_exchange(a, new_value);
    // atomic_ptr_on_exchange(a);  // Debug hook would need atomic_ptr_t *
    return result;
}

#endif  // !NDEBUG
