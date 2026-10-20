#include <ascii-chat/network/negotiation.h>
#include "connection_internal.h"
#include "server.h"
#include "audio.h"
#include "crypto.h"
#include "protocol_adapter.h"
#include "../main.h"
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/debug/named.h>
#include <string.h>
#include <ascii-chat/options/options.h>

static void client_sync_entries(client_connection_t *ctx, bool add) {
  if (add)
    NAMED_REGISTER_ATOMIC(&ctx->display_first_frame, "client.display_first_frame", NULL);
  else
    NAMED_UNREGISTER(&ctx->display_first_frame);
  if (add)
    NAMED_REGISTER_ATOMIC(&ctx->lifecycle.state, "client.lifecycle.state", NULL);
  else
    NAMED_UNREGISTER(&ctx->lifecycle.state);
  if (add)
    NAMED_REGISTER_ATOMIC(&ctx->stop_requested, "client.stop_requested", NULL);
  else
    NAMED_UNREGISTER(&ctx->stop_requested);
  if (add)
    NAMED_REGISTER_ATOMIC(&ctx->connection_active, "client.connection_active", NULL);
  else
    NAMED_UNREGISTER(&ctx->connection_active);
  if (add)
    NAMED_REGISTER_ATOMIC(&ctx->remote_error, "client.remote_error", NULL);
  else
    NAMED_UNREGISTER(&ctx->remote_error);
  if (add)
    NAMED_REGISTER_ATOMIC(&ctx->connection_lost, "client.connection_lost", NULL);
  else
    NAMED_UNREGISTER(&ctx->connection_lost);
  if (add)
    NAMED_REGISTER_ATOMIC(&ctx->data_thread_exited, "client.data_thread_exited", NULL);
  else
    NAMED_UNREGISTER(&ctx->data_thread_exited);
  if (add)
    NAMED_REGISTER_ATOMIC(&ctx->frames_rendered, "client.frames_rendered", NULL);
  else
    NAMED_UNREGISTER(&ctx->frames_rendered);
  if (add)
    NAMED_REGISTER_ATOMIC(&ctx->snapshot_first_frame_time_ns, "client.snapshot_first_frame_time_ns", NULL);
  else
    NAMED_UNREGISTER(&ctx->snapshot_first_frame_time_ns);
  if (add)
    NAMED_REGISTER_ATOMIC(&ctx->capture_thread_exited, "client.capture_thread_exited", NULL);
  else
    NAMED_UNREGISTER(&ctx->capture_thread_exited);
  if (add)
    NAMED_REGISTER_ATOMIC(&ctx->audio_capture_thread_exited, "client.audio_capture_thread_exited", NULL);
  else
    NAMED_UNREGISTER(&ctx->audio_capture_thread_exited);
  if (add)
    NAMED_REGISTER_ATOMIC(&ctx->audio_sender_should_exit, "client.audio_sender_should_exit", NULL);
  else
    NAMED_UNREGISTER(&ctx->audio_sender_should_exit);
  if (add)
    NAMED_REGISTER_ATOMIC(&ctx->audio_sender_exited, "client.audio_sender_exited", NULL);
  else
    NAMED_UNREGISTER(&ctx->audio_sender_exited);
  if (add)
    NAMED_REGISTER_ATOMIC(&ctx->ping_thread_exited, "client.ping_thread_exited", NULL);
  else
    NAMED_UNREGISTER(&ctx->ping_thread_exited);
}

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

asciichat_error_t client_connection_create(client_connection_t **out) {
  if (!out) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Missing client connection output");
  }
  *out = SAFE_CALLOC(1, sizeof(client_connection_t), client_connection_t *);
  if (!*out)
    return SET_ERRNO(ERROR_MEMORY, "Could not allocate client connection");
  client_connection_t *connection = *out;
  client_sync_entries(connection, true);
  session_connection_init(&connection->lifecycle,
                          (session_connection_ops_t){client_interrupt, client_join, client_release}, connection);
  connection->crypto_mode = ACIP_CRYPTO_ENCRYPT;
  asciichat_error_t result = ASCIICHAT_OK;
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
  connection_endpoint_t endpoint;
  asciichat_error_t result = network_connection_open("client", address, port, NULL, &connection->network, &endpoint);
  if (result != ASCIICHAT_OK)
    return result;
  connection->transport = network_connection_transport(connection->network);
  server_connection_set_ip(connection, endpoint.host);
  server_connection_set_port(connection, endpoint.port);
  result = client_crypto_init(connection);
  if (result != ASCIICHAT_OK)
    return result;
  result = network_client_negotiate(connection->transport, connection->crypto_ctx, connection->crypto_mode,
                                    NETWORK_PROTOCOL_MEDIA);
  if (result != ASCIICHAT_OK)
    return result;
  network_connection_ready(connection->network);
  if (should_exit())
    return SET_ERRNO(ERROR_NETWORK, "Connection startup interrupted");
  server_connection_set_transport(connection, connection->transport);
  result = client_connection_checkpoint("transport");
  if (result != ASCIICHAT_OK)
    return result;
  result = client_protocol_start(connection);
  if (result != ASCIICHAT_OK)
    return result;
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
  session_capture_set_audio_context(connection->capture_context, NULL);
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
  buffer_workspace_destroy(&connection->encoded_video_workspace);
  buffer_workspace_destroy(&connection->decoded_ascii_workspace);
  /* The shared owner drains callbacks before releasing transport and security. */
  log_debug("CLIENT_CONNECTION_TRANSPORT_CLEANUP");
  network_connection_destroy(&connection->network);
  log_debug("CLIENT_CONNECTION_TRANSPORT_RELEASED");
  connection->transport = NULL;
  connection->crypto_ctx = NULL;
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
  client_sync_entries(connection, false);
  SAFE_FREE(*owner);
  *owner = NULL;
  return ASCIICHAT_OK;
}
