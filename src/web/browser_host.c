/** Browser adapter for the same session host used by CLI discovery. */
#include <emscripten.h>
#include <emscripten/html5.h>
#include <sodium.h>
#include <ascii-chat/common.h>
#include <ascii-chat/network/acip/acds.h>
#include <ascii-chat/network/acip/acds_client.h>
#include <ascii-chat/network/acip/transport.h>
#include "session/host.h"

EM_JS(void, browser_host_send, (uint32_t client, const uint8_t *data, size_t length), {
  if (Module.hostPacketCallback)
    Module.hostPacketCallback(client, HEAPU8.slice(data, data + length));
});

#define BROWSER_HOST_CLIENTS 8
static session_host_t *host;
static acip_transport_t transports[BROWSER_HOST_CLIENTS];
static uint32_t client_ids[BROWSER_HOST_CLIENTS];
static asciichat_error_t host_transport_send(acip_transport_t *transport, const void *data, size_t length) {
  browser_host_send((uint32_t)(uintptr_t)transport->impl_data, data, length);
  return ASCIICHAT_OK;
}
static bool connected(acip_transport_t *transport) {
  return host != NULL;
}
static socket_t no_socket(acip_transport_t *transport) {
  return INVALID_SOCKET_VALUE;
}
static acip_transport_type_t transport_type(acip_transport_t *transport) {
  return ACIP_TRANSPORT_WEBRTC;
}
static const acip_transport_methods_t methods = {
    .send = host_transport_send, .is_connected = connected, .get_socket = no_socket, .get_type = transport_type};

EMSCRIPTEN_KEEPALIVE void browser_host_destroy(void) {
  session_host_destroy(host);
  host = NULL;
  memset(client_ids, 0, sizeof(client_ids));
  memset(transports, 0, sizeof(transports));
}
EMSCRIPTEN_KEEPALIVE asciichat_error_t browser_host_create(void) {
  browser_host_destroy();
  session_host_config_t config = {.max_clients = BROWSER_HOST_CLIENTS};
  host = session_host_create(&config);
  return host ? ASCIICHAT_OK : ERROR_MEMORY;
}
EMSCRIPTEN_KEEPALIVE uint32_t browser_host_add_client(void) {
  if (!host)
    return 0;
  for (size_t i = 0; i < BROWSER_HOST_CLIENTS; ++i) {
    if (client_ids[i])
      continue;
    uint32_t id = session_host_add_client(host, INVALID_SOCKET_VALUE, "browser", 0);
    if (!id)
      return 0;
    client_ids[i] = id;
    transports[i] = (acip_transport_t){.methods = &methods, .impl_data = (void *)(uintptr_t)id};
    if (session_host_set_client_transport(host, id, &transports[i]) != ASCIICHAT_OK) {
      session_host_remove_client(host, id);
      client_ids[i] = 0;
      return 0;
    }
    return id;
  }
  return 0;
}
EMSCRIPTEN_KEEPALIVE void browser_host_remove_client(uint32_t id) {
  if (!host)
    return;
  session_host_remove_client(host, id);
  for (size_t i = 0; i < BROWSER_HOST_CLIENTS; ++i)
    if (client_ids[i] == id)
      client_ids[i] = 0;
}
EMSCRIPTEN_KEEPALIVE asciichat_error_t browser_host_receive(uint32_t id, uint32_t type, const void *data,
                                                            size_t length) {
  return session_host_receive_packet(host, id, (packet_type_t)type, data, length);
}
EMSCRIPTEN_KEEPALIVE asciichat_error_t browser_host_render(void) {
  return session_host_render_video(host);
}
EMSCRIPTEN_KEEPALIVE asciichat_error_t browser_session_create_request(const char *password,
                                                                      acip_session_create_t *request) {
  if (!password || !request || strlen(password) > 127)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid session password");
  memset(request, 0, sizeof(*request));
  uint8_t secret[crypto_sign_SECRETKEYBYTES];
  if (crypto_sign_keypair(request->identity_pubkey, secret) != 0)
    return SET_ERRNO(ERROR_CRYPTO, "Could not generate session identity");
  request->timestamp = (uint64_t)emscripten_date_now();
  request->capabilities = 3;
  request->max_participants = BROWSER_HOST_CLIENTS;
  request->session_type = SESSION_TYPE_WEBRTC;
  asciichat_error_t result = acds_sign_session_create(secret, request->timestamp, request->capabilities,
                                                      request->max_participants, request->signature);
  sodium_memzero(secret, sizeof(secret));
  if (result != ASCIICHAT_OK)
    return result;
  request->has_password = password[0] != 0;
  if (request->has_password &&
      crypto_pwhash_str((char *)request->password_hash, password, strlen(password), crypto_pwhash_OPSLIMIT_INTERACTIVE,
                        crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0)
    return SET_ERRNO(ERROR_CRYPTO, "Could not hash session password");
  return ASCIICHAT_OK;
}
