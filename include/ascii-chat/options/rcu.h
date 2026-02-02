/**
 * @file options_state.h
 * @brief 🔒 Thread-safe RCU-based options state management
 * @ingroup options
 *
 * ## Overview
 *
 * This module provides a thread-safe options management system using Read-Copy-Update (RCU)
 * pattern. RCU is perfect for ascii-chat's workload:
 * - **Read-heavy**: Options are accessed constantly by render threads (60fps video, 172fps audio)
 * - **Write-rare**: Options are set at startup and rarely changed (terminal resize is rare)
 * - **Lock-free reads**: Zero contention for readers, no blocking, no cache line bouncing
 *
 * ## RCU Pattern Benefits
 *
 * 1. **Lock-free reads**: Readers use atomic_load (single instruction, no locks)
 * 2. **No blocking**: Readers never wait for writers, writers never block readers
 * 3. **Scalability**: Perfect scaling to many threads (no lock contention)
 * 4. **Cache-friendly**: Read-only pointer loads hit CPU cache efficiently
 *
 * ## Architecture
 *
 * ```
 * Global atomic pointer → Current options struct (immutable for readers)
 *                          ↓
 *                     [options_t fields...]
 *
 * Writer updates:
 * 1. Allocate new options struct
 * 2. Copy current values
 * 3. Apply modifications
 * 4. Atomic pointer swap
 * 5. Defer free old struct (grace period)
 * ```
 *
 * ## Usage Patterns
 *
 * ### Reading Options (Lock-Free)
 *
 * ```c
 * // Fast path - no locks, just atomic pointer load
 * const options_t *opts = options_get();
 * int width = opts->width;
 * int height = opts->height;
 * // opts pointer is guaranteed stable for this scope
 * ```
 *
 * ### Updating Options (Copy-on-Write)
 *
 * Use the generic setter functions for all option updates:
 *
 * ```c
 * // Single field updates
 * options_set_int("width", 160);
 * options_set_int("height", 60);
 * options_set_bool("audio_enabled", true);
 * options_set_string("log_file", "/var/log/app.log");
 *
 * // Multiple updates use separate setter calls (each creates one RCU update)
 * options_set_int("width", 160);
 * options_set_int("height", 60);
 * ```
 *
 * ## Memory Safety
 *
 * **Deferred Reclamation**: Old options structs are not freed immediately.
 * We use a simple grace period mechanism:
 * - Keep a reference count of active readers (optional, currently we just defer)
 * - Free old struct after all threads guaranteed to have loaded new pointer
 * - Simplest approach: Never free (acceptable for infrequent updates)
 * - Advanced: Hazard pointers or epoch-based reclamation (future work)
 *
 * ## Thread Safety Guarantees
 *
 * - **Readers**: Always see a consistent snapshot (no partial updates)
 * - **Writers**: Serialized with mutex (multiple writers don't conflict)
 * - **Memory**: Safe reclamation ensures no use-after-free
 * - **Ordering**: Acquire/release semantics ensure visibility
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#pragma once

#include "../options/options.h" // Provides options_t struct definition
#include "../platform/abstraction.h"
#include <stdbool.h>
#include <stdatomic.h>

/**
 * @brief Initialize RCU options system
 *
 * Must be called once at program startup before any threads access options.
 * Allocates initial options struct and sets up atomic pointer.
 *
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t options_state_init(void);

/**
 * @brief Set options from a parsed options struct
 *
 * Called by options_init() after parsing to publish the options struct to RCU.
 * This atomically replaces the current options with the provided struct.
 *
 * @param opts Pointer to options struct to publish (will be copied)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t options_state_set(const options_t *opts);

/**
 * @brief Shutdown RCU options system
 *
 * Frees the current options struct and cleans up resources.
 * Should be called at program shutdown after all threads have exited.
 */
void options_state_shutdown(void);

/**
 * @brief Clean up schema resources
 *
 * Frees the dynamically allocated options schema array and all associated strings.
 * Should be called at program shutdown after options_state_shutdown().
 * Safe to call multiple times or if schema was never built.
 */
void options_cleanup_schema(void);
