/**
 * @file network/tcp_client.h
 * @brief TCP client abstraction for ascii-chat and other applications
 *
 * This module provides a reusable TCP client implementation with thread management,
 * connection state tracking, and protocol support. It serves as the counterpart to
 * tcp_server.h, providing client-side networking abstractions.
 *
 * ## Architecture
 *
 * The tcp_client module encapsulates all client-side networking state in a single
 * tcp_client_t structure, eliminating the need for global variables and enabling:
 * - Multiple client instances in the same application
 * - Isolated unit testing with mock connections
 * - Clean separation between network layer and application logic
 * - Thread-safe state management with explicit ownership
 *
 * ## Thread Model
 *
 * The tcp_client manages multiple worker threads:
 * - **Data reception thread**: Receives packets from server
 * - **Keepalive thread**: Sends periodic ping packets
 * - **Media capture threads**: Captures video/audio (application-specific)
 * - **Media sender threads**: Sends captured media (application-specific)
 *
 * All threads receive the tcp_client_t pointer as their void* arg parameter,
 * eliminating reliance on global state.
 *
 * ## Comparison with tcp_server
 *
 * | Feature              | tcp_server_t           | tcp_client_t           |
 * |----------------------|------------------------|------------------------|
 * | Socket management    | Multiple client sockets| Single server socket   |
 * | Thread pool          | Per-client threads     | Client worker threads  |
 * | Connection model     | Accept incoming        | Connect outgoing       |
 * | State management     | client_info_t array    | Single client state    |
 * | Lifecycle            | Server-managed         | Application-managed    |
 *
 * ## Integration with ascii-chat Client
 *
 * The ascii-chat client creates a tcp_client_t instance and passes it to all
 * subsystems (audio, video, protocol, etc.) instead of using global variables.
 * This enables proper encapsulation and testability.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 * @version 1.0
 */

#ifndef NETWORK_TCP_CLIENT_H
#define NETWORK_TCP_CLIENT_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "../../platform/abstraction.h"
#include "../../platform/socket.h"
#include "../../platform/terminal.h"
#include "../../audio/audio.h"
#include "../../crypto/handshake/common.h"

/* Audio queue configuration */
#define TCP_CLIENT_AUDIO_QUEUE_SIZE 256

/**
 * @brief Audio packet for async network transmission
 *
 * Queued by audio capture thread and sent by audio sender thread to
 * decouple audio processing from network I/O blocking.
 */
typedef struct {
  uint8_t data[4096];       /**< Opus-encoded audio data */
  size_t size;              /**< Size of encoded data */
  uint16_t frame_sizes[48]; /**< Individual frame sizes for Opus batching */
  int frame_count;          /**< Number of frames in packet */
} tcp_client_audio_packet_t;

/**
 * @brief TCP client connection and state management
 *
 * Encapsulates all state for a single TCP client connection, including:
 * - Network connection state (socket, server info, connection flags)
 * - Thread management (handles, creation flags, exit flags)
 * - Audio processing state (queues, buffers, context)
 * - Protocol state (packet tracking, server state)
 * - Display state (terminal capabilities, TTY info)
 * - Crypto state (handshake context, encryption flags)
 *
 * ## Ownership Model
 *
 * - Created by tcp_client_create() in main thread
 * - Owned by application, shared read-only or synchronized across threads
 * - Destroyed by tcp_client_destroy() after all worker threads joined
 *
 * ## Thread Safety
 *
 * - Atomic fields: Safe for concurrent read/write without locks
 * - Mutex-protected fields: Acquire mutex before access
 * - Immutable after init: Safe for concurrent reads
 *
 * @see tcp_server_t For server-side equivalent structure
 */
typedef struct tcp_client {
  /* ========================================================================
   * Connection State
   * ======================================================================== */

  /** Socket file descriptor for server connection */
  socket_t sockfd;

  /** Connection is active and ready for I/O operations */
  atomic_bool connection_active;

  /** Connection was lost (triggers reconnection logic) */
  atomic_bool connection_lost;

  /** Reconnection should be attempted */
  atomic_bool should_reconnect;

  /** Client ID assigned by server during initial handshake */
  uint32_t my_client_id;

  /** Server IP address (for display and reconnection) */
  char server_ip[256];

  /** Mutex protecting concurrent socket send operations */
  mutex_t send_mutex;

  /** Encryption is enabled for this connection */
  bool encryption_enabled;

  /* ========================================================================
   * Audio State
   * ======================================================================== */

  /** Audio capture and playback context */
  audio_context_t audio_ctx;

  /** Queue of audio packets awaiting transmission */
  tcp_client_audio_packet_t audio_send_queue[TCP_CLIENT_AUDIO_QUEUE_SIZE];

  /** Write position in audio send queue (producer) */
  int audio_send_queue_head;

  /** Read position in audio send queue (consumer) */
  int audio_send_queue_tail;

  /** Mutex protecting audio send queue access */
  mutex_t audio_send_queue_mutex;

  /** Condition variable for audio queue signaling */
  cond_t audio_send_queue_cond;

  /** Audio send queue has been initialized */
  bool audio_send_queue_initialized;

  /** Signal audio sender thread to exit */
  atomic_bool audio_sender_should_exit;

  /** Audio capture thread handle */
  asciichat_thread_t audio_capture_thread;

  /** Audio sender thread handle */
  asciichat_thread_t audio_sender_thread;

  /** Audio capture thread was successfully created */
  bool audio_capture_thread_created;

  /** Audio sender thread was successfully created */
  bool audio_sender_thread_created;

  /** Audio capture thread has exited */
  atomic_bool audio_capture_thread_exited;

  /* ========================================================================
   * Protocol State
   * ======================================================================== */

  /** Data reception thread handle */
  asciichat_thread_t data_reception_thread;

  /** Data reception thread was successfully created */
  bool data_thread_created;

  /** Data reception thread has exited */
  atomic_bool data_thread_exited;

  /** Last active client count received from server */
  uint32_t last_active_count;

  /** Server state packet has been received and processed */
  bool server_state_initialized;

  /** Terminal should be cleared before next frame display */
  bool should_clear_before_next_frame;

  /* ========================================================================
   * Capture State
   * ======================================================================== */

  /** Webcam capture thread handle */
  asciichat_thread_t capture_thread;

  /** Capture thread was successfully created */
  bool capture_thread_created;

  /** Capture thread has exited */
  atomic_bool capture_thread_exited;

  /* ========================================================================
   * Keepalive State
   * ======================================================================== */

  /** Ping/keepalive thread handle */
  asciichat_thread_t ping_thread;

  /** Ping thread was successfully created */
  bool ping_thread_created;

  /** Ping thread has exited */
  atomic_bool ping_thread_exited;

  /* ========================================================================
   * Display State
   * ======================================================================== */

  /** Client has a TTY (not redirected output) */
  bool has_tty;

  /** This is the first frame of a new connection */
  atomic_bool is_first_frame_of_connection;

  /** TTY information and capabilities */
  tty_info_t tty_info;

  /* ========================================================================
   * Crypto State
   * ======================================================================== */

  /** Cryptographic handshake context */
  crypto_handshake_context_t crypto_ctx;

  /** Crypto has been initialized for this connection */
  bool crypto_initialized;

} tcp_client_t;

/**
 * @brief Create and initialize a TCP client instance
 *
 * Allocates a new tcp_client_t structure and initializes all fields to safe
 * defaults. This function must be called before starting any worker threads.
 *
 * ## Initialization Steps
 *
 * 1. Allocate tcp_client_t structure
 * 2. Zero-initialize all fields
 * 3. Set atomic flags to initial states
 * 4. Initialize mutexes and condition variables
 * 5. Set socket to INVALID_SOCKET_VALUE
 *
 * ## Error Handling
 *
 * Returns NULL if allocation fails or mutex initialization fails.
 * Check errno or use HAS_ERRNO() for detailed error information.
 *
 * @return Pointer to initialized client, or NULL on failure
 *
 * @note Caller must call tcp_client_destroy() when done
 * @see tcp_client_destroy() For proper cleanup
 */
tcp_client_t *tcp_client_create(void);

/**
 * @brief Destroy TCP client and free all resources
 *
 * Destroys all synchronization primitives and frees the client structure.
 * This function must be called AFTER all worker threads have been joined.
 *
 * ## Cleanup Steps
 *
 * 1. Verify all threads have exited (debug builds only)
 * 2. Destroy all mutexes and condition variables
 * 3. Free tcp_client_t structure
 * 4. Set pointer to NULL (via parameter)
 *
 * ## Thread Safety
 *
 * This function is NOT thread-safe. All worker threads must be joined
 * before calling this function.
 *
 * @param client_ptr Pointer to client pointer (set to NULL after free)
 *
 * @warning All threads must be stopped and joined before calling
 * @note No-op if client_ptr is NULL or *client_ptr is NULL
 */
void tcp_client_destroy(tcp_client_t **client_ptr);

/* ============================================================================
 * Connection State Queries
 * ============================================================================ */

/**
 * @brief Check if connection is currently active
 * @param client TCP client instance
 * @return true if connection is active, false otherwise
 */
bool tcp_client_is_active(const tcp_client_t *client);

/**
 * @brief Check if connection was lost
 * @param client TCP client instance
 * @return true if connection loss was detected, false otherwise
 */
bool tcp_client_is_lost(const tcp_client_t *client);

/**
 * @brief Get current socket descriptor
 * @param client TCP client instance
 * @return Socket descriptor or INVALID_SOCKET_VALUE if not connected
 */
socket_t tcp_client_get_socket(const tcp_client_t *client);

/**
 * @brief Get client ID assigned by server
 * @param client TCP client instance
 * @return Client ID (from local port) or 0 if not connected
 */
uint32_t tcp_client_get_id(const tcp_client_t *client);

/* ============================================================================
 * Connection Control
 * ============================================================================ */

/**
 * @brief Signal that connection was lost (triggers reconnection)
 * @param client TCP client instance
 */
void tcp_client_signal_lost(tcp_client_t *client);

/**
 * @brief Close connection gracefully
 * @param client TCP client instance
 */
void tcp_client_close(tcp_client_t *client);

/**
 * @brief Shutdown connection forcefully (for signal handlers)
 * @param client TCP client instance
 */
void tcp_client_shutdown(tcp_client_t *client);

/**
 * @brief Cleanup connection resources
 * @param client TCP client instance
 */
void tcp_client_cleanup(tcp_client_t *client);

/* ============================================================================
 * Connection Establishment
 * ============================================================================ */

/**
 * @brief Establish TCP connection to server
 *
 * Performs full connection lifecycle including DNS resolution, socket creation,
 * connection with timeout, and socket configuration. Does NOT perform crypto
 * handshake or send initial packets - those are application responsibilities.
 *
 * @param client TCP client instance
 * @param address Server hostname or IP address
 * @param port Server port number
 * @param reconnect_attempt Current reconnection attempt (0 for first, 1+ for retries)
 * @param first_connection True if this is the very first connection since program start
 * @param has_ever_connected True if client has successfully connected at least once
 * @return 0 on success, negative on error
 */
int tcp_client_connect(tcp_client_t *client, const char *address, int port, int reconnect_attempt,
                       bool first_connection, bool has_ever_connected);

/* ============================================================================
 * Thread-Safe Packet Transmission
 * ============================================================================ */

/**
 * @brief Send packet with thread-safe mutex protection
 *
 * All packet transmission goes through this function to ensure packets
 * aren't interleaved on the wire. Automatically handles encryption if
 * crypto context is ready.
 *
 * @param client TCP client instance
 * @param type Packet type identifier
 * @param data Packet payload
 * @param len Payload length
 * @return 0 on success, negative on error
 */
int tcp_client_send_packet(tcp_client_t *client, packet_type_t type, const void *data, size_t len);

/**
 * @brief Send ping packet
 * @param client TCP client instance
 * @return 0 on success, negative on error
 */
int tcp_client_send_ping(tcp_client_t *client);

/**
 * @brief Send pong packet
 * @param client TCP client instance
 * @return 0 on success, negative on error
 */
int tcp_client_send_pong(tcp_client_t *client);

/* ============================================================================
 * Advanced Packet Sending Functions
 * ============================================================================ */

/**
 * @brief Send Opus-encoded audio frame
 * @param client TCP client instance
 * @param opus_data Opus-encoded audio data
 * @param opus_size Size of encoded frame
 * @param sample_rate Sample rate in Hz
 * @param frame_duration Frame duration in milliseconds
 * @return 0 on success, negative on error
 */
int tcp_client_send_audio_opus(tcp_client_t *client, const uint8_t *opus_data, size_t opus_size, int sample_rate,
                               int frame_duration);

/**
 * @brief Send Opus audio batch packet
 * @param client TCP client instance
 * @param opus_data Opus-encoded audio data (multiple frames)
 * @param opus_size Total size of Opus data
 * @param frame_sizes Array of individual frame sizes
 * @param frame_count Number of frames in batch
 * @return 0 on success, negative on error
 */
int tcp_client_send_audio_opus_batch(tcp_client_t *client, const uint8_t *opus_data, size_t opus_size,
                                     const uint16_t *frame_sizes, int frame_count);

/**
 * @brief Send terminal capabilities packet
 * @param client TCP client instance
 * @param width Terminal width in characters
 * @param height Terminal height in characters
 * @return 0 on success, negative on error
 */
int tcp_client_send_terminal_capabilities(tcp_client_t *client, unsigned short width, unsigned short height);

/**
 * @brief Send client join packet
 * @param client TCP client instance
 * @param display_name Client display name
 * @param capabilities Client capability flags
 * @return 0 on success, negative on error
 */
int tcp_client_send_join(tcp_client_t *client, const char *display_name, uint32_t capabilities);

/**
 * @brief Send stream start packet
 * @param client TCP client instance
 * @param stream_type Type of stream (audio/video)
 * @return 0 on success, negative on error
 */
int tcp_client_send_stream_start(tcp_client_t *client, uint32_t stream_type);

/**
 * @brief Send audio batch packet
 * @param client TCP client instance
 * @param samples Audio sample buffer
 * @param num_samples Number of samples in buffer
 * @param batch_count Number of chunks in batch
 * @return 0 on success, negative on error
 */
int tcp_client_send_audio_batch(tcp_client_t *client, const float *samples, int num_samples, int batch_count);

#endif /* NETWORK_TCP_CLIENT_H */
