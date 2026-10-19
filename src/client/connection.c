#include "connection_internal.h"
#include "server.h"
#include "audio.h"
#include "crypto.h"
#include "protocol.h"
#include "../main.h"
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/debug/named.h>
#include <string.h>

static void client_interrupt(void *context);
static asciichat_error_t client_join(void *context);
static void client_release(void *context);

asciichat_error_t client_connection_checkpoint(const char *stage) {
#ifndef NDEBUG
  const char *failure = SAFE_GETENV("ASCII_CHAT_TEST_CLIENT_FAIL_STAGE");
  if (failure && strcmp(failure, stage) == 0) {
    log_debug("Injected client startup failure after %s", stage);
    return SET_ERRNO(ERROR_THREAD, "Injected client startup failure after %s", stage);
  }
#else
  (void)stage;
#endif
  return ASCIICHAT_OK;
}

static void attempt_set_ip(void *context, const char *ip) {
  server_connection_set_ip(context, ip);
}
static void attempt_set_port(void *context, int port) {
  server_connection_set_port(context, port);
}
static void attempt_set_mode(void *context, uint8_t mode) {
  client_crypto_set_mode(context, mode);
}
static int attempt_init(void *context) {
  return client_crypto_init(context);
}
static asciichat_error_t attempt_handshake(void *context, acip_transport_t *transport) {
  return client_crypto_handshake(context, transport);
}
static bool attempt_is_ready(void *context) {
  return crypto_client_is_ready(context);
}
static const crypto_context_t *attempt_get_context(void *context) {
  return crypto_client_get_context(context);
}

asciichat_error_t client_connection_create(client_connection_t **out) {
  if (!out) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Missing client connection output");
  }
  *out = SAFE_CALLOC(1, sizeof(client_connection_t), client_connection_t *);
  if (!*out)
    return SET_ERRNO(ERROR_MEMORY, "Could not allocate client connection");
  client_connection_t *connection = *out;
  session_connection_init(&connection->lifecycle,
                          (session_connection_ops_t){client_interrupt, client_join, client_release}, connection);
  connection->sockfd = INVALID_SOCKET_VALUE;
  connection->crypto_mode = ACIP_CRYPTO_ENCRYPT;
  asciichat_error_t result = connection_context_init(&connection->attempt);
  connection->attempt.context = connection;
  connection->attempt.ops =
      (connection_attempt_ops_t){attempt_set_ip,    attempt_set_port, attempt_set_mode,   attempt_init,
                                 attempt_handshake, attempt_is_ready, attempt_get_context};
  if (result == ASCIICHAT_OK && server_connection_init(connection) != 0)
    result = ERROR_THREAD;
  if (result == ASCIICHAT_OK)
    result = client_connection_checkpoint("send-mutex");
  if (result == ASCIICHAT_OK) {
    connection->client_worker_pool = thread_pool_create("client_connection");
    if (!connection->client_worker_pool)
      result = SET_ERRNO(ERROR_THREAD, "Could not create connection worker pool");
  }
  if (result == ASCIICHAT_OK)
    result = client_connection_checkpoint("worker-pool");
  if (result != ASCIICHAT_OK) {
    asciichat_error_t cleanup = client_connection_destroy(out);
    if (cleanup != ASCIICHAT_OK)
      return cleanup;
  }
  if (result == ASCIICHAT_OK)
    log_info("CLIENT_CONNECTION_CREATED: workers=0 frames=0");
  return result;
}

asciichat_error_t client_connection_start(client_connection_t *connection, const char *address, uint16_t port) {
  if (!connection || atomic_load_int(&connection->lifecycle.state) != SESSION_CONNECTION_STARTING) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Connection is not ready for startup");
  }
  asciichat_error_t result = connection_attempt_tcp(&connection->attempt, address, port);
  if (result != ASCIICHAT_OK)
    return result;
  if (should_exit())
    return SET_ERRNO(ERROR_NETWORK, "Connection startup interrupted");
  if (!connection->attempt.connection.transport)
    return SET_ERRNO(ERROR_NETWORK, "Connection has no transport");
  server_connection_set_transport(connection, connection->attempt.connection.transport);
  result = client_connection_checkpoint("transport");
  if (result != ASCIICHAT_OK)
    return result;
  if (protocol_start_connection(connection) != 0)
    return ERROR_NETWORK;
  if (atomic_load_bool(&connection->stop_requested))
    return SET_ERRNO(ERROR_NETWORK, "Connection stopped during startup");
  result = session_connection_start(&connection->lifecycle);
  if (result != ASCIICHAT_OK)
    return result;
  log_info("CLIENT_CONNECTION_RUNNING");
  return ASCIICHAT_OK;
}

void client_connection_request_stop(client_connection_t *connection) {
  if (connection)
    session_connection_request_stop(&connection->lifecycle);
}

static void client_interrupt(void *context) {
  client_connection_t *connection = context;
  atomic_store_bool(&connection->stop_requested, true);
  server_connection_shutdown(connection);
}

asciichat_error_t client_connection_stop(client_connection_t *connection) {
  return connection ? session_connection_stop(&connection->lifecycle) : ASCIICHAT_OK;
}

static asciichat_error_t client_join(void *context) {
  client_connection_t *connection = context;
  log_debug("CLIENT_CONNECTION_IO_INTERRUPTED");
  audio_sender_cleanup(connection);
  audio_stop_thread(connection);
  audio_context_t *audio = connection->audio_context;
  if (audio && audio->initialized)
    audio_stop_duplex(audio);

  /* Pool entries are retained on join failure. Nothing below may free a
   * dependency until every worker has actually joined. */
  if (connection->client_worker_pool) {
    asciichat_error_t result = thread_pool_stop_all(connection->client_worker_pool);
    if (result != ASCIICHAT_OK)
      return result;
  }
  log_info("CLIENT_CONNECTION_STOPPED: workers=0 frames=%llu",
           (unsigned long long)atomic_load_u64(&connection->frames_rendered));
  return ASCIICHAT_OK;
}

static void client_release(void *context) {
  client_connection_t *connection = context;
  audio_context_t *audio = connection->audio_context;
  audio_sender_finalize(connection);
  if (audio && audio->initialized) {
    audio_set_pipeline(audio, NULL);
    audio_ring_buffer_clear(audio->capture_buffer);
    audio_flush_playback_buffers(audio);
    audio->capture_media_source = NULL;
  }
  client_audio_pipeline_destroy(connection->audio_pipeline);
  connection->audio_pipeline = NULL;
  h265_encoder_destroy(connection->video_encoder);
  connection->video_encoder = NULL;
  /* The handle destroys the transport and its event/client owner, quiescing
   * callbacks before releasing the crypto state referenced by the transport. */
  log_debug("CLIENT_CONNECTION_TRANSPORT_CLEANUP");
  connection_context_cleanup(&connection->attempt);
  log_debug("CLIENT_CONNECTION_TRANSPORT_RELEASED");
  connection->attempt.connection.transport = NULL;
  connection->sockfd = INVALID_SOCKET_VALUE;
  crypto_client_cleanup(connection);
  NAMED_UNREGISTER(&connection->crypto_ctx);
}

asciichat_error_t client_connection_destroy(client_connection_t **owner) {
  if (!owner || !*owner)
    return ASCIICHAT_OK;
  client_connection_t *connection = *owner;
  asciichat_error_t result = client_connection_stop(connection);
  if (result != ASCIICHAT_OK)
    return result;
  if (connection->client_worker_pool)
    thread_pool_destroy(connection->client_worker_pool);
  if (connection->send_mutex_initialized)
    mutex_destroy(&connection->send_mutex);
  log_info("CLIENT_CONNECTION_RELEASED");
  SAFE_FREE(*owner);
  *owner = NULL;
  return ASCIICHAT_OK;
}
