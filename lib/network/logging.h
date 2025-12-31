#pragma once

/**
 * @file lib/network/logging.h
 * @brief Network logging macros and remote log direction enumeration.
 * @ingroup logging
 * @addtogroup logging
 * @{
 */

/**
 * @brief Remote log packet direction enumeration.
 *
 * Indicates the originator of a remote log message so receivers can annotate
 * logs clearly.
 *
 * @note This typedef MUST be defined before includes to avoid circular
 *       dependency issues with packet.h
 */
typedef enum remote_log_direction {
  REMOTE_LOG_DIRECTION_UNKNOWN = 0,
  REMOTE_LOG_DIRECTION_SERVER_TO_CLIENT = 1,
  REMOTE_LOG_DIRECTION_CLIENT_TO_SERVER = 2
} remote_log_direction_t;

#include "logging/logging.h"

// Forward declarations to avoid circular includes with crypto/handshake.h
struct crypto_handshake_context_t;
const struct crypto_context_t *crypto_handshake_get_context(const struct crypto_handshake_context_t *ctx);

/* ============================================================================
 * Network Logging Macros
 * ============================================================================
 * Convenience macros for sending log messages over the network.
 *
 * Server-side (logging TO a client):
 *   log_info_client(client, "Hello %s", name);
 *   - Takes client_info_t*, direction is automatically SERVER_TO_CLIENT
 *   - Only sends if client->crypto_initialized is true
 *
 * Client-side (logging TO the server):
 *   log_info_server(socket, crypto_ctx, "Hello %s", name);
 *   - Takes socket and crypto context directly
 *   - Direction is automatically CLIENT_TO_SERVER
 */

/* Server-side: log TO client (takes client_info_t*) */
#ifdef NDEBUG
#define LOG_CLIENT_IMPL(client, level, fmt, ...)                                                                       \
  do {                                                                                                                 \
    if ((client)->crypto_initialized) {                                                                                \
      const struct crypto_context_t *_ctx = crypto_handshake_get_context(&(client)->crypto_handshake_ctx);             \
      log_net_message((client)->socket, _ctx, level, REMOTE_LOG_DIRECTION_SERVER_TO_CLIENT, NULL, 0, NULL, fmt,        \
                      ##__VA_ARGS__);                                                                                  \
    }                                                                                                                  \
  } while (0)
#else
#define LOG_CLIENT_IMPL(client, level, fmt, ...)                                                                       \
  do {                                                                                                                 \
    if ((client)->crypto_initialized) {                                                                                \
      const struct crypto_context_t *_ctx = crypto_handshake_get_context(&(client)->crypto_handshake_ctx);             \
      log_net_message((client)->socket, _ctx, level, REMOTE_LOG_DIRECTION_SERVER_TO_CLIENT, __FILE__, __LINE__,        \
                      __func__, fmt, ##__VA_ARGS__);                                                                   \
    }                                                                                                                  \
  } while (0)
#endif

/** @brief Server sends DEBUG log message to client */
#define log_debug_client(client, fmt, ...) LOG_CLIENT_IMPL(client, LOG_DEBUG, fmt, ##__VA_ARGS__)

/** @brief Server sends INFO log message to client */
#define log_info_client(client, fmt, ...) LOG_CLIENT_IMPL(client, LOG_INFO, fmt, ##__VA_ARGS__)

/** @brief Server sends WARN log message to client */
#define log_warn_client(client, fmt, ...) LOG_CLIENT_IMPL(client, LOG_WARN, fmt, ##__VA_ARGS__)

/** @brief Server sends ERROR log message to client */
#define log_error_client(client, fmt, ...) LOG_CLIENT_IMPL(client, LOG_ERROR, fmt, ##__VA_ARGS__)

/** @brief Server sends FATAL log message to client */
#define log_fatal_client(client, fmt, ...) LOG_CLIENT_IMPL(client, LOG_FATAL, fmt, ##__VA_ARGS__)

/* Client-side: log TO server (takes socket + crypto_ctx) */
#ifdef NDEBUG
#define LOG_SERVER_IMPL(sockfd, crypto_ctx, level, fmt, ...)                                                           \
  log_net_message(sockfd, (const struct crypto_context_t *)(crypto_ctx), level, REMOTE_LOG_DIRECTION_CLIENT_TO_SERVER, \
                  NULL, 0, NULL, fmt, ##__VA_ARGS__)
#else
#define LOG_SERVER_IMPL(sockfd, crypto_ctx, level, fmt, ...)                                                           \
  log_net_message(sockfd, (const struct crypto_context_t *)(crypto_ctx), level, REMOTE_LOG_DIRECTION_CLIENT_TO_SERVER, \
                  __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#endif

/** @brief Client sends DEBUG log message to server */
#define log_debug_server(sockfd, crypto_ctx, fmt, ...)                                                                 \
  LOG_SERVER_IMPL(sockfd, crypto_ctx, LOG_DEBUG, fmt, ##__VA_ARGS__)

/** @brief Client sends INFO log message to server */
#define log_info_server(sockfd, crypto_ctx, fmt, ...) LOG_SERVER_IMPL(sockfd, crypto_ctx, LOG_INFO, fmt, ##__VA_ARGS__)

/** @brief Client sends WARN log message to server */
#define log_warn_server(sockfd, crypto_ctx, fmt, ...) LOG_SERVER_IMPL(sockfd, crypto_ctx, LOG_WARN, fmt, ##__VA_ARGS__)

/** @brief Client sends ERROR log message to server */
#define log_error_server(sockfd, crypto_ctx, fmt, ...)                                                                 \
  LOG_SERVER_IMPL(sockfd, crypto_ctx, LOG_ERROR, fmt, ##__VA_ARGS__)

/** @brief Client sends FATAL log message to server */
#define log_fatal_server(sockfd, crypto_ctx, fmt, ...)                                                                 \
  LOG_SERVER_IMPL(sockfd, crypto_ctx, LOG_FATAL, fmt, ##__VA_ARGS__)

/** @} */
