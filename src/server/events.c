#include "events.h"
#include "crypto.h"
#include "protocol.h"
#include "main.h"
#include <ascii-chat/network/tcp/async.h>
#include <ascii-chat/network/packet/decoder.h>
#include <ascii-chat/network/acip/server.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/options/options.h>

typedef struct {
  server_context_t *server;
  platform_event_stream_t *stream;
  acip_transport_t *transport;
  client_info_t *client;
  packet_decoder_t decoder;
  char ip[INET6_ADDRSTRLEN];
  const void *input;
  size_t length;
  unsigned handshake_step;
  bool ready, capabilities, packet_received;
  uint64_t partial_since;
  asciichat_error_t result;
} server_event_connection_t;

static asciichat_error_t consume_packet(const void *data, size_t length, void *arg) {
  server_event_connection_t *connection = arg;
  if (!connection->client) {
    connection->client = add_webrtc_client(connection->server, connection->transport, connection->ip, false);
    if (!connection->client)
      return SET_ERRNO(ERROR_SESSION_FULL, "Cannot admit event client");
  }
  client_info_t *client = connection->client;
  packet_header_t header;
  memcpy(&header, data, sizeof(header));
  packet_type_t type = NET_TO_HOST_U16(header.type);
  const void *payload = (const uint8_t *)data + sizeof(header);
  size_t size = length - sizeof(header);
  connection->packet_received = true;
  if (!connection->ready) {
    asciichat_error_t result =
        server_crypto_event(client, &connection->handshake_step, type, payload, size, &connection->ready);
    if (result != ASCIICHAT_OK || type != PACKET_TYPE_CLIENT_CAPABILITIES)
      return result;
  }
  packet_envelope_t envelope = {.type = type, .data = (void *)payload, .len = size};
  asciichat_error_t result = ASCIICHAT_OK;
  mutex_lock(&client->client_state_mutex);
  if (type == PACKET_TYPE_ENCRYPTED) {
    envelope.allocated_buffer = buffer_alloc(NULL, length);
    envelope.allocated_size = length;
    if (!envelope.allocated_buffer) {
      mutex_unlock(&client->client_state_mutex);
      return SET_ERRNO(ERROR_MEMORY, "Cannot allocate received packet");
    }
    memcpy(envelope.allocated_buffer, data, length);
    envelope.data = (uint8_t *)envelope.allocated_buffer + sizeof(header);
    result = packet_decrypt_envelope(&envelope, client->transport->crypto_ctx);
    if (result == ASCIICHAT_OK) {
      packet_header_t inner;
      memcpy(&inner, envelope.allocated_buffer, sizeof(inner));
      if (NET_TO_HOST_U64(inner.magic) != PACKET_MAGIC ||
          (envelope.len && asciichat_crc32(envelope.data, envelope.len) != NET_TO_HOST_U32(inner.crc32)))
        result = SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid decrypted packet");
    }
  } else if (client->encrypt_data && !packet_is_handshake_type(type)) {
    result = SET_ERRNO(ERROR_CRYPTO, "Plaintext application packet on encrypted connection");
  }
  mutex_unlock(&client->client_state_mutex);
  if (result == ASCIICHAT_OK && !connection->capabilities) {
    if (envelope.type != PACKET_TYPE_CLIENT_CAPABILITIES) {
      result = SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Expected initial client capabilities");
    } else {
      handle_client_capabilities_packet(client, envelope.data, envelope.len);
      connection->capabilities = true;
      if (start_webrtc_client_threads(connection->server, client->client_id) != 0)
        result = SET_ERRNO(ERROR_THREAD, "Cannot start client rendering");
    }
  } else if (result == ASCIICHAT_OK) {
    result = server_event_dispatch(client, envelope.type, envelope.data, envelope.len);
  }
  if (envelope.allocated_buffer)
    buffer_release(envelope.allocated_buffer);
  return result;
}

static void process_input(void *arg) {
  server_event_connection_t *connection = arg;
  connection->packet_received = false;
  connection->result =
      packet_decoder_feed(&connection->decoder, connection->input, connection->length, consume_packet, connection);
}

static void input_done(platform_event_stream_t *stream, void *arg, bool closing) {
  server_event_connection_t *connection = arg;
  if (closing)
    return;
  if (connection->result != ASCIICHAT_OK) {
    platform_event_stream_close(stream, connection->result);
    return;
  }
  if (connection->capabilities) {
    if (packet_decoder_pending(&connection->decoder)) {
      if (!connection->partial_since || connection->packet_received)
        connection->partial_since = time_get_ns();
      uint64_t elapsed = time_get_ns() - connection->partial_since;
      if (elapsed >= 30 * NS_PER_SEC_INT) {
        platform_event_stream_close(stream, ERROR_NETWORK_TIMEOUT);
        return;
      }
      platform_event_stream_deadline(stream, (30 * NS_PER_SEC_INT - elapsed) / NS_PER_MS_INT + 1);
    } else {
      connection->partial_since = 0;
      platform_event_stream_deadline(stream, 0);
    }
  }
  if (platform_event_stream_resume(stream) != ASCIICHAT_OK)
    platform_event_stream_close(stream, ERROR_NETWORK);
}

static void read_input(platform_event_stream_t *stream, const void *data, size_t length, void *arg) {
  server_event_connection_t *connection = arg;
  connection->input = data;
  connection->length = length;
  asciichat_error_t result = platform_event_stream_work(stream, process_input, input_done, connection);
  if (result != ASCIICHAT_OK)
    platform_event_stream_close(stream, result);
}

static void cleanup_connection(void *arg) {
  server_event_connection_t *connection = arg;
  if (connection->client) {
    atomic_store_bool(&connection->client->event_cleanup, true);
    remove_client(connection->server, connection->client->client_id);
  } else {
    acip_transport_destroy(connection->transport);
  }
  packet_decoder_reset(&connection->decoder);
}
static void free_connection(void *arg) {
  server_event_connection_t *connection = arg;
  SAFE_FREE(connection);
}
static void connection_closed(void *arg, asciichat_error_t reason) {
  (void)reason;
  server_event_connection_t *connection = arg;
  acip_async_tcp_transport_detach(connection->transport);
  if (platform_event_loop_work(connection->server->tcp_server->event_loop, cleanup_connection, free_connection,
                               connection) != ASCIICHAT_OK) {
    cleanup_connection(connection);
    free_connection(connection);
  }
}

void server_event_accept(platform_event_stream_t *stream, void *arg) {
  server_event_connection_t *connection = SAFE_CALLOC(1, sizeof(*connection), server_event_connection_t *);
  connection->server = arg;
  connection->stream = stream;
  struct sockaddr_storage peer;
  socklen_t length = sizeof(peer);
  if (platform_event_stream_peer(stream, &peer, &length) == ASCIICHAT_OK)
    format_ip_address(peer.ss_family, (struct sockaddr *)&peer, connection->ip, sizeof(connection->ip));
  connection->transport = acip_async_tcp_transport_create(stream, NULL);
  if (!connection->transport) {
    SAFE_FREE(connection);
    platform_event_stream_close(stream, ERROR_MEMORY);
    return;
  }
  asciichat_error_t result = platform_event_stream_start(stream, read_input, connection_closed, connection);
  if (result == ASCIICHAT_OK)
    result = platform_event_stream_deadline(stream, 30000);
  if (result != ASCIICHAT_OK)
    platform_event_stream_close(stream, result);
}

void server_event_video(client_info_t *client, acip_frame_workspace_t *workspace, const char *data, size_t length,
                        uint32_t width, uint32_t height, int sources) {
  mutex_lock(&client->client_state_mutex);
  mutex_lock(&client->send_mutex);
  acip_transport_t *transport = client->transport;
  if (transport && !atomic_load_bool(&client->shutting_down) && atomic_load_bool(&client->event_ready)) {
    // ASCII frames are independent. Skip an unencrypted, unqueued frame when
    // the peer already has a frame pending; retain room for control and audio.
    if (acip_async_tcp_transport_queued(transport) < 256 * 1024) {
      if (client->crypto_initialized && crypto_handshake_should_rekey(&client->crypto_handshake_ctx))
        crypto_handshake_rekey_request(&client->crypto_handshake_ctx, transport);
      if (sources > 0 && atomic_load_u64(&client->last_sent_grid_sources) != (uint64_t)sources) {
        acip_send_clear_console(transport);
        atomic_store_u64(&client->last_sent_grid_sources, (uint64_t)sources);
      }
      if (acip_send_ascii_frame(transport, workspace, data, length, width, height, client->client_id) == ASCIICHAT_OK)
        atomic_fetch_add_u64(&client->frames_sent_count, 1);
    }
  }
  mutex_unlock(&client->send_mutex);
  mutex_unlock(&client->client_state_mutex);
}

asciichat_error_t server_event_audio(client_info_t *client, const void *data, size_t length) {
  mutex_lock(&client->client_state_mutex);
  mutex_lock(&client->send_mutex);
  asciichat_error_t result = ASCIICHAT_OK;
  if (client->transport && !atomic_load_bool(&client->shutting_down) && atomic_load_bool(&client->event_ready) &&
      acip_async_tcp_transport_queued(client->transport) < 512 * 1024)
    result = packet_send_via_transport(client->transport, PACKET_TYPE_AUDIO_OPUS_BATCH, data, length,
                                       fnv1a_hash_string(client->client_id));
  mutex_unlock(&client->send_mutex);
  mutex_unlock(&client->client_state_mutex);
  return result;
}
