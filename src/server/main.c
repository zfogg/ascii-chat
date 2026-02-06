/**
 * @file server/main.c
 * @ingroup server_main
 * @brief 🖥️ Server main entry point: multi-client connection manager with per-client rendering threads (60fps video +
 * 172fps audio)
 *
 * MODULAR COMPONENTS:
 * ===================
 * - main.c (this file): Server initialization, signal handling, connection management
 * - client.c:           Per-client lifecycle, threading, and state management
 * - protocol.c:         Network packet processing and protocol implementation
 * - stream.c:           Video mixing, ASCII frame generation, and caching
 * - render.c:           Per-client rendering threads with rate limiting
 * - stats.c:            Performance monitoring and resource tracking
 *
 * CONCURRENCY MODEL:
 * ==================
 * The server creates multiple thread types per client:
 * 1. Receive thread: Handles incoming packets from client (protocol.c functions)
 * 2. Send thread: Manages outgoing packet delivery (client.c)
 * 3. Video render thread: Generates ASCII frames at 60fps (render.c)
 * 4. Audio render thread: Mixes audio streams at 172fps (render.c)
 * 5. Stats logger thread: Periodic performance reporting (stats.c)
 *
 * CRITICAL THREAD SAFETY:
 * ========================
 * - Lock ordering: Always acquire g_client_manager_rwlock BEFORE per-client mutexes
 * - Snapshot pattern: Copy client state under mutex, then process without locks
 * - Signal-safe shutdown: SIGINT handler only sets flags and closes sockets
 * - Deterministic cleanup: Main thread waits for all worker threads before exit
 *
 * WHY THE REFACTORING:
 * ====================
 * The original server.c became unmaintainable at 2408+ lines, making it:
 * - Too large for LLM context windows (limited AI-assisted development)
 * - Difficult for humans to navigate and understand
 * - Slow to compile and modify
 * - Hard to isolate bugs and add new features
 *
 * The modular design enables:
 * - Faster development cycles (smaller compilation units)
 * - Better IDE support (jump-to-definition, IntelliSense)
 * - Easier testing and debugging (isolated components)
 * - Future extensibility (new protocols, renderers, optimizations)
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 * @version 2.0 (Post-Modularization)
 */

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h> // For write() and STDOUT_FILENO (signal-safe I/O)
#endif

#include <ascii-chat/platform/network.h> // Consolidates platform-specific network headers

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

#include "main.h"
#include <ascii-chat/common.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/uthash/uthash.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/socket.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/platform/question.h>
#include <ascii-chat/video/image.h>
#include <ascii-chat/video/simd/ascii_simd.h>
#include <ascii-chat/video/simd/common.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/network/network.h>
#include <ascii-chat/network/tcp/server.h>
#include <ascii-chat/network/acip/acds_client.h>
#include <ascii-chat/discovery/strings.h>
#include <ascii-chat/network/webrtc/stun.h>
#include <ascii-chat/thread_pool.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h> // For RCU-based options access
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/audio/mixer.h>
#include <ascii-chat/audio/audio.h>
#include "client.h"
#include "stream.h"
#include "stats.h"
#include <ascii-chat/platform/string.h>
#include <ascii-chat/platform/symbols.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/crypto/keys.h>
#include <ascii-chat/network/rate_limit/rate_limit.h>
#include <ascii-chat/network/mdns/mdns.h>
#include <ascii-chat/network/mdns/discovery.h>
#include <ascii-chat/network/errors.h>
#include <ascii-chat/network/nat/upnp.h>
#include <ascii-chat/network/webrtc/peer_manager.h>
#include <ascii-chat/network/webrtc/webrtc.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/acip/protocol.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/acip/handlers.h>
#include <ascii-chat/network/acip/server.h>
#include <ascii-chat/network/acip/client.h>
#include <ascii-chat/ui/server_status.h>

/* ============================================================================
 * Global State
 * ============================================================================
 */

/**
 * @brief Global atomic shutdown flag shared across all threads
 *
 * This flag is the primary coordination mechanism for clean server shutdown.
 * It's atomic to ensure thread-safe access without mutexes, as it's checked
 * frequently in tight loops across all worker threads.
 *
 * USAGE PATTERN:
 * - Set to true by signal handlers (SIGINT/SIGTERM) or main loop on error
 * - Checked by all worker threads to know when to exit gracefully
 * - Must be atomic to prevent race conditions during shutdown cascade
 */
atomic_bool g_server_should_exit = false;

/**
 * @brief Shutdown check callback for library code
 *
 * Provides clean separation between application state and library code.
 * Registered with shutdown_register_callback() so library code can check
 * shutdown status without directly accessing g_server_should_exit.
 */
static bool check_shutdown(void) {
  return atomic_load(&g_server_should_exit);
}

/**
 * @brief Global audio mixer instance for multi-client audio processing
 *
 * The mixer combines audio streams from multiple clients, excluding each client's
 * own audio from their outbound stream (preventing echo). Created once during
 * server initialization and shared by all audio render threads.
 *
 * THREAD SAFETY: The mixer itself is thread-safe and can be used concurrently
 * by multiple render.c audio threads without external synchronization.
 * During shutdown, set to NULL before destroying to prevent use-after-free.
 */
mixer_t *volatile g_audio_mixer = NULL;

/**
 * @brief Global shutdown condition variable for waking blocked threads
 *
 * Used to wake up threads that might be blocked on condition variables
 * (like packet queues) during shutdown. This ensures responsive shutdown
 * even when threads are waiting on blocking operations.
 */
static_cond_t g_shutdown_cond = STATIC_COND_INIT;

/**
 * @brief Global rate limiter for connection attempts and packet processing
 *
 * In-memory rate limiter to prevent connection flooding and DoS attacks.
 * Tracks connection attempts and packet rates per IP address with configurable limits.
 *
 * Default limits (from rate_limit.c):
 * - RATE_EVENT_CONNECTION: 50 connections per 60 seconds
 * - RATE_EVENT_IMAGE_FRAME: 144 FPS (8640 frames/min)
 * - RATE_EVENT_AUDIO: 172 FPS (10320 packets/min)
 * - RATE_EVENT_PING: 2 Hz (120 pings/min)
 * - RATE_EVENT_CLIENT_JOIN: 10 joins per 60 seconds
 * - RATE_EVENT_CONTROL: 100 control packets per 60 seconds
 *
 * THREAD SAFETY: The rate limiter is thread-safe and can be used concurrently
 * from the main accept loop and packet handlers without external synchronization.
 *
 * @see main.h for extern declaration
 */
rate_limiter_t *g_rate_limiter = NULL;

/**
 * @brief TCP server instance for accepting client connections
 *
 * Uses lib/network/tcp_server abstraction for dual-stack IPv4/IPv6 support.
 * Handles socket creation, binding, listening, and provides thread-safe
 * client registry for managing connected clients.
 *
 * PLATFORM NOTE: tcp_server_t handles platform-abstracted socket_t types
 * internally for proper cross-platform Windows/POSIX support.
 *
 * @ingroup server_main
 */
static tcp_server_t g_tcp_server;

/**
 * @brief Server start time for uptime calculation and status display
 *
 * Captured at server startup and used by status screen to calculate uptime.
 * Shared with status update callback.
 */
static time_t g_server_start_time = 0;

/**
 * @brief Last status screen update time
 *
 * Tracks when status was last displayed to avoid excessive updates.
 * Used by status screen thread to track frame timing.
 */
static uint64_t g_last_status_update = 0; // Microseconds from platform_get_monotonic_time_us()

/**
 * @brief Status screen thread handle
 *
 * Dedicated thread for rendering status screen at target FPS, independent
 * of network accept loop timing.
 */
static asciichat_thread_t g_status_screen_thread;

/**
 * @brief Current session string for status display
 *
 * Holds the memorable session string (e.g., "happy-sunset-ocean") for display
 * in the status screen. Set when ACDS session is created, cleared on shutdown.
 */
static char g_session_string[64] = {0};

/**
 * @brief Whether the current session is mDNS-only (not registered with ACDS)
 */
static bool g_session_is_mdns_only = false;

/**
 * @brief Global UPnP context for port mapping on home routers
 *
 * Stores the active UPnP/NAT-PMP port mapping state. Enables direct TCP
 * connectivity for ~70% of home users without requiring WebRTC.
 * Set to NULL if UPnP is disabled, unavailable, or failed to map.
 *
 * @ingroup server_main
 */
static nat_upnp_context_t *g_upnp_ctx = NULL;

/**
 * @brief Global mDNS context for LAN service discovery
 *
 * Used to advertise the server on the local network via mDNS (Multicast DNS).
 * Set to NULL if mDNS is disabled or fails to initialize.
 * Advertises service as "_ascii-chat._tcp.local"
 *
 * @ingroup server_main
 */
static asciichat_mdns_t *g_mdns_ctx = NULL;

/**
 * @brief Global ACDS client for WebRTC signaling relay
 *
 * Stores the active ACDS connection for receiving WebRTC SDP/ICE packets.
 * Used when server is registered with ACDS and session_type == SESSION_TYPE_WEBRTC.
 * Set to NULL if ACDS is disabled or connection fails.
 *
 * @ingroup server_main
 */
static acds_client_t *g_acds_client = NULL;

/**
 * @brief Global ACDS transport wrapper for sending signaling packets
 *
 * ACIP transport wrapping the ACDS client socket for sending SDP/ICE packets.
 * Created after successful ACDS connection, destroyed on shutdown.
 *
 * @ingroup server_main
 */
static acip_transport_t *g_acds_transport = NULL;

/**
 * @brief Server's participant ID in the ACDS session
 *
 * Used as sender_id in WebRTC SDP/ICE packets sent via ACDS relay.
 * Set during SESSION_JOIN, all zeros if not using ACDS.
 */
static uint8_t g_server_participant_id[16] = {0};

/**
 * @brief Global WebRTC peer manager for accepting client connections
 *
 * Manages WebRTC peer connections when acting as session creator (server role).
 * Handles SDP offer/answer exchange and ICE candidate gathering.
 * Set to NULL if WebRTC is disabled or peer manager creation fails.
 *
 * @ingroup server_main
 */
static webrtc_peer_manager_t *g_webrtc_peer_manager = NULL;

/**
 * @brief Global ACDS receive thread handle
 *
 * Background thread that receives WebRTC signaling packets from ACDS.
 * Dispatches SDP/ICE to peer_manager via callbacks.
 * Joined during server shutdown.
 *
 * @ingroup server_main
 */
static asciichat_thread_t g_acds_receive_thread;

/**
 * @brief Flag indicating if ACDS receive thread was started
 *
 * Used to determine if thread needs to be joined during shutdown.
 *
 * @ingroup server_main
 */
static bool g_acds_receive_thread_started = false;

/**
 * @brief Global ACDS ping thread handle
 *
 * Background thread that sends periodic PING packets to keep ACDS connection alive.
 * Prevents 15-second receive timeout on idle connections.
 * Joined during server shutdown.
 *
 * @ingroup server_main
 */
static asciichat_thread_t g_acds_ping_thread;

/**
 * @brief Flag indicating if ACDS ping thread was started
 *
 * Used to determine if thread needs to be joined during shutdown.
 *
 * @ingroup server_main
 */
static bool g_acds_ping_thread_started = false;

/**
 * @brief Background worker thread pool for server operations
 *
 * Manages background threads like stats logger, lock debugging, etc.
 * Threads in this pool are independent of client connections.
 */
static thread_pool_t *g_server_worker_pool = NULL;

/* ============================================================================
 * Server Crypto State
 * ============================================================================
 */

/**
 * @brief Global flag indicating if server encryption is enabled
 *
 * Set to true when the server is configured to use encryption and has
 * successfully loaded a private key. Controls whether the server performs
 * cryptographic handshakes with clients.
 *
 * @note Accessed from crypto.c for server-side crypto operations
 * @ingroup server_main
 */
bool g_server_encryption_enabled = false;

/**
 * @brief Global server private key (first identity key, for backward compatibility)
 *
 * Stores the server's primary private key loaded from the first --key flag.
 * Used for cryptographic handshakes and packet encryption/decryption.
 * Initialized during server startup from the configured key file path.
 *
 * @note This is an alias to g_server_identity_keys[0] for backward compatibility
 * @note Accessed from crypto.c for server-side crypto operations
 * @ingroup server_main
 */
private_key_t g_server_private_key = {0};

/**
 * @brief Global server identity keys array (multi-key support)
 *
 * Stores all server identity keys loaded from multiple --key flags.
 * Enables servers to present different keys (SSH, GPG) based on client expectations.
 * Server selects the appropriate key during handshake based on what the client
 * downloaded from ACDS.
 *
 * @note g_server_private_key points to g_server_identity_keys[0] for compatibility
 * @ingroup server_main
 */
private_key_t g_server_identity_keys[MAX_IDENTITY_KEYS] = {0};

/**
 * @brief Number of loaded server identity keys
 *
 * Tracks how many identity keys were successfully loaded from --key flags.
 * Zero means server is running in simple mode (no identity key).
 *
 * @ingroup server_main
 */
size_t g_num_server_identity_keys = 0;

/**
 * @brief Global client public key whitelist
 *
 * Array of public keys for clients that are authorized to connect to the
 * server. Used for client authentication when whitelist mode is enabled.
 * Sized to hold up to MAX_CLIENTS entries.
 *
 * @note Only used when client authentication is enabled
 * @note Accessed from crypto.c for client authentication
 * @ingroup server_main
 */
public_key_t g_client_whitelist[MAX_CLIENTS] = {0};

/**
 * @brief Number of whitelisted clients
 *
 * Tracks the current number of entries in g_client_whitelist that are
 * valid and active. Used to iterate the whitelist and check authorization.
 *
 * @ingroup server_main
 */
size_t g_num_whitelisted_clients = 0;

/* ============================================================================
 * Signal Handlers
 * ============================================================================
 */

/**
 * @brief Critical signal handler for SIGINT (Ctrl+C) - initiates server shutdown
 *
 * This handler is the primary entry point for graceful server shutdown. It's designed
 * to be signal-safe and perform minimal work to avoid deadlocks and undefined behavior
 * common in complex signal handlers.
 *
 * SIGNAL SAFETY STRATEGY:
 * =======================
 * Signal handlers are severely restricted in what they can safely do:
 * - Only async-signal-safe functions are allowed
 * - No mutex operations (can deadlock if main thread holds mutex)
 * - No malloc/SAFE_FREE(heap corruption if interrupted during allocation)
 * - No non-reentrant library calls (logging, printf, etc. are dangerous)
 *
 * SHUTDOWN PROCESS:
 * =================
 * 1. Set atomic g_server_should_exit flag (signal-safe, checked by all threads)
 * 2. Use raw write() for immediate user feedback (async-signal-safe)
 * 3. Broadcast shutdown condition to wake sleeping threads
 * 4. Close all sockets to interrupt blocking I/O operations
 * 5. Return quickly - let main thread handle complex cleanup
 *
 * SOCKET CLOSING RATIONALE:
 * =========================
 * Without socket closure, threads would remain blocked in:
 * - accept() in main loop (waiting for new connections)
 * - recv() in client receive threads (waiting for packets)
 * - send() in client send threads (if network is slow)
 *
 * Closing sockets causes these functions to return with error codes,
 * allowing threads to check g_server_should_exit and exit gracefully.
 *
 * PLATFORM CONSIDERATIONS:
 * ========================
 * - Windows: socket_shutdown() required to interrupt blocked recv()
 * - POSIX: socket_close() alone typically suffices
 * - Both: Avoid mutex operations (signal may interrupt mutex holder)
 *
 * @param sigint The signal number (unused, required by signal handler signature)
 */
static void server_handle_sigint(int sigint) {
  (void)(sigint);
  static _Atomic int sigint_count = 0;
  int count = atomic_fetch_add(&sigint_count, 1) + 1;
  if (count > 1) {
    platform_force_exit(1);
  }

  // STEP 1: Set atomic shutdown flag (checked by all worker threads)
  atomic_store(&g_server_should_exit, true);

  // STEP 2: Log without file I/O (no mutex, avoids deadlocks in signal handlers)
  log_info_nofile("SIGINT received - shutting down server...");

  // STEP 3: Signal TCP server to stop and close listening sockets
  // This interrupts the accept() call in the main loop
  atomic_store(&g_tcp_server.running, false);
  if (g_tcp_server.listen_socket != INVALID_SOCKET_VALUE) {
    socket_close(g_tcp_server.listen_socket);
  }
  if (g_tcp_server.listen_socket6 != INVALID_SOCKET_VALUE) {
    socket_close(g_tcp_server.listen_socket6);
  }

  // STEP 4: DO NOT access client data structures in signal handler
  // Signal handlers CANNOT safely use mutexes, rwlocks, or access complex data structures
  // This causes deadlocks and memory access violations because:
  // 1. Signal may interrupt a thread that already holds these locks
  // 2. Attempting to acquire locks in signal handler = instant deadlock
  // 3. Client array might be in an inconsistent state during modification
  //
  // SOLUTION: The listening socket closure above is sufficient to unblock accept_with_timeout()
  // The main thread will detect g_server_should_exit and properly close client sockets with timeouts

  // NOTE: Do NOT call log_destroy() here - it's not async-signal-safe
  // The main thread will handle cleanup when it detects g_server_should_exit
}

// bind_and_listen() function removed - now using lib/network/tcp_server abstraction

/* ============================================================================
 * WebRTC Callbacks
 * ============================================================================
 */

/**
 * @brief Send SDP answer/offer via ACDS signaling relay
 *
 * Called by peer_manager when it needs to send SDP to a remote participant.
 * Relays the SDP through the ACDS server to the target client.
 *
 * @param session_id Session UUID (16 bytes)
 * @param recipient_id Recipient participant UUID (16 bytes)
 * @param sdp_type SDP type ("offer" or "answer")
 * @param sdp SDP string (null-terminated)
 * @param user_data User context pointer (unused)
 * @return ASCIICHAT_OK on success, error code on failure
 */
static asciichat_error_t server_send_sdp(const uint8_t session_id[16], const uint8_t recipient_id[16],
                                         const char *sdp_type, const char *sdp, void *user_data) {
  (void)user_data;

  if (!g_acds_transport) {
    return SET_ERRNO(ERROR_INVALID_STATE, "ACDS transport not available for SDP relay");
  }

  // Calculate SDP length
  size_t sdp_len = strlen(sdp);
  if (sdp_len == 0 || sdp_len >= 8192) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid SDP length: %zu", sdp_len);
  }

  // Allocate packet buffer (header + SDP string)
  size_t total_len = sizeof(acip_webrtc_sdp_t) + sdp_len;
  uint8_t *packet = SAFE_MALLOC(total_len, uint8_t *);
  if (!packet) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate SDP packet");
  }

  // Fill header
  acip_webrtc_sdp_t *header = (acip_webrtc_sdp_t *)packet;
  memcpy(header->session_id, session_id, 16);
  // Use server's participant_id from SESSION_JOIN as sender
  memcpy(header->sender_id, g_server_participant_id, 16);
  memcpy(header->recipient_id, recipient_id, 16);
  header->sdp_type = (strcmp(sdp_type, "offer") == 0) ? 0 : 1;
  header->sdp_len = HOST_TO_NET_U16((uint16_t)sdp_len);

  // Copy SDP string after header
  memcpy(packet + sizeof(acip_webrtc_sdp_t), sdp, sdp_len);

  log_debug("Server sending WebRTC SDP %s to participant (sender=%02x%02x..., recipient=%02x%02x...) via ACDS",
            sdp_type, g_server_participant_id[0], g_server_participant_id[1], recipient_id[0], recipient_id[1]);

  // Send via ACDS transport using generic packet sender
  asciichat_error_t result =
      packet_send_via_transport(g_acds_transport, PACKET_TYPE_ACIP_WEBRTC_SDP, packet, total_len);

  SAFE_FREE(packet);

  if (result != ASCIICHAT_OK) {
    return SET_ERRNO(result, "Failed to send SDP via ACDS");
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Send ICE candidate via ACDS signaling relay
 *
 * Called by peer_manager when it gathers a new ICE candidate.
 * Relays the candidate through the ACDS server to the target client.
 *
 * @param session_id Session UUID (16 bytes)
 * @param recipient_id Recipient participant UUID (16 bytes)
 * @param candidate ICE candidate string (null-terminated)
 * @param mid Media stream ID (null-terminated)
 * @param user_data User context pointer (unused)
 * @return ASCIICHAT_OK on success, error code on failure
 */
static asciichat_error_t server_send_ice(const uint8_t session_id[16], const uint8_t recipient_id[16],
                                         const char *candidate, const char *mid, void *user_data) {
  (void)user_data;

  if (!g_acds_transport) {
    return SET_ERRNO(ERROR_INVALID_STATE, "ACDS transport not available for ICE relay");
  }

  // Calculate payload length (candidate + null + mid + null)
  size_t candidate_len = strlen(candidate);
  size_t mid_len = strlen(mid);
  size_t payload_len = candidate_len + 1 + mid_len + 1;

  if (payload_len >= 8192) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "ICE payload too large: %zu", payload_len);
  }

  // Allocate packet buffer (header + payload)
  size_t total_len = sizeof(acip_webrtc_ice_t) + payload_len;
  uint8_t *packet = SAFE_MALLOC(total_len, uint8_t *);
  if (!packet) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate ICE packet");
  }

  // Fill header
  acip_webrtc_ice_t *header = (acip_webrtc_ice_t *)packet;
  memcpy(header->session_id, session_id, 16);
  // Use server's participant_id from SESSION_JOIN as sender
  memcpy(header->sender_id, g_server_participant_id, 16);
  memcpy(header->recipient_id, recipient_id, 16);
  header->candidate_len = HOST_TO_NET_U16((uint16_t)candidate_len); // FIXED: Use candidate length, not total payload

  // Copy candidate and mid after header
  uint8_t *payload = packet + sizeof(acip_webrtc_ice_t);
  memcpy(payload, candidate, candidate_len);
  payload[candidate_len] = '\0';
  memcpy(payload + candidate_len + 1, mid, mid_len);
  payload[candidate_len + 1 + mid_len] = '\0';

  log_debug("Server sending WebRTC ICE candidate to participant (%.8s..., mid=%s) via ACDS", (const char *)recipient_id,
            mid);
  log_debug("  [2] Before ACDS send - candidate: '%s' (len=%zu)", candidate, strlen(candidate));
  log_debug("  [2] Before ACDS send - mid: '%s' (len=%zu)", mid, strlen(mid));
  log_debug("  [2] Before ACDS send - payload_len=%zu, header.candidate_len=%u", payload_len,
            NET_TO_HOST_U16(header->candidate_len));

  // Hex dump payload for debugging
  log_debug("  [2] Hex dump of payload being sent (first 100 bytes):");
  for (size_t i = 0; i < 100 && i < payload_len; i += 16) {
    char hex[64] = {0};
    char ascii[20] = {0};
    for (size_t j = 0; j < 16 && (i + j) < 100 && (i + j) < payload_len; j++) {
      snprintf(hex + j * 3, sizeof(hex) - j * 3, "%02x ", payload[i + j]);
      ascii[j] = (payload[i + j] >= 32 && payload[i + j] < 127) ? payload[i + j] : '.';
    }
    log_debug("    [%04zx] %-48s %s", i, hex, ascii);
  }

  // Send via ACDS transport using generic packet sender
  asciichat_error_t result =
      packet_send_via_transport(g_acds_transport, PACKET_TYPE_ACIP_WEBRTC_ICE, packet, total_len);

  SAFE_FREE(packet);

  if (result != ASCIICHAT_OK) {
    return SET_ERRNO(result, "Failed to send ICE via ACDS");
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Callback when WebRTC DataChannel is ready and wrapped in ACIP transport
 *
 * Called by the WebRTC peer_manager when a client's DataChannel opens.
 * Adds the client to the server's client manager and starts media threads.
 *
 * @param transport ACIP transport wrapping the WebRTC DataChannel (ownership transferred)
 * @param participant_id Remote participant UUID (16 bytes)
 * @param user_data User context pointer (server_context_t*)
 */
static void on_webrtc_transport_ready(acip_transport_t *transport, const uint8_t participant_id[16], void *user_data) {
  server_context_t *server_ctx = (server_context_t *)user_data;
  if (!transport || !participant_id || !server_ctx) {
    log_error("on_webrtc_transport_ready: Invalid parameters");
    if (transport) {
      acip_transport_destroy(transport);
    }
    return;
  }

  log_debug("WebRTC transport ready for participant %.8s...", (const char *)participant_id);

  // Convert participant_id to string for logging (ASCII-safe portion)
  char participant_str[33];
  for (int i = 0; i < 16; i++) {
    safe_snprintf(participant_str + (i * 2), 3, "%02x", participant_id[i]);
  }
  participant_str[32] = '\0';

  // Add client to server (calls add_webrtc_client internally)
  int client_id = add_webrtc_client(server_ctx, transport, participant_str);
  if (client_id < 0) {
    log_error("Failed to add WebRTC client for participant %s", participant_str);
    acip_transport_destroy(transport);
    return;
  }

  log_debug("Successfully added WebRTC client ID=%d for participant %s", client_id, participant_str);
}

/**
 * @brief Callback when WebRTC SDP received from ACDS signaling relay
 *
 * Called when a client sends SDP offer/answer via ACDS.
 * Forwards the SDP to the WebRTC peer_manager for processing.
 *
 * @param sdp SDP packet header (session_id, sender_id, recipient_id, sdp_type, sdp_len)
 * @param total_len Total packet length (header + SDP string)
 * @param ctx User context (unused, server uses globals)
 */
static void on_webrtc_sdp_server(const acip_webrtc_sdp_t *sdp, size_t total_len, void *ctx) {
  (void)ctx;

  if (!sdp || !g_webrtc_peer_manager) {
    log_error("on_webrtc_sdp_server: Invalid parameters or peer_manager not initialized");
    return;
  }

  // Validate packet length
  uint16_t sdp_len = NET_TO_HOST_U16(sdp->sdp_len);
  if (total_len < sizeof(acip_webrtc_sdp_t) + sdp_len) {
    log_error("on_webrtc_sdp_server: Invalid packet length (total=%zu, expected>=%zu)", total_len,
              sizeof(acip_webrtc_sdp_t) + sdp_len);
    return;
  }

  // Determine SDP type (offer=0, answer=1)
  const char *sdp_type = (sdp->sdp_type == 0) ? "offer" : "answer";

  log_debug("Received WebRTC SDP %s from participant %.8s... (len=%u)", sdp_type, (const char *)sdp->sender_id,
            sdp_len);

  // Forward to peer_manager (pass full packet structure)
  asciichat_error_t result = webrtc_peer_manager_handle_sdp(g_webrtc_peer_manager, sdp);

  if (result != ASCIICHAT_OK) {
    log_error("Failed to handle remote SDP from participant %.8s...: %s", (const char *)sdp->sender_id,
              asciichat_error_string(result));
  }
}

/**
 * @brief Callback when WebRTC ICE candidate received from ACDS signaling relay
 *
 * Called when a client sends ICE candidate via ACDS.
 * Forwards the candidate to the WebRTC peer_manager for processing.
 *
 * @param ice ICE packet header (session_id, sender_id, recipient_id, candidate_len)
 * @param total_len Total packet length (header + candidate + mid)
 * @param ctx User context (unused, server uses globals)
 */
static void on_webrtc_ice_server(const acip_webrtc_ice_t *ice, size_t total_len, void *ctx) {
  (void)ctx;

  if (!ice || !g_webrtc_peer_manager) {
    log_error("on_webrtc_ice_server: Invalid parameters or peer_manager not initialized");
    return;
  }

  // Validate packet length
  uint16_t payload_len = NET_TO_HOST_U16(ice->candidate_len);
  if (total_len < sizeof(acip_webrtc_ice_t) + payload_len) {
    log_error("on_webrtc_ice_server: Invalid packet length (total=%zu, expected>=%zu)", total_len,
              sizeof(acip_webrtc_ice_t) + payload_len);
    return;
  }

  log_debug("Received WebRTC ICE candidate from participant %.8s...", (const char *)ice->sender_id);

  // Forward to peer_manager (pass full packet structure)
  asciichat_error_t result = webrtc_peer_manager_handle_ice(g_webrtc_peer_manager, ice);

  if (result != ASCIICHAT_OK) {
    log_error("Failed to handle remote ICE candidate from participant %.8s...: %s", (const char *)ice->sender_id,
              asciichat_error_string(result));
  }
}

/**
 * @brief Advertise server via mDNS with given session string
 *
 * Called after session string is determined (either from ACDS or random generation).
 * Advertises the server on the LAN via mDNS for local discovery.
 *
 * @param session_string Session string to advertise
 * @param port Server port
 */
static void advertise_mdns_with_session(const char *session_string, uint16_t port) {
  if (!g_mdns_ctx) {
    log_debug("mDNS context not initialized, skipping advertisement");
    return;
  }

  // Build session name from hostname for mDNS service name
  char hostname[256] = {0};
  char session_name[256] = "ascii-chat-Server";
  if (gethostname(hostname, sizeof(hostname) - 1) == 0 && strlen(hostname) > 0) {
    safe_snprintf(session_name, sizeof(session_name), "%s", hostname);
  }

  // Prepare TXT records with session string and host public key
  char txt_session_string[512];
  char txt_host_pubkey[512];
  const char *txt_records[2];
  int txt_count = 0;

  // Add session string to TXT records (for client discovery)
  safe_snprintf(txt_session_string, sizeof(txt_session_string), "session_string=%s", session_string);
  txt_records[txt_count++] = txt_session_string;

  // Add host public key to TXT records (for cryptographic verification)
  // Convert server's Ed25519 public key to hex format
  if (g_server_encryption_enabled) {
    char hex_pubkey[65];
    pubkey_to_hex(g_server_private_key.public_key, hex_pubkey);
    safe_snprintf(txt_host_pubkey, sizeof(txt_host_pubkey), "host_pubkey=%s", hex_pubkey);
    txt_records[txt_count++] = txt_host_pubkey;
    log_debug("mDNS: Host pubkey=%s", hex_pubkey);
  } else {
    // If encryption is disabled, still advertise a zero pubkey for clients to detect
    safe_snprintf(txt_host_pubkey, sizeof(txt_host_pubkey), "host_pubkey=");
    for (int i = 0; i < 32; i++) {
      safe_snprintf(txt_host_pubkey + strlen(txt_host_pubkey), sizeof(txt_host_pubkey) - strlen(txt_host_pubkey), "00");
    }
    txt_records[txt_count++] = txt_host_pubkey;
    log_debug("mDNS: Encryption disabled, advertising zero pubkey");
  }

  asciichat_mdns_service_t service = {
      .name = session_name,
      .type = "_ascii-chat._tcp",
      .host = hostname,
      .port = port,
      .txt_records = txt_records,
      .txt_count = txt_count,
  };

  asciichat_error_t mdns_advertise_result = asciichat_mdns_advertise(g_mdns_ctx, &service);
  if (mdns_advertise_result != ASCIICHAT_OK) {
    LOG_ERRNO_IF_SET("Failed to advertise mDNS service");
    log_warn("mDNS advertising failed - LAN discovery disabled");
    asciichat_mdns_shutdown(g_mdns_ctx);
    g_mdns_ctx = NULL;
  } else {
    log_info("🌐 mDNS: Server advertised as '%s.local' on LAN", session_name);
    log_debug("mDNS: Service advertised as '%s.local' (name=%s, port=%d, session=%s, txt_count=%d)", service.type,
              service.name, service.port, session_string, service.txt_count);
  }
}

/**
 * @brief ACDS ping thread - sends periodic keepalive PING packets
 *
 * Sends PING packet every 10 seconds to keep ACDS connection alive.
 * Prevents 15-second receive timeout on idle connections.
 *
 * @param arg Unused (server uses globals)
 * @return NULL
 */
static void *acds_ping_thread(void *arg) {
  (void)arg;

  log_debug("ACDS keepalive ping thread started");

  while (!atomic_load(&g_server_should_exit)) {
    if (!g_acds_transport) {
      log_debug("ACDS transport destroyed, exiting ping thread");
      break;
    }

    // Send PING every 10 seconds to keep connection alive
    socket_t acds_socket = g_acds_transport->methods->get_socket(g_acds_transport);
    if (acds_socket != INVALID_SOCKET_VALUE) {
      asciichat_error_t ping_result = packet_send(acds_socket, PACKET_TYPE_PING, NULL, 0);
      if (ping_result == ASCIICHAT_OK) {
        log_debug("ACDS keepalive: Sent periodic PING");
      } else {
        log_warn("ACDS keepalive: Failed to send PING: %s", asciichat_error_string(ping_result));
      }
    }

    // Sleep for 10 seconds before next ping (well before 15s timeout)
    for (int i = 0; i < 100 && !atomic_load(&g_server_should_exit); i++) {
      platform_sleep_ms(100); // Check exit flag every 100ms
    }
  }

  log_debug("ACDS keepalive ping thread exiting");
  return NULL;
}

/**
 * @brief ACDS receive thread - processes WebRTC signaling packets
 *
 * Receives packets from ACDS transport and dispatches to WebRTC callbacks.
 * Runs until g_acds_transport is destroyed or connection closes.
 *
 * @param arg Unused (server uses globals)
 * @return NULL
 */
/**
 * @brief ACDS PING callback - respond with PONG to keep connection alive
 */
static void on_acds_ping(void *ctx) {
  (void)ctx;
  log_debug("ACDS keepalive: Received PING from ACDS, responding with PONG");
  if (g_acds_transport) {
    packet_send_via_transport(g_acds_transport, PACKET_TYPE_PONG, NULL, 0);
  }
}

/**
 * @brief ACDS PONG callback - log keepalive response
 */
static void on_acds_pong(void *ctx) {
  (void)ctx;
  log_debug("ACDS keepalive: Received PONG from ACDS server");
}

static void *acds_receive_thread(void *arg) {
  (void)arg;

  log_debug("ACDS receive thread started");

  // Configure callbacks for WebRTC signaling packets and keepalive
  acip_client_callbacks_t callbacks = {
      .on_ascii_frame = NULL,
      .on_audio = NULL,
      .on_webrtc_sdp = on_webrtc_sdp_server,
      .on_webrtc_ice = on_webrtc_ice_server,
      .on_session_joined = NULL,
      .on_ping = on_acds_ping,
      .on_pong = on_acds_pong,
      .app_ctx = NULL,
  };

  // Receive loop - just handle incoming packets
  // Keepalive is handled by separate ping thread
  while (!atomic_load(&g_server_should_exit)) {
    if (!g_acds_transport) {
      log_warn("ACDS transport is NULL, exiting receive thread");
      break;
    }

    asciichat_error_t result = acip_client_receive_and_dispatch(g_acds_transport, &callbacks);

    if (result != ASCIICHAT_OK) {
      // Check error context to see if connection actually closed
      asciichat_error_context_t err_ctx;
      bool has_context = HAS_ERRNO(&err_ctx);

      // Timeouts are normal when there are no packets - just continue waiting
      if (result == ERROR_NETWORK_TIMEOUT) {
        continue;
      }

      // ERROR_NETWORK could be:
      // 1. Receive timeout (non-fatal - continue waiting)
      // 2. EOF/connection closed (fatal - exit thread)
      // Check the error context message to distinguish
      if (result == ERROR_NETWORK) {
        if (has_context && strstr(err_ctx.context_message, "Failed to receive packet") != NULL) {
          // Generic receive failure (likely timeout) - continue waiting
          log_debug("ACDS receive timeout, continuing to wait for packets");
          continue;
        } else if (has_context && (strstr(err_ctx.context_message, "EOF") != NULL ||
                                   strstr(err_ctx.context_message, "closed") != NULL)) {
          // Connection actually closed
          log_warn("ACDS connection closed: %s", err_ctx.context_message);
          break;
        } else {
          // Unknown ERROR_NETWORK - log and exit
          log_warn("ACDS connection error: %s", has_context ? err_ctx.context_message : "unknown");
          break;
        }
      }

      // Other errors - exit thread
      log_error("ACDS receive error: %s, exiting receive thread", asciichat_error_string(result));
      break;
    }
  }

  if (atomic_load(&g_server_should_exit)) {
    log_debug("ACDS receive thread exiting (server shutdown)");
  } else {
    log_warn("ACDS receive thread exiting unexpectedly");
  }
  return NULL;
}

/* ============================================================================
 * Signal Handlers
 * ============================================================================
 */

/**
 * @brief Handler for SIGTERM (termination request) signals
 *
 * SIGTERM is the standard "please terminate gracefully" signal sent by process
 * managers, systemd, Docker, etc. Unlike SIGINT (user Ctrl+C), SIGTERM indicates
 * a system-initiated shutdown request that should be honored promptly.
 *
 * IMPLEMENTATION STRATEGY:
 * This handler must aggressively interrupt the accept loop, just like SIGINT,
 * to ensure responsive shutdown when triggered by automated systems like docker stop.
 * Process managers and Docker expect clean shutdown within a timeout window.
 *
 * SIGNAL SAFETY:
 * - Sets atomic flags (signal-safe)
 * - Closes listening sockets to interrupt accept() (signal-safe)
 * - Does NOT access complex data structures (avoids deadlocks)
 *
 * @param sigterm The signal number (unused, required by signal handler signature)
 */
static void server_handle_sigterm(int sigterm) {
  (void)(sigterm);
  atomic_store(&g_server_should_exit, true);

  // Log without file I/O (no mutex, avoids deadlocks in signal handlers)
  log_info_nofile("SIGTERM received - shutting down server...");

  // CRITICAL: Stop the TCP server accept loop immediately
  // Without this, the select() call with ACCEPT_TIMEOUT could delay shutdown
  atomic_store(&g_tcp_server.running, false);
  if (g_tcp_server.listen_socket != INVALID_SOCKET_VALUE) {
    socket_close(g_tcp_server.listen_socket);
  }
  if (g_tcp_server.listen_socket6 != INVALID_SOCKET_VALUE) {
    socket_close(g_tcp_server.listen_socket6);
  }
}

/**
 * @brief Handler for SIGUSR1 - triggers lock debugging output
 *
 * This signal handler allows external triggering of lock debugging output
 * by sending SIGUSR1 to the server process. This is useful for debugging
 * deadlocks without modifying the running server.
 *
 * @param sigusr1 The signal number (unused, required by signal handler signature)
 */
static void server_handle_sigusr1(int sigusr1) {
  (void)(sigusr1);

#ifndef NDEBUG
  // Trigger lock debugging output (signal-safe)
  lock_debug_trigger_print();
#endif
}

/* ============================================================================
 * Status Screen Update Callback (for tcp_server integration)
 * ============================================================================
 */

/**
 * @brief Periodic status screen update callback
 *
 * Called by tcp_server_run() on select() timeout to update the status display.
 * Updates server status including session string, bind addresses, connected clients, and uptime.
 * Rate-limited to update every 1-2 seconds.
 *
 * @param user_data Pointer to server context (server_context_t*)
 */
/**
 * @brief Status screen thread function
 *
 * Runs independently at target FPS (default 60 Hz), rendering the status
 * screen with server stats and recent logs. Decoupled from network accept loop.
 */
static void *status_screen_thread(void *arg) {
  (void)arg; // Unused

  uint32_t fps = GET_OPTION(fps);
  if (fps == 0) {
    fps = 60; // Default
  }
  uint64_t frame_interval_us = 1000000ULL / fps;

  log_debug("Status screen thread started (target %u FPS)", fps);

  while (!atomic_load(&g_server_should_exit)) {
    uint64_t frame_start = platform_get_monotonic_time_us();

    // Get the IPv4 and IPv6 addresses from TCP server config
    const char *ipv4_address = g_tcp_server.config.ipv4_address;
    const char *ipv6_address = g_tcp_server.config.ipv6_address;

    // Render status screen (server_status_update handles rate limiting internally)
    server_status_update(&g_tcp_server, g_session_string, ipv4_address, ipv6_address, GET_OPTION(port),
                         g_server_start_time, "Server", g_session_is_mdns_only, &g_last_status_update);

    // Sleep until next frame
    uint64_t frame_end = platform_get_monotonic_time_us();
    uint64_t frame_time = frame_end - frame_start;
    if (frame_time < frame_interval_us) {
      platform_sleep_us(frame_interval_us - frame_time);
    }
  }

  log_debug("Status screen thread exiting");
  return NULL;
}

/* ============================================================================
 * Client Handler Thread (for tcp_server integration)
 * ============================================================================
 */

/**
 * @brief Client handler thread function for tcp_server integration
 *
 * Called by tcp_server_run() for each accepted connection. This function:
 * 1. Extracts client connection info from tcp_client_context_t
 * 2. Performs connection rate limiting
 * 3. Calls add_client() to initialize client structure and spawn workers
 * 4. Blocks until client disconnects
 * 5. Calls remove_client() to cleanup
 *
 * @param arg Pointer to tcp_client_context_t allocated by tcp_server_run()
 * @return NULL (thread exit value)
 */
static void *ascii_chat_client_handler(void *arg) {
  tcp_client_context_t *ctx = (tcp_client_context_t *)arg;
  if (!ctx) {
    log_error("Client handler: NULL context");
    return NULL;
  }

  // Extract server context from user_data
  server_context_t *server_ctx = (server_context_t *)ctx->user_data;
  if (!server_ctx) {
    log_error("Client handler: NULL server context");
    socket_close(ctx->client_socket);
    SAFE_FREE(ctx);
    return NULL;
  }

  socket_t client_socket = ctx->client_socket;

  // Extract client IP and port using tcp_server helpers
  char client_ip[INET6_ADDRSTRLEN] = {0};
  if (!tcp_client_context_get_ip(ctx, client_ip, sizeof(client_ip))) {
    safe_snprintf(client_ip, sizeof(client_ip), "unknown");
  }

  int client_port = tcp_client_context_get_port(ctx);
  if (client_port < 0) {
    client_port = 0;
  }

  log_debug("Client handler started for %s:%d", client_ip, client_port);

  // Check connection rate limit (prevent DoS attacks)
  if (server_ctx->rate_limiter) {
    bool allowed = false;
    asciichat_error_t rate_check =
        rate_limiter_check(server_ctx->rate_limiter, client_ip, RATE_EVENT_CONNECTION, NULL, &allowed);
    if (rate_check != ASCIICHAT_OK || !allowed) {
      tcp_server_reject_client(client_socket, "Connection rate limit exceeded");
      SAFE_FREE(ctx);
      return NULL;
    }
    // Record successful connection attempt
    rate_limiter_record(server_ctx->rate_limiter, client_ip, RATE_EVENT_CONNECTION);
  }

  // Add client (initializes structures, spawns workers via tcp_server_spawn_thread)
  int client_id = add_client(server_ctx, client_socket, client_ip, client_port);
  if (client_id < 0) {
    if (HAS_ERRNO(&asciichat_errno_context)) {
      PRINT_ERRNO_CONTEXT(&asciichat_errno_context);
      CLEAR_ERRNO();
    }
    tcp_server_reject_client(client_socket, "Failed to add client");
    SAFE_FREE(ctx);
    return NULL;
  }

  log_debug("Client %d added successfully from %s:%d", client_id, client_ip, client_port);

  // Block until client disconnects (active flag is set by receive thread)
  client_info_t *client = find_client_by_id((uint32_t)client_id);
  if (!client) {
    log_error("CRITICAL: Client %d not found after successful add! (not in hash table?)", client_id);
  } else {
    log_debug("HANDLER: Client %d found, waiting for disconnect (active=%d)", client_id, atomic_load(&client->active));
    int wait_count = 0;
    while (atomic_load(&client->active) && !atomic_load(server_ctx->server_should_exit)) {
      wait_count++;
      if (wait_count % 10 == 0) {
        // Log every 1 second (10 * 100ms)
        log_debug("HANDLER: Client %d still active (waited %d seconds), active=%d", client_id, wait_count / 10,
                  atomic_load(&client->active));
      }
      platform_sleep_ms(100); // Check every 100ms
    }
    log_info("Client %d disconnected from %s:%d (waited %d seconds, active=%d, server_should_exit=%d)", client_id,
             client_ip, client_port, wait_count / 10, atomic_load(&client->active),
             atomic_load(server_ctx->server_should_exit));
  }

  // Cleanup (this will call tcp_server_stop_client_threads internally)
  if (remove_client(server_ctx, (uint32_t)client_id) != 0) {
    log_error("CRITICAL BUG: Failed to remove client %d from server (potential zombie client leak!)", client_id);
  }

  // Close socket and free context
  socket_close(client_socket);
  SAFE_FREE(ctx);

  log_debug("Client handler finished for %s:%d", client_ip, client_port);
  return NULL;
}

/* ============================================================================
 * Main Function
 * ============================================================================
 */

/**
 * @brief ascii-chat Server main entry point - orchestrates the entire server architecture
 *
 * This function serves as the conductor of the ascii-chat server's modular architecture.
 * It replaces the original monolithic server design with a clean initialization sequence
 * followed by a robust multi-client connection management loop.
 *
 * ARCHITECTURAL OVERVIEW:
 * =======================
 * The main function coordinates several major subsystems:
 * 1. Platform initialization (Windows/POSIX compatibility)
 * 2. Logging and configuration setup
 * 3. Network socket creation and binding
 * 4. Global resource initialization (audio mixer, buffer pools, etc.)
 * 5. Background thread management (statistics logging)
 * 6. Main connection accept loop with client lifecycle management
 * 7. Graceful shutdown with proper resource cleanup
 *
 * MODULAR COMPONENT INTEGRATION:
 * ==============================
 * This main function ties together the modular components:
 * - client.c: add_client(), remove_client() for client lifecycle
 * - protocol.c: Not directly called (used by client receive threads)
 * - stream.c: Not directly called (used by render threads)
 * - render.c: create_client_render_threads() called via client.c
 * - stats.c: stats_logger_thread_func() runs in background
 *
 * CONCURRENCY STRATEGY:
 * =====================
 * Main thread responsibilities:
 * - Accept new connections (blocking with timeout)
 * - Manage client lifecycle (add/remove)
 * - Handle disconnection cleanup
 * - Coordinate graceful shutdown
 *
 * Background thread responsibilities:
 * - Per-client receive: Handle incoming packets (client.c)
 * - Per-client send: Manage outgoing packets (client.c)
 * - Per-client video render: Generate ASCII frames (render.c)
 * - Per-client audio render: Mix audio streams (render.c)
 * - Stats logger: Monitor server performance (stats.c)
 *
 * CLEANUP GUARANTEES:
 * ===================
 * The shutdown sequence ensures:
 * 1. Signal handlers set g_server_should_exit atomically
 * 2. All worker threads check flag and exit gracefully
 * 3. Main thread waits for all threads to finish
 * 4. Resources cleaned up in reverse dependency order
 * 5. No memory leaks or hanging processes
 *
 * @param argc Command line argument count
 * @param argv Command line argument vector
 * @return Exit code: 0 for success, non-zero for failure
 */

/**
 * @brief Initialize crypto for server
 * @return 0 on success, -1 on error
 * @ingroup server_main
 */
static int init_server_crypto(void) {
  // Check if encryption is disabled
  if (GET_OPTION(no_encrypt)) {
    log_info("Encryption: DISABLED (--no-encrypt)");
    g_server_encryption_enabled = false;
    return 0;
  }

  // Load server identity keys (supports multiple --key flags for multi-key mode)
  size_t num_keys = GET_OPTION(num_identity_keys);

  if (num_keys > 0) {
    // Multi-key mode: load all identity keys from identity_keys[] array
    log_info("Loading %zu identity key(s) for multi-key support...", num_keys);

    for (size_t i = 0; i < num_keys && i < MAX_IDENTITY_KEYS; i++) {
      const char *key_path = GET_OPTION(identity_keys[i]);

      if (strlen(key_path) == 0) {
        continue; // Skip empty entries
      }

      // Validate SSH key file (skip validation for special prefixes)
      bool is_special_key = (strncmp(key_path, "gpg:", 4) == 0 || strncmp(key_path, "github:", 7) == 0 ||
                             strncmp(key_path, "gitlab:", 7) == 0);

      if (!is_special_key) {
        if (validate_ssh_key_file(key_path) != 0) {
          log_warn("Skipping invalid SSH key file: %s", key_path);
          continue;
        }
      }

      // Parse key (handles SSH files and gpg: prefix, rejects github:/gitlab:)
      log_debug("Loading identity key #%zu: %s", i + 1, key_path);
      if (parse_private_key(key_path, &g_server_identity_keys[g_num_server_identity_keys]) == ASCIICHAT_OK) {
        log_debug("Successfully loaded identity key #%zu: %s", i + 1, key_path);

        // Display key fingerprint for verification
        char hex_pubkey[65];
        pubkey_to_hex(g_server_identity_keys[g_num_server_identity_keys].public_key, hex_pubkey);
        log_debug("  Key fingerprint: %s", hex_pubkey);

        g_num_server_identity_keys++;
      } else {
        log_warn("Failed to parse identity key #%zu: %s (skipping)", i + 1, key_path);
      }
    }

    if (g_num_server_identity_keys == 0) {
      log_error("No valid identity keys loaded despite %zu --key flag(s)", num_keys);
      SET_ERRNO(ERROR_CRYPTO_KEY, "No valid identity keys loaded");
      return -1;
    }

    // Copy first key to g_server_private_key for backward compatibility
    memcpy(&g_server_private_key, &g_server_identity_keys[0], sizeof(private_key_t));
    log_info("Loaded %zu identity key(s) total", g_num_server_identity_keys);

  } else if (strlen(GET_OPTION(encrypt_key)) > 0) {
    // Single-key mode (backward compatibility): load from encrypt_key field
    const char *key_path = GET_OPTION(encrypt_key);

    // Validate SSH key file (skip validation for special prefixes)
    bool is_special_key = (strncmp(key_path, "gpg:", 4) == 0 || strncmp(key_path, "github:", 7) == 0 ||
                           strncmp(key_path, "gitlab:", 7) == 0);

    if (!is_special_key) {
      if (validate_ssh_key_file(key_path) != 0) {
        SET_ERRNO(ERROR_CRYPTO_KEY, "Invalid SSH key file: %s", key_path);
        return -1;
      }
    }

    // Parse key
    log_info("Loading key for authentication: %s", key_path);
    if (parse_private_key(key_path, &g_server_private_key) == ASCIICHAT_OK) {
      log_info("Successfully loaded server key: %s", key_path);

      // Also store in identity_keys array for consistency
      memcpy(&g_server_identity_keys[0], &g_server_private_key, sizeof(private_key_t));
      g_num_server_identity_keys = 1;
    } else {
      log_error("Failed to parse key: %s\n"
                "This may be due to:\n"
                "  - Wrong password for encrypted key\n"
                "  - Unsupported key type (only Ed25519 is currently supported)\n"
                "  - Corrupted key file\n"
                "\n"
                "Note: RSA and ECDSA keys are not yet supported\n"
                "To generate an Ed25519 key: ssh-keygen -t ed25519\n",
                key_path);
      SET_ERRNO(ERROR_CRYPTO_KEY, "Key parsing failed: %s", key_path);
      return -1;
    }
  } else if (strlen(GET_OPTION(password)) == 0) {
    // No identity key provided - server will run in simple mode
    // The server will still generate ephemeral keys for encryption, but no identity key
    g_server_private_key.type = KEY_TYPE_UNKNOWN;
    g_num_server_identity_keys = 0;
    log_info("Server running without identity key (simple mode)");
  }

  // Load client whitelist if provided
  if (strlen(GET_OPTION(client_keys)) > 0) {
    if (parse_public_keys(GET_OPTION(client_keys), g_client_whitelist, &g_num_whitelisted_clients, MAX_CLIENTS) != 0) {
      SET_ERRNO(ERROR_CRYPTO_KEY, "Client key parsing failed: %s", GET_OPTION(client_keys));
      return -1;
    }
    log_debug("Loaded %zu whitelisted clients", g_num_whitelisted_clients);
    log_info("Server will only accept %zu whitelisted clients", g_num_whitelisted_clients);
  }

  g_server_encryption_enabled = true;
  return 0;
}

int server_main(void) {
  // Common initialization (options, logging, lock debugging) now happens in main.c before dispatch
  // This function focuses on server-specific initialization

  // Register shutdown check callback for library code
  shutdown_register_callback(check_shutdown);

  // Initialize status screen log buffer if enabled (terminal output already disabled in main.c)
  if (GET_OPTION(status_screen)) {
    server_status_log_init();
  }

  // Initialize crypto after logging is ready
  log_debug("Initializing crypto...");
  if (init_server_crypto() != 0) {
    // Print detailed error context if available
    LOG_ERRNO_IF_SET("Crypto initialization failed");
    FATAL(ERROR_CRYPTO, "Crypto initialization failed");
  }
  log_debug("Crypto initialized successfully");

  // Handle keepawake: check for mutual exclusivity and apply mode default
  // Server default: keepawake DISABLED (use --keepawake to enable)
  if (GET_OPTION(enable_keepawake) && GET_OPTION(disable_keepawake)) {
    FATAL(ERROR_INVALID_PARAM, "--keepawake and --no-keepawake are mutually exclusive");
  }
  if (GET_OPTION(enable_keepawake)) {
    (void)platform_enable_keepawake();
  }

  log_info("ascii-chat server starting...");

  // log_info("SERVER: Options initialized, using log file: %s", log_filename);
  int port = GET_OPTION(port);
  if (port < 1 || port > 65535) {
    log_error("Invalid port configuration: %d", port);
    FATAL(ERROR_CONFIG, "Invalid port configuration: %d", port);
  }

  ascii_simd_init();
  precalc_rgb_palettes(weight_red, weight_green, weight_blue);

  // Simple signal handling (temporarily disable complex threading signal handling)
  log_debug("Setting up simple signal handlers...");

  // Handle Ctrl+C for cleanup
  platform_signal(SIGINT, server_handle_sigint);
  // Handle termination signal (SIGTERM is defined with limited support on Windows)
  platform_signal(SIGTERM, server_handle_sigterm);
  // Handle lock debugging trigger signal
#ifndef _WIN32
  platform_signal(SIGUSR1, server_handle_sigusr1);
#else
  UNUSED(server_handle_sigusr1);
#endif
#ifndef _WIN32
  // SIGPIPE not supported on Windows
  platform_signal(SIGPIPE, SIG_IGN);
#endif

#ifndef NDEBUG
  // Start the lock debug thread (system already initialized earlier)
  if (lock_debug_start_thread() != 0) {
    FATAL(ERROR_THREAD, "Failed to start lock debug thread");
  }
  // Initialize statistics system
  if (stats_init() != 0) {
    FATAL(ERROR_THREAD, "Statistics system initialization failed");
  }
#endif

  // Create background worker thread pool for server operations
  g_server_worker_pool = thread_pool_create("server_workers");
  if (!g_server_worker_pool) {
    LOG_ERRNO_IF_SET("Failed to create server worker thread pool");
    FATAL(ERROR_MEMORY, "Failed to create server worker thread pool");
  }

  // Spawn statistics logging thread in worker pool
  if (thread_pool_spawn(g_server_worker_pool, stats_logger_thread, NULL, 0, "stats_logger") != ASCIICHAT_OK) {
    LOG_ERRNO_IF_SET("Statistics logger thread creation failed");
  } else {
    log_debug("Statistics logger thread started");
  }

  // Network setup - Use tcp_server abstraction for dual-stack IPv4/IPv6 binding
  log_debug("Config check: GET_OPTION(address)='%s', GET_OPTION(address6)='%s'", GET_OPTION(address),
            GET_OPTION(address6));

  bool ipv4_has_value = (strlen(GET_OPTION(address)) > 0);
  bool ipv6_has_value = (strlen(GET_OPTION(address6)) > 0);
  bool ipv4_is_default = (strcmp(GET_OPTION(address), "127.0.0.1") == 0);
  bool ipv6_is_default = (strcmp(GET_OPTION(address6), "::1") == 0);

  log_debug("Binding decision: ipv4_has_value=%d, ipv6_has_value=%d, ipv4_is_default=%d, ipv6_is_default=%d",
            ipv4_has_value, ipv6_has_value, ipv4_is_default, ipv6_is_default);

  // Determine bind configuration
  bool bind_ipv4 = false;
  bool bind_ipv6 = false;
  const char *ipv4_address = NULL;
  const char *ipv6_address = NULL;

  if (ipv4_has_value && ipv6_has_value && ipv4_is_default && ipv6_is_default) {
    // Both are defaults: dual-stack with default localhost addresses
    bind_ipv4 = true;
    bind_ipv6 = true;
    ipv4_address = "127.0.0.1";
    ipv6_address = "::1";
    log_info("Default dual-stack: binding to 127.0.0.1 (IPv4) and ::1 (IPv6)");
  } else if (ipv4_has_value && !ipv4_is_default && (!ipv6_has_value || ipv6_is_default)) {
    // IPv4 explicitly set, IPv6 is default or empty: bind only IPv4
    bind_ipv4 = true;
    bind_ipv6 = false;
    ipv4_address = GET_OPTION(address);
    log_info("Binding only to IPv4 address: %s", ipv4_address);
  } else if (ipv6_has_value && !ipv6_is_default && (ipv4_is_default || !ipv4_has_value)) {
    // IPv6 explicitly set, IPv4 is default or empty: bind only IPv6
    bind_ipv4 = false;
    bind_ipv6 = true;
    ipv6_address = GET_OPTION(address6);
    log_info("Binding only to IPv6 address: %s", ipv6_address);
  } else {
    // Both explicitly set or one explicit + one default: dual-stack
    bind_ipv4 = true;
    bind_ipv6 = true;
    ipv4_address = ipv4_has_value ? GET_OPTION(address) : "127.0.0.1";
    ipv6_address = ipv6_has_value ? GET_OPTION(address6) : "::1";
    log_info("Dual-stack binding: IPv4=%s, IPv6=%s", ipv4_address, ipv6_address);
  }

  // Create server context - encapsulates all server state for passing to client handlers
  // This reduces global state and improves modularity by using tcp_server.user_data
  server_context_t server_ctx = {
      .tcp_server = &g_tcp_server,
      .rate_limiter = g_rate_limiter,
      .client_manager = &g_client_manager,
      .client_manager_rwlock = &g_client_manager_rwlock,
      .server_should_exit = &g_server_should_exit,
      .audio_mixer = g_audio_mixer,
      .stats = &g_stats,
      .stats_mutex = &g_stats_mutex,
      .encryption_enabled = g_server_encryption_enabled,
      .server_private_key = &g_server_private_key,
      .client_whitelist = g_client_whitelist,
      .num_whitelisted_clients = g_num_whitelisted_clients,
      .session_host = NULL, // Will be created after TCP server init
  };

  // Configure TCP server
  tcp_server_config_t tcp_config = {
      .port = port,
      .ipv4_address = ipv4_address,
      .ipv6_address = ipv6_address,
      .bind_ipv4 = bind_ipv4,
      .bind_ipv6 = bind_ipv6,
      .accept_timeout_sec = ACCEPT_TIMEOUT,
      .client_handler = ascii_chat_client_handler,
      .user_data = &server_ctx, // Pass server context to client handlers
      .status_update_fn = NULL, // Status screen runs in its own thread
      .status_update_data = NULL,
  };

  // Initialize TCP server (creates and binds sockets)
  memset(&g_tcp_server, 0, sizeof(g_tcp_server));
  asciichat_error_t tcp_init_result = tcp_server_init(&g_tcp_server, &tcp_config);
  if (tcp_init_result != ASCIICHAT_OK) {
    FATAL(ERROR_NETWORK, "Failed to initialize TCP server");
  }

  // =========================================================================
  // UPnP Port Mapping (Quick Win for Direct TCP)
  // =========================================================================
  // Track UPnP success for ACDS session type decision
  // If UPnP fails, we need to create a WebRTC session to enable client connectivity
  bool upnp_succeeded = false;

  // Try to open port via UPnP so direct TCP works for ~70% of home users.
  // If this fails, clients fall back to WebRTC automatically - not fatal.
  //
  // Strategy:
  //   1. UPnP (works on ~90% of home routers)
  //   2. NAT-PMP fallback (Apple routers)
  //   3. If both fail: use ACDS + WebRTC (reliable, but slightly higher latency)
  if (GET_OPTION(enable_upnp)) {
    asciichat_error_t upnp_result = nat_upnp_open(port, "ascii-chat Server", &g_upnp_ctx);

    if (upnp_result == ASCIICHAT_OK && g_upnp_ctx) {
      char public_addr[22];
      if (nat_upnp_get_address(g_upnp_ctx, public_addr, sizeof(public_addr)) == ASCIICHAT_OK) {
        printf("🌐 Public endpoint: %s (direct TCP)\\n", public_addr);
        log_info("UPnP: Port mapping successful, public endpoint: %s", public_addr);
        upnp_succeeded = true;
      }
    } else {
      log_info("UPnP: Port mapping unavailable or failed - will use WebRTC fallback");
      printf("📡 Clients behind strict NATs will use WebRTC fallback\\n");
    }
  } else {
    log_debug("UPnP: Disabled (use --upnp to enable automatic port mapping)");
  }

  // Initialize synchronization primitives
  if (rwlock_init(&g_client_manager_rwlock) != 0) {
    FATAL(ERROR_THREAD, "Failed to initialize client manager rwlock");
  }

  // Lock debug system already initialized earlier in main()

  // Check if SIGINT/SIGTERM was received during initialization
  // If so, skip the accept loop entirely and go to cleanup
  if (atomic_load(&g_server_should_exit)) {
    log_info("Shutdown signal received during initialization, skipping server startup");
    goto cleanup;
  }

  // Lock debug thread already started earlier in main()

  // NOTE: g_client_manager is already zero-initialized in client.c with = {0}
  // We only need to initialize the mutex
  mutex_init(&g_client_manager.mutex);

  // Initialize uthash head pointer for O(1) lookup (uthash requires NULL initialization)
  g_client_manager.clients_by_id = NULL;

  // Initialize connection rate limiter (prevents DoS attacks)
  if (!atomic_load(&g_server_should_exit)) {
    log_debug("Initializing connection rate limiter...");
    g_rate_limiter = rate_limiter_create_memory();
    if (!g_rate_limiter) {
      LOG_ERRNO_IF_SET("Failed to initialize rate limiter");
      if (!atomic_load(&g_server_should_exit)) {
        FATAL(ERROR_MEMORY, "Failed to create connection rate limiter");
      }
    } else {
      log_info("Connection rate limiter initialized (50 connections/min per IP)");
    }
  }

  // Initialize audio mixer (always enabled on server)
  if (!atomic_load(&g_server_should_exit)) {
    log_debug("Initializing audio mixer for per-client audio rendering...");
    g_audio_mixer = mixer_create(MAX_CLIENTS, AUDIO_SAMPLE_RATE);
    if (!g_audio_mixer) {
      LOG_ERRNO_IF_SET("Failed to initialize audio mixer");
      if (!atomic_load(&g_server_should_exit)) {
        FATAL(ERROR_AUDIO, "Failed to initialize audio mixer");
      }
    } else {
      if (!atomic_load(&g_server_should_exit)) {
        log_debug("Audio mixer initialized successfully for per-client audio rendering");
      }
    }
  }

  // Initialize mDNS context for LAN service discovery (optional)
  // mDNS allows clients on the LAN to discover this server without knowing its IP
  // Can be disabled with --no-mdns-advertise
  // Note: Actual advertisement is deferred until after ACDS session creation (if --acds is enabled)
  if (!atomic_load(&g_server_should_exit) && !GET_OPTION(no_mdns_advertise)) {
    log_debug("Initializing mDNS for LAN service discovery...");
    g_mdns_ctx = asciichat_mdns_init();
    if (!g_mdns_ctx) {
      LOG_ERRNO_IF_SET("Failed to initialize mDNS (non-fatal, LAN discovery disabled)");
      log_warn("mDNS disabled - LAN service discovery will not be available");
      g_mdns_ctx = NULL;
    } else {
      log_debug("mDNS context initialized, advertisement deferred until session string is ready");
    }
  } else if (GET_OPTION(no_mdns_advertise)) {
    log_info("mDNS service advertisement disabled via --no-mdns-advertise");
  }

  // ========================================================================
  // Session Host Creation (for discovery mode support)
  // ========================================================================
  // Create session_host to track clients in a transport-agnostic way.
  // This enables future discovery mode where participants can become hosts.
  if (!atomic_load(&g_server_should_exit)) {
    session_host_config_t host_config = {
        .port = port,
        .ipv4_address = ipv4_address,
        .ipv6_address = ipv6_address,
        .max_clients = GET_OPTION(max_clients),
        .encryption_enabled = g_server_encryption_enabled,
        .key_path = GET_OPTION(encrypt_key),
        .password = GET_OPTION(password),
        .callbacks = {0}, // No callbacks for now
        .user_data = NULL,
    };

    server_ctx.session_host = session_host_create(&host_config);
    if (!server_ctx.session_host) {
      // Non-fatal: session_host is optional, server can work without it
      log_warn("Failed to create session_host (discovery mode support disabled)");
    } else {
      log_debug("Session host created for discovery mode support");
    }
  }

  // ========================================================================
  // MAIN CONNECTION LOOP - Delegated to tcp_server
  // ========================================================================
  //
  // The tcp_server module handles:
  // 1. Dual-stack IPv4/IPv6 accept loop with select() timeout
  // 2. Spawning client_handler threads for each connection
  // 3. Responsive shutdown when g_tcp_server.running is set to false
  //
  // Client lifecycle is managed by ascii_chat_client_handler() which:
  // - Performs rate limiting
  // - Calls add_client() to initialize structures and spawn workers
  // - Blocks until client disconnects
  // - Calls remove_client() to cleanup and stop worker threads

  // ACDS Session Creation: Register this server with discovery service
  // This also determines the session string for mDNS (if --acds is enabled)
  char session_string[64] = {0};
  bool session_is_mdns_only = false;

  // ACDS Registration (conditional on --discovery flag)
  if (GET_OPTION(discovery)) {
    // Security Requirement Check (Issue #239):
    // Server IP must be protected by password, identity verification, or explicit opt-in

    // Auto-detection: Check if password or identity verification is configured
    const char *password = GET_OPTION(password);
    bool has_password = password && strlen(password) > 0;
    const char *encrypt_key = GET_OPTION(encrypt_key);
    bool has_identity = encrypt_key && strlen(encrypt_key) > 0;
    bool explicit_expose = GET_OPTION(discovery_expose_ip) != 0;

    // Validate security configuration BEFORE attempting ACDS connection
    bool acds_expose_ip_flag = false;

    if (has_password || has_identity) {
      // Auto-enable privacy: IP revealed only after verification
      acds_expose_ip_flag = false;
      log_plain("🔒 ACDS privacy enabled: IP disclosed only after %s verification",
                has_password ? "password" : "identity");
    } else if (explicit_expose) {
      // Explicit opt-in to public IP disclosure
      // Only prompt if running interactively (stdin is a TTY)
      // When stdin is not a TTY (automated/scripted), treat explicit flag as confirmation
      bool is_interactive = platform_isatty(STDIN_FILENO);

      if (is_interactive) {
        log_plain_stderr("");
        log_plain_stderr("⚠️  WARNING: You are about to allow PUBLIC IP disclosure!");
        log_plain_stderr("⚠️  Anyone with the session string will be able to see your IP address.");
        log_plain_stderr("⚠️  This is NOT RECOMMENDED unless you understand the privacy implications.");
        log_plain_stderr("");

        if (!platform_prompt_yes_no("Do you want to proceed with public IP disclosure", false)) {
          log_plain_stderr("");
          log_plain_stderr("❌ IP disclosure not confirmed. Server will run WITHOUT discovery service.");
          goto skip_acds_session;
        }
      }

      // User confirmed (or running non-interactively with explicit flag) - proceed with public IP disclosure
      acds_expose_ip_flag = true;
      log_plain_stderr("");
      log_plain_stderr("⚠️  Public IP disclosure CONFIRMED");
      log_plain_stderr("⚠️  Your IP address will be visible to anyone with the session string");
    } else {
      // Security violation: No password, no identity, no explicit opt-in
      log_plain_stderr("❌ Cannot create ACDS session: No security configured!");
      log_plain_stderr("   You must either:");
      log_plain_stderr("   1. Set a password: --password \"your-secret\"");
      log_plain_stderr("   2. Use identity key: --key ~/.ssh/id_ed25519");
      log_plain_stderr("   3. Explicitly allow public IP: --acds-expose-ip (NOT RECOMMENDED)");
      log_plain_stderr("");
      log_plain_stderr("Server will run WITHOUT discovery service.");
      goto skip_acds_session;
    }

    // Security is configured, proceed with ACDS connection
    const char *acds_server = GET_OPTION(discovery_server);
    uint16_t acds_port = (uint16_t)GET_OPTION(discovery_port);

    log_info("Attempting to create session on ACDS server at %s:%d...", acds_server, acds_port);

    acds_client_config_t acds_config;
    acds_client_config_init_defaults(&acds_config);
    SAFE_STRNCPY(acds_config.server_address, acds_server, sizeof(acds_config.server_address));
    acds_config.server_port = acds_port;
    acds_config.timeout_ms = 5000;

    // Allocate ACDS client on heap for server lifecycle
    g_acds_client = SAFE_MALLOC(sizeof(acds_client_t), acds_client_t *);
    if (!g_acds_client) {
      log_error("Failed to allocate ACDS client");
      goto skip_acds_session;
    }

    asciichat_error_t acds_connect_result = acds_client_connect(g_acds_client, &acds_config);
    if (acds_connect_result != ASCIICHAT_OK) {
      log_error("Failed to connect to ACDS server at %s:%d: %s", acds_server, acds_port,
                asciichat_error_string(acds_connect_result));
      goto skip_acds_session;
    }
    if (acds_connect_result == ASCIICHAT_OK) {
      // Prepare session creation parameters
      acds_session_create_params_t create_params;
      memset(&create_params, 0, sizeof(create_params));

      // Use server's Ed25519 identity public key if available
      if (g_server_encryption_enabled && has_identity) {
        memcpy(create_params.identity_pubkey, g_server_private_key.public_key, 32);
        log_debug("Using server identity key for ACDS session");
      } else {
        // No identity key available - use zero key
        // ACDS will accept this if identity verification is not required
        memset(create_params.identity_pubkey, 0, 32);
        log_debug("No server identity key - using zero key for ACDS session");
      }

      create_params.capabilities = 0x03; // Video + Audio
      create_params.max_participants = GET_OPTION(max_clients);
      log_debug("ACDS: max_clients option value = %d", GET_OPTION(max_clients));

      // Set password if configured
      create_params.has_password = has_password;
      if (has_password) {
        // TODO: Hash password with Argon2id
        SAFE_STRNCPY(create_params.password, password, sizeof(create_params.password));
      }

      // Set IP disclosure policy (determined above)
      create_params.acds_expose_ip = acds_expose_ip_flag;
      log_info("DEBUG: Server setting acds_expose_ip=%d (explicit_expose=%d, has_password=%d, has_identity=%d)",
               create_params.acds_expose_ip, explicit_expose, has_password, has_identity);

      // Set session type (Direct TCP or WebRTC)
      // Auto-detect: Use WebRTC if UPnP failed OR if explicitly requested via --webrtc
      // Exception: If bind address is 0.0.0.0, server is on public IP - use Direct TCP
      const char *bind_addr = GET_OPTION(address);
      bool bind_all_interfaces = (strcmp(bind_addr, "0.0.0.0") == 0);

      // Determine session type: prefer WebRTC by default (unless explicitly disabled)
      // Priority: explicit --webrtc flag > connection type detection > UPnP > default
      if (GET_OPTION(webrtc)) {
        // Explicit WebRTC request
        create_params.session_type = SESSION_TYPE_WEBRTC;
        log_info("ACDS session type: WebRTC (explicitly requested via --webrtc)");
      } else if (bind_all_interfaces) {
        // Bind to 0.0.0.0: use WebRTC as default (better NAT compatibility)
        create_params.session_type = SESSION_TYPE_WEBRTC;
        log_info("ACDS session type: WebRTC (default for 0.0.0.0 binding, provides NAT-agnostic connections)");
      } else if (upnp_succeeded) {
        // UPnP port mapping worked - can use direct TCP
        create_params.session_type = SESSION_TYPE_DIRECT_TCP;
        log_info("ACDS session type: Direct TCP (UPnP succeeded, server is publicly accessible)");
      } else {
        // UPnP failed and not on public IP - use WebRTC for NAT traversal
        create_params.session_type = SESSION_TYPE_WEBRTC;
        log_info("ACDS session type: WebRTC (UPnP failed, server behind NAT)");
      }

      // Server connection information (where clients should connect)
      // If bind address is 0.0.0.0, leave server_address empty for ACDS to auto-detect public IP
      if (bind_all_interfaces) {
        create_params.server_address[0] = '\0'; // Empty - ACDS will use connection source IP
        log_debug("Bind address is 0.0.0.0, ACDS will auto-detect public IP from connection");
      } else {
        SAFE_STRNCPY(create_params.server_address, bind_addr, sizeof(create_params.server_address));
      }
      create_params.server_port = port;

      // DEBUG: Log what we're sending to ACDS
      log_info("DEBUG: Before SESSION_CREATE - expose_ip_publicly=%d, server_address='%s' port=%u, session_type=%u",
               create_params.acds_expose_ip, create_params.server_address, create_params.server_port,
               create_params.session_type);

      // Create session
      acds_session_create_result_t create_result;
      asciichat_error_t create_err = acds_session_create(g_acds_client, &create_params, &create_result);

      if (create_err == ASCIICHAT_OK) {
        SAFE_STRNCPY(session_string, create_result.session_string, sizeof(session_string));
        SAFE_STRNCPY(g_session_string, create_result.session_string, sizeof(g_session_string));
        session_is_mdns_only = false; // Session is now registered with ACDS (globally discoverable)
        log_info("Session created: %s", session_string);

        // Server must join its own session so ACDS can route signaling messages
        log_debug("Server joining session as first participant for WebRTC signaling...");
        acds_session_join_params_t join_params = {0};
        join_params.session_string = session_string;

        // Use same identity key as session creation
        memcpy(join_params.identity_pubkey, create_params.identity_pubkey, 32);

        // Include password if session is password-protected
        if (has_password) {
          join_params.has_password = true;
          SAFE_STRNCPY(join_params.password, password, sizeof(join_params.password));
        }

        acds_session_join_result_t join_result = {0};
        asciichat_error_t join_err = acds_session_join(g_acds_client, &join_params, &join_result);
        if (join_err != ASCIICHAT_OK || !join_result.success) {
          log_error("Failed to join own session: %s (error: %s)", asciichat_error_string(join_err),
                    join_result.error_message[0] ? join_result.error_message : "unknown");
          // Continue anyway - this is not fatal for Direct TCP sessions
        } else {
          log_debug("Server joined session successfully (participant_id: %02x%02x...)", join_result.participant_id[0],
                    join_result.participant_id[1]);
          // Store participant ID for WebRTC signaling (needed to identify server in SDP/ICE messages)
          memcpy(g_server_participant_id, join_result.participant_id, 16);
          log_debug("Stored server participant_id for signaling: %02x%02x...", g_server_participant_id[0],
                    g_server_participant_id[1]);
          memcpy(create_result.session_id, join_result.session_id, 16);
        }

        // Keep ACDS connection alive for WebRTC signaling relay
        log_debug("Server staying connected to ACDS for signaling relay");

        // Create ACDS transport wrapper for sending signaling packets
        g_acds_transport = acip_tcp_transport_create(g_acds_client->socket, NULL);
        if (!g_acds_transport) {
          log_error("Failed to create ACDS transport wrapper");
        } else {
          log_debug("ACDS transport wrapper created for signaling");

          // Start ACDS ping thread to keep connection alive (for ALL session types)
          int ping_thread_result = asciichat_thread_create(&g_acds_ping_thread, acds_ping_thread, NULL);
          if (ping_thread_result != 0) {
            log_error("Failed to create ACDS ping thread: %d", ping_thread_result);
          } else {
            log_debug("ACDS ping thread started to keep connection alive");
            g_acds_ping_thread_started = true;
          }
        }

        // Initialize WebRTC peer_manager if session type is WebRTC
        if (create_params.session_type == SESSION_TYPE_WEBRTC) {
          log_debug("Initializing WebRTC library and peer manager for session (role=CREATOR)...");

          // Initialize WebRTC library (libdatachannel)
          asciichat_error_t webrtc_init_result = webrtc_init();
          if (webrtc_init_result != ASCIICHAT_OK) {
            log_error("Failed to initialize WebRTC library: %s", asciichat_error_string(webrtc_init_result));
            g_webrtc_peer_manager = NULL;
          } else {
            log_debug("WebRTC library initialized successfully");

            // Configure STUN servers for ICE gathering (static to persist for peer_manager lifetime)
            static stun_server_t stun_servers[4] = {0};
            static unsigned int g_stun_init_refcount = 0;
            static static_mutex_t g_stun_init_mutex = STATIC_MUTEX_INIT;
            static int stun_count = 0; // Store actual count separately

            static_mutex_lock(&g_stun_init_mutex);
            if (g_stun_init_refcount == 0) {
              log_debug("Parsing STUN servers from options: '%s'", GET_OPTION(stun_servers));
              int count =
                  stun_servers_parse(GET_OPTION(stun_servers), OPT_ENDPOINT_STUN_SERVERS_DEFAULT, stun_servers, 4);
              if (count > 0) {
                stun_count = count;
                log_debug("Parsed %d STUN servers", count);
                for (int i = 0; i < count; i++) {
                  log_debug("  STUN[%d]: '%s' (len=%d)", i, stun_servers[i].host, stun_servers[i].host_len);
                }
              } else {
                log_warn("Failed to parse STUN servers, using defaults");
                stun_count = stun_servers_parse(OPT_ENDPOINT_STUN_SERVERS_DEFAULT, OPT_ENDPOINT_STUN_SERVERS_DEFAULT,
                                                stun_servers, 4);
                log_debug("Using default STUN servers, count=%d", stun_count);
                for (int i = 0; i < stun_count; i++) {
                  log_debug("  STUN[%d]: '%s' (len=%d)", i, stun_servers[i].host, stun_servers[i].host_len);
                }
              }
              g_stun_init_refcount = 1;
            }
            static_mutex_unlock(&g_stun_init_mutex);

            // Configure peer_manager
            webrtc_peer_manager_config_t pm_config = {
                .role = WEBRTC_ROLE_CREATOR, // Server accepts offers, generates answers
                .stun_servers = stun_servers,
                .stun_count = stun_count,
                .turn_servers = NULL, // No TURN for server (clients should have public IP or use TURN)
                .turn_count = 0,
                .on_transport_ready = on_webrtc_transport_ready,
                .user_data = &server_ctx,
                .crypto_ctx = NULL // WebRTC handles crypto internally
            };

            // Configure signaling callbacks for relaying SDP/ICE via ACDS
            webrtc_signaling_callbacks_t signaling_callbacks = {
                .send_sdp = server_send_sdp, .send_ice = server_send_ice, .user_data = NULL};

            // Create peer_manager
            asciichat_error_t pm_result =
                webrtc_peer_manager_create(&pm_config, &signaling_callbacks, &g_webrtc_peer_manager);
            if (pm_result != ASCIICHAT_OK) {
              log_error("Failed to create WebRTC peer_manager: %s", asciichat_error_string(pm_result));
              g_webrtc_peer_manager = NULL;
            } else {
              log_debug("WebRTC peer_manager initialized successfully");

              // Start ACDS receive thread for WebRTC signaling relay
              int thread_result = asciichat_thread_create(&g_acds_receive_thread, acds_receive_thread, NULL);
              if (thread_result != 0) {
                log_error("Failed to create ACDS receive thread: %d", thread_result);
                // Cleanup peer_manager since signaling won't work
                webrtc_peer_manager_destroy(g_webrtc_peer_manager);
                g_webrtc_peer_manager = NULL;
              } else {
                log_debug("ACDS receive thread started for WebRTC signaling relay");
                g_acds_receive_thread_started = true;
              }
            }
          } // Close else block from webrtc_init() success
        } else {
          log_debug("Session type is DIRECT_TCP, skipping WebRTC peer_manager initialization");
        }

        // Advertise mDNS with ACDS session string
        // This ensures both mDNS and ACDS discovery return the same session string
        advertise_mdns_with_session(session_string, (uint16_t)port);
      } else {
        log_warn("Failed to create session on ACDS server (server will run without discovery)");
        // Clean up failed ACDS client
        if (g_acds_client) {
          acds_client_disconnect(g_acds_client);
          SAFE_FREE(g_acds_client);
          g_acds_client = NULL;
        }
      }
    } else {
      log_warn("Could not connect to ACDS server at %s:%d (server will run without discovery)", acds_server, acds_port);
    }
  } else {
    log_info("ACDS registration disabled (use --acds to enable)");
  }

skip_acds_session:
  // Fallback: If no session string was set by ACDS (either disabled or failed),
  // generate a random session string for mDNS discovery only
  if (session_string[0] == '\0' && g_mdns_ctx) {
    log_debug("No ACDS session string available, generating random session for mDNS");

    // Use the proper session string generation from discovery module
    // This generates adjective-noun-noun format using the full wordlists
    if (acds_string_generate(session_string, sizeof(session_string)) != ASCIICHAT_OK) {
      log_error("Failed to generate session string for mDNS");
      return 1;
    }

    log_debug("Generated random session string for mDNS: '%s'", session_string);

    // Mark that this session is mDNS-only (not globally discoverable via ACDS)
    session_is_mdns_only = true;

    // Advertise mDNS with random session string
    advertise_mdns_with_session(session_string, (uint16_t)port);
  }

  // ====================================================================
  // Display session string prominently as the FINAL startup message
  // This ensures users see the connection info clearly without logs
  // wiping it away
  // ====================================================================
  if (session_string[0] != '\0') {
    log_plain("");
    log_plain("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    if (session_is_mdns_only) {
      log_plain("📋 Session String: %s (LAN only via mDNS)", session_string);
      log_plain("🔗 Share with others on your LAN to join:");
    } else {
      log_plain("📋 Session String: %s", session_string);
      log_plain("🔗 Share this globally to join:");
    }
    log_plain("   ascii-chat %s", session_string);
    log_plain("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    log_plain("");
  }

  // Copy session info to globals for status screen display
  SAFE_STRNCPY(g_session_string, session_string, sizeof(g_session_string));
  g_session_is_mdns_only = session_is_mdns_only;

  log_debug("Server entering accept loop (port %d)...", port);

  // Initialize status screen
  g_server_start_time = time(NULL);
  g_last_status_update = platform_get_monotonic_time_us();

  // Clear status screen log buffer to discard initialization logs
  // This ensures only NEW logs (generated after status screen starts) are displayed
  if (GET_OPTION(status_screen)) {
    extern void server_status_log_clear(void);
    server_status_log_clear();
  }

  // Start status screen thread if enabled
  // Runs independently at target FPS (default 60 Hz), decoupled from network accept loop
  if (GET_OPTION(status_screen)) {
    if (asciichat_thread_create(&g_status_screen_thread, status_screen_thread, NULL) != 0) {
      log_error("Failed to create status screen thread");
      goto cleanup;
    }
    log_debug("Status screen thread started");
  }

  // Run TCP server (blocks until shutdown signal received)
  // tcp_server_run() handles:
  // - select() on IPv4/IPv6 sockets with timeout
  // - accept() new connections
  // - Spawn ascii_chat_client_handler() thread for each connection
  // - Responsive shutdown when atomic_store(&g_tcp_server.running, false)
  asciichat_error_t run_result = tcp_server_run(&g_tcp_server);
  if (run_result != ASCIICHAT_OK) {
    log_error("TCP server exited with error");
  }

  log_debug("Server accept loop exited");

cleanup:
  // Signal status screen thread to exit
  atomic_store(&g_server_should_exit, true);

  // Wait for status screen thread to finish if it was started
  if (GET_OPTION(status_screen)) {
    log_debug("Waiting for status screen thread to exit...");
    asciichat_thread_join(&g_status_screen_thread, NULL);
    log_debug("Status screen thread exited");
  }

  // Cleanup status screen log capture
  server_status_log_cleanup();

  // Cleanup
  log_debug("Server shutting down...");
  memset(g_session_string, 0, sizeof(g_session_string)); // Clear session string for status screen

  // Wake up any threads that might be blocked on condition variables
  // (like packet queues) to ensure responsive shutdown
  // This must happen BEFORE client cleanup to wake up any blocked threads
  static_cond_broadcast(&g_shutdown_cond);
  // NOTE: Do NOT call cond_destroy() on statically-initialized condition variables
  // g_shutdown_cond uses STATIC_COND_INIT which doesn't allocate resources that need cleanup
  // Calling cond_destroy() on a static cond is undefined behavior on some platforms

  // CRITICAL: Close all client sockets immediately to unblock receive threads
  // The signal handler only closed the listening socket, but client receive threads
  // are still blocked in recv_with_timeout(). We need to close their sockets to unblock them.
  log_debug("Closing all client sockets to unblock receive threads...");

  // Use write lock since we're modifying client->socket
  rwlock_wrlock(&g_client_manager_rwlock);
  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];
    if (atomic_load(&client->client_id) != 0 && client->socket != INVALID_SOCKET_VALUE) {
      socket_close(client->socket);
      client->socket = INVALID_SOCKET_VALUE;
    }
  }
  rwlock_wrunlock(&g_client_manager_rwlock);

  log_debug("Signaling all clients to stop (sockets closed, g_server_should_exit set)...");

  // Stop and destroy server worker thread pool (stats logger, etc.)
  if (g_server_worker_pool) {
    thread_pool_destroy(g_server_worker_pool);
    g_server_worker_pool = NULL;
    log_debug("Server worker thread pool stopped");
  }

  // Destroy rate limiter
  if (g_rate_limiter) {
    rate_limiter_destroy(g_rate_limiter);
    g_rate_limiter = NULL;
  }

  // Clean up all connected clients
  log_debug("Cleaning up connected clients...");
  // FIXED: Simplified to collect client IDs first, then remove them without holding locks
  uint32_t clients_to_remove[MAX_CLIENTS];
  int client_count = 0;

  rwlock_rdlock(&g_client_manager_rwlock);
  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];

    // Only attempt to clean up clients that were actually connected
    // (client_id is 0 for uninitialized clients, starts from 1 for connected clients)
    // FIXED: Only access mutex for initialized clients to avoid accessing uninitialized mutex
    if (atomic_load(&client->client_id) == 0) {
      continue; // Skip uninitialized clients
    }

    // Use snapshot pattern to avoid holding both locks simultaneously
    // This prevents deadlock by not acquiring client_state_mutex while holding rwlock
    uint32_t client_id_snapshot = atomic_load(&client->client_id); // Atomic read is safe under rwlock

    // Clean up ANY client that was allocated, whether active or not
    // (disconnected clients may not be active but still have resources)
    clients_to_remove[client_count++] = client_id_snapshot;
  }
  rwlock_rdunlock(&g_client_manager_rwlock);

  // Remove all clients without holding any locks
  for (int i = 0; i < client_count; i++) {
    if (remove_client(&server_ctx, clients_to_remove[i]) != 0) {
      log_error("Failed to remove client %u during shutdown", clients_to_remove[i]);
    }
  }

  // Clean up hash table
  // Clean up uthash table (uthash handles deletion when the last item is removed,
  // but we should clear it here just in case there are stragglers)
  if (g_client_manager.clients_by_id) {
    client_info_t *current_client, *tmp;
    HASH_ITER(hh, g_client_manager.clients_by_id, current_client, tmp) {
      HASH_DELETE(hh, g_client_manager.clients_by_id, current_client);
      // Note: We don't free current_client here because it's part of the clients[] array
    }
    g_client_manager.clients_by_id = NULL;
  }

  // Clean up audio mixer
  if (g_audio_mixer) {
    // CRITICAL: Set to NULL FIRST before destroying
    // Client handler threads may still be running and checking g_audio_mixer
    // Setting it to NULL first prevents use-after-free race condition
    // volatile ensures this write is visible to other threads immediately
    mixer_t *mixer_to_destroy = g_audio_mixer;
    g_audio_mixer = NULL;
    mixer_destroy(mixer_to_destroy);
  }

  // Clean up mDNS context
  if (g_mdns_ctx) {
    asciichat_mdns_shutdown(g_mdns_ctx);
    g_mdns_ctx = NULL;
    log_debug("mDNS context shut down");
  }

  // Clean up synchronization primitives
  rwlock_destroy(&g_client_manager_rwlock);
  mutex_destroy(&g_client_manager.mutex);

#ifdef NDEBUG
  // Clean up statistics system
  stats_cleanup();
#endif

#ifndef NDEBUG
  // Clean up lock debugging system (always, regardless of build type)
  // Lock debug records are allocated in debug builds too, so they must be cleaned up
  lock_debug_cleanup();
#endif

  // Destroy session host (before TCP server shutdown)
  if (server_ctx.session_host) {
    log_debug("Destroying session host");
    session_host_destroy(server_ctx.session_host);
    server_ctx.session_host = NULL;
  }

  // Shutdown TCP server (closes listen sockets and cleans up)
  tcp_server_shutdown(&g_tcp_server);

  // Join ACDS threads (if started)
  // NOTE: Must be done BEFORE destroying transport to ensure clean shutdown
  if (g_acds_ping_thread_started) {
    log_debug("Joining ACDS ping thread");
    asciichat_thread_join(&g_acds_ping_thread, NULL);
    g_acds_ping_thread_started = false;
    log_debug("ACDS ping thread joined");
  }

  if (g_acds_receive_thread_started) {
    log_debug("Joining ACDS receive thread");
    asciichat_thread_join(&g_acds_receive_thread, NULL);
    g_acds_receive_thread_started = false;
    log_debug("ACDS receive thread joined");
  }

  // Clean up WebRTC peer manager (if initialized for ACDS signaling relay)
  if (g_webrtc_peer_manager) {
    log_debug("Destroying WebRTC peer manager");
    webrtc_peer_manager_destroy(g_webrtc_peer_manager);
    g_webrtc_peer_manager = NULL;
  }

  // Clean up ACDS transport wrapper (if created)
  if (g_acds_transport) {
    log_debug("Destroying ACDS transport wrapper");
    acip_transport_destroy(g_acds_transport);
    g_acds_transport = NULL;
  }

  // Disconnect from ACDS server (if connected for WebRTC signaling relay)
  if (g_acds_client) {
    log_debug("Disconnecting from ACDS server");
    acds_client_disconnect(g_acds_client);
    SAFE_FREE(g_acds_client);
    g_acds_client = NULL;
  }

  // Clean up SIMD caches
  simd_caches_destroy_all();

  // Clean up symbol cache
  // This must be called BEFORE log_destroy() as symbol_cache_cleanup() uses log_debug()
  // Safe to call even if atexit() runs - it's idempotent (checks g_symbol_cache_initialized)
  // Also called via platform_cleanup() atexit handler, but explicit call ensures proper ordering
  symbol_cache_cleanup();

  // Clean up global buffer pool (explicitly, as atexit may not run on Ctrl-C)
  // Note: This is also registered with atexit(), but calling it explicitly is safe (idempotent)
  // Safe to call even if atexit() runs - it checks g_global_buffer_pool and sets it to NULL
  buffer_pool_cleanup_global();

  // Disable keepawake mode (re-allow OS to sleep)
  platform_disable_keepawake();

  // Clean up binary path cache explicitly
  // Note: This is also called by platform_cleanup() via atexit(), but it's idempotent
  // (checks g_cache_initialized and sets it to false, sets g_bin_path_cache to NULL)
  // Safe to call even if atexit() runs later
  platform_cleanup_binary_path_cache();

  // Clean up errno context (allocated strings, backtrace symbols)
  asciichat_errno_cleanup();

  // Clean up RCU-based options state
  options_state_shutdown();

  // Clean up platform-specific resources (Windows: Winsock cleanup, timer restoration)
  // POSIX: minimal cleanup (symbol cache already handled above on Windows)
  socket_cleanup();
  platform_restore_timer_resolution(); // Restore timer resolution (no-op on POSIX)

#ifndef NDEBUG
  // Join the lock debug thread as one of the very last things before exit
  lock_debug_cleanup_thread();
#endif

  log_info("Server shutdown complete");

  asciichat_error_stats_print();

  log_destroy();

  // Use exit() to allow atexit() handlers to run
  // Cleanup functions are idempotent (check if initialized first)
  exit(0);
}
