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
#include "common.h"
#include "platform/abstraction.h"
#include "platform/socket.h"
#include "network/tcp/server.h"
#include "thread_pool.h"
#include "acds/main.h"
#include "acds/session.h"

/**
 * @brief Per-client connection data
 *
 * Stored in tcp_server client registry to track which session and
 * participant this connection represents. Used by signaling relay
 * to map participant_id → socket for message delivery.
 */
typedef struct {
  uint8_t session_id[16];     ///< Session UUID (valid if joined_session)
  uint8_t participant_id[16]; ///< Participant UUID (valid if joined_session)
  bool joined_session;        ///< Whether client has successfully joined a session
} acds_client_data_t;

/**
 * @brief Discovery server state
 *
 * Contains all runtime state for the discovery server including
 * network sockets, identity keys, session registry, and database.
 */
typedef struct {
  tcp_server_t tcp_server; ///< TCP server abstraction

  // Identity
  uint8_t identity_public[32]; ///< Ed25519 public key
  uint8_t identity_secret[64]; ///< Ed25519 secret key

  // Session management
  session_registry_t *sessions; ///< In-memory session registry

  // Persistence
  sqlite3 *db; ///< SQLite database handle

  // Rate limiting
  struct rate_limiter_s *rate_limiter; ///< SQLite-backed rate limiter

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
