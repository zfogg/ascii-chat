/**
 * @file main.h
 * @ingroup main
 * @brief Global application exit and signal handling API
 *
 * Provides a centralized exit mechanism and signal handler registration
 * used by all modes (client, mirror, discovery) to coordinate graceful
 * shutdown. All modes share a single global exit flag and interrupt callback.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#pragma once

#include <stdbool.h>

/**
 * Check if the application should exit
 *
 * Called by mode main loops to detect shutdown requests from signals or errors.
 *
 * @return true if shutdown has been requested, false otherwise
 */
bool should_exit(void);

/**
 * Signal that the application should exit
 *
 * Sets the global exit flag and calls the registered interrupt callback if set.
 * Safe to call from signal handlers (uses only atomics and function pointers).
 * Called by signal handlers (SIGTERM, Ctrl+C) and normal code (errors, timeouts).
 */
void signal_exit(void);

/**
 * Register a mode-specific interrupt callback
 *
 * Called by mode-specific code to register a callback that will be invoked
 * when signal_exit() is called. Used to shut down network sockets so threads
 * blocked in recv() unblock quickly instead of waiting for timeouts.
 *
 * The callback is called synchronously from signal_exit(), so it must be
 * async-signal-safe (only atomics, socket_shutdown(), no malloc/etc).
 *
 * Only one callback can be registered at a time. Setting a new callback
 * replaces the previous one.
 *
 * @param cb Function to call on exit signal, or NULL to unregister
 */
void set_interrupt_callback(void (*cb)(void));

/**
 * Set up global signal handlers
 *
 * Called once at program startup in main() BEFORE mode dispatch.
 * Registers:
 * - SIGTERM → graceful shutdown with logging
 * - SIGPIPE → ignore (prevent crashes from broken pipes)
 * - SIGUSR1 → synchronization debugging output (debug builds only)
 * - SIGUSR2 → memory report (debug builds only)
 * - Console Ctrl+C handler (all platforms)
 *
 * SIGWINCH (terminal resize) is NOT registered here - client mode
 * registers its own handler because it needs special processing.
 */
void setup_signal_handlers(void);
