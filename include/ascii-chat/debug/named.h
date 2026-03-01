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

#ifdef __cplusplus
extern "C" {
#endif

#include "../platform/thread.h"

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
asciichat_error_t named_init(void);

/**
 * @brief Destroy the named object registry
 * @ingroup debug_named
 *
 * Cleans up all registered names and internal structures.
 * Typically called from lock_debug_destroy().
 */
void named_destroy(void);

/**
 * @brief Register a resource with an auto-suffixed name, type, and location info
 * @param key A uintptr_t representing the resource (pointer or integer handle)
 * @param base_name The base name (e.g., "recv"); suffix auto-generated
 * @param type Data type label (e.g., "mutex", "rwlock", "socket"); stored and printed
 * @param file Source file where registration occurred (typically __FILE__)
 * @param line Source line where registration occurred (typically __LINE__)
 * @param func Function where registration occurred (typically __func__)
 * @return Pointer to the full registered name (e.g., "recv.7"), valid until unregister
 * @ingroup debug_named
 *
 * The returned name string is stored in the registry and remains valid until
 * named_unregister(key) is called. Do not modify or free the returned pointer.
 *
 * The type parameter is stored in the registry and used by named_describe() to format
 * output as "type: name (0xKEY) @ file:line:func()" automatically, without needing
 * a type_hint parameter.
 *
 * Multiple registrations of the same key will overwrite the previous entry.
 * In release builds (NDEBUG), this is a no-op and returns base_name.
 */
const char *named_register(uintptr_t key, const char *base_name, const char *type, const char *format_spec,
                           const char *file, int line, const char *func);

/**
 * @brief Register a resource with a formatted name, type, and location info (no auto-suffix)
 * @param key A uintptr_t representing the resource
 * @param type Data type label (e.g., "mutex", "socket"); stored and printed
 * @param file Source file where registration occurred
 * @param line Source line where registration occurred
 * @param func Function where registration occurred
 * @param fmt Printf-style format string
 * @param ... Format arguments
 * @return Pointer to the full registered name
 * @ingroup debug_named
 *
 * Allows full control over the registered name. The format string and arguments
 * are printed to create the name (e.g., "client.%u" with args "17" → "client.17").
 * No auto-suffix counter is applied.
 *
 * The type parameter is stored in the registry and used by named_describe() for
 * automatic formatting without needing type_hint.
 *
 * In release builds (NDEBUG), this is a no-op and returns "?".
 */
const char *named_register_fmt(uintptr_t key, const char *type, const char *format_spec, const char *file, int line,
                               const char *func, const char *fmt, ...);

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
 * @brief Update the registered name for a resource with a new base name
 * @param key The resource key
 * @param new_base_name New base name (will be auto-suffixed with counter)
 * @return Updated name string, or NULL if key not found
 * @ingroup debug_named
 *
 * Updates an existing registration with a new base name. The new name will be
 * auto-suffixed with a counter to make it unique (e.g., "client_123.0").
 * Useful when a resource gets an ID assigned after creation (e.g., client_id).
 *
 * In release builds (NDEBUG), this is a no-op and returns NULL.
 */
const char *named_update_name(uintptr_t key, const char *new_base_name);

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
 * @brief Look up the registered type for a resource
 * @param key The resource key
 * @return Registered type string, or NULL if not registered
 * @ingroup debug_named
 *
 * Returns the type string registered with named_register() or named_register_fmt(),
 * or NULL if the key is not in the registry.
 *
 * In release builds (NDEBUG), this always returns NULL.
 */
const char *named_get_type(uintptr_t key);

/**
 * @brief Get the format specifier for a registered named object
 * @param key The resource key
 * @return Format specifier string (e.g., "0x%tx"), or NULL if not registered
 * @ingroup debug_named
 *
 * Returns the format specifier registered with named_register(), or NULL if
 * the key is not in the registry.
 *
 * In release builds (NDEBUG), this always returns NULL.
 */
const char *named_get_format_spec(uintptr_t key);

/**
 * @brief Register a file descriptor with namespace encoding
 * @param fd File descriptor value
 * @param file Source file where registration occurred
 * @param line Source line where registration occurred
 * @param func Function where registration occurred
 * @return Registered name string
 * @ingroup debug_named
 *
 * Registers an FD with a key that includes type namespace to avoid collisions
 * with packet types or other numeric values. Name is auto-generated as "fd=%d".
 * In release builds (NDEBUG), this is a no-op and returns "fd=%d" string.
 */
const char *named_register_fd(int fd, const char *file, int line, const char *func);

/**
 * @brief Look up a registered file descriptor
 * @param fd File descriptor value
 * @return Registered name string, or NULL if not registered
 * @ingroup debug_named
 *
 * Looks up an FD using the same namespace encoding as named_register_fd().
 * In release builds (NDEBUG), this always returns NULL.
 */
const char *named_get_fd(int fd);

/**
 * @brief Get the format specifier for a registered file descriptor
 * @param fd File descriptor value
 * @return Format specifier string (e.g., "%d"), or NULL if not registered
 * @ingroup debug_named
 */
const char *named_get_fd_format_spec(int fd);

/**
 * @brief Register a packet type with namespace encoding
 * @param pkt_type Packet type value
 * @param file Source file where registration occurred
 * @param line Source line where registration occurred
 * @param func Function where registration occurred
 * @return Registered name string
 * @ingroup debug_named
 *
 * Registers a packet type with a key that includes type namespace to avoid collisions
 * with FDs or other numeric values. Name is auto-generated as "PACKET_TYPE=%d".
 * In release builds (NDEBUG), this is a no-op and returns "PACKET_TYPE=%d" string.
 */
const char *named_register_packet_type(int pkt_type, const char *file, int line, const char *func);

/**
 * @brief Look up a registered packet type
 * @param pkt_type Packet type value
 * @return Registered name string, or NULL if not registered
 * @ingroup debug_named
 *
 * Looks up a packet type using the same namespace encoding as named_register_packet_type().
 * In release builds (NDEBUG), this always returns NULL.
 */
const char *named_get_packet_type(int pkt_type);

/**
 * @brief Get the format specifier for a registered packet type
 * @param pkt_type Packet type value
 * @return Format specifier string (e.g., "%d"), or NULL if not registered
 * @ingroup debug_named
 */
const char *named_get_packet_type_format_spec(int pkt_type);

/**
 * @brief Look up an integer ID by type name and value
 * @param type_name Type string (e.g., "socket", "client", "connection")
 * @param type_len Length of type name
 * @param id Integer ID value to look up
 * @return Registered name string, or NULL if not found
 * @ingroup debug_named
 *
 * Searches the registry for entries matching the given type and integer ID.
 * Enables formatting of patterns like "socket 12" → "socket/server_listener (socket=12)".
 * In release builds (NDEBUG), this always returns NULL.
 */
const char *named_get_by_type_and_id(const char *type_name, size_t type_len, int id);

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
 * @brief Register any pointer with base name, type, format spec, and location (auto-suffix)
 * @param ptr Object pointer (void* or typed pointer)
 * @param name Base name string
 * @param type Data type label (e.g., "mutex", "socket")
 * @param fmt Format specifier for printing (e.g., "0x%tx")
 * @ingroup debug_named
 *
 * In debug builds, automatically captures __FILE__, __LINE__, and __func__ for location info.
 * In release builds (NDEBUG), this is a no-op.
 */
#ifndef NDEBUG
#define NAMED_REGISTER(ptr, name, type, fmt)                                                                           \
  named_register((uintptr_t)(const void *)(ptr), (name), (type), (fmt), __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER(ptr, name, type, fmt) (name)
#endif

/**
 * @brief Register with a formatted name, type, and format spec
 * @param ptr Object pointer
 * @param type Data type label
 * @param fmt_spec Format specifier for printing (e.g., "0x%tx")
 * @param fmt Printf-style format string for name
 * @param ... Format arguments
 * @ingroup debug_named
 *
 * In debug builds, automatically captures __FILE__, __LINE__, and __func__ for location info.
 * In release builds (NDEBUG), this is a no-op.
 */
#ifndef NDEBUG
#define NAMED_REGISTER_FMT(ptr, type, fmt_spec, fmt, ...)                                                              \
  named_register_fmt((uintptr_t)(const void *)(ptr), (type), (fmt_spec), __FILE__, __LINE__, __func__, (fmt),          \
                     __VA_ARGS__)
#else
#define NAMED_REGISTER_FMT(ptr, type, fmt_spec, fmt, ...) ("?")
#endif

/**
 * @brief Unregister a pointer
 * @param ptr Object pointer
 * @ingroup debug_named
 */
#ifndef NDEBUG
#define NAMED_UNREGISTER(ptr) named_unregister((uintptr_t)(const void *)(ptr))
#else
#define NAMED_UNREGISTER(ptr) ((void)0)
#endif

/**
 * @brief Look up registered name for a pointer
 * @param ptr Object pointer
 * @ingroup debug_named
 */
#ifndef NDEBUG
#define NAMED_GET(ptr) named_get((uintptr_t)(const void *)(ptr))
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
#define NAMED_DESCRIBE(ptr, hint) named_describe((uintptr_t)(const void *)(ptr), (hint))
#else
#define NAMED_DESCRIBE(ptr, hint) ("?")
#endif

// ============================================================================
// Convenience Macros — Integer Handle Registration
// ============================================================================

/**
 * @brief Register an integer handle (fd, socket, etc.) with type
 * @param id Integer handle (socket_t, int, etc.)
 * @param name Base name string
 * @param type Data type label (e.g., "socket", "fd")
 * @ingroup debug_named
 *
 * In debug builds, automatically captures __FILE__, __LINE__, and __func__ for location info.
 * In release builds (NDEBUG), this is a no-op.
 */
#ifndef NDEBUG
#define NAMED_REGISTER_ID(id, name, type, fmt)                                                                         \
  named_register((uintptr_t)(intptr_t)(id), (name), (type), (fmt), __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_ID(id, name, type) (name)
#endif

/**
 * @brief Unregister an integer handle
 * @param id Integer handle
 * @ingroup debug_named
 */
#ifndef NDEBUG
#define NAMED_UNREGISTER_ID(id) named_unregister((uintptr_t)(intptr_t)(id))
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
#define NAMED_DESCRIBE_ID(id, hint) named_describe((uintptr_t)(intptr_t)(id), (hint))
#else
#define NAMED_DESCRIBE_ID(id, hint) ("?")
#endif

// ============================================================================
// Type-Specific Convenience Macros (Mutex, RWLock, Cond, Thread)
// ============================================================================

/**
 * @brief Register a mutex with automatic format specifier
 * @param mutex Mutex pointer
 * @param name Base name string
 * @ingroup debug_named
 *
 * Convenience macro that automatically uses "0x%tx" format specifier for mutex addresses.
 */
#ifndef NDEBUG
#define NAMED_REGISTER_MUTEX(mutex, name)                                                                              \
  named_register((uintptr_t)(const void *)(mutex), (name), "mutex", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_MUTEX(mutex, name) (name)
#endif

/**
 * @brief Register an rwlock with automatic format specifier
 * @param lock RWLock pointer
 * @param name Base name string
 * @ingroup debug_named
 *
 * Convenience macro that automatically uses "0x%tx" format specifier for rwlock addresses.
 */
#ifndef NDEBUG
#define NAMED_REGISTER_RWLOCK(lock, name)                                                                              \
  named_register((uintptr_t)(const void *)(lock), (name), "rwlock", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_RWLOCK(lock, name) (name)
#endif

/**
 * @brief Register a condition variable with automatic format specifier
 * @param cond Condition variable pointer
 * @param name Base name string
 * @ingroup debug_named
 *
 * Convenience macro that automatically uses "0x%tx" format specifier for cond addresses.
 */
#ifndef NDEBUG
#define NAMED_REGISTER_COND(cond, name)                                                                                \
  named_register((uintptr_t)(const void *)(cond), (name), "cond", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_COND(cond, name) (name)
#endif

/**
 * @brief Register a socket with automatic format specifier
 * @param socket Socket pointer
 * @param name Base name string
 * @ingroup debug_named
 *
 * Convenience macro that automatically uses "0x%tx" format specifier for socket addresses.
 */
#ifndef NDEBUG
#define NAMED_REGISTER_SOCKET(socket, name)                                                                            \
  named_register((uintptr_t)(intptr_t)(socket), (name), "socket", "0x%tx", __FILE__, __LINE__, __func__)
#define NAMED_UNREGISTER_SOCKET(socket) NAMED_UNREGISTER_ID((socket))
#else
#define NAMED_REGISTER_SOCKET(socket, name) (name)
#define NAMED_UNREGISTER_SOCKET(socket) ((void)0)
#endif

/**
 * @brief Register a WebSocket connection with automatic format specifier
 * @param websocket WebSocket pointer
 * @param name Base name string
 * @ingroup debug_named
 *
 * Convenience macro that automatically uses "0x%tx" format specifier for websocket addresses.
 */
#ifndef NDEBUG
#define NAMED_REGISTER_WEBSOCKET(websocket, name)                                                                      \
  named_register((uintptr_t)(const void *)(websocket), (name), "websocket", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_WEBSOCKET(websocket, name) (name)
#endif

/**
 * @brief Register a data channel with automatic format specifier
 * @param datachannel Data channel pointer
 * @param name Base name string
 * @ingroup debug_named
 *
 * Convenience macro that automatically uses "0x%tx" format specifier for datachannel addresses.
 */
#ifndef NDEBUG
#define NAMED_REGISTER_DATACHANNEL(datachannel, name)                                                                  \
  named_register((uintptr_t)(const void *)(datachannel), (name), "datachannel", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_DATACHANNEL(datachannel, name) (name)
#endif

/**
 * @brief Register a thread pool work item with automatic format specifier
 * @param work Work item pointer
 * @param name Base name string
 * @ingroup debug_named
 *
 * Convenience macro that automatically uses "0x%tx" format specifier for work item addresses.
 */
#ifndef NDEBUG
#define NAMED_REGISTER_THREADPOOL_WORK(work, name)                                                                     \
  named_register((uintptr_t)(const void *)(work), (name), "work", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_THREADPOOL_WORK(work, name) (name)
#endif

/**
 * @brief Register a client connection with automatic format specifier
 * @param client Client pointer
 * @param name Base name string
 * @ingroup debug_named
 *
 * Convenience macro that automatically uses "0x%tx" format specifier for client addresses.
 */
#ifndef NDEBUG
#define NAMED_REGISTER_CLIENT(client, name)                                                                            \
  named_register((uintptr_t)(const void *)(client), (name), "client", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_CLIENT(client, name) (name)
#endif

/**
 * @brief Register a crypto context with automatic format specifier
 * @param ctx Crypto context pointer
 * @param name Base name string (typically "crypto_client_<id>")
 * @ingroup debug_named
 *
 * Convenience macro that automatically uses "0x%tx" format specifier for crypto context addresses.
 */
#ifndef NDEBUG
#define NAMED_REGISTER_CRYPTO_CONTEXT(ctx, name)                                                                       \
  named_register((uintptr_t)(const void *)(ctx), (name), "crypto", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_CRYPTO_CONTEXT(ctx, name) (name)
#endif

/**
 * @brief Register a transport with automatic format specifier
 * @param transport Transport pointer
 * @param name Base name string (typically "transport_client_<id>" or "transport_tcp_<id>")
 * @ingroup debug_named
 *
 * Convenience macro that automatically uses "0x%tx" format specifier for transport addresses.
 */
#ifndef NDEBUG
#define NAMED_REGISTER_TRANSPORT(transport, name)                                                                      \
  named_register((uintptr_t)(const void *)(transport), (name), "transport", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_TRANSPORT(transport, name) (name)
#endif

/**
 * @brief Register a frame buffer with automatic format specifier
 * @param buf Frame buffer pointer
 * @param name Base name string
 * @ingroup debug_named
 *
 * Convenience macro that automatically uses "0x%tx" format specifier for frame buffer addresses.
 */
#ifndef NDEBUG
#define NAMED_REGISTER_FRAME_BUFFER(buf, name)                                                                         \
  named_register((uintptr_t)(const void *)(buf), (name), "frame_buffer", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_FRAME_BUFFER(buf, name) (name)
#endif

/**
 * @brief Register a packet queue with automatic format specifier
 * @param queue Packet queue pointer
 * @param name Base name string
 * @ingroup debug_named
 *
 * Convenience macro that automatically uses "0x%tx" format specifier for packet queue addresses.
 */
#ifndef NDEBUG
#define NAMED_REGISTER_PACKET_QUEUE(queue, name)                                                                       \
  named_register((uintptr_t)(const void *)(queue), (name), "packet_queue", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_PACKET_QUEUE(queue, name) (name)
#endif

/**
 * @brief Register an audio ring buffer with automatic format specifier
 * @param buf Audio ring buffer pointer
 * @param name Base name string
 * @ingroup debug_named
 *
 * Convenience macro that automatically uses "0x%tx" format specifier for ring buffer addresses.
 */
#ifndef NDEBUG
#define NAMED_REGISTER_AUDIO_RINGBUF(buf, name)                                                                        \
  named_register((uintptr_t)(const void *)(buf), (name), "audio_ringbuf", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_AUDIO_RINGBUF(buf, name) (name)
#endif

/**
 * @brief Register an audio mixer with automatic format specifier
 * @param mixer Mixer pointer
 * @param name Base name string
 * @ingroup debug_named
 *
 * Convenience macro that automatically uses "0x%tx" format specifier for mixer addresses.
 */
#ifndef NDEBUG
#define NAMED_REGISTER_MIXER(mixer, name)                                                                              \
  named_register((uintptr_t)(const void *)(mixer), (name), "mixer", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_MIXER(mixer, name) (name)
#endif

/**
 * @brief Register an audio codec with automatic format specifier
 * @param codec Audio codec pointer
 * @param name Base name string
 * @ingroup debug_named
 *
 * Convenience macro that automatically uses "0x%tx" format specifier for audio codec addresses.
 */
#ifndef NDEBUG
#define NAMED_REGISTER_AUDIO_CODEC(codec, name)                                                                        \
  named_register((uintptr_t)(const void *)(codec), (name), "audio_codec", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_AUDIO_CODEC(codec, name) (name)
#endif

/**
 * @brief Register a video encoder with automatic format specifier
 * @param encoder Video encoder pointer
 * @param name Base name string
 * @ingroup debug_named
 *
 * Convenience macro that automatically uses "0x%tx" format specifier for video encoder addresses.
 */
#ifndef NDEBUG
#define NAMED_REGISTER_VIDEO_ENCODER(encoder, name)                                                                    \
  named_register((uintptr_t)(const void *)(encoder), (name), "video_encoder", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_VIDEO_ENCODER(encoder, name) (name)
#endif

/**
 * @brief Register a buffer pool with automatic format specifier
 * @param pool Buffer pool pointer
 * @param name Base name string
 * @ingroup debug_named
 *
 * Convenience macro that automatically uses "0x%tx" format specifier for buffer pool addresses.
 */
#ifndef NDEBUG
#define NAMED_REGISTER_BUFFER_POOL(pool, name)                                                                         \
  named_register((uintptr_t)(const void *)(pool), (name), "buffer_pool", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_BUFFER_POOL(pool, name) (name)
#endif

/**
 * @brief Register an FFmpeg decoder with automatic format specifier
 * @param decoder FFmpeg decoder pointer
 * @param name Base name string
 * @ingroup debug_named
 *
 * Convenience macro that automatically uses "0x%tx" format specifier for decoder addresses.
 */
#ifndef NDEBUG
#define NAMED_REGISTER_FFMPEG_DECODER(decoder, name)                                                                   \
  named_register((uintptr_t)(const void *)(decoder), (name), "ffmpeg_decoder", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_FFMPEG_DECODER(decoder, name) (name)
#endif

/**
 * @brief Register an audio context with automatic format specifier
 * @param ctx Audio context pointer
 * @param name Base name string
 * @ingroup debug_named
 *
 * Convenience macro that automatically uses "0x%tx" format specifier for audio context addresses.
 */
#ifndef NDEBUG
#define NAMED_REGISTER_AUDIO_CONTEXT(ctx, name)                                                                        \
  named_register((uintptr_t)(const void *)(ctx), (name), "audio_context", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_AUDIO_CONTEXT(ctx, name) (name)
#endif

/**
 * @brief Register a rate limiter with automatic format specifier
 * @param limiter Rate limiter pointer
 * @param name Base name string
 * @ingroup debug_named
 *
 * Convenience macro that automatically uses "0x%tx" format specifier for rate limiter addresses.
 */
#ifndef NDEBUG
#define NAMED_REGISTER_RATE_LIMITER(limiter, name)                                                                     \
  named_register((uintptr_t)(const void *)(limiter), (name), "rate_limiter", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_RATE_LIMITER(limiter, name) (name)
#endif

/**
 * @brief Register a WAV writer with automatic format specifier
 * @param writer WAV writer pointer
 * @param name Base name string
 * @ingroup debug_named
 *
 * Convenience macro that automatically uses "0x%tx" format specifier for WAV writer addresses.
 */
#ifndef NDEBUG
#define NAMED_REGISTER_WAV_WRITER(writer, name)                                                                        \
  named_register((uintptr_t)(const void *)(writer), (name), "wav_writer", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_WAV_WRITER(writer, name) (name)
#endif

/**
 * @brief Register a media source with automatic format specifier
 * @param source Media source pointer
 * @param name Base name string
 * @ingroup debug_named
 *
 * Convenience macro that automatically uses "0x%tx" format specifier for media source addresses.
 */
#ifndef NDEBUG
#define NAMED_REGISTER_MEDIA_SOURCE(source, name)                                                                      \
  named_register((uintptr_t)(const void *)(source), (name), "media_source", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_MEDIA_SOURCE(source, name) (name)
#endif

/**
 * @brief Open a file and register the file descriptor
 * @param pathname File path to open
 * @param name Debug name for the file descriptor
 * @param flags Open flags (O_RDONLY, O_WRONLY, etc.)
 * @param ... Optional mode argument for O_CREAT
 * @ingroup debug_named
 *
 * Convenience macro that opens a file and registers the returned FD.
 * Automatically uses "%d" format specifier for file descriptor integers.
 *
 * Usage:
 *   int fd = NAMED_OPEN("/path/to/file", "myfile", O_RDONLY);
 *   int fd = NAMED_OPEN("/path/to/file", "myfile", O_CREAT | O_WRONLY, 0644);
 */
#ifndef NDEBUG
#define NAMED_OPEN(pathname, name, flags, ...)                                                                         \
  ({                                                                                                                   \
    int _fd;                                                                                                           \
    if ((flags) & O_CREAT) {                                                                                           \
      _fd = platform_open((pathname), (flags), __VA_ARGS__);                                                           \
    } else {                                                                                                           \
      _fd = platform_open((pathname), (flags));                                                                        \
    }                                                                                                                  \
    if (_fd >= 0) {                                                                                                    \
      named_register_fd(_fd, __FILE__, __LINE__, __func__);                                                            \
    }                                                                                                                  \
    _fd;                                                                                                               \
  })
#else
#define NAMED_OPEN(pathname, name, flags, ...)                                                                         \
  ({                                                                                                                   \
    int _fd;                                                                                                           \
    if ((flags) & O_CREAT) {                                                                                           \
      _fd = platform_open((pathname), (flags), __VA_ARGS__);                                                           \
    } else {                                                                                                           \
      _fd = platform_open((pathname), (flags));                                                                        \
    }                                                                                                                  \
    _fd;                                                                                                               \
  })
#endif

/**
 * @brief Unregister a file descriptor
 * @param fd File descriptor to unregister
 * @ingroup debug_named
 *
 * Call this before closing a named file descriptor.
 */
#define NAMED_UNREGISTER_FD(fd) NAMED_UNREGISTER_ID((fd))

/**
 * @brief Register an existing file descriptor
 * @param fd File descriptor (must be >= 0)
 * @param name Ignored - kept for API compatibility
 * @ingroup debug_named
 *
 * Convenience macro for registering file descriptors that are already open.
 * Automatically uses "fd=%d" format specifier and type namespace to avoid collisions.
 * In release builds (NDEBUG), this is a no-op.
 */
#ifndef NDEBUG
#define NAMED_REGISTER_FD(fd, name) named_register_fd((fd), __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_FD(fd, name) ((void)0)
#endif

/**
 * @brief Register an atomic_t with automatic format specifier
 * @param a Pointer to atomic_t
 * @param name Base name string
 * @ingroup debug_named
 *
 * Convenience macro that automatically uses "0x%tx" format specifier for atomic addresses.
 */
#ifndef NDEBUG
#define NAMED_REGISTER_ATOMIC(a, name)                                                                                     \
  named_register((uintptr_t)(const void *)(a), (name), "atomic", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_ATOMIC(a, name) (name)
#endif

/**
 * @brief Register an atomic_ptr_t with automatic format specifier
 * @param a Pointer to atomic_ptr_t
 * @param name Base name string
 * @ingroup debug_named
 *
 * Convenience macro that automatically uses "0x%tx" format specifier for atomic_ptr addresses.
 */
#ifndef NDEBUG
#define NAMED_REGISTER_ATOMIC_PTR(a, name)                                                                                 \
  named_register((uintptr_t)(const void *)(a), (name), "atomic_ptr", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_ATOMIC_PTR(a, name) (name)
#endif

// ============================================================================
// Thread-Specific Registration Macros
// ============================================================================

/**
 * @brief Register a thread handle with name and automatic format specifier
 * @param thread asciichat_thread_t handle (NOT a pointer)
 * @param name Base name string
 * @ingroup debug_named
 *
 * Convenience macro that automatically uses "0x%tx" format specifier for thread addresses.
 * In debug builds, automatically captures __FILE__, __LINE__, and __func__ for location info.
 * The thread handle itself (not a pointer) is used as the registry key.
 * In release builds (NDEBUG), this is a no-op.
 */
#ifndef NDEBUG
#define NAMED_REGISTER_THREAD(thread, name)                                                                            \
  named_register(asciichat_thread_to_key((thread)), (name), "thread", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_THREAD(thread, name) ((void)0)
#endif

/**
 * @brief Unregister a thread handle
 * @param thread asciichat_thread_t handle (NOT a pointer)
 * @ingroup debug_named
 */
#ifndef NDEBUG
#define NAMED_UNREGISTER_THREAD(thread) named_unregister(asciichat_thread_to_key((thread)))
#else
#define NAMED_UNREGISTER_THREAD(thread) ((void)0)
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
#define NAME_DESCRIBE_MUTEX(m) named_describe((uintptr_t)(const void *)(m), "mutex")

/**
 * @brief Describe an rwlock_t for logging
 * @param l Pointer to rwlock_t
 * @ingroup debug_named
 */
#define NAME_DESCRIBE_RWLOCK(l) named_describe((uintptr_t)(const void *)(l), "rwlock")

/**
 * @brief Describe a cond_t for logging
 * @param c Pointer to cond_t
 * @ingroup debug_named
 */
#define NAME_DESCRIBE_COND(c) named_describe((uintptr_t)(const void *)(c), "cond")

/**
 * @brief Describe a thread for logging
 * @param t asciichat_thread_t handle
 * @ingroup debug_named
 *
 * Uses named_describe_thread() function to handle platform-specific conversion.
 */
#define NAME_DESCRIBE_THREAD(t) named_describe_thread(t)

/**
 * @brief Describe a transport for logging
 * @param tr Pointer to acip_transport_t
 * @ingroup debug_named
 */
#define NAME_DESCRIBE_TRANSPORT(tr) named_describe((uintptr_t)(const void *)(tr), "transport")

/**
 * @brief Describe a socket for logging
 * @param fd socket_t file descriptor
 * @ingroup debug_named
 */
#define NAME_DESCRIBE_SOCKET(fd) named_describe((uintptr_t)(intptr_t)(fd), "socket")

/**
 * @brief Describe a ring buffer for logging
 * @param rb Pointer to ringbuffer_t
 * @ingroup debug_named
 */
#define NAME_DESCRIBE_RINGBUF(rb) named_describe((uintptr_t)(const void *)(rb), "ringbuf")

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

// ============================================================================
// Registry Iteration
// ============================================================================

/**
 * @brief Callback function for iterating registered entries
 * @param key The registered key (uintptr_t)
 * @param name The registered name string
 * @param user_data User-provided context pointer
 * @ingroup debug_named
 *
 * Called for each entry in the registry. The callback should not modify
 * the registry during iteration.
 */
typedef void (*named_iter_callback_t)(uintptr_t key, const char *name, void *user_data);

/**
 * @brief Register a TCP client with automatic format specifier
 * @param client TCP client pointer
 * @param name Base name string
 * @ingroup debug_named
 */
#ifndef NDEBUG
#define NAMED_REGISTER_TCP_CLIENT(client, name)                                                                        \
  named_register((uintptr_t)(const void *)(client), (name), "tcp_client", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_TCP_CLIENT(client, name) (name)
#endif

/**
 * @brief Register a WebSocket client with automatic format specifier
 * @param client WebSocket client pointer
 * @param name Base name string
 * @ingroup debug_named
 */
#ifndef NDEBUG
#define NAMED_REGISTER_WEBSOCKET_CLIENT(client, name)                                                                  \
  named_register((uintptr_t)(const void *)(client), (name), "websocket_client", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_WEBSOCKET_CLIENT(client, name) (name)
#endif

/**
 * @brief Register an app client with automatic format specifier
 * @param client App client pointer
 * @param name Base name string
 * @ingroup debug_named
 */
#ifndef NDEBUG
#define NAMED_REGISTER_APP_CLIENT(client, name)                                                                        \
  named_register((uintptr_t)(const void *)(client), (name), "app_client", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_APP_CLIENT(client, name) (name)
#endif

/**
 * @brief Register a video frame buffer with automatic format specifier
 * @param buf Video frame buffer pointer
 * @param name Base name string
 * @ingroup debug_named
 */
#ifndef NDEBUG
#define NAMED_REGISTER_VIDEO_FRAME_BUFFER(buf, name)                                                                   \
  named_register((uintptr_t)(const void *)(buf), (name), "video_frame_buffer", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_VIDEO_FRAME_BUFFER(buf, name) (name)
#endif

/**
 * @brief Register a node pool with automatic format specifier
 * @param pool Node pool pointer
 * @param name Base name string
 * @ingroup debug_named
 */
#ifndef NDEBUG
#define NAMED_REGISTER_NODE_POOL(pool, name)                                                                           \
  named_register((uintptr_t)(const void *)(pool), (name), "node_pool", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_NODE_POOL(pool, name) (name)
#endif

/**
 * @brief Register a thread pool with automatic format specifier
 * @param pool Thread pool pointer
 * @param name Base name string
 * @ingroup debug_named
 */
#ifndef NDEBUG
#define NAMED_REGISTER_THREAD_POOL(pool, name)                                                                         \
  named_register((uintptr_t)(const void *)(pool), (name), "thread_pool", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_THREAD_POOL(pool, name) (name)
#endif

/**
 * @brief Register an options builder with automatic format specifier
 * @param builder Options builder pointer
 * @param name Base name string
 * @ingroup debug_named
 */
#ifndef NDEBUG
#define NAMED_REGISTER_OPTIONS_BUILDER(builder, name)                                                                  \
  named_register((uintptr_t)(const void *)(builder), (name), "options_builder", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_OPTIONS_BUILDER(builder, name) (name)
#endif

/**
 * @brief Register a simple frame swap with automatic format specifier
 * @param swap Simple frame swap pointer
 * @param name Base name string
 * @ingroup debug_named
 */
#ifndef NDEBUG
#define NAMED_REGISTER_SIMPLE_FRAME_SWAP(swap, name)                                                                   \
  named_register((uintptr_t)(const void *)(swap), (name), "simple_frame_swap", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_SIMPLE_FRAME_SWAP(swap, name) (name)
#endif

/**
 * @brief Register a client audio pipeline with automatic format specifier
 * @param pipeline Client audio pipeline pointer
 * @param name Base name string
 * @ingroup debug_named
 */
#ifndef NDEBUG
#define NAMED_REGISTER_CLIENT_AUDIO_PIPELINE(pipeline, name)                                                           \
  named_register((uintptr_t)(const void *)(pipeline), (name), "client_audio_pipeline", "0x%tx", __FILE__, __LINE__,    \
                 __func__)
#else
#define NAMED_REGISTER_CLIENT_AUDIO_PIPELINE(pipeline, name) (name)
#endif

#ifdef DEBUG_MEMORY
#define NAMED_REGISTER_WEBSOCKET_IMPL(data, name)                                                                      \
  named_register((uintptr_t)(const void *)(data), (name), "websocket_impl", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_WEBSOCKET_IMPL(data, name) (name)
#endif

/**
 * @brief Register all packet types from packet_type_t enum
 * @ingroup debug_named
 *
 * Registers all packet type enum values in the named registry using keys
 * in the format "PACKET_TYPE=%d" where %d is the enum value.
 * This should be called once during initialization to enable packet type
 * identification in log message formatting.
 */
void named_registry_register_packet_types(void);

/**
 * @brief Iterate through all registered entries
 * @param callback Function to call for each entry
 * @param user_data Opaque context passed to callback
 * @ingroup debug_named
 *
 * Safely iterates through all registered entries without holding the lock
 * for the entire iteration (entries are copied). Callback is invoked for
 * each entry. In release builds (NDEBUG), this is a no-op.
 */
void named_registry_for_each(named_iter_callback_t callback, void *user_data);

/**
 * @brief Register a libwebsockets context with automatic format specifier
 * @param context LWS context pointer (struct lws_context *)
 * @param name Base name string (e.g., "ws_server" or "ws_client")
 * @ingroup debug_named
 *
 * Convenience macro for naming libwebsockets contexts. Automatically uses "0x%tx" format.
 * Usage: NAMED_REGISTER_LWS_CONTEXT(lws_ctx, "ws_server");
 */
#ifndef NDEBUG
#define NAMED_REGISTER_LWS_CONTEXT(context, name)                                                                      \
  named_register((uintptr_t)(const void *)(context), (name), "lws_context", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_LWS_CONTEXT(context, name) (name)
#endif

/**
 * @brief Register an FFmpeg decoder context with automatic format specifier
 * @param context FFmpeg context pointer (AVFormatContext * or similar)
 * @param name Base name string (e.g., "ffmpeg_decoder")
 * @ingroup debug_named
 *
 * Convenience macro for naming FFmpeg contexts. Automatically uses "0x%tx" format.
 * Usage: NAMED_REGISTER_FFMPEG_CONTEXT(avctx, "ffmpeg_input");
 */
#ifndef NDEBUG
#define NAMED_REGISTER_FFMPEG_CONTEXT(context, name)                                                                   \
  named_register((uintptr_t)(const void *)(context), (name), "ffmpeg_context", "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_FFMPEG_CONTEXT(context, name) (name)
#endif

/**
 * @brief Register a generic context with automatic format specifier
 * @param context Any context pointer
 * @param context_type Type string (e.g., "ssl_context", "decoder_context")
 * @param name Base name string
 * @ingroup debug_named
 *
 * Convenience macro for naming any opaque context. Automatically uses "0x%tx" format.
 * Usage: NAMED_REGISTER_CONTEXT(ctx, "ssl_context", "tls_server");
 */
#ifndef NDEBUG
#define NAMED_REGISTER_CONTEXT(context, context_type, name)                                                            \
  named_register((uintptr_t)(const void *)(context), (name), (context_type), "0x%tx", __FILE__, __LINE__, __func__)
#else
#define NAMED_REGISTER_CONTEXT(context, context_type, name) (name)
#endif

#ifdef __cplusplus
}
#endif

/** @} */ /* debug_named */
