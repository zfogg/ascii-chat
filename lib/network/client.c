/**
 * @file network/client.c
 * @brief Client application state implementation
 *
 * Implements app_client_create() and app_client_destroy() for managing
 * the application-layer state of ASCII chat clients.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#include <ascii-chat/network/client.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/platform/abstraction.h>

#include <string.h>
#include <stdatomic.h>

/**
 * @brief Create and initialize client application context
 */
app_client_t *app_client_create(void) {
  app_client_t *client = SAFE_MALLOC(sizeof(app_client_t), app_client_t *);
  if (!client) {
    log_error("Failed to allocate app_client_t");
    return NULL;
  }

  // Zero-initialize all fields
  memset(client, 0, sizeof(*client));

  /* Transport */
  client->active_transport = NULL;
  client->transport_type = ACIP_TRANSPORT_TCP;
  client->tcp_client = NULL;
  client->ws_client = NULL;

  /* Audio State */
  memset(&client->audio_ctx, 0, sizeof(client->audio_ctx));
  memset(client->audio_send_queue, 0, sizeof(client->audio_send_queue));
  client->audio_send_queue_head = 0;
  client->audio_send_queue_tail = 0;
  client->audio_send_queue_initialized = false;
  atomic_store(&client->audio_sender_should_exit, false);
  client->audio_capture_thread_created = false;
  client->audio_sender_thread_created = false;
  atomic_store(&client->audio_capture_thread_exited, false);

  // Initialize audio queue mutex and condition variable
  if (mutex_init(&client->audio_send_queue_mutex, "audio_queue") != 0) {
    log_error("Failed to initialize audio queue mutex");
    SAFE_FREE(client);
    return NULL;
  }

  if (cond_init(&client->audio_send_queue_cond, "audio_queue") != 0) {
    log_error("Failed to initialize audio queue cond");
    mutex_destroy(&client->audio_send_queue_mutex);
    SAFE_FREE(client);
    return NULL;
  }

  /* Protocol State */
  client->data_thread_created = false;
  atomic_store(&client->data_thread_exited, false);
  client->last_active_count = 0;
  client->server_state_initialized = false;
  client->should_clear_before_next_frame = false;
  client->my_client_id = 0;
  client->encryption_enabled = false;

  /* Capture State */
  client->capture_thread_created = false;
  atomic_store(&client->capture_thread_exited, false);

  /* Keepalive State */
  client->ping_thread_created = false;
  atomic_store(&client->ping_thread_exited, false);

  /* Display State */
  client->has_tty = false;
  atomic_store(&client->is_first_frame_of_connection, false);
  memset(&client->tty_info, 0, sizeof(client->tty_info));

  /* Crypto State */
  memset(&client->crypto_ctx, 0, sizeof(client->crypto_ctx));
  client->crypto_initialized = false;

  log_debug("App client created");

  return client;
}

/**
 * @brief Destroy client application context and free all resources
 */
void app_client_destroy(app_client_t **client_ptr) {
  if (!client_ptr || !*client_ptr) {
    return; // No-op if NULL
  }

  app_client_t *client = *client_ptr;

  log_debug("Destroying app client");

  // Destroy mutexes and condition variables
  mutex_destroy(&client->audio_send_queue_mutex);
  cond_destroy(&client->audio_send_queue_cond);

  // Note: Network clients (tcp_client, ws_client) are not destroyed here.
  // They should be destroyed separately in connection_context_cleanup()
  // to maintain proper lifecycle management.

  SAFE_FREE(*client_ptr);
}
