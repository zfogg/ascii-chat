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

static client_connection_t *active_connection;

client_connection_t *client_connection_current(void) {
  return active_connection;
}

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

asciichat_error_t client_connection_create(client_connection_t **out) {
  if (!out || active_connection) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Previous client connection has not been released");
  }
  *out = SAFE_CALLOC(1, sizeof(client_connection_t), client_connection_t *);
  if (!*out)
    return SET_ERRNO(ERROR_MEMORY, "Could not allocate client connection");
  client_connection_t *connection = *out;
  active_connection = connection;
  connection->owner_thread = asciichat_thread_self();
  atomic_store_int(&connection->state, CLIENT_CONNECTION_STARTING);
  connection->sockfd = INVALID_SOCKET_VALUE;
  connection->crypto_mode = ACIP_CRYPTO_ENCRYPT;
  asciichat_error_t result = connection_context_init(&connection->attempt);
  if (result == ASCIICHAT_OK && server_connection_init() != 0)
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
  if (!connection || connection != active_connection ||
      atomic_load_int(&connection->state) != CLIENT_CONNECTION_STARTING) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Connection is not ready for startup");
  }
  asciichat_error_t result = connection_attempt_tcp(&connection->attempt, address, port);
  if (result != ASCIICHAT_OK)
    return result;
  if (should_exit())
    return SET_ERRNO(ERROR_NETWORK, "Connection startup interrupted");
  if (!connection->attempt.connection.transport)
    return SET_ERRNO(ERROR_NETWORK, "Connection has no transport");
  server_connection_set_transport(connection->attempt.connection.transport);
  result = client_connection_checkpoint("transport");
  if (result != ASCIICHAT_OK)
    return result;
  if (protocol_start_connection(connection) != 0)
    return ERROR_NETWORK;
  if (atomic_load_bool(&connection->stop_requested))
    return SET_ERRNO(ERROR_NETWORK, "Connection stopped during startup");
  atomic_store_int(&connection->state, CLIENT_CONNECTION_RUNNING);
  log_info("CLIENT_CONNECTION_RUNNING");
  return ASCIICHAT_OK;
}

void client_connection_request_stop(client_connection_t *connection) {
  if (!connection || atomic_load_int(&connection->state) == CLIENT_CONNECTION_STOPPED)
    return;
  atomic_store_int(&connection->state, CLIENT_CONNECTION_STOPPING);
  atomic_store_bool(&connection->stop_requested, true);
  /* Interrupt without the send mutex: a blocked sender may own that mutex. */
  server_connection_shutdown();
}

asciichat_error_t client_connection_stop(client_connection_t *connection) {
  if (!connection || atomic_load_int(&connection->state) == CLIENT_CONNECTION_STOPPED)
    return ASCIICHAT_OK;
  if (!asciichat_thread_equal(connection->owner_thread, asciichat_thread_self())) {
    client_connection_request_stop(connection);
    return SET_ERRNO(ERROR_INVALID_STATE, "Connection cleanup must run on its controlling thread");
  }
  log_debug("CLIENT_CONNECTION_STOP_BEGIN");
  client_connection_request_stop(connection);
  log_debug("CLIENT_CONNECTION_IO_INTERRUPTED");
  audio_sender_cleanup();
  audio_stop_thread();
  audio_context_t *audio = audio_get_context();
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
  audio_sender_finalize();
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
  crypto_client_cleanup();
  NAMED_UNREGISTER(&connection->crypto_ctx);
  atomic_store_int(&connection->state, CLIENT_CONNECTION_STOPPED);
  return ASCIICHAT_OK;
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
  active_connection = NULL;
  SAFE_FREE(*owner);
  *owner = NULL;
  return ASCIICHAT_OK;
}
