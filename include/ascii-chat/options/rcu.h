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
#include <ascii-chat/atomic.h>

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
void options_state_destroy(void);

/**
 * @brief Get pointer to current options struct (lock-free, thread-safe)
 *
 * Returns a pointer to the currently published options struct or a safe static
 * default if options haven't been initialized yet (before options_state_init())
 * or after options have been destroyed (options_state_destroy()).
 *
 * This function is specifically designed to be safe to call:
 * - **Before initialization**: Early startup code gets sensible defaults
 * - **During normal operation**: Lock-free atomic load with no blocking
 * - **During cleanup**: atexit handlers and shutdown code get fallback defaults
 * - **After destruction**: Code after options_state_destroy() still works
 *
 * ## Return Behavior
 *
 * Returns pointer to either:
 * 1. **Published dynamic options** (after options_state_init() and before options_state_destroy())
 * 2. **Static fallback defaults** (before init or after destroy)
 *
 * The static fallback ensures:
 * - **Never NULL**: Code never crashes on NULL pointer dereference
 * - **Sensible defaults**: All OPT_*_DEFAULT constants properly initialized
 * - **Static lifetime**: Outlives all dynamically allocated options structs
 * - **No cleanup needed**: Static memory never freed
 * - **Thread-safe**: Immutable const data, safe to read from any thread
 *
 * ## Usage Example
 *
 * @code
 * // Safe before options_state_init()
 * const options_t *opts = options_get();
 * printf("Default width: %d\n", opts->width);
 *
 * // After initialization
 * options_state_init();
 * options_state_set(&parsed_opts);
 * opts = options_get();  // Returns published struct
 *
 * // Safe after options_state_destroy()
 * options_state_destroy();
 * opts = options_get();  // Returns static defaults again
 * // Can still safely call GET_OPTION() in atexit handlers
 * @endcode
 *
 * @return Pointer to options_t struct (guaranteed never NULL)
 *
 * @note **Thread-safe**: Lock-free atomic load with acquire semantics
 * @note **Lock-free**: No mutexes or blocking calls
 * @note **Fast**: Single atomic instruction (~1-2ns latency)
 * @note **Fallback support**: Returns static defaults when not initialized or after destroy
 *
 * @see options_state_init() - Initializes options, publishes to RCU
 * @see options_state_destroy() - Clears options, returns fallback to defaults
 * @see GET_OPTION() - Convenience macro for reading single fields
 *
 * @ingroup options
 */
const options_t *options_get(void);

/**
 * @brief Clean up schema resources
 *
 * Frees the dynamically allocated options schema array and all associated strings.
 * Should be called at program shutdown after options_state_destroy().
 * Safe to call multiple times or if schema was never built.
 */
void options_cleanup_schema(void);
