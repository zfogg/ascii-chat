/**
 * @file client/keepalive.c
 * @ingroup client_keepalive
 * @brief 💓 Client keepalive: periodic ping/pong exchange for reliable connection failure detection
 *
 * The keepalive system uses a dedicated ping thread:
 * - **Ping Thread**: Sends periodic ping packets to server
 * - **Response Monitoring**: Server responds with pong packets
 * - **Timeout Detection**: Connection loss detected via failed pings
 * - **Coordinated Shutdown**: Thread integrates with global shutdown logic
 *
 * ## Timing Strategy
 *
 * Keepalive timing optimized for connection reliability:
 * - **Ping Interval**: 3 seconds between ping packets
 * - **Server Timeout**: Server times out clients after 5 seconds of silence
 * - **Safety Margin**: 2-second buffer prevents false disconnections
 * - **Network Tolerance**: Accounts for network jitter and processing delays
 *
 * ## Thread Management
 *
 * Ping thread follows robust lifecycle management:
 * - **Creation**: Thread started after successful connection
 * - **Monitoring**: Continuous health checks and connection validation
 * - **Coordination**: Respects global shutdown flags and connection state
 * - **Termination**: Graceful shutdown with resource cleanup
 * - **Recovery**: Thread recreated for each new connection
 *
 * ## Connection Health Monitoring
 *
 * Multiple layers of connection health detection:
 * 1. **Socket Validity**: Check socket file descriptor before sending
 * 2. **Connection Flags**: Monitor atomic connection state variables
 * 3. **Send Failures**: Detect network errors during ping transmission
 * 4. **Global Shutdown**: Respect application-wide shutdown requests
 *
 * ## Integration Points
 *
 * - **main.c**: Keepalive thread lifecycle management
 * - **server.c**: Ping packet transmission and connection monitoring
 * - **protocol.c**: Pong packet reception and response handling
 * - **network.c**: Low-level ping/pong packet formatting
 *
 * ## Error Handling
 *
 * Keepalive errors handled with appropriate escalation:
 * - **Ping Send Failures**: Signal connection loss for reconnection
 * - **Socket Errors**: Clean thread exit and connection cleanup
 * - **Network Timeouts**: Graceful handling without false alarms
 * - **Thread Failures**: Log errors and continue with degraded monitoring
 *
 * ## Resource Management
 *
 * Minimal resource usage for efficient keepalive:
 * - **Thread Resources**: Single lightweight thread with minimal stack
 * - **Network Overhead**: Small ping/pong packets with minimal bandwidth
 * - **Timing Precision**: Efficient sleep implementation with early wake
 * - **Memory Usage**: No dynamic allocations in steady-state operation
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 */

#include "keepalive.h"
#include "main.h"
#include "server.h"
#include "crypto.h"

#include "common.h"
#include "platform/abstraction.h"

#include <stdatomic.h>

/* ============================================================================
 * Keepalive Thread Management
 * ============================================================================ */

/**
 * @brief Ping/keepalive thread handle
 *
 * Thread handle for the background thread that sends periodic PING packets
 * to detect connection health. Created during connection establishment,
 * joined during shutdown.
 *
 * @ingroup client_keepalive
 */
static asciithread_t g_ping_thread;

/**
 * @brief Flag indicating if ping thread was successfully created
 *
 * Used during shutdown to determine whether the thread handle is valid and
 * should be joined. Prevents attempting to join a thread that was never created.
 *
 * @ingroup client_keepalive
 */
static bool g_ping_thread_created = false;

/**
 * @brief Atomic flag indicating ping thread has exited
 *
 * Set by the ping thread when it exits. Used by other threads to detect
 * thread termination without blocking on thread join operations.
 *
 * @ingroup client_keepalive
 */
static atomic_bool g_ping_thread_exited = false;

/* ============================================================================
 * Keepalive Configuration
 * ============================================================================ */

/** Ping interval in seconds (must be less than server timeout) */
#define PING_INTERVAL_SECONDS 3

/** Sleep interval for ping timing loop (1 second) */
#define PING_SLEEP_INTERVAL_SECONDS 1

/* ============================================================================
 * Ping Thread Implementation
 * ============================================================================ */

/**
 * Main ping/keepalive thread function
 *
 * Implements periodic ping transmission to maintain connection health.
 * Monitors connection state and coordinates with global shutdown logic.
 *
 * Ping Loop Operation:
 * 1. Check global shutdown flags and connection status
 * 2. Validate socket file descriptor before transmission
 * 3. Send ping packet to server via connection module
 * 4. Handle transmission errors and connection loss detection
 * 5. Sleep with interruptible timing for responsive shutdown
 * 6. Repeat until connection loss or shutdown requested
 *
 * Error Handling:
 * - Socket validation failures trigger clean thread exit
 * - Ping transmission failures signal connection loss
 * - Network errors handled gracefully without panic
 * - Thread coordination respects shutdown timing
 *
 * @param arg Unused thread argument
 * @return NULL on thread exit
 *
 * @ingroup client_keepalive
 */
static void *ping_thread_func(void *arg) {
  (void)arg;

#ifdef DEBUG_THREADS
  log_debug("Ping thread started");
#endif

  while (!should_exit() && !server_connection_is_lost()) {
    // Check if connection is still active before sending
    if (!server_connection_is_active()) {
      log_debug("Connection inactive, exiting ping thread");
      break;
    }

    // Check if session rekeying should be triggered
    if (crypto_client_should_rekey()) {
      log_debug("Rekey threshold reached, initiating session rekey");
      if (crypto_client_initiate_rekey() < 0) {
        log_error("Failed to initiate rekey");
        // Don't break - continue with keepalive, rekey will be retried
      }
    }

    // Send ping packet every PING_INTERVAL_SECONDS to keep connection alive
    // Server timeout is 5 seconds, so 3-second pings provide safety margin
    if (threaded_send_ping_packet() < 0) {
      log_debug("Failed to send ping packet");
      // Set connection lost flag so main loop knows to reconnect
      server_connection_lost();
      break;
    }

    // Sleep with early wake capability for responsive shutdown
    // Break sleep into 1-second intervals to check shutdown flags
    for (int i = 0;
         i < PING_INTERVAL_SECONDS && !should_exit() && !server_connection_is_lost() && server_connection_is_active();
         i++) {
      platform_sleep_usec(PING_SLEEP_INTERVAL_SECONDS * 1000 * 1000); // 1 second
    }
  }

#ifdef DEBUG_THREADS
  log_debug("Ping thread stopped");
#endif

  atomic_store(&g_ping_thread_exited, true);
  return NULL;
}

/* ============================================================================
 * Public Interface Functions
 * ============================================================================ */

/**
 * Start keepalive/ping thread
 *
 * Creates and starts the ping thread for connection keepalive.
 * Must be called after successful server connection establishment.
 *
 * @return 0 on success, negative on error
 *
 * @ingroup client_keepalive
 */
int keepalive_start_thread() {
  if (g_ping_thread_created) {
    log_warn("Ping thread already created");
    return 0;
  }

  // Start ping thread for keepalive
  atomic_store(&g_ping_thread_exited, false);
  if (ascii_thread_create(&g_ping_thread, ping_thread_func, NULL) != 0) {
    log_error("Failed to create ping thread");
    return -1;
  }

  g_ping_thread_created = true;
  return 0;
}

/**
 * Stop keepalive/ping thread
 *
 * Gracefully stops the ping thread and cleans up resources.
 * Safe to call multiple times.
 *
 * @ingroup client_keepalive
 */
void keepalive_stop_thread() {
  if (!g_ping_thread_created) {
    return;
  }

  // Don't call signal_exit() here - that's for global shutdown only!
  // The ping thread monitors connection state and will exit when connection is lost

  // Wait for thread to exit gracefully
  int wait_count = 0;
  while (wait_count < 20 && !atomic_load(&g_ping_thread_exited)) {
    platform_sleep_usec(100000); // 100ms
    wait_count++;
  }

  if (!atomic_load(&g_ping_thread_exited)) {
    log_error("Ping thread not responding - forcing join");
  }

  // Join the thread
  ascii_thread_join(&g_ping_thread, NULL);
  g_ping_thread_created = false;

  log_info("Ping thread stopped and joined");
}

/**
 * Check if keepalive thread has exited
 *
 * @return true if thread has exited, false otherwise
 *
 * @ingroup client_keepalive
 */
bool keepalive_thread_exited() {
  return atomic_load(&g_ping_thread_exited);
}
