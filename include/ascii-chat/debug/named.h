#pragma once

/**
 * @file debug/named.h
 * @brief Named object registry for debugging — log identifiable resource names
 * @ingroup debug_named
 * @addtogroup debug_named
 * @{
 *
 * This module provides a centralized registry for naming any addressable resource:
 * mutexes, threads, sockets, ring buffers, network connections, etc.
 *
 * Any value that fits in a uintptr_t can be registered with a human-readable name.
 * Names are auto-suffixed with a unique ID (e.g., "recv_mutex.7") to distinguish
 * multiple instances of the same conceptual resource.
 *
 * The registry is thread-safe and zero-overhead in release builds (NDEBUG).
 *
 * @note Registry is not thread-safe for unregistration during unwind. Always
 *       unregister resources before they become invalid (e.g., in destroy/cleanup).
 *
 * USAGE EXAMPLES:
 * ===============
 * // Pointer-based (for heap objects, structs)
 * ringbuffer_t *rb = ringbuffer_create(...);
 * NAMED_REGISTER(rb, "recv_audio");       // auto name: "recv_audio.3"
 * // ... use rb ...
 * NAMED_UNREGISTER(rb);
 * ringbuffer_destroy(rb);
 *
 * // Integer handle-based (for fd, socket, etc.)
 * int client_fd = accept(server_fd, ...);
 * NAMED_REGISTER_ID(client_fd, "client_socket");
 * // ... use client_fd ...
 * NAMED_UNREGISTER_ID(client_fd);
 * close(client_fd);
 *
 * // Describing in logs
 * log_info("Data received on %s", NAME_DESCRIBE_SOCKET(fd));
 * // Output: "socket: client_socket.2 (0x8)"
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#include <stdint.h>
#include <stdbool.h>
#include "../platform/thread.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Core Registry API
// ============================================================================

/**
 * @brief Initialize the named object registry
 * @return 0 on success, non-zero on error
 * @ingroup debug_named
 *
 * Must be called once at startup before any named_register() calls.
 * Typically called from lock_debug_init().
 */
int named_init(void);

/**
 * @brief Destroy the named object registry
 * @ingroup debug_named
 *
 * Cleans up all registered names and internal structures.
 * Typically called from lock_debug_destroy().
 */
void named_destroy(void);

/**
 * @brief Register a resource with an auto-suffixed name
 * @param key A uintptr_t representing the resource (pointer or integer handle)
 * @param base_name The base name (e.g., "recv_mutex"); suffix auto-generated
 * @return Pointer to the full registered name (e.g., "recv_mutex.7"), valid until unregister
 * @ingroup debug_named
 *
 * The returned name string is stored in the registry and remains valid until
 * named_unregister(key) is called. Do not modify or free the returned pointer.
 *
 * Multiple registrations of the same key will overwrite the previous entry.
 * In release builds (NDEBUG), this is a no-op and returns base_name.
 */
const char *named_register(uintptr_t key, const char *base_name);

/**
 * @brief Register a resource with a formatted name (no auto-suffix)
 * @param key A uintptr_t representing the resource
 * @param fmt Printf-style format string
 * @param ... Format arguments
 * @return Pointer to the full registered name
 * @ingroup debug_named
 *
 * Allows full control over the registered name. The format string and arguments
 * are printed to create the name (e.g., "client.%u" with args "17" → "client.17").
 * No auto-suffix counter is applied.
 *
 * In release builds (NDEBUG), this is a no-op and returns "?".
 */
const char *named_register_fmt(uintptr_t key, const char *fmt, ...);

/**
 * @brief Unregister a resource by key
 * @param key The same uintptr_t passed to named_register
 * @ingroup debug_named
 *
 * Removes the resource from the registry. Safe to call multiple times for the
 * same key (idempotent).
 */
void named_unregister(uintptr_t key);

/**
 * @brief Look up the registered name for a resource
 * @param key The resource key
 * @return Registered name string, or NULL if not registered
 * @ingroup debug_named
 *
 * Returns the name string registered with named_register() or named_register_fmt(),
 * or NULL if the key is not in the registry.
 *
 * In release builds (NDEBUG), this always returns NULL.
 */
const char *named_get(uintptr_t key);

/**
 * @brief Format a description string for logging
 * @param key The resource key
 * @param type_hint A type label (e.g., "mutex", "socket", "thread")
 * @return Description string in format "type_hint: name (0xKEY)" or type_hint if not registered
 * @ingroup debug_named
 *
 * Returns a per-thread static buffer suitable for a single log call. The buffer
 * is overwritten on the next call to this function within the same thread.
 * Do not store or free the returned pointer.
 *
 * If the key is not registered, returns just the type_hint.
 * In release builds (NDEBUG), this always returns "?".
 */
const char *named_describe(uintptr_t key, const char *type_hint);

/**
 * @brief Convert a thread handle to a registry key
 * @param thread Thread handle (asciichat_thread_t)
 * @return uintptr_t suitable for registry lookups
 * @ingroup debug_named
 *
 * Platform-specific conversion. On POSIX, pthread_t is cast directly.
 * On Windows, HANDLE is cast directly.
 *
 * This function is defined in lib/platform/posix/thread.c or lib/platform/windows/thread.c
 */
uintptr_t asciichat_thread_to_key(asciichat_thread_t thread);

// ============================================================================
// Convenience Macros — Pointer-based Registration
// ============================================================================

/**
 * @brief Register any pointer with base name (auto-suffix)
 * @param ptr Object pointer (void* or typed pointer)
 * @param name Base name string
 * @ingroup debug_named
 */
#ifndef NDEBUG
#define NAMED_REGISTER(ptr, name) \
  named_register((uintptr_t)(const void *)(ptr), (name))
#else
#define NAMED_REGISTER(ptr, name) ((void)0)
#endif

/**
 * @brief Register with a formatted name
 * @param ptr Object pointer
 * @param fmt Printf-style format string
 * @param ... Format arguments
 * @ingroup debug_named
 */
#ifndef NDEBUG
#define NAMED_REGISTER_FMT(ptr, fmt, ...) \
  named_register_fmt((uintptr_t)(const void *)(ptr), (fmt), __VA_ARGS__)
#else
#define NAMED_REGISTER_FMT(ptr, fmt, ...) ((void)0)
#endif

/**
 * @brief Unregister a pointer
 * @param ptr Object pointer
 * @ingroup debug_named
 */
#ifndef NDEBUG
#define NAMED_UNREGISTER(ptr) \
  named_unregister((uintptr_t)(const void *)(ptr))
#else
#define NAMED_UNREGISTER(ptr) ((void)0)
#endif

/**
 * @brief Look up registered name for a pointer
 * @param ptr Object pointer
 * @ingroup debug_named
 */
#ifndef NDEBUG
#define NAMED_GET(ptr) \
  named_get((uintptr_t)(const void *)(ptr))
#else
#define NAMED_GET(ptr) (NULL)
#endif

/**
 * @brief Describe a pointer in log format
 * @param ptr Object pointer
 * @param hint Type hint string (e.g., "mutex")
 * @ingroup debug_named
 */
#ifndef NDEBUG
#define NAMED_DESCRIBE(ptr, hint) \
  named_describe((uintptr_t)(const void *)(ptr), (hint))
#else
#define NAMED_DESCRIBE(ptr, hint) ("?")
#endif

// ============================================================================
// Convenience Macros — Integer Handle Registration
// ============================================================================

/**
 * @brief Register an integer handle (fd, socket, etc.)
 * @param id Integer handle (socket_t, int, etc.)
 * @param name Base name string
 * @ingroup debug_named
 */
#ifndef NDEBUG
#define NAMED_REGISTER_ID(id, name) \
  named_register((uintptr_t)(intptr_t)(id), (name))
#else
#define NAMED_REGISTER_ID(id, name) ((void)0)
#endif

/**
 * @brief Unregister an integer handle
 * @param id Integer handle
 * @ingroup debug_named
 */
#ifndef NDEBUG
#define NAMED_UNREGISTER_ID(id) \
  named_unregister((uintptr_t)(intptr_t)(id))
#else
#define NAMED_UNREGISTER_ID(id) ((void)0)
#endif

/**
 * @brief Describe an integer handle in log format
 * @param id Integer handle
 * @param hint Type hint string (e.g., "socket")
 * @ingroup debug_named
 */
#ifndef NDEBUG
#define NAMED_DESCRIBE_ID(id, hint) \
  named_describe((uintptr_t)(intptr_t)(id), (hint))
#else
#define NAMED_DESCRIBE_ID(id, hint) ("?")
#endif

// ============================================================================
// Type-Specific Description Macros
// ============================================================================

/**
 * @name Type-Specific Description Macros
 * @{
 *
 * These macros encapsulate the proper casting and type hint for common types.
 * Use these instead of NAMED_DESCRIBE for better readability and consistency.
 */

/**
 * @brief Describe a mutex_t for logging
 * @param m Pointer to mutex_t
 * @ingroup debug_named
 */
#define NAME_DESCRIBE_MUTEX(m) \
  named_describe((uintptr_t)(const void *)(m), "mutex")

/**
 * @brief Describe an rwlock_t for logging
 * @param l Pointer to rwlock_t
 * @ingroup debug_named
 */
#define NAME_DESCRIBE_RWLOCK(l) \
  named_describe((uintptr_t)(const void *)(l), "rwlock")

/**
 * @brief Describe a cond_t for logging
 * @param c Pointer to cond_t
 * @ingroup debug_named
 */
#define NAME_DESCRIBE_COND(c) \
  named_describe((uintptr_t)(const void *)(c), "cond")

/**
 * @brief Describe a thread for logging
 * @param t asciichat_thread_t handle
 * @ingroup debug_named
 *
 * Uses named_describe_thread() function to handle platform-specific conversion.
 */
#define NAME_DESCRIBE_THREAD(t) \
  named_describe_thread(t)

/**
 * @brief Describe a transport for logging
 * @param tr Pointer to acip_transport_t
 * @ingroup debug_named
 */
#define NAME_DESCRIBE_TRANSPORT(tr) \
  named_describe((uintptr_t)(const void *)(tr), "transport")

/**
 * @brief Describe a socket for logging
 * @param fd socket_t file descriptor
 * @ingroup debug_named
 */
#define NAME_DESCRIBE_SOCKET(fd) \
  named_describe((uintptr_t)(intptr_t)(fd), "socket")

/**
 * @brief Describe a ring buffer for logging
 * @param rb Pointer to ringbuffer_t
 * @ingroup debug_named
 */
#define NAME_DESCRIBE_RINGBUF(rb) \
  named_describe((uintptr_t)(const void *)(rb), "ringbuf")

/** @} */ /* Type-Specific Description Macros */

// ============================================================================
// Thread-Specific Description Function
// ============================================================================

/**
 * @brief Describe a thread for logging (function wrapper)
 * @param thread asciichat_thread_t handle
 * @return Description string "thread: name (0xID)"
 * @ingroup debug_named
 *
 * This is a function (not a macro) to handle the opaque asciichat_thread_t type
 * properly, converting it through asciichat_thread_to_key().
 */
const char *named_describe_thread(void *thread);

#ifdef __cplusplus
}
#endif

/** @} */ /* debug_named */
