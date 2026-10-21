#include <criterion/criterion.h>
#include <ascii-chat/network/acip/backpressure.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/network/acip/security.h>
#include <ascii-chat/network/connection.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/acip/prepare.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/network/webrtc/webrtc.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/endian.h>
#include <string.h>
#include <sodium.h>

typedef struct endpoint {
  struct endpoint *other;
  webrtc_peer_connection_t *pc;
  webrtc_data_channel_t *dc;
  acip_transport_t *transport;
  acip_security_t *security;
  atomic_t opened, ready;
} endpoint_t;

static void description(webrtc_peer_connection_t *pc, const char *sdp, const char *type, void *arg) {
  (void)pc;
  endpoint_t *endpoint = arg;
  char message_limit[64];
  safe_snprintf(message_limit, sizeof(message_limit), "a=max-message-size:%u", ACIP_OUTBOUND_SCTP_RESERVE);
  const char *limit = strstr(sdp, message_limit);
  cr_assert(limit);
  // Model a browser advertising a larger receive limit than our local SCTP buffer.
  // The oversized ACIP exchange below must still use locally bounded chunks.
  size_t capacity = strlen(sdp) + 64;
  char *remote_sdp = SAFE_MALLOC(capacity, char *);
  safe_snprintf(remote_sdp, capacity, "%.*sa=max-message-size:%u%s", (int)(limit - sdp), sdp,
                4 * ACIP_OUTBOUND_SCTP_RESERVE, limit + strlen(message_limit));
  cr_assert_eq(webrtc_set_remote_description(endpoint->other->pc, remote_sdp, type), ASCIICHAT_OK);
  SAFE_FREE(remote_sdp);
}

static void candidate(webrtc_peer_connection_t *pc, const char *ice, const char *mid, void *arg) {
  (void)pc;
  endpoint_t *endpoint = arg;
  cr_assert_eq(webrtc_add_remote_candidate(endpoint->other->pc, ice, mid), ASCIICHAT_OK);
}

static void ready(acip_transport_t *transport, void *arg) {
  (void)transport;
  endpoint_t *endpoint = arg;
  atomic_store_bool(&endpoint->ready, true);
}

static void opened(webrtc_data_channel_t *dc, void *arg) {
  endpoint_t *endpoint = arg;
  endpoint->dc = dc;
  size_t max_message_size = 0;
  cr_assert_eq(webrtc_datachannel_get_max_message_size(dc, &max_message_size), ASCIICHAT_OK);
  cr_assert_eq(max_message_size, ACIP_OUTBOUND_SCTP_RESERVE);
  if (!endpoint->security->handshake.is_server) {
    endpoint->transport = acip_webrtc_transport_create_secure(endpoint->pc, dc, endpoint->security, ready, endpoint);
    cr_assert(endpoint->transport);
  }
  atomic_store_bool(&endpoint->opened, true);
}

static void await_flag(atomic_t *flag) {
  uint64_t start = time_get_ns();
  while (!atomic_load_bool(flag) && time_elapsed_ns(start, time_get_ns()) < 15 * NS_PER_SEC_INT)
    platform_sleep_ms(5);
  cr_assert(atomic_load_bool(flag));
}

static void initialize(endpoint_t *endpoint, bool server, uint8_t mode, const char *password) {
  endpoint->security = SAFE_CALLOC(1, sizeof(acip_security_t), acip_security_t *);
  cr_assert(endpoint->security);
  endpoint->security->mode = mode;
  cr_assert_eq(
      (password ? crypto_handshake_init_with_password("webrtc_test", &endpoint->security->handshake, server, password)
                : crypto_handshake_init("webrtc_test", &endpoint->security->handshake, server)),
      ASCIICHAT_OK);
  webrtc_config_t config = {.bind_address = "127.0.0.1",
                            .on_local_description = description,
                            .on_local_candidate = candidate,
                            .on_datachannel_open = opened,
                            .user_data = endpoint};
  cr_assert_eq(webrtc_create_peer_connection(&config, &endpoint->pc), ASCIICHAT_OK);
}

static void exchange(endpoint_t *sender, endpoint_t *receiver) {
  uint8_t payload[65536];
  memset(payload, 0x5a, sizeof(payload));
  acip_outbound_stats_t before;
  cr_assert_eq(acip_outbound_snapshot(sender->transport, &before), ASCIICHAT_OK);
  uint64_t nonce = sender->security->handshake.crypto_ctx.nonce_counter;
  cr_assert_eq(packet_send_via_transport(sender->transport, PACKET_TYPE_PING, payload, sizeof(payload), 0),
               ASCIICHAT_OK);
  uint64_t submitted_start = time_get_ns();
  for (;;) {
    acip_outbound_stats_t after;
    cr_assert_eq(acip_outbound_snapshot(sender->transport, &after), ASCIICHAT_OK);
    if (after.submitted > before.submitted)
      break;
    cr_assert(time_get_ns() - submitted_start < 5 * NS_PER_SEC_INT);
    platform_sleep_ms(1);
  }
  cr_assert_eq(sender->security->handshake.crypto_ctx.nonce_counter,
               nonce + (ACIP_CRYPTO_HAS_ENCRYPT(sender->security->mode) ? 1 : 0));
  uint64_t start = time_get_ns();
  while (!acip_transport_has_pending_data(receiver->transport) &&
         time_elapsed_ns(start, time_get_ns()) < 5 * NS_PER_SEC_INT)
    platform_sleep_ms(5);
  cr_assert(acip_transport_has_pending_data(receiver->transport));
  void *data = NULL, *owned = NULL;
  size_t length = 0;
  cr_assert_eq(acip_transport_recv(receiver->transport, &data, &length, &owned), ASCIICHAT_OK);
  cr_assert_eq(length, sizeof(packet_header_t) + sizeof(payload));
  packet_header_t header;
  memcpy(&header, data, sizeof(header));
  cr_assert_eq(NET_TO_HOST_U16(header.type), PACKET_TYPE_PING);
  cr_assert_eq(memcmp((uint8_t *)data + sizeof(header), payload, sizeof(payload)), 0);
  buffer_release(owned);
}

static void exchange_unused_outer_crc(endpoint_t *sender, endpoint_t *receiver) {
  const uint8_t payload[] = {1, 2, 3, 4};
  packet_header_t inner = {.magic = HOST_TO_NET_U64(PACKET_MAGIC),
                           .type = HOST_TO_NET_U16(PACKET_TYPE_PING),
                           .length = HOST_TO_NET_U32(sizeof(payload)),
                           .crc32 = HOST_TO_NET_U32(asciichat_crc32(payload, sizeof(payload)))};
  socket_buffer_t buffers[] = {{.data = &inner, .len = sizeof(inner)}, {.data = payload, .len = sizeof(payload)}};
  acip_packet_plan_t plan;
  cr_assert_eq(acip_packet_plan(&sender->security->handshake.crypto_ctx, buffers, 2, &plan), ASCIICHAT_OK);
  cr_assert(plan.encrypt);
  uint8_t *wire = SAFE_MALLOC(sizeof(inner) + plan.ciphertext_capacity, uint8_t *);
  packet_header_t outer;
  size_t encrypted_length = 0;
  cr_assert_eq(acip_packet_encrypt(&sender->security->handshake.crypto_ctx, &plan, NULL, wire + sizeof(outer),
                                   plan.ciphertext_capacity, &outer, &encrypted_length),
               ASCIICHAT_OK);
  // The protocol leaves the outer CRC unused; authenticated inner CRC stays valid.
  outer.crc32 ^= UINT32_MAX;
  memcpy(wire, &outer, sizeof(outer));
  cr_assert_eq(webrtc_datachannel_send(sender->dc, wire, sizeof(outer) + encrypted_length), ASCIICHAT_OK);
  SAFE_FREE(wire);
  uint64_t start = time_get_ns();
  while (!acip_transport_has_pending_data(receiver->transport) && time_get_ns() - start < 5 * NS_PER_SEC_INT)
    platform_sleep_ms(5);
  cr_assert(acip_transport_has_pending_data(receiver->transport));
  void *data = NULL, *owned = NULL;
  size_t length = 0;
  cr_assert_eq(acip_transport_recv(receiver->transport, &data, &length, &owned), ASCIICHAT_OK);
  cr_assert_eq(length, sizeof(inner) + sizeof(payload));
  cr_assert_eq(memcmp((uint8_t *)data + sizeof(inner), payload, sizeof(payload)), 0);
  buffer_release(owned);
}

static void fixture(uint8_t mode, const char *password, bool identity, int rejection) {
  network_connection_t *server_owner = NULL, *client_owner = NULL;
  endpoint_t server = {0}, client = {0};
  server.other = &client;
  client.other = &server;
  initialize(&server, true, mode, password);
  initialize(&client, false, mode, rejection == 1 ? "incorrect-password" : password);
  if (identity) {
    crypto_handshake_context_t *host = &server.security->handshake;
    crypto_handshake_context_t *guest = &client.security->handshake;
    host->server_private_key.type = host->server_public_key.type = KEY_TYPE_ED25519;
    guest->client_private_key.type = guest->client_public_key.type = KEY_TYPE_ED25519;
    crypto_sign_keypair(host->server_public_key.key, host->server_private_key.key.ed25519);
    crypto_sign_keypair(guest->client_public_key.key, guest->client_private_key.key.ed25519);
    memcpy(host->server_private_key.public_key, host->server_public_key.key, 32);
    memcpy(guest->client_private_key.public_key, guest->client_public_key.key, 32);
    guest->verify_server_key = guest->expected_server_key_bytes_set = true;
    memcpy(guest->expected_server_key_bytes, host->server_public_key.key, 32);
    host->client_whitelist = SAFE_CALLOC(1, sizeof(public_key_t), public_key_t *);
    cr_assert(host->client_whitelist);
    host->client_whitelist[0] = guest->client_public_key;
    host->num_whitelisted_clients = 1;
    host->require_client_auth = true;
    if (rejection == 2)
      guest->expected_server_key_bytes[0] ^= 1;
    if (rejection == 3)
      host->client_whitelist[0].key[0] ^= 1;
  }
  cr_assert_eq(webrtc_create_datachannel(client.pc, "acip", &client.dc), ASCIICHAT_OK);
  cr_assert_eq(webrtc_create_offer(client.pc), ASCIICHAT_OK);
  await_flag(&server.opened);
  await_flag(&client.opened);
  cr_assert_eq(acip_security_start(client.security, client.transport), ASCIICHAT_OK);
  // Exercise the browser startup ordering: the hello reaches an open channel
  // before its host has installed the transport receiver. Registration must
  // retain and drain that hello with a fully initialized response path.
  platform_sleep_ms(100);
  server.transport = acip_webrtc_transport_create_secure(server.pc, server.dc, server.security, ready, &server);
  cr_assert(server.transport);
  if (rejection) {
    uint64_t start = time_get_ns();
    while (acip_transport_is_connected(client.transport) && acip_transport_is_connected(server.transport) &&
           time_elapsed_ns(start, time_get_ns()) < 10 * NS_PER_SEC_INT)
      platform_sleep_ms(5);
    cr_assert(!acip_transport_is_connected(client.transport) || !acip_transport_is_connected(server.transport));
    cr_assert(!atomic_load_bool(&client.ready));
    cr_assert(!atomic_load_bool(&server.ready));
    cr_assert(!acip_transport_has_pending_data(client.transport));
    cr_assert(!acip_transport_has_pending_data(server.transport));
  } else {
    await_flag(&server.ready);
    await_flag(&client.ready);
    cr_assert_eq(network_connection_adopt(server.transport, &server_owner), ASCIICHAT_OK);
    cr_assert_eq(network_connection_adopt(client.transport, &client_owner), ASCIICHAT_OK);
    cr_assert_eq(network_connection_handshake(server_owner), &server.security->handshake);
    cr_assert_eq(network_connection_handshake(client_owner), &client.security->handshake);
    cr_assert_eq(network_connection_crypto_mode(server_owner), mode);
    cr_assert_eq(network_connection_crypto_mode(client_owner), mode);
    exchange(&client, &server);
    exchange(&server, &client);
    if (ACIP_CRYPTO_HAS_ENCRYPT(mode)) {
      exchange_unused_outer_crc(&client, &server);
      exchange_unused_outer_crc(&server, &client);
    }
  }
  if (client_owner)
    network_connection_destroy(&client_owner);
  else
    acip_transport_destroy(client.transport);
  if (server_owner)
    network_connection_destroy(&server_owner);
  else
    acip_transport_destroy(server.transport);
  webrtc_datachannel_destroy(client.dc);
  webrtc_datachannel_destroy(server.dc);
  webrtc_peer_connection_destroy(client.pc);
  webrtc_peer_connection_destroy(server.pc);
  log_warn("WebRTC security fixture passed: mode=%u identity=%d rejection=%d", mode, identity, rejection);
}

static void setup(void) {
  log_init(NULL, LOG_WARN, false, false);
  asciichat_errno_suppress(true);
  cr_assert_eq(socket_init(), ASCIICHAT_OK);
  cr_assert_eq(webrtc_init(), ASCIICHAT_OK);
}

static void teardown(void) {
  webrtc_destroy();
  buffer_stats_t stats;
  buffer_domain_stats(buffer_default_domain(), &stats);
  cr_assert_eq(stats.outstanding, 0, "Test leaked buffer owners");
  buffer_system_cleanup();
  cr_assert_eq(buffer_process_reserved(), 0);
  socket_cleanup();
  log_destroy();
}

TestSuite(webrtc_security, .init = setup, .fini = teardown, .timeout = 30);

Test(webrtc_security, plaintext) {
  fixture(ACIP_CRYPTO_NONE, NULL, false, 0);
}

Test(webrtc_security, encrypted) {
  fixture(ACIP_CRYPTO_ENCRYPT, NULL, false, 0);
}

Test(webrtc_security, password_full) {
  fixture(ACIP_CRYPTO_FULL, "peer-test-password", false, 0);
}

Test(webrtc_security, password_auth) {
  fixture(ACIP_CRYPTO_AUTH, "peer-test-password", false, 0);
}

Test(webrtc_security, identity_auth) {
  fixture(ACIP_CRYPTO_AUTH, NULL, true, 0);
}

Test(webrtc_security, reject_password) {
  fixture(ACIP_CRYPTO_FULL, "peer-test-password", false, 1);
}

Test(webrtc_security, reject_identity) {
  fixture(ACIP_CRYPTO_FULL, NULL, true, 2);
}

Test(webrtc_security, reject_signature) {
  fixture(ACIP_CRYPTO_AUTH, NULL, true, 3);
}
