#pragma once

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
#include <ascii-chat/network/tcp/server.h>
#include <ascii-chat/network/acip/acds.h>
#include <ascii-chat/options/options.h> // For MAX_IDENTITY_KEYS
#include <ascii-chat/thread_pool.h>
#include <ascii-chat/crypto/handshake/common.h>
#include "discovery-service/main.h"
#include <ascii-chat/discovery/session.h> // For host_lost_candidate_t and MAX_PARTICIPANTS

/**
 * @brief Per-client connection data
 *
 * Stored in tcp_server client registry to track which session and
 * participant this connection represents. Used by signaling relay
 * to map participant_id → socket for message delivery.
 *
 * Multi-Key Session Creation Protocol:
 * =====================================
 * When creating a session with multiple identity keys (e.g., SSH + GPG):
 * 1. Client sends SESSION_CREATE with first key (creates session UUID)
 * 2. Client sends SESSION_CREATE with second key (adds to same session)
 * 3. Client sends SESSION_CREATE with zero key (finalizes session)
 *
 * During multi-key creation:
 * - in_multikey_session_create = true
 * - All keys stored in pending_session_keys[]
 * - Only PING/PONG allowed, other messages blocked
 * - Keys validated to ensure no duplicates
 */
typedef struct {
  uint8_t session_id[16];     ///< Session UUID (valid if joined_session)
  uint8_t participant_id[16]; ///< Participant UUID (valid if joined_session)
  bool joined_session;        ///< Whether client has successfully joined a session

  // Crypto handshake state
  crypto_handshake_context_t handshake_ctx; ///< Handshake context for encrypted communication
  bool handshake_complete;                  ///< Whether crypto handshake has completed

  // Multi-key session creation state
  bool in_multikey_session_create;                     ///< True during multi-key SESSION_CREATE sequence
  acip_session_create_t pending_session;               ///< Pending session data (from first SESSION_CREATE)
  uint8_t pending_session_keys[MAX_IDENTITY_KEYS][32]; ///< Array of identity public keys
  size_t num_pending_keys;                             ///< Number of keys received so far
} acds_client_data_t;

/**
 * @brief In-memory host migration context
 *
 * Tracks migration timeout for sessions undergoing host failover.
 * Used by monitor_host_migrations() to detect stalled migrations and timeout.
 *
 * NOTE: Election happens proactively (host picks future host every 5 minutes).
 * No candidate collection or election needed here - just timeout tracking.
 */
typedef struct {
  uint8_t session_id[16];      ///< Session UUID
  uint64_t migration_start_ns; ///< When migration started (nanoseconds since sokol_time setup)
} migration_context_t;

/**
 * @brief Discovery server state
 *
 * Contains all runtime state for the discovery server including
 * network sockets, identity keys, and database. Sessions are stored
 * directly in SQLite as the single source of truth.
 */
typedef struct {
  tcp_server_t tcp_server; ///< TCP server abstraction

  // Identity
  uint8_t identity_public[32]; ///< Ed25519 public key
  uint8_t identity_secret[64]; ///< Ed25519 secret key

  // Persistence (SQLite as single source of truth for sessions)
  sqlite3 *db; ///< SQLite database handle

  // Rate limiting
  struct rate_limiter_s *rate_limiter; ///< SQLite-backed rate limiter

  // Host migration tracking (in-memory during active migrations)
  migration_context_t active_migrations[32]; ///< Slots for up to 32 concurrent migrations
  size_t num_active_migrations;              ///< Number of active migrations

  // Background worker threads (cleanup, etc.)
  thread_pool_t *worker_pool; ///< Thread pool for background workers
  atomic_bool shutdown;       ///< Shutdown flag for worker threads

  // Configuration
  acds_config_t config; ///< Runtime configuration
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
 * @brief Run discovery server main loop
 *
 * Accepts client connections and spawns handler threads. Blocks
 * until shutdown signal received.
 *
 * @param server Initialized server structure
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t acds_server_run(acds_server_t *server);

/**
 * @brief Shutdown discovery server
 *
 * Closes listen socket, stops accepting connections, waits for
 * handler threads to exit, closes database, and frees resources.
 *
 * @param server Server structure to clean up
 */
void acds_server_shutdown(acds_server_t *server);

/**
 * @brief Per-client connection handler (thread entry point)
 *
 * Processes ACIP packets from a connected client. Handles crypto
 * handshake, then dispatches packets to session/signaling handlers.
 *
 * @param arg Pointer to client connection context
 * @return NULL (thread exit value)
 */
void *acds_client_handler(void *arg);
