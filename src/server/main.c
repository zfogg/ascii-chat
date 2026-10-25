#include <ascii-chat/network/acip/backpressure.h>
#include <ascii-chat/debug/sync.h>
#include <ascii-chat/network/errors.h>
#include <ascii-chat/debug/stats.h>
#include <ascii-chat/debug/errno.h>
#include <ascii-chat/stats/runtime.h>
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
 * Thread Safety:
 * ==============
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

#include <ascii-chat/crypto/key_identity/display.h>
#include <ascii-chat/tui/notice.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h> // For write(), STDERR_FILENO (signal handler), dup(), dup2()
#include <fcntl.h>  // For open() and O_WRONLY
#endif

#include <ascii-chat/platform/network.h> // Consolidates platform-specific network headers
#include <ascii-chat/app_callbacks.h>

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
#include "../main.h" // For should_exit() and signal_exit() API
#include "common/session/server_like.h"
#include "connections.h"
#include <ascii-chat/common.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/lifecycle.h>
#include <ascii-chat/uthash.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/socket.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/platform/question.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/video/rgba/image.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/network/network.h>
#include <ascii-chat/network/tcp/server.h>
#include <ascii-chat/network/acip/acds_client.h>
#include <ascii-chat/network/consensus/handoff.h>
#include <ascii-chat/discovery/strings.h>
#include <ascii-chat/network/webrtc/stun.h>
#include <ascii-chat/thread_pool.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h> // For RCU-based options access
#include <ascii-chat/buffer.h>
#include <ascii-chat/audio/mixer.h>
#include <ascii-chat/audio/audio.h>
#include "session/h265/server.h"
#include "client.h"
#include "stream.h"
#include "stats.h"
#include <ascii-chat/platform/string.h>
#include <ascii-chat/platform/symbols.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/crypto/keys.h>
#include <ascii-chat/crypto/handshake/server.h>
#include <ascii-chat/crypto/handshake/common.h>
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
#include <ascii-chat/tui/status.h>
#include <ascii-chat/log/search.h>
#include <ascii-chat/log/json.h>
#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/debug/memory.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/atomic.h>

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
 *
 * Uses the global g_should_exit from src/main.c instead of a mode-specific flag.
 */

/**
 * @brief Shutdown check callback for library code
 *
 * Provides clean separation between application state and library code.
 * Registered with shutdown_register_callback() so library code can check
 * shutdown status without directly accessing g_should_exit.
 */
static bool check_shutdown(void) {
  return should_exit();
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
 * @brief Global H.265 codec server context
 *
 * Shared H.265 decoding context used by all packet handlers to decode
 * client H.265 frames. Each client gets its own decoder instance.
 *
 * THREAD SAFETY: The h265_server context is thread-safe and can be used
 * concurrently by multiple client handlers. Decoders are per-client.
 * During shutdown, set to NULL before destroying to prevent use-after-free.
 */
h265_server_context_t *volatile g_h265_server = NULL;

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

// TCP server is now owned by session_server_like; access via session_server_like_get_network_server().

// WebSocket server is now owned by session_server_like; access via session_server_like_get_websocket_server().

/**
 * @brief Server start time for uptime calculation and status display
 *
 * Captured at server startup and used by status screen to calculate uptime.
 * Shared with status update callback.
 */
static time_t g_server_start_time = 0;

// Status screen thread, keyboard thread, and SPSC queue are now owned by session_server_like.

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

// UPnP and mDNS contexts are now owned by session_server_like; access via accessors.

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
 * 1. Set atomic g_should_exit flag (signal-safe, checked by all threads)
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
 * allowing threads to check g_should_exit and exit gracefully.
 *
 * PLATFORM CONSIDERATIONS:
 * ========================
 * - Windows: socket_shutdown() required to interrupt blocked recv()
 * - POSIX: socket_close() alone typically suffices
 * - Both: Avoid mutex operations (signal may interrupt mutex holder)
 *
 * @param sig The signal number (SIGINT=2, SIGTERM=15)
 */
static void server_handle_sigint(int sig) {
  (void)sig;
  // If in grep input mode, cancel grep instead of shutting down.
  // Uses atomic reads only (no mutex) - safe from signal context.
  if (log_search_is_entering_atomic()) {
    log_search_signal_cancel();
    return;
  }

  static atomic_t sigint_count = {0};
  int count = atomic_fetch_add_int(&sigint_count, 1) + 1;
  if (count > 1) {
    platform_force_exit(1);
  }

  // STEP 1: Set atomic shutdown flag (checked by all worker threads)
  atomic_store_bool(&g_should_exit, true);

  // STEP 2: Use raw write() for user feedback (async-signal-safe).
  // log_info_nofile() is NOT safe here - it calls SAFE_MALLOC (via ansi_strip_escapes),
  // SAFE_FREE, and server_like_status_log_append (mutex). If the signal interrupts a thread
  // holding the memory tracking lock or log buffer lock, the handler deadlocks.
  // Since SIGINT auto-masks during handler execution, a second Ctrl+C would be
  // blocked, preventing platform_force_exit() from ever running.
  // INFO: commented out because it's too noisy, uncomment to debug
  // static const char sigint_msg[] = "SIGINT received - shutting down server...\n";
  // platform_write_all(STDERR_FILENO, sigint_msg, sizeof(sigint_msg) - 1);

  network_server_request_stop(session_server_like_get_network_server());

  // STEP 4: DO NOT access client data structures in signal handler
  // Signal handlers CANNOT safely use mutexes, rwlocks, or access complex data structures
  // This causes deadlocks and memory access violations because:
  // 1. Signal may interrupt a thread that already holds these locks
  // 2. Attempting to acquire locks in signal handler = instant deadlock
  // 3. Client array might be in an inconsistent state during modification
  //
  // SOLUTION: The listening socket closure above is sufficient to unblock accept_with_timeout()
  // The main thread will detect g_should_exit and properly close client sockets with timeouts

  // NOTE: Do NOT call log_destroy() here - it's not async-signal-safe
  // The main thread will handle cleanup when it detects g_should_exit
}

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

  // Copy SDP string after header (binary data, not null-terminated)
  // NOLINTNEXTLINE(bugprone-not-null-terminated-result)
  memcpy(packet + sizeof(acip_webrtc_sdp_t), sdp, sdp_len);

  log_debug("Server sending WebRTC SDP %s to participant (sender=%02x%02x..., recipient=%02x%02x...) via ACDS",
            sdp_type, g_server_participant_id[0], g_server_participant_id[1], recipient_id[0], recipient_id[1]);

  // Send via ACDS transport using generic packet sender
  asciichat_error_t result =
      packet_send_via_transport(g_acds_transport, PACKET_TYPE_ACIP_WEBRTC_SDP, packet, total_len, 0);

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
      packet_send_via_transport(g_acds_transport, PACKET_TYPE_ACIP_WEBRTC_ICE, packet, total_len, 0);

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
    log_error("Invalid parameters");
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

  asciichat_error_t result = network_server_adopt(server_ctx->network_server, transport, participant_str, true);
  if (result != ASCIICHAT_OK) {
    log_error("Cannot adopt authenticated peer: %s", asciichat_error_format(NULL, result));
    acip_transport_destroy(transport);
  } else {
    log_info("Authenticated WebRTC connection adopted for %s", participant_str);
  }
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
    log_error("Invalid parameters or peer_manager not initialized");
    return;
  }

  // Validate packet length
  uint16_t sdp_len = NET_TO_HOST_U16(sdp->sdp_len);
  if (total_len < sizeof(acip_webrtc_sdp_t) + sdp_len) {
    log_error("Invalid packet length (total=%zu, expected>=%zu)", total_len, sizeof(acip_webrtc_sdp_t) + sdp_len);
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
              asciichat_error_format(NULL, result));
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
    log_error("Invalid parameters or peer_manager not initialized");
    return;
  }

  // Validate packet length
  uint16_t payload_len = NET_TO_HOST_U16(ice->candidate_len);
  if (total_len < sizeof(acip_webrtc_ice_t) + payload_len) {
    log_error("Invalid packet length (total=%zu, expected>=%zu)", total_len, sizeof(acip_webrtc_ice_t) + payload_len);
    return;
  }

  log_debug("Received WebRTC ICE candidate from participant %02x%02x%02x%02x...", ice->sender_id[0], ice->sender_id[1],
            ice->sender_id[2], ice->sender_id[3]);

  // Forward to peer_manager (pass full packet structure)
  asciichat_error_t result = webrtc_peer_manager_handle_ice(g_webrtc_peer_manager, ice);

  if (result != ASCIICHAT_OK) {
    log_error("Failed to handle remote ICE candidate from participant %02x%02x%02x%02x...: %s", ice->sender_id[0],
              ice->sender_id[1], ice->sender_id[2], ice->sender_id[3], asciichat_error_format(NULL, result));
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
  asciichat_mdns_t *mdns_ctx = session_server_like_get_mdns_ctx();
  if (!mdns_ctx) {
    log_debug("mDNS context not initialized, skipping advertisement");
    return;
  }

  // Build session name from hostname for mDNS service name
  char hostname[256] = {0};
  // Reserve the service label suffix for the port used by the mDNS backend.
  char session_name[56] = "ascii-chat-Server";
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

  asciichat_error_t mdns_advertise_result = asciichat_mdns_advertise(mdns_ctx, &service);
  if (mdns_advertise_result != ASCIICHAT_OK) {
    LOG_ERRNO_IF_SET("Failed to advertise mDNS service");
    log_warn("mDNS advertising failed - LAN discovery disabled");
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

  while (!atomic_load_bool(&g_should_exit)) {
    if (!g_acds_transport) {
      log_debug("ACDS transport destroyed, exiting ping thread");
      break;
    }

    // Send PING every 10 seconds to keep connection alive
    socket_t acds_socket = g_acds_transport->methods->get_socket(g_acds_transport);
    if (acds_socket != INVALID_SOCKET_VALUE) {
      asciichat_error_t ping_result = packet_send_via_transport(g_acds_transport, PACKET_TYPE_PING, NULL, 0, 0);
      if (ping_result == ASCIICHAT_OK) {
        log_debug("ACDS keepalive: Sent periodic PING");
      } else {
        log_warn("ACDS keepalive: Failed to send PING: %s", asciichat_error_format(NULL, ping_result));
      }
    }

    // Sleep for 10 seconds before next ping (well before 15s timeout)
    for (int i = 0; i < 100 && !atomic_load_bool(&g_should_exit); i++) {
      APP_CALLBACK_VOID(platform_pump_events);
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
    packet_send_via_transport(g_acds_transport, PACKET_TYPE_PONG, NULL, 0, 0);
  }
}

/**
 * @brief ACDS PONG callback - log keepalive response
 */
static void on_acds_pong(void *ctx) {
  (void)ctx;
  log_debug("ACDS keepalive: Received PONG from ACDS server");
}

static asciichat_error_t acds_signaling_packet(network_connection_t *connection, packet_type_t type,
                                               const void *payload, size_t length, void *arg) {
  (void)arg;
  if ((unsigned)type == CONSENSUS_HANDOFF_PACKET) {
    consensus_handoff_message_t message;
    return consensus_handoff_decode(payload, length, &message);
  }
  const acip_client_callbacks_t callbacks = {.on_webrtc_sdp = on_webrtc_sdp_server,
                                             .on_webrtc_ice = on_webrtc_ice_server,
                                             .on_ping = on_acds_ping,
                                             .on_pong = on_acds_pong};
  return acip_handle_client_packet(network_connection_transport(connection), type, payload, length, &callbacks);
}
static void *acds_receive_thread(void *arg) {
  (void)arg;
  const network_connection_callbacks_t callbacks = {.packet = acds_signaling_packet};
  asciichat_error_t result = network_connection_run(g_acds_client->connection, &callbacks, NULL);
  if (result != ASCIICHAT_OK && !atomic_load_bool(&g_should_exit))
    log_warn("Discovery signaling connection stopped: %s", asciichat_error_format(NULL, result));
  return NULL;
}

/* ============================================================================
 * Signal Handlers
 * ============================================================================
 */

// SIGTERM handling is now done by session_server_like (both SIGINT and SIGTERM call interrupt_fn).
// WebSocket server thread wrapper and status screen thread are now in session_server_like.

/* ============================================================================
 * Discovery signaling callbacks
 * ============================================================================
 */

/**
 * @brief WebSocket client handler thread function
 *
 * Called by websocket_server for each accepted WebSocket connection.
 * Similar to TCP handler but transport is already created.
 *
 * @param arg Pointer to websocket_client_context_t
 * @return NULL (thread exit value)
 */

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
 * 4. Global resource initialization (audio mixer, owned buffers, etc.)
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
 * 1. Signal handlers set g_should_exit atomically
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
      NOTICE(DANGER, "NO VALID IDENTITY KEYS", "No valid identity keys loaded despite %zu --key flag(s)", num_keys);
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

  for (size_t i = 0; i < g_num_server_identity_keys; ++i) {
    public_key_t identity = {.type = KEY_TYPE_ED25519};
    memcpy(identity.key, g_server_identity_keys[i].public_key, sizeof(identity.key));
    char label[64];
    safe_snprintf(label, sizeof(label), "SERVER PUBLIC IDENTITY %zu", i + 1);
    key_identity_announce(label, &identity);
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

/* ============================================================================
 * Static server context (populated during init, used by callbacks)
 * ============================================================================ */
static server_context_t g_server_ctx;

/* ============================================================================
 * Status screen callback (populates ui_status_t from server state)
 * ============================================================================ */
typedef struct {
  stats_view_t *view;
  unsigned active, queued;
} server_stats_visit_t;
static void server_stats_connection(network_connection_t *connection, void *arg) {
  server_stats_visit_t *visit = arg;
  server_connection_state_t *state = network_connection_context(connection);
  client_info_t *client = state ? atomic_ptr_load(&state->client) : NULL;
  if (!client || !atomic_load_bool(&client->event_ready))
    return;
  ++visit->active;
  acip_outbound_stats_t outbound = {0};
  acip_transport_t *transport = network_connection_transport(connection);
  if (transport->outbound)
    acip_outbound_snapshot(transport, &outbound);
  size_t queued = outbound.application_bytes + outbound.backend_bytes;
  if (queued)
    ++visit->queued;
  if (!visit->view->page)
    return;
  mutex_lock(&client->client_state_mutex);
  uint64_t received =
      client->incoming_video_buffer ? atomic_load_u64(&client->incoming_video_buffer->total_frames_received) : 0;
  unsigned width = client->width, height = client->height;
  mutex_unlock(&client->client_state_mutex);
  stats_view_add(visit->view, "%-16.16s %ux%u  %8llu  %8llu  %8llu / %zu", client->client_id, width, height,
                 (unsigned long long)atomic_load_u64(&client->frames_sent_count), (unsigned long long)received,
                 (unsigned long long)outbound.dropped, queued);
}
static void server_stats_provider(stats_view_t *view, void *unused) {
  (void)unused;
  server_stats_visit_t visit = {.view = view};
  if (view->page) {
    stats_view_add(view, "");
    stats_view_add_header(view, "SERVER CLIENTS   dimensions    sent    received    outbound drops / queued bytes");
  }
  network_server_foreach(session_server_like_get_network_server(), server_stats_connection, &visit);
  stats_gauge_set(stats_runtime_scope(), STATS_GAUGE_CONNECTIONS_ACTIVE, visit.active);
  stats_gauge_set(stats_runtime_scope(), STATS_GAUGE_VIDEO_QUEUE_DEPTH, visit.queued);
  stats_view_add(view, "Connected clients: %u", visit.active);
}

static void server_status_fn(void *user_data, ui_status_t *out_status) {
  (void)user_data;

  network_server_t *tcp = session_server_like_get_network_server();
  if (!tcp) {
    return;
  }

  ui_status_gather(tcp, g_session_string, network_server_config(tcp)->ipv4_address,
                   network_server_config(tcp)->ipv6_address, GET_OPTION(port), g_server_start_time, "Server",
                   g_session_is_mdns_only, out_status);
}

/* ============================================================================
 * Mode-specific initialization callback
 * ============================================================================ */
typedef struct {
  const char *password;
  bool has_password;
  bool has_identity;
  bool explicit_expose;
  bool expose_ip;
} server_discovery_policy_t;

typedef struct {
  char session_string[64];
  bool mdns_only;
} server_discovery_startup_t;

static asciichat_error_t server_init_client_manager_lock(void) {
  // Initialize synchronization primitives
  if (rwlock_init(&g_client_manager_rwlock, "clients") != 0) {
    return SET_ERRNO(ERROR_THREAD, "Failed to initialize client manager rwlock");
  }
  g_client_manager_rwlock_initialized = true;

  // Register global sync primitives for debugging
  NAMED_REGISTER_RWLOCK(&g_client_manager_rwlock, "client_manager_rwlock", NULL);

  return ASCIICHAT_OK;
}

static asciichat_error_t server_init_rate_limiter(void) {
  // Initialize connection rate limiter (prevents DoS attacks)
  if (!atomic_load_bool(&g_should_exit)) {
    log_debug("Initializing connection rate limiter...");
    g_rate_limiter = rate_limiter_create_memory();
    if (!g_rate_limiter) {
      LOG_ERRNO_IF_SET("Failed to initialize rate limiter");
      if (!atomic_load_bool(&g_should_exit)) {
        return SET_ERRNO(ERROR_MEMORY, "Failed to create connection rate limiter");
      }
    } else {
      log_info("Connection rate limiter initialized (50 connections/min per IP)");
    }
  }

  return ASCIICHAT_OK;
}

static asciichat_error_t server_init_audio_mixer(void) {
  // Initialize audio mixer (always enabled on server)
  if (!atomic_load_bool(&g_should_exit)) {
    log_debug("Initializing audio mixer for per-client audio rendering...");
    g_audio_mixer = mixer_create(MAX_CLIENTS, AUDIO_SAMPLE_RATE);
    if (!g_audio_mixer) {
      LOG_ERRNO_IF_SET("Failed to initialize audio mixer");
      if (!atomic_load_bool(&g_should_exit)) {
        return SET_ERRNO(ERROR_AUDIO, "Failed to initialize audio mixer");
      }
    } else {
      if (!atomic_load_bool(&g_should_exit)) {
        log_debug("Audio mixer initialized successfully for per-client audio rendering");
      }
    }
  }

  return ASCIICHAT_OK;
}

static asciichat_error_t server_init_h265(void) {
  // Initialize H.265 codec server context (always enabled on server)
  if (!atomic_load_bool(&g_should_exit)) {
    log_debug("Initializing H.265 codec server context...");
    g_h265_server = h265_server_context_create();
    if (!g_h265_server) {
      LOG_ERRNO_IF_SET("Failed to initialize H.265 codec context");
      if (!atomic_load_bool(&g_should_exit)) {
        return SET_ERRNO(ERROR_MEDIA_INIT, "Failed to initialize H.265 codec context");
      }
    } else {
      if (!atomic_load_bool(&g_should_exit)) {
        log_debug("H.265 codec server context initialized successfully");
      }
    }
  }

  return ASCIICHAT_OK;
}

static void server_init_session_host(network_server_t *tcp, int port) {
  // Session Host Creation (for discovery mode support)
  // Create session_host to track clients in a transport-agnostic way.
  if (!atomic_load_bool(&g_should_exit)) {
    const char *ipv4_address = network_server_config(tcp)->ipv4_address;
    const char *ipv6_address = network_server_config(tcp)->ipv6_address;

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

    g_server_ctx.session_host = session_host_create(&host_config);
    if (!g_server_ctx.session_host) {
      // Non-fatal: session_host is optional, server can work without it
      log_warn("Failed to create session_host (discovery mode support disabled)");
    } else {
      log_debug("Session host created for discovery mode support");
    }
  }
}

static bool server_allow_discovery(server_discovery_policy_t *policy) {
  if (policy->has_password || policy->has_identity) {
    // Auto-enable privacy: IP revealed only after verification
    policy->expose_ip = false;
    log_plain("ACDS privacy enabled: IP disclosed only after %s verification",
              policy->has_password ? "password" : "identity");
  } else if (policy->explicit_expose) {
    // Explicit opt-in to public IP disclosure
    // Only prompt if running interactively (can prompt user)
    // When non-interactive (automated/scripted), treat explicit flag as confirmation
    bool is_interactive = terminal_can_prompt_user();

    if (is_interactive) {
      if (!ui_notice_confirm_kind(UI_PROMPT_IP_DISCLOSURE, UI_NOTICE_DANGER,
                                  "PUBLIC IP DISCLOSURE\nAnyone with the session string will be able to see your IP "
                                  "address.\n\nDo you want to proceed",
                                  30)) {
        log_info("IP disclosure not confirmed. Server will run WITHOUT discovery service.");
        return false;
      }
    }

    // User confirmed (or running non-interactively with explicit flag) - proceed with public IP disclosure
    policy->expose_ip = true;
    NOTICE(DANGER, "PUBLIC IP DISCLOSURE ENABLED",
           "Public IP disclosure CONFIRMED.\n"
           "Your IP address will be visible to anyone with the session string");
  } else {
    // Security violation: No password, no identity, no explicit opt-in
    NOTICE(DANGER, "DISCOVERY DISABLED: SECURITY REQUIRED",
           "Cannot create ACDS session: No security configured!\n"
           "You must either:\n"
           "1. Set a password: --password \"your-secret\"\n"
           "2. Use identity key: --key ~/.ssh/id_ed25519\n"
           "3. Explicitly allow public IP: --acds-expose-ip (NOT RECOMMENDED)\n"
           "\n"
           "Server will run WITHOUT discovery service.");
    return false;
  }

  return true;
}

static void server_prepare_discovery_request(acds_session_create_params_t *params,
                                             const server_discovery_policy_t *policy, int port,
                                             nat_upnp_context_t *upnp_ctx, bool upnp_succeeded) {
  // Use server's Ed25519 identity public key if available
  if (g_server_encryption_enabled && policy->has_identity) {
    memcpy(params->identity_pubkey, g_server_private_key.public_key, 32);
    memcpy(params->identity_seckey, g_server_private_key.key.ed25519, sizeof(params->identity_seckey));
    log_debug("Using server identity key for ACDS session");
  } else {
    // No identity key available - use zero key
    // ACDS will accept this if identity verification is not required
    memset(params->identity_pubkey, 0, 32);
    log_debug("No server identity key - using zero key for ACDS session");
  }

  params->capabilities = 0x03; // Video + Audio
  params->max_participants = GET_OPTION(max_clients);
  log_debug("ACDS: max_clients option value = %d", GET_OPTION(max_clients));

  // Set password if configured
  params->has_password = policy->has_password;
  if (policy->has_password) {
    // TODO: Hash password with Argon2id
    SAFE_STRNCPY(params->password, policy->password, sizeof(params->password));
  }

  // Set IP disclosure policy (determined above)
  params->acds_expose_ip = policy->expose_ip;
  log_debug("Server setting acds_expose_ip=%d (explicit_expose=%d, has_password=%d, has_identity=%d)",
            params->acds_expose_ip, policy->explicit_expose, policy->has_password, policy->has_identity);

  // Set session type (Direct TCP or WebRTC)
  // Auto-detect: Use WebRTC if UPnP failed OR if explicitly requested via --webrtc
  // Exception: If bind address is 0.0.0.0, server is on public IP - use Direct TCP
  const char *bind_addr = GET_OPTION(address);
  bool bind_all_interfaces = (strcmp(bind_addr, "0.0.0.0") == 0);

  // Determine session type: prefer WebRTC by default (unless explicitly disabled)
  // Priority: explicit --webrtc flag > connection type detection > UPnP > default
  if (GET_OPTION(webrtc)) {
    // Explicit WebRTC request
    params->session_type = SESSION_TYPE_WEBRTC;
    log_info("ACDS session type: WebRTC (explicitly requested via --webrtc)");
  } else if (bind_all_interfaces) {
    // Bind to 0.0.0.0: use WebRTC as default (better NAT compatibility)
    params->session_type = SESSION_TYPE_WEBRTC;
    log_info("ACDS session type: WebRTC (default for 0.0.0.0 binding, provides NAT-agnostic connections)");
  } else if (upnp_succeeded) {
    // UPnP port mapping worked - can use direct TCP
    params->session_type = SESSION_TYPE_DIRECT_TCP;
    log_info("ACDS session type: Direct TCP (router mapping created; external reachability unverified)");
  } else {
    // UPnP failed and not on public IP - use WebRTC for NAT traversal
    params->session_type = SESSION_TYPE_WEBRTC;
    log_info("ACDS session type: WebRTC (no router mapping or direct connection type selected)");
  }

  // Server connection information (where clients should connect)
  // If bind address is 0.0.0.0, leave server_address empty for ACDS to auto-detect public IP
  if (bind_all_interfaces) {
    params->server_address[0] = '\0'; // Empty - ACDS will use connection source IP
    log_debug("Bind address is 0.0.0.0, ACDS will auto-detect public IP from connection");
  } else {
    SAFE_STRNCPY(params->server_address, bind_addr, sizeof(params->server_address));
  }
  params->server_port = port;
  if (params->session_type == SESSION_TYPE_DIRECT_TCP) {
    if (nat_upnp_get_endpoint(upnp_ctx, params->server_address, sizeof(params->server_address), &params->server_port) !=
        ASCIICHAT_OK) {
      params->session_type = SESSION_TYPE_WEBRTC;
      log_info("ACDS session type: WebRTC (router mapping expired before registration)");
    }
  }

  // DEBUG: Log what we're sending to ACDS
  log_debug("Before SESSION_CREATE - expose_ip_publicly=%d, server_address='%s' port=%u, session_type=%u",
            params->acds_expose_ip, params->server_address, params->server_port, params->session_type);
}

static void server_join_discovery_session(const acds_session_create_params_t *params,
                                          acds_session_create_result_t *result, const char *session_string,
                                          const server_discovery_policy_t *policy) {
  // Server must join its own session so ACDS can route signaling messages
  log_debug("Server joining session as first participant for WebRTC signaling...");
  acds_session_join_params_t join_params = {0};
  join_params.session_string = session_string;

  // Use same identity key as session creation
  memcpy(join_params.identity_pubkey, params->identity_pubkey, 32);
  memcpy(join_params.identity_seckey, params->identity_seckey, sizeof(join_params.identity_seckey));

  // Include password if session is password-protected
  if (policy->has_password) {
    join_params.has_password = true;
    SAFE_STRNCPY(join_params.password, policy->password, sizeof(join_params.password));
  }

  acds_session_join_result_t join_result = {0};
  asciichat_error_t join_err = acds_session_join(g_acds_client, &join_params, &join_result);
  if (join_err != ASCIICHAT_OK || !join_result.success) {
    log_error("Failed to join own session: %s (error: %s)", asciichat_error_format(NULL, join_err),
              join_result.error_message[0] ? join_result.error_message : "unknown");
    // Continue anyway - this is not fatal for Direct TCP sessions
  } else {
    log_debug("Server joined session successfully (participant_id: %02x%02x...)", join_result.participant_id[0],
              join_result.participant_id[1]);
    // Store participant ID for WebRTC signaling (needed to identify server in SDP/ICE messages)
    memcpy(g_server_participant_id, join_result.participant_id, 16);
    log_debug("Stored server participant_id for signaling: %02x%02x...", g_server_participant_id[0],
              g_server_participant_id[1]);
    memcpy(result->session_id, join_result.session_id, 16);
  }
}

static void server_start_discovery_keepalive(void) {
  // Keep ACDS connection alive for WebRTC signaling relay
  log_debug("Server staying connected to ACDS for signaling relay");

  // Signaling shares the authenticated connection and its ordered send scheduler.
  g_acds_transport = g_acds_client->transport;
  if (!g_acds_transport) {
    log_error("Failed to create ACDS transport wrapper");
  } else {
    log_debug("ACDS transport wrapper created for signaling");

    // Start ACDS ping thread to keep connection alive (for ALL session types)
    int ping_thread_result = acthread_create(&g_acds_ping_thread, "acds_ping", acds_ping_thread, NULL);
    if (ping_thread_result != 0) {
      log_error("Failed to create ACDS ping thread: %d", ping_thread_result);
    } else {
      log_debug("ACDS ping thread started to keep connection alive");
      g_acds_ping_thread_started = true;
    }
  }
}

static int server_configure_stun(stun_server_t stun_servers[4]) {
  static lifecycle_t g_stun_lc = LIFECYCLE_INIT;
  static int stun_count = 0; // Store actual count separately

  if (lifecycle_init(&g_stun_lc, "stun")) {
    log_debug("Parsing STUN servers from options: '%s'", GET_OPTION(stun_servers));
    int count = stun_servers_parse(GET_OPTION(stun_servers), OPT_ENDPOINT_STUN_SERVERS_DEFAULT, stun_servers, 4);
    if (count > 0) {
      stun_count = count;
      log_debug("Parsed %d STUN servers", count);
      for (int i = 0; i < count; i++) {
        log_debug("  STUN[%d]: '%s' (len=%d)", i, stun_servers[i].host, stun_servers[i].host_len);
      }
    } else {
      log_warn("Failed to parse STUN servers, using defaults");
      stun_count =
          stun_servers_parse(OPT_ENDPOINT_STUN_SERVERS_DEFAULT, OPT_ENDPOINT_STUN_SERVERS_DEFAULT, stun_servers, 4);
      log_debug("Using default STUN servers, count=%d", stun_count);
      for (int i = 0; i < stun_count; i++) {
        log_debug("  STUN[%d]: '%s' (len=%d)", i, stun_servers[i].host, stun_servers[i].host_len);
      }
    }
  }

  return stun_count;
}

static size_t server_configure_turn(turn_server_t turn_servers[4], const char *turn_username,
                                    const char *turn_credential) {
  size_t turn_count = 0;
  if (turn_username[0] && turn_credential[0]) {
    char urls[OPTIONS_BUFF_SIZE];
    SAFE_STRNCPY(urls, GET_OPTION(turn_servers), sizeof(urls));
    char *saveptr = NULL;
    char *url = platform_strtok_r(urls, ",", &saveptr);
    while (url && turn_count < 4) {
      while (*url == ' ' || *url == '\t')
        url++;
      size_t len = strlen(url);
      while (len && (url[len - 1] == ' ' || url[len - 1] == '\t'))
        url[--len] = '\0';
      if (len && len < sizeof(turn_servers[0].url)) {
        turn_server_t *turn = &turn_servers[turn_count++];
        SAFE_STRNCPY(turn->url, url, sizeof(turn->url));
        SAFE_STRNCPY(turn->username, turn_username, sizeof(turn->username));
        SAFE_STRNCPY(turn->credential, turn_credential, sizeof(turn->credential));
        turn->url_len = (uint8_t)len;
        turn->username_len = (uint8_t)strlen(turn->username);
        turn->credential_len = (uint8_t)strlen(turn->credential);
      }
      url = platform_strtok_r(NULL, ",", &saveptr);
    }
  }

  return turn_count;
}

static void server_init_webrtc(void) {
  log_debug("Initializing WebRTC library and peer manager for session (role=CREATOR)...");

  // Initialize WebRTC library (libdatachannel)
  asciichat_error_t webrtc_init_result = webrtc_init();
  if (webrtc_init_result != ASCIICHAT_OK) {
    log_error("Failed to initialize WebRTC library: %s", asciichat_error_format(NULL, webrtc_init_result));
    g_webrtc_peer_manager = NULL;
  } else {
    log_debug("WebRTC library initialized successfully");

    // Configure STUN servers for ICE gathering (static to persist for peer_manager lifetime)
    static stun_server_t stun_servers[4] = {0};
    int stun_count = server_configure_stun(stun_servers);

    static turn_server_t turn_servers[4];
    static char turn_auth_username[128];
    static char turn_auth_credential[128];
    size_t turn_count = 0;
    const char *turn_username = GET_OPTION(turn_username);
    const char *turn_credential = GET_OPTION(turn_credential);
    SAFE_STRNCPY(turn_auth_username, turn_username, sizeof(turn_auth_username));
    SAFE_STRNCPY(turn_auth_credential, turn_credential, sizeof(turn_auth_credential));
    turn_count = server_configure_turn(turn_servers, turn_username, turn_credential);

    // Configure peer_manager
    const char *server_address = GET_OPTION(address);
    bool bind_webrtc_to_server_address = server_address && server_address[0] &&
                                         strcmp(server_address, "0.0.0.0") != 0 && strcmp(server_address, "::") != 0;
    webrtc_peer_manager_config_t pm_config = {
        .role = WEBRTC_ROLE_CREATOR, // Server accepts offers, generates answers
                                     // Honor an explicitly selected server interface for ICE. With a
                                     // wildcard bind, let libdatachannel gather candidates on all interfaces.
        .bind_address = bind_webrtc_to_server_address ? server_address : NULL,
        .stun_servers = stun_servers,
        .stun_count = stun_count,
        .turn_servers = turn_servers,
        .turn_count = turn_count,
        .relay_only = GET_OPTION(webrtc_relay_only),
        .turn_username = turn_auth_username,
        .turn_credential = turn_auth_credential,
        .on_transport_ready = on_webrtc_transport_ready,
        .user_data = &g_server_ctx};

    // Configure signaling callbacks for relaying SDP/ICE via ACDS
    webrtc_signaling_callbacks_t signaling_callbacks = {
        .send_sdp = server_send_sdp, .send_ice = server_send_ice, .user_data = NULL};

    // Create peer_manager
    asciichat_error_t pm_result = webrtc_peer_manager_create(&pm_config, &signaling_callbacks, &g_webrtc_peer_manager);
    if (pm_result != ASCIICHAT_OK) {
      log_error("Failed to create WebRTC peer_manager: %s", asciichat_error_format(NULL, pm_result));
      g_webrtc_peer_manager = NULL;
    } else {
      log_debug("WebRTC peer_manager initialized successfully");

      // Start ACDS receive thread for WebRTC signaling relay
      int thread_result = acthread_create(&g_acds_receive_thread, "acds_recv", acds_receive_thread, NULL);
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
  }
}

static void server_register_discovery(server_discovery_startup_t *startup, int port, nat_upnp_context_t *upnp_ctx,
                                      bool upnp_succeeded) {
  if (!GET_OPTION(discovery)) {
    log_info("ACDS registration disabled (use --acds to enable)");
    return;
  }
  const char *password = GET_OPTION(password);
  const char *encrypt_key = GET_OPTION(encrypt_key);
  server_discovery_policy_t policy = {
      .password = password,
      .has_password = password && strlen(password) > 0,
      .has_identity = encrypt_key && strlen(encrypt_key) > 0,
      .explicit_expose = GET_OPTION(discovery_expose_ip) != 0,
  };
  if (!server_allow_discovery(&policy)) {
    return;
  }
  // Security is configured, proceed with ACDS connection
  const char *acds_server = GET_OPTION(discovery_server);
  uint16_t acds_port = (uint16_t)GET_OPTION(discovery_port);

  log_info("Attempting to create session on ACDS server at %s:%d...", acds_server, acds_port);

  acds_client_config_t acds_config;
  acds_client_config_init_defaults(&acds_config);
  SAFE_STRNCPY(acds_config.server_address, acds_server, sizeof(acds_config.server_address));
  acds_config.server_port = acds_port;
  acds_config.timeout_ms = 5 * MS_PER_SEC_INT;
  acds_config.identity_key = GET_OPTION(encrypt_key);

  // Allocate ACDS client on heap for server lifecycle
  g_acds_client = SAFE_MALLOC(sizeof(acds_client_t), acds_client_t *);
  if (!g_acds_client) {
    log_error("Failed to allocate ACDS client");
    return;
  }

  asciichat_error_t acds_connect_result = acds_client_connect(g_acds_client, &acds_config);
  if (acds_connect_result != ASCIICHAT_OK) {
    log_error("Failed to connect to ACDS server at %s:%d: %s", acds_server, acds_port,
              asciichat_error_format(NULL, acds_connect_result));
    return;
  }
  acds_session_create_params_t create_params = {0};
  server_prepare_discovery_request(&create_params, &policy, port, upnp_ctx, upnp_succeeded);
  acds_session_create_result_t create_result;
  asciichat_error_t result = acds_session_create(g_acds_client, &create_params, &create_result);
  if (result != ASCIICHAT_OK) {
    log_warn("Failed to create session on ACDS server (server will run without discovery)");
    if (g_acds_client) {
      acds_client_disconnect(g_acds_client);
      SAFE_FREE(g_acds_client);
      g_acds_client = NULL;
    }
    return;
  }
  SAFE_STRNCPY(startup->session_string, create_result.session_string, sizeof(startup->session_string));
  SAFE_STRNCPY(g_session_string, create_result.session_string, sizeof(g_session_string));
  startup->mdns_only = false;
  log_info("Session created: %s", startup->session_string);
  server_join_discovery_session(&create_params, &create_result, startup->session_string, &policy);
  server_start_discovery_keepalive();
  if (create_params.session_type == SESSION_TYPE_WEBRTC) {
    server_init_webrtc();
  } else {
    log_debug("Session type is DIRECT_TCP, skipping WebRTC peer_manager initialization");
  }
  advertise_mdns_with_session(startup->session_string, (uint16_t)port);
}

static asciichat_error_t server_announce_session(server_discovery_startup_t *startup, int port) {
  // Fallback: If no session string was set by ACDS (either disabled or failed),
  // generate a random session string for mDNS discovery only
  if (startup->session_string[0] == '\0' && session_server_like_get_mdns_ctx()) {
    log_debug("No ACDS session string available, generating random session for mDNS");

    // Use the proper session string generation from discovery module
    // This generates adjective-noun-noun format using the full wordlists
    if (acds_string_generate(startup->session_string, sizeof(startup->session_string)) != ASCIICHAT_OK) {
      log_error("Failed to generate session string for mDNS");
      return SET_ERRNO(ERROR_INVALID_STATE, "Failed to generate session string for mDNS");
    }

    log_debug("Generated random session string for mDNS: '%s'", startup->session_string);

    // Mark that this session is mDNS-only (not globally discoverable via ACDS)
    startup->mdns_only = true;

    // Advertise mDNS with random session string
    advertise_mdns_with_session(startup->session_string, (uint16_t)port);
  }

  if (startup->session_string[0] != '\0') {
    if (startup->mdns_only) {
      NOTICE_ANNOUNCE("SESSION READY",
                      "Session String: %s (LAN only via "
                      "mDNS)\nShare with others on your LAN to join:\n   ascii-chat "
                      "%s",
                      startup->session_string, startup->session_string);
    } else {
      NOTICE_ANNOUNCE("SESSION READY",
                      "Session String: %s\nShare this "
                      "globally to join:\n   ascii-chat %s",
                      startup->session_string, startup->session_string);
    }
  }

  return ASCIICHAT_OK;
}

static void server_tick_fn(void *user_data) {
  (void)user_data;
  asciichat_error_t result = webrtc_peer_manager_dispatch_ready(g_webrtc_peer_manager);
  if (result != ASCIICHAT_OK) {
    log_error("WebRTC ready dispatch failed: %s", asciichat_error_format(NULL, result));
    atomic_store_bool(&g_should_exit, true);
    network_server_t *tcp = session_server_like_get_network_server();
    if (tcp)
      network_server_request_stop(tcp);
  }
}

static asciichat_error_t server_init_fn(void *user_data) {
  (void)user_data;
  int port = GET_OPTION(port);

  // Get TCP server from server_like (it's already initialized at this point)
  network_server_t *tcp = session_server_like_get_network_server();
  if (!tcp) {
    return SET_ERRNO(ERROR_INVALID_STATE, "TCP server not available from server_like");
  }

  // Point server context at the server_like-owned TCP server
  g_server_ctx.network_server = tcp;

  // A local mapping behind upstream NAT must not suppress WebRTC discovery.
  nat_upnp_context_t *upnp_ctx = session_server_like_get_upnp_ctx();
  bool upnp_succeeded =
      nat_upnp_is_active(upnp_ctx) && !upnp_ctx->external_is_private && is_internet_ipv4(upnp_ctx->external_ip);

  asciichat_error_t result = server_init_client_manager_lock();
  if (result != ASCIICHAT_OK) {
    return result;
  }
  // Check if SIGINT/SIGTERM was received during initialization
  if (atomic_load_bool(&g_should_exit)) {
    log_info("Shutdown signal received during initialization, skipping server startup");
    return ASCIICHAT_OK;
  }

  // NOTE: g_client_manager is already zero-initialized in client.c with = {0}
  // We only need to initialize the mutex
  mutex_init(&g_client_manager.mutex, "client_manager");

  // Initialize uthash head pointer for O(1) lookup (uthash requires NULL initialization)
  g_client_manager.clients_by_id = NULL;

  result = server_init_rate_limiter();
  if (result != ASCIICHAT_OK) {
    return result;
  }
  result = server_init_audio_mixer();
  if (result != ASCIICHAT_OK) {
    return result;
  }
  result = server_init_h265();
  if (result != ASCIICHAT_OK) {
    return result;
  }
  server_init_session_host(tcp, port);

  // Update server context fields that depend on init results
  g_server_ctx.rate_limiter = g_rate_limiter;
  g_server_ctx.audio_mixer = g_audio_mixer;
  g_server_ctx.h265_server = g_h265_server; // Sync H.265 context created in this function

  server_discovery_startup_t startup = {0};
  server_register_discovery(&startup, port, upnp_ctx, upnp_succeeded);
  result = server_announce_session(&startup, port);
  if (result != ASCIICHAT_OK) {
    return result;
  }

  // Copy session info to globals for status screen display
  SAFE_STRNCPY(g_session_string, startup.session_string, sizeof(g_session_string));
  g_session_is_mdns_only = startup.mdns_only;

  // Initialize the status screen uptime
  g_server_start_time = time(NULL);

  stats_runtime_set_provider(server_stats_provider, NULL);
  log_debug("Server init_fn complete");
  return ASCIICHAT_OK;
}

/* ============================================================================
 * Mode-specific cleanup callback
 * ============================================================================ */
static void server_stop_workers(void) {
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
}

static void server_wait_for_clients(void) {
  // Wait for all client handler threads to exit (only if rwlock was initialized)
  if (g_client_manager_rwlock_initialized) {
    int retry_count = 0;
    while (retry_count < 50) { // Max 500ms wait
      rwlock_rdlock(&g_client_manager_rwlock);
      bool all_cleaned = true;
      for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_client_manager.clients[i].client_id[0] != '\0') {
          all_cleaned = false;
          break;
        }
      }
      rwlock_rdunlock(&g_client_manager_rwlock);

      if (all_cleaned)
        break;
      platform_sleep_us(10 * US_PER_MS_INT); // 10ms
      retry_count++;
    }
    log_debug("Client handler threads cleaned up");
  }
}

static void server_remove_remaining_clients(void) {
  // Clean up all connected clients (only if rwlock was initialized)
  log_debug("Cleaning up connected clients...");
  char clients_to_remove[MAX_CLIENTS][MAX_CLIENT_ID_LEN];
  int client_count = 0;

  if (g_client_manager_rwlock_initialized) {
    rwlock_rdlock(&g_client_manager_rwlock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
      client_info_t *client = &g_client_manager.clients[i];
      if (client->client_id[0] == '\0') {
        continue;
      }
      SAFE_STRNCPY(clients_to_remove[client_count], client->client_id, MAX_CLIENT_ID_LEN - 1);
      client_count++;
    }
    rwlock_rdunlock(&g_client_manager_rwlock);
  }

  for (int i = 0; i < client_count; i++) {
    if (remove_client(&g_server_ctx, clients_to_remove[i]) != 0) {
      log_error("Failed to remove client %s during shutdown", clients_to_remove[i]);
    }
  }

  // Clean up hash table
  while (g_client_manager.clients_by_id) {
    client_info_t *current_client = g_client_manager.clients_by_id;
    HASH_DELETE(hh, g_client_manager.clients_by_id, current_client);
  }
}

static void server_destroy_media(void) {
  // Clean up audio mixer
  if (g_audio_mixer) {
    mixer_t *mixer_to_destroy = g_audio_mixer;
    g_audio_mixer = NULL;
    mixer_destroy(mixer_to_destroy);
  }

  // Clean up H.265 codec server context
  if (g_h265_server) {
    h265_server_context_t *h265_to_destroy = g_h265_server;
    g_h265_server = NULL;
    g_server_ctx.h265_server = NULL;
    h265_server_context_destroy(h265_to_destroy);
    log_debug("H.265 codec server context shut down");
  }
}

static void server_destroy_session_host(void) {
  // Destroy session host
  if (g_server_ctx.session_host) {
    log_debug("Destroying session host");
    session_host_destroy(g_server_ctx.session_host);
    g_server_ctx.session_host = NULL;
  }

  // Destroy H.265 server codec context from server_ctx
  if (g_server_ctx.h265_server) {
    log_debug("Destroying H.265 server context");
    h265_server_context_destroy(g_server_ctx.h265_server);
    g_server_ctx.h265_server = NULL;
  }
}

static void server_stop_discovery(void) {
  // Join ACDS threads (if started)
  if (g_acds_ping_thread_started) {
    log_debug("Joining ACDS ping thread");
    thread_join(&g_acds_ping_thread, NULL);
    g_acds_ping_thread_started = false;
    log_debug("ACDS ping thread joined");
  }

  if (g_acds_client && g_acds_client->connection)
    network_connection_request_stop(g_acds_client->connection);
  if (g_acds_receive_thread_started) {
    log_debug("Joining ACDS receive thread");
    thread_join(&g_acds_receive_thread, NULL);
    g_acds_receive_thread_started = false;
    log_debug("ACDS receive thread joined");
  }

  // Clean up WebRTC peer manager
  if (g_webrtc_peer_manager) {
    log_debug("Destroying WebRTC peer manager");
    webrtc_peer_manager_destroy(g_webrtc_peer_manager);
    g_webrtc_peer_manager = NULL;
  }

  // Clean up ACDS transport wrapper
  if (g_acds_transport) {
    log_debug("Destroying ACDS transport wrapper");
    g_acds_transport = NULL;
  }

  // Disconnect from ACDS server
  if (g_acds_client) {
    log_debug("Disconnecting from ACDS server");
    acds_client_disconnect(g_acds_client);
    SAFE_FREE(g_acds_client);
    g_acds_client = NULL;
  }
}

static void server_cleanup_fn(void *user_data) {
  stats_runtime_set_provider(NULL, NULL);
  (void)user_data;

  log_debug("Server shutting down...");
  memset(g_session_string, 0, sizeof(g_session_string)); // Clear session string for status screen

  // Wake up any threads that might be blocked on condition variables
  // (like packet queues) to ensure responsive shutdown
  static_cond_broadcast(&g_shutdown_cond);

  log_debug("Signaling all clients to stop (sockets closed, g_should_exit set)...");

  server_stop_workers();

  server_wait_for_clients();

  // Cleanup debug sync BEFORE destroying websocket_server
#ifndef NDEBUG
  debug_sync_destroy();
  debug_errno_destroy();
  debug_stats_destroy();
#endif

  server_remove_remaining_clients();

  server_destroy_media();

  // Clean up synchronization primitives (only if they were initialized)
  if (g_client_manager_rwlock_initialized) {
    rwlock_destroy(&g_client_manager_rwlock);
    g_client_manager_rwlock_initialized = false;
  }
  mutex_destroy(&g_client_manager.mutex);

#ifndef NDEBUG
  stats_cleanup();
#endif

  server_destroy_session_host();

  // TCP server, WebSocket server, mDNS, UPnP are destroyed by server_like.

  server_stop_discovery();

  // Clean up platform-specific resources
  socket_cleanup();

  log_info("Server shutdown complete");
  asciichat_error_stats_print();
}

/* ============================================================================
 * server_main() - Entry point, delegates to session_server_like_run()
 * ============================================================================ */
int server_main(void) {
  // Common initialization (options, logging, lock debugging) now happens in main.c before dispatch

  // Register atomic for debug tracking
  ATOMIC_REGISTER_AUTO(g_should_exit, NULL);

  // Register shutdown check callback for library code
  shutdown_register_callback(check_shutdown);

  // Initialize crypto after logging is ready
  log_debug("Initializing crypto...");
  if (init_server_crypto() != 0) {
    LOG_ERRNO_IF_SET("Crypto initialization failed");
    FATAL(ERROR_CRYPTO, "Crypto initialization failed");
  }
  log_debug("Crypto initialized successfully");

  log_info("ascii-chat server starting...");

  int port = GET_OPTION(port);
  if (port < 1 || port > 65535) {
    log_error("Invalid port configuration: %d", port);
    FATAL(ERROR_CONFIG, "Invalid port configuration: %d", port);
  }

  ascii_simd_init();
  precalc_rgb_palettes(weight_red, weight_green, weight_blue);

#ifndef NDEBUG
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

  // Initialize static server context (populated further in server_init_fn)
  // H.265 context is initialized in server_init_fn, not here
  g_server_ctx = (server_context_t){
      .network_server = NULL, // Set in server_init_fn after TCP server is ready
      .rate_limiter = g_rate_limiter,
      .client_manager = &g_client_manager,
      .client_manager_rwlock = &g_client_manager_rwlock,
      .server_should_exit = &g_should_exit,
      .audio_mixer = g_audio_mixer,
      .stats = &g_stats,
      .stats_mutex = &g_stats_mutex,
      .encryption_enabled = g_server_encryption_enabled,
      .server_private_key = &g_server_private_key,
      .client_whitelist = g_client_whitelist,
      .num_whitelisted_clients = g_num_whitelisted_clients,
      .session_host = NULL,
      .h265_server = NULL, // Initialized in server_init_fn
  };

  session_server_like_config_t config = {
      .init_fn = server_init_fn,
      .tick_fn = server_tick_fn,
      .init_user_data = NULL,
      .interrupt_fn = server_handle_sigint,
      .cleanup_fn = server_cleanup_fn,
      .cleanup_user_data = NULL,
      .status_fn = server_status_fn,
      .status_user_data = NULL,
      .connections = server_network_callbacks,
      .connection_user_data = &g_server_ctx,
      .websocket_enabled = true,
      .partial_timeout_ms = 30000,
      .mdns =
          {
              .enabled = !GET_OPTION(no_mdns_advertise),
              .service_name = NULL, // Deferred: advertised after ACDS or mDNS session string is ready
              .service_type = "_ascii-chat._tcp",
          },
      .upnp =
          {
              .enabled = GET_OPTION(enable_upnp),
              .description = "ascii-chat Server",
          },
      // Raise FD limit for high-client-count scenarios (matches discovery-service policy)
      // Server accepts MAX_CLIENTS connections plus WebSocket, TCP, UPnP, mDNS, etc.
      // Each client may use multiple FDs (socket, media buffers, file handles)
      // Raising to 65536 ensures we don't hit system limits under load
      .raise_fd_limit = true,
      .fd_limit_target = 65536,
  };

  asciichat_error_t result = session_server_like_run(&config);
  return (result == ASCIICHAT_OK) ? 0 : 1;
}
