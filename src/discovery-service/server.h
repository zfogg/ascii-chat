#pragma once
#include <ascii-chat/crypto/sizes.h>
#include <ascii-chat/network/consensus/handoff.h>
#include <ascii-chat/network/connection.h>
#include <ascii-chat/network/negotiation.h>

extern const network_connection_callbacks_t acds_network_callbacks;

/**
 * @file acds/server.h
 * @brief 🌐 Discovery server TCP connection manager
 *
 * Implements the core TCP server for the discovery service. Handles
 * client connections, crypto handshakes, and dispatches packets to
 * appropriate handlers (session management, WebRTC signaling).
 *
 * ## Architecture
 *
 * - Main thread accepts connections on TCP socket
 * - Per-client handler threads process ACIP packets
 * - Reuses lib/network/ for packet I/O
 * - Session registry protected by rwlock
 * - SQLite for persistence
 *
 * ## Lifecycle
 *
 * 1. acds_server_init() - Create server, load identity, open database
 * 2. acds_server_run() - Accept connections, spawn handlers
 * 3. acds_server_shutdown() - Close sockets, stop threads, cleanup
 */

#include <stdint.h>
#include <stdbool.h>
#include <sqlite3.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/network/acip/acds.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/common/limits.h>
#include <ascii-chat/thread_pool.h>
#include <ascii-chat/crypto/handshake/common.h>
#include "discovery-service/main.h"
#include <ascii-chat/discovery/key_set.h>
#include <ascii-chat/discovery/session.h> // For host_lost_candidate_t and participant storage

/**
 * @brief Per-client connection data
 *
 * Stored in network connection registry to track which session and
 * participant this connection represents. Used by signaling relay
 * to map participant_id to connection for message delivery.
 *
 * Multiple-key creation is an ordered, counted sequence of Ed25519 proofs.
 * No session is persisted until the entire sequence has passed validation.
 */
typedef struct {
  uint8_t session_id[SESSION_ID_SIZE];     ///< Session UUID (valid if joined_session)
  uint8_t participant_id[SESSION_ID_SIZE]; ///< Participant UUID (valid if joined_session)
  bool joined_session;                     ///< Whether client has successfully joined a session

  // Crypto handshake state
  network_negotiation_t negotiation;

  // Multi-key session creation state
  uint8_t create_transport_metadata[521];
  size_t create_transport_length;
  acds_create_key_set_t create_keys; ///< Pending connection-local key set

  // Transport abstraction
  acip_transport_t *transport; ///< Persistent transport for this client connection

  // Protocol state
  bool session_established; ///< True after SESSION_CREATE/SESSION_JOIN completes; handler exits loop
} acds_client_data_t;

/**
 * @brief Network statistics for both TCP and WebSocket connections
 */
typedef struct {
  atomic_t bytes_sent;       ///< Total bytes sent (TCP + WebSocket)
  atomic_t bytes_received;   ///< Total bytes received (TCP + WebSocket)
  atomic_t packets_sent;     ///< Total packets sent (TCP + WebSocket)
  atomic_t packets_received; ///< Total packets received (TCP + WebSocket)
  time_t start_time;         ///< Server start time
} acds_stats_t;

/**
 * @brief Discovery server state
 *
 * Contains all runtime state for the discovery server including
 * network sockets, identity keys, and database. Sessions are stored
 * directly in SQLite as the single source of truth.
 */
typedef struct {
  network_server_t *network_server; ///< Network runtime owned by server_like
  mutex_t handoff_mutex;
  struct acds_handoff_entry *handoffs;

  // Identity
  uint8_t identity_public[ED25519_PUBLIC_KEY_SIZE];  ///< Ed25519 public key
  uint8_t identity_secret[ED25519_PRIVATE_KEY_SIZE]; ///< Ed25519 secret key

  // Persistence (SQLite as single source of truth for sessions)
  sqlite3 *db; ///< SQLite database handle

  // Rate limiting
  struct rate_limiter_s *rate_limiter; ///< SQLite-backed rate limiter

  // Background worker threads (cleanup, etc.)
  thread_pool_t *worker_pool; ///< Thread pool for background workers
  atomic_t shutdown;          ///< Shutdown flag for worker threads

  // Statistics (TCP + WebSocket)
  acds_stats_t stats; ///< Network statistics for all connections

  // Configuration
  acds_config_t config;           ///< Runtime configuration
  bool handoff_mutex_initialized; ///< Whether shutdown owns the handoff mutex
} acds_server_t;

/**
 * @brief Initialize discovery server
 *
 * Loads or generates identity keys, opens database, creates session
 * registry, and binds TCP socket.
 *
 * @param server Server structure to initialize
 * @param config Configuration from command-line parsing
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t acds_server_init(acds_server_t *server, const acds_config_t *config);

/**
 * @brief Shutdown discovery server
 *
 * Closes listen socket, stops accepting connections, waits for
 * handler threads to exit, closes database, and frees resources.
 *
 * @param server Server structure to clean up
 */
void acds_server_shutdown(acds_server_t *server);

// Host authority delivery; the transport must carry its joined connection identity.
void acds_on_host_announcement(const acip_host_announcement_t *, acip_transport_t *, const char *, void *);
void acds_send_current_host(acds_server_t *, acip_transport_t *, const uint8_t[16]);

void acds_on_host_announcement_ex(const acip_host_announcement_t *announcement, const void *metadata,
                                  size_t metadata_length, acip_transport_t *transport, const char *client_ip,
                                  void *app_ctx);
