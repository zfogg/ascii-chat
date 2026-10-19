#include "connections.h"
#include "render.h"
#include "protocol.h"
#include "main.h"
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/util/time.h>

static asciichat_error_t server_connection_open(network_connection_t *connection, void *arg) {
  server_context_t *server = arg;
  if (server->rate_limiter) {
    bool allowed = false;
    asciichat_error_t result = rate_limiter_check(server->rate_limiter, network_connection_peer(connection),
                                                  RATE_EVENT_CONNECTION, NULL, &allowed);
    if (result != ASCIICHAT_OK)
      return result;
    if (!allowed)
      return SET_ERRNO(ERROR_RATE_LIMITED, "Connection rate limit exceeded");
    result = rate_limiter_record(server->rate_limiter, network_connection_peer(connection), RATE_EVENT_CONNECTION);
    if (result != ASCIICHAT_OK)
      return result;
  }
  server_connection_state_t *state = SAFE_CALLOC(1, sizeof(*state), server_connection_state_t *);
  network_connection_set_context(connection, state);
  bool adopted_ready = network_connection_state(connection) == NETWORK_READY;
  crypto_handshake_context_t *handshake = network_connection_handshake(connection);
  state->negotiation = (network_negotiation_t){
      .protocol = NETWORK_PROTOCOL_MEDIA,
      .handshake = handshake,
      .initialized = adopted_ready && crypto_handshake_is_ready(handshake),
      .encrypt_data = adopted_ready && ACIP_CRYPTO_HAS_ENCRYPT(network_connection_crypto_mode(connection)),
      .identity_keys = g_server_identity_keys,
      .identity_key_count = g_num_server_identity_keys,
      .whitelist = g_client_whitelist,
      .whitelist_count = g_num_whitelisted_clients,
      .password = GET_OPTION(password),
      .secure = network_connection_secure(connection),
      .require_auth = GET_OPTION(password)[0] || g_num_whitelisted_clients || GET_OPTION(require_server_verify),
      .require_identity = g_num_whitelisted_clients || GET_OPTION(require_server_verify),
      .require_encryption = GET_OPTION(encrypt_enabled),
      .forbid_encryption = GET_OPTION(no_encrypt)};
  return network_connection_state(connection) == NETWORK_READY
             ? server_send_state(network_connection_transport(connection))
             : ASCIICHAT_OK;
}
static asciichat_error_t server_connection_packet(network_connection_t *connection, packet_type_t type,
                                                  const void *payload, size_t length, void *arg) {
  server_context_t *server = arg;
  server_connection_state_t *state = network_connection_context(connection);
  if (network_connection_state(connection) == NETWORK_NEGOTIATING) {
    asciichat_error_t result = network_negotiation_receive(
        &state->negotiation, network_connection_transport(connection), type, payload, length);
    if (result == ASCIICHAT_OK && state->negotiation.ready) {
      network_connection_ready(connection);
      result = server_send_state(network_connection_transport(connection));
    }
    return result;
  }
  client_info_t *client = atomic_ptr_load(&state->client);
  if (!client) {
    if (type != PACKET_TYPE_CLIENT_CAPABILITIES)
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Expected initial client capabilities");
    // Authentication and capabilities precede allocation of media slots and buffers.
    client =
        add_transport_client(server, network_connection_transport(connection), network_connection_peer(connection));
    if (!client)
      return SET_ERRNO(ERROR_SESSION_FULL, "Cannot admit client");
    client->network_connection = connection;
    client->crypto_handshake_ctx = state->negotiation.handshake;
    client->crypto_initialized = state->negotiation.initialized;
    client->encrypt_data = state->negotiation.encrypt_data;
    client->transport_encrypted = network_connection_secure(connection);
    client->security_ready = true;
    atomic_ptr_store(&state->client, client);
    handle_client_capabilities_packet(client, payload, length);
    return start_transport_client_threads(server, client->client_id) == 0
               ? ASCIICHAT_OK
               : SET_ERRNO(ERROR_THREAD, "Cannot start client rendering");
  }
  return server_dispatch_packet(client, type, payload, length);
}
static asciichat_error_t server_connection_closing(network_connection_t *connection, void *arg) {
  (void)arg;
  server_connection_state_t *state = network_connection_context(connection);
  client_info_t *client = state ? atomic_ptr_load(&state->client) : NULL;
  if (!client)
    return ASCIICHAT_OK;
  atomic_store_bool(&client->active, false);
  atomic_store_bool(&client->event_ready, false);
  return stop_client_render_threads(client);
}
static void server_connection_closed(network_connection_t *connection, void *arg) {
  server_connection_state_t *state = network_connection_context(connection);
  client_info_t *client = state ? atomic_ptr_load(&state->client) : NULL;
  if (client) {
    client->transport = NULL;
    client->network_connection = NULL;
    atomic_store_bool(&client->event_cleanup, true);
    remove_client(arg, client->client_id);
  }
  SAFE_FREE(state);
  network_connection_set_context(connection, NULL);
}
const network_connection_callbacks_t server_network_callbacks = {server_connection_open, server_connection_packet,
                                                                 server_connection_closing, server_connection_closed};

void server_send_video(client_info_t *client, acip_frame_workspace_t *workspace, const char *data, size_t length,
                       uint32_t width, uint32_t height, int sources) {
  mutex_lock(&client->client_state_mutex);
  mutex_lock(&client->send_mutex);
  acip_transport_t *transport = client->transport;
  if (transport && !atomic_load_bool(&client->shutting_down) && atomic_load_bool(&client->event_ready)) {
    if (client->crypto_initialized && crypto_handshake_should_rekey(client->crypto_handshake_ctx))
      crypto_handshake_rekey_request(client->crypto_handshake_ctx, transport);
    if (sources > 0 && atomic_load_u64(&client->last_sent_grid_sources) != (uint64_t)sources) {
      acip_send_clear_console(transport);
      atomic_store_u64(&client->last_sent_grid_sources, (uint64_t)sources);
    }
    if (acip_send_ascii_frame(transport, workspace, data, length, width, height, client->client_id) == ASCIICHAT_OK)
      atomic_fetch_add_u64(&client->frames_sent_count, 1);
  }
  mutex_unlock(&client->send_mutex);
  mutex_unlock(&client->client_state_mutex);
}

asciichat_error_t server_send_audio(client_info_t *client, const void *data, size_t length) {
  mutex_lock(&client->client_state_mutex);
  mutex_lock(&client->send_mutex);
  asciichat_error_t result = ASCIICHAT_OK;
  if (client->transport && !atomic_load_bool(&client->shutting_down) && atomic_load_bool(&client->event_ready))
    result = packet_send_via_transport(client->transport, PACKET_TYPE_AUDIO_OPUS_BATCH, data, length,
                                       fnv1a_hash_string(client->client_id));
  mutex_unlock(&client->send_mutex);
  mutex_unlock(&client->client_state_mutex);
  return result;
}
