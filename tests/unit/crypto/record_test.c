#include <criterion/criterion.h>
#include <ascii-chat/crypto/record.h>
#include <ascii-chat/crypto/rekey.h>
#include <ascii-chat/crypto/session.h>
#include <ascii-chat/network/acip/security.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/log/log.h>
#include <string.h>
#define CHECK(condition) cr_assert((condition), "%s", #condition)

static void setup(void) {
  log_init(NULL, LOG_WARN, false, false);
  log_set_terminal_output(false);
}
static void teardown(void) {
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TestSuite(crypto_records, .init = setup, .fini = teardown);

Test(crypto_records, rejects_raw_secretbox_packets_without_accepting_an_alternate_format) {
  crypto_context_t sender, receiver;
  CHECK(crypto_init(&sender) == CRYPTO_OK && crypto_init(&receiver) == CRYPTO_OK);
  CHECK(crypto_set_peer_public_key(&sender, receiver.public_key) == CRYPTO_OK);
  CHECK(crypto_set_peer_public_key(&receiver, sender.public_key) == CRYPTO_OK);
  sender.handshake_complete = receiver.handshake_complete = true;
  uint8_t packet[sizeof(packet_header_t)], ciphertext[256];
  size_t packet_length = 0, ciphertext_length = 0;
  CHECK(packet_encode(PACKET_TYPE_PING, NULL, 0, 0, packet, sizeof(packet), &packet_length) == ASCIICHAT_OK);
  CHECK(crypto_encrypt(&sender, packet, packet_length, ciphertext, sizeof(ciphertext), &ciphertext_length) ==
        CRYPTO_OK);
  uint8_t *plain = NULL;
  size_t length = 0;
  uint64_t sequence;
  bool consumed;
  CHECK(acip_security_decrypt(&receiver, CRYPTO_RECORD_ANY_CHANNEL, ACIP_DELIVERY_RELIABLE, ciphertext,
                              ciphertext_length, &plain, &length, &sequence, &consumed) != ASCIICHAT_OK);
  CHECK(!plain && !length);
  CLEAR_ERRNO_ALL();
  uint8_t *wire = NULL;
  size_t wire_length = 0;
  CHECK(acip_security_seal(&sender, ACIP_TRAFFIC_CONTROL, ACIP_DELIVERY_RELIABLE, packet, packet_length, &wire,
                           &wire_length) == ASCIICHAT_OK);
  CHECK(acip_security_open(&receiver, CRYPTO_RECORD_ANY_CHANNEL, ACIP_DELIVERY_RELIABLE, wire, wire_length, &plain,
                           &length, &sequence, &consumed) == ASCIICHAT_OK);
  CHECK(!consumed && sequence == 1 && length == packet_length && !memcmp(plain, packet, length));
  buffer_release(plain);
  buffer_release(wire);
  crypto_destroy(&sender);
  crypto_destroy(&receiver);
}

static uint8_t *make_packet(packet_type_t type, size_t payload_length, size_t *length) {
  *length = sizeof(packet_header_t) + payload_length;
  uint8_t *packet = buffer_alloc(NULL, *length);
  CHECK(packet);
  for (size_t i = 0; i < payload_length; ++i)
    packet[sizeof(packet_header_t) + i] = (uint8_t)(i * 37U + (unsigned)type);
  packet_header_t header = {.magic = HOST_TO_NET_U64(PACKET_MAGIC),
                            .type = HOST_TO_NET_U16(type),
                            .length = HOST_TO_NET_U32((uint32_t)payload_length),
                            .crc32 =
                                HOST_TO_NET_U32(asciichat_crc32(packet + sizeof(packet_header_t), payload_length))};
  memcpy(packet, &header, sizeof(header));
  return packet;
}

Test(crypto_records, authenticates_channels_and_delivery_and_rejects_replays) {
  crypto_context_t client_crypto, server_crypto;
  CHECK(crypto_init(&client_crypto) == CRYPTO_OK && crypto_init(&server_crypto) == CRYPTO_OK);
  CHECK(crypto_set_peer_public_key(&client_crypto, server_crypto.public_key) == CRYPTO_OK);
  CHECK(crypto_set_peer_public_key(&server_crypto, client_crypto.public_key) == CRYPTO_OK);
  CHECK(memcmp(server_crypto.shared_key, client_crypto.shared_key, sizeof(client_crypto.shared_key)) == 0);
  crypto_record_t client = {0}, server = {0};
  CHECK(crypto_record_init(&client, &client_crypto, false) == ERROR_INVALID_STATE);
  client_crypto.handshake_complete = server_crypto.handshake_complete = true;
  CHECK(crypto_record_init(&client, &client_crypto, false) == ASCIICHAT_OK);
  CHECK(crypto_record_init(&server, &server_crypto, true) == ASCIICHAT_OK);
  CHECK(crypto_record_init(&server, &server_crypto, true) == ERROR_INVALID_STATE);
  size_t audio_length = 0, control_length = 0;
  uint8_t *audio = make_packet(PACKET_TYPE_AUDIO_BATCH, 16, &audio_length);
  uint8_t *control = make_packet(PACKET_TYPE_PING, 8, &control_length);
  uint8_t *wire[3] = {NULL, NULL, NULL}, *plain = NULL;
  size_t wire_length[3] = {0}, plain_length = 0;
  for (size_t i = 0; i < 3; ++i)
    CHECK(acip_packet_seal(&client, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, audio, audio_length, &wire[i],
                           &wire_length[i]) == ASCIICHAT_OK);
  CHECK(memcmp(wire[0] + sizeof(packet_header_t) + 8, wire[1] + sizeof(packet_header_t) + 8, CRYPTO_NONCE_SIZE) != 0);
  CHECK(acip_packet_open(&server, ACIP_TRAFFIC_VISUAL, ACIP_DELIVERY_DATAGRAM, wire[1], wire_length[1], &plain,
                         &plain_length) == ERROR_NETWORK_PROTOCOL &&
        !plain);
  CHECK(acip_packet_open(&server, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_RELIABLE, wire[1], wire_length[1], &plain,
                         &plain_length) == ERROR_NETWORK_PROTOCOL &&
        !plain);
  CHECK(acip_packet_open(&client, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, wire[1], wire_length[1], &plain,
                         &plain_length) == ERROR_NETWORK_PROTOCOL &&
        !plain);
  CHECK(acip_packet_open(&server, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, wire[1], wire_length[1], &plain,
                         &plain_length) == ASCIICHAT_OK);
  CHECK(plain_length == audio_length && memcmp(plain, audio, audio_length) == 0);
  buffer_release(plain);
  plain = NULL;
  CHECK(acip_packet_open(&server, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, wire[0], wire_length[0], &plain,
                         &plain_length) == ASCIICHAT_OK);
  buffer_release(plain);
  plain = NULL;
  CHECK(acip_packet_open(&server, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, wire[1], wire_length[1], &plain,
                         &plain_length) == ERROR_CRYPTO &&
        !plain);
  uint8_t *tampered = buffer_alloc(NULL, wire_length[2]);
  CHECK(tampered);
  memcpy(tampered, wire[2], wire_length[2]);
  tampered[wire_length[2] - 1] ^= 1;
  packet_header_t outer;
  memcpy(&outer, tampered, sizeof(outer));
  outer.crc32 = HOST_TO_NET_U32(asciichat_crc32(tampered + sizeof(outer), wire_length[2] - sizeof(outer)));
  memcpy(tampered, &outer, sizeof(outer));
  CHECK(acip_packet_open(&server, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, tampered, wire_length[2], &plain,
                         &plain_length) == ERROR_CRYPTO &&
        !plain);
  CHECK(acip_packet_open(&server, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, wire[2], wire_length[2], &plain,
                         &plain_length) == ASCIICHAT_OK);
  buffer_release(plain);
  buffer_release(tampered);
  for (size_t i = 0; i < 3; ++i)
    buffer_release(wire[i]);
  wire[0] = NULL;
  CHECK(acip_packet_seal(&client, ACIP_TRAFFIC_CONTROL, ACIP_DELIVERY_DATAGRAM, control, control_length, &wire[0],
                         &wire_length[0]) == ERROR_INVALID_STATE);
  CHECK(acip_packet_seal(&client, ACIP_TRAFFIC_CONTROL, ACIP_DELIVERY_RELIABLE, control, control_length, &wire[0],
                         &wire_length[0]) == ASCIICHAT_OK);
  CHECK(acip_packet_open(&server, ACIP_TRAFFIC_CONTROL, ACIP_DELIVERY_RELIABLE, wire[0], wire_length[0], &plain,
                         &plain_length) == ASCIICHAT_OK);
  buffer_release(plain);
  plain = NULL;
  CHECK(acip_packet_open(&server, ACIP_TRAFFIC_CONTROL, ACIP_DELIVERY_RELIABLE, wire[0], wire_length[0], &plain,
                         &plain_length) == ERROR_CRYPTO &&
        !plain);
  uint8_t *reply = NULL;
  size_t reply_length = 0;
  CHECK(acip_packet_seal(&server, ACIP_TRAFFIC_CONTROL, ACIP_DELIVERY_RELIABLE, control, control_length, &reply,
                         &reply_length) == ASCIICHAT_OK);
  CHECK(acip_packet_open(&client, ACIP_TRAFFIC_CONTROL, ACIP_DELIVERY_RELIABLE, reply, reply_length, &plain,
                         &plain_length) == ASCIICHAT_OK);
  buffer_release(plain);
  plain = NULL;
  buffer_release(reply);
  size_t visual_length = 0;
  uint8_t *visual = make_packet(PACKET_TYPE_IMAGE_FRAME, 12, &visual_length);
  uint8_t *first = NULL, *within_window = NULL, *last = NULL;
  size_t first_length = 0, within_window_length = 0, last_length = 0;
  for (unsigned i = 0; i < 65; ++i) {
    uint8_t *next = NULL;
    size_t next_length = 0;
    CHECK(acip_packet_seal(&client, ACIP_TRAFFIC_VISUAL, ACIP_DELIVERY_DATAGRAM, visual, visual_length, &next,
                           &next_length) == ASCIICHAT_OK);
    if (i == 0) {
      first = next;
      first_length = next_length;
    } else if (i == 1) {
      within_window = next;
      within_window_length = next_length;
    } else if (i == 64) {
      last = next;
      last_length = next_length;
    } else {
      buffer_release(next);
    }
  }
  CHECK(acip_packet_open(&server, ACIP_TRAFFIC_VISUAL, ACIP_DELIVERY_DATAGRAM, last, last_length, &plain,
                         &plain_length) == ASCIICHAT_OK);
  buffer_release(plain);
  plain = NULL;
  CHECK(acip_packet_open(&server, ACIP_TRAFFIC_VISUAL, ACIP_DELIVERY_DATAGRAM, first, first_length, &plain,
                         &plain_length) == ERROR_CRYPTO &&
        !plain);
  CHECK(acip_packet_open(&server, ACIP_TRAFFIC_VISUAL, ACIP_DELIVERY_DATAGRAM, within_window, within_window_length,
                         &plain, &plain_length) == ASCIICHAT_OK);
  buffer_release(plain);
  plain = NULL;
  CHECK(acip_packet_open(&server, ACIP_TRAFFIC_VISUAL, ACIP_DELIVERY_DATAGRAM, within_window, within_window_length,
                         &plain, &plain_length) == ERROR_CRYPTO &&
        !plain);
  buffer_release(first);
  buffer_release(within_window);
  buffer_release(last);
  buffer_release(visual);
  server_crypto.rekey_in_progress = true;
  CHECK(acip_packet_open(&server, ACIP_TRAFFIC_CONTROL, ACIP_DELIVERY_RELIABLE, wire[0], wire_length[0], &plain,
                         &plain_length) == ERROR_CRYPTO &&
        !plain);
  server_crypto.rekey_in_progress = false;
  ++server_crypto.rekey_count;
  CHECK(acip_packet_open(&server, ACIP_TRAFFIC_CONTROL, ACIP_DELIVERY_RELIABLE, wire[0], wire_length[0], &plain,
                         &plain_length) == ERROR_INVALID_STATE &&
        !plain);
  buffer_release(wire[0]);
  buffer_release(audio);
  buffer_release(control);
  crypto_destroy(&server_crypto);
  crypto_destroy(&client_crypto);
}

typedef struct {
  crypto_record_t security;
  crypto_rekey_session_t rekey;
  uint8_t *wire[32];
  size_t length[32];
  acip_traffic_class_t flow[32];
  unsigned count, read;
  bool fail_send;
} rekey_peer_t;

static asciichat_error_t rekey_enqueue(void *context, unsigned flow, crypto_rekey_message_t message, const void *packet,
                                       size_t length) {
  rekey_peer_t *peer = context;
  if (peer->fail_send || peer->count == 32)
    return SET_ERRNO(ERROR_NETWORK, "Test rekey queue is unavailable");
  unsigned i = peer->count;
  asciichat_error_t result =
      acip_security_rekey_seal(&peer->security, flow, message, packet, length, &peer->wire[i], &peer->length[i]);
  if (result == ASCIICHAT_OK) {
    peer->flow[i] = flow;
    ++peer->count;
  }
  return result;
}

static void rekey_deliver(rekey_peer_t *from, rekey_peer_t *to, uint64_t now) {
  CHECK(from->read < from->count);
  unsigned i = from->read++;
  uint8_t *plain = NULL;
  size_t length = 0;
  CHECK(acip_packet_open(&to->security, from->flow[i], ACIP_DELIVERY_RELIABLE, from->wire[i], from->length[i], &plain,
                         &length) == ASCIICHAT_OK);
  bool consumed = false;
  CHECK(acip_security_rekey_receive(&to->rekey, from->flow[i], ACIP_DELIVERY_RELIABLE, plain, length, now, &consumed) ==
            ASCIICHAT_OK &&
        consumed);
  buffer_release(plain);
}

Test(crypto_records, rotates_twice_and_bounds_control_and_key_retirement) {
  crypto_context_t client_crypto, server_crypto;
  CHECK(crypto_init(&client_crypto) == CRYPTO_OK && crypto_init(&server_crypto) == CRYPTO_OK);
  CHECK(crypto_set_peer_public_key(&client_crypto, server_crypto.public_key) == CRYPTO_OK);
  CHECK(crypto_set_peer_public_key(&server_crypto, client_crypto.public_key) == CRYPTO_OK);
  client_crypto.handshake_complete = server_crypto.handshake_complete = true;
  rekey_peer_t client = {0}, server = {0};
  CHECK(crypto_record_init(&client.security, &client_crypto, false) == ASCIICHAT_OK);
  CHECK(crypto_record_init(&server.security, &server_crypto, true) == ASCIICHAT_OK);
  uint64_t now = NS_PER_SEC_INT;
  CHECK(crypto_rekey_session_init(&client.rekey, &client.security, rekey_enqueue, &client, now) == ASCIICHAT_OK);
  CHECK(crypto_rekey_session_init(&server.rekey, &server.security, rekey_enqueue, &server, now) == ASCIICHAT_OK);
  CHECK(crypto_rekey_session_request(&client.rekey, now) == ASCIICHAT_OK && !client.count);
  now += REKEY_MIN_INTERVAL;
  client_crypto.rekey_last_time = server_crypto.rekey_last_time = 0;
  CHECK(crypto_rekey_session_tick(&client.rekey, now) == ASCIICHAT_OK && client.count == 1);
  rekey_deliver(&client, &server, now); // client asks; server owns the DH exchange
  CHECK(server.rekey.waiting_response);
  rekey_deliver(&server, &client, now);
  CHECK(client.security.pending_prepared && client.rekey.expecting_complete);
  rekey_deliver(&client, &server, now);
  CHECK(server.security.key_epoch == 1 && server.security.previous_valid);
  // New audio can arrive before the reliable COMPLETE and commit the prepared key.
  size_t packet_length = 0, wire_length = 0, plain_length = 0;
  uint8_t *packet = make_packet(PACKET_TYPE_AUDIO_BATCH, 16, &packet_length), *wire = NULL, *plain = NULL;
  CHECK(acip_packet_seal(&server.security, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, packet, packet_length, &wire,
                         &wire_length) == ASCIICHAT_OK);
  CHECK(acip_packet_open(&client.security, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, wire, wire_length, &plain,
                         &plain_length) == ASCIICHAT_OK);
  bool consumed = true;
  CHECK(acip_security_rekey_receive(&client.rekey, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, plain, plain_length, now,
                                    &consumed) == ASCIICHAT_OK &&
        !consumed);
  CHECK(client.security.key_epoch == 1 && client.rekey.markers_sent && client.rekey.expecting_complete);
  buffer_release(packet);
  buffer_release(wire);
  buffer_release(plain);
  // Hold the server's last retirement marker while the client retires all server flows.
  while (client.read < client.count)
    rekey_deliver(&client, &server, now);
  CHECK(!server.security.previous_valid && client.security.previous_valid);
  while (server.read + 2 < server.count)
    rekey_deliver(&server, &client, now);
  CHECK(client.security.previous_valid && server.rekey.peer_ack == 0);
  CHECK(crypto_rekey_session_request(&server.rekey, now) == ASCIICHAT_OK && server.rekey.exchange_ns);
  while (server.read < server.count)
    rekey_deliver(&server, &client, now);
  while (client.read < client.count)
    rekey_deliver(&client, &server, now);
  CHECK(!client.security.previous_valid && !server.security.previous_valid);
  CHECK(!client.rekey.exchange_ns && !server.rekey.exchange_ns);
  CHECK(client.rekey.peer_ack == 1 && server.rekey.peer_ack == 1);
  // A second exchange must wait for the request rate limit, even after retirement.
  CHECK(crypto_rekey_session_request(&server.rekey, now + REKEY_MIN_INTERVAL) == ASCIICHAT_OK);
  CHECK(!server.rekey.waiting_response);
  now += REKEY_MIN_REQUEST_INTERVAL;
  client_crypto.rekey_last_time = server_crypto.rekey_last_time = 0;
  CHECK(crypto_rekey_session_tick(&server.rekey, now) == ASCIICHAT_OK && server.rekey.waiting_response);
  // The next request can arrive slightly sooner at the receiver because jitter changed.
  client.rekey.last_request_ns += NS_PER_MS_INT;
  rekey_deliver(&server, &client, now);
  CHECK(client.rekey.deferred_request && !client.security.pending_prepared);
  now += NS_PER_MS_INT;
  CHECK(crypto_rekey_session_tick(&client.rekey, now) == ASCIICHAT_OK && client.security.pending_prepared);
  for (unsigned round = 0; round < 5; ++round) {
    while (server.read < server.count)
      rekey_deliver(&server, &client, now);
    while (client.read < client.count)
      rekey_deliver(&client, &server, now);
  }
  CHECK(client.security.key_epoch == 2 && server.security.key_epoch == 2);
  CHECK(!client.rekey.exchange_ns && !server.rekey.exchange_ns);
  CHECK(crypto_rekey_session_tick(&client.rekey, now - 1) == ERROR_INVALID_STATE);
  now += REKEY_MIN_REQUEST_INTERVAL;
  server_crypto.rekey_last_time = 0;
  CHECK(crypto_rekey_session_request(&server.rekey, now) == ASCIICHAT_OK && server.rekey.waiting_response);
  CHECK(crypto_rekey_session_tick(&server.rekey, now + 31ULL * NS_PER_SEC_INT) == ERROR_NETWORK_TIMEOUT);
  crypto_rekey_session_cleanup(&server.rekey);
  CHECK(!server_crypto.rekey_in_progress && !server_crypto.has_temp_key);
  crypto_rekey_session_cleanup(&client.rekey);
  // Enqueue failure is visible to the caller; cleanup aborts the unfinished DH.
  server.fail_send = true;
  now += 32ULL * NS_PER_SEC_INT;
  CHECK(crypto_rekey_session_init(&server.rekey, &server.security, rekey_enqueue, &server, now) == ASCIICHAT_OK);
  server_crypto.rekey_last_time = 0;
  CHECK(crypto_rekey_session_request(&server.rekey, now + REKEY_MIN_INTERVAL) == ERROR_NETWORK);
  CHECK(server_crypto.rekey_in_progress);
  crypto_rekey_session_cleanup(&server.rekey);
  CHECK(!server_crypto.rekey_in_progress && !server_crypto.has_temp_key);
  for (unsigned i = 0; i < server.count; ++i)
    buffer_release(server.wire[i]);
  for (unsigned i = 0; i < client.count; ++i)
    buffer_release(client.wire[i]);
  crypto_record_cleanup(&server.security);
  crypto_record_cleanup(&client.security);
  crypto_destroy(&server_crypto);
  crypto_destroy(&client_crypto);
}

Test(crypto_records, accepts_inflight_epochs_until_authenticated_channel_retirement) {
  crypto_context_t client_crypto, server_crypto;
  CHECK(crypto_init(&client_crypto) == CRYPTO_OK && crypto_init(&server_crypto) == CRYPTO_OK);
  CHECK(crypto_set_peer_public_key(&client_crypto, server_crypto.public_key) == CRYPTO_OK);
  CHECK(crypto_set_peer_public_key(&server_crypto, client_crypto.public_key) == CRYPTO_OK);
  client_crypto.handshake_complete = server_crypto.handshake_complete = true;
  crypto_record_t client = {0}, server = {0};
  CHECK(crypto_record_init(&client, &client_crypto, false) == ASCIICHAT_OK);
  CHECK(crypto_record_init(&server, &server_crypto, true) == ASCIICHAT_OK);
  size_t packet_length = 0, old_length = 0, next_length = 0, plain_length = 0;
  uint8_t *packet = make_packet(PACKET_TYPE_AUDIO_BATCH, 32, &packet_length);
  uint8_t *old = NULL, *next = NULL, *plain = NULL;
  CHECK(acip_packet_seal(&client, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, packet, packet_length, &old,
                         &old_length) == ASCIICHAT_OK);
  uint8_t *stale = NULL, *old_visual = NULL;
  size_t stale_length = 0, visual_length = 0, old_visual_length = 0;
  CHECK(acip_packet_seal(&client, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, packet, packet_length, &stale,
                         &stale_length) == ASCIICHAT_OK);
  uint8_t *visual = make_packet(PACKET_TYPE_IMAGE_FRAME, 24, &visual_length);
  CHECK(acip_packet_seal(&client, ACIP_TRAFFIC_VISUAL, ACIP_DELIVERY_RELIABLE, visual, visual_length, &old_visual,
                         &old_visual_length) == ASCIICHAT_OK);
  CHECK(crypto_record_prepare_epoch(&client) == ERROR_INVALID_STATE);
  client_crypto.rekey_last_time = 0;
  CHECK(crypto_rekey_init(&client_crypto) == CRYPTO_OK);
  CHECK(crypto_rekey_process_request(&server_crypto, client_crypto.temp_public_key) == CRYPTO_OK);
  CHECK(crypto_rekey_process_response(&client_crypto, server_crypto.temp_public_key) == CRYPTO_OK);
  CHECK(crypto_record_prepare_epoch(&client) == ASCIICHAT_OK);
  CHECK(crypto_record_prepare_epoch(&server) == ASCIICHAT_OK);
  CHECK(crypto_record_commit_epoch(&server) == ASCIICHAT_OK);
  CHECK(server.key_epoch == 1 && server.previous_valid);
  CHECK(acip_packet_seal(&server, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, packet, packet_length, &next,
                         &next_length) == ASCIICHAT_OK);
  // An epoch selector is authenticated; changing it cannot switch keys or replay state.
  uint8_t *forged = buffer_alloc(NULL, next_length);
  CHECK(forged);
  memcpy(forged, next, next_length);
  memset(forged + sizeof(packet_header_t), 0, 8);
  packet_header_t header;
  memcpy(&header, forged, sizeof(header));
  header.crc32 = HOST_TO_NET_U32(asciichat_crc32(forged + sizeof(header), next_length - sizeof(header)));
  memcpy(forged, &header, sizeof(header));
  CHECK(acip_packet_open(&client, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, forged, next_length, &plain,
                         &plain_length) == ERROR_CRYPTO &&
        !plain);
  CHECK(client.key_epoch == 0 && client.pending_prepared && !client.previous_valid);
  CHECK(acip_packet_open(&client, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, next, next_length, &plain,
                         &plain_length) == ASCIICHAT_OK);
  CHECK(client.key_epoch == 1 && client.previous_valid && plain_length == packet_length &&
        !memcmp(packet, plain, packet_length));
  buffer_release(plain);
  plain = NULL;
  // A delayed old datagram remains valid until its flow crosses the retirement marker.
  CHECK(acip_packet_open(&server, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, old, old_length, &plain, &plain_length) ==
        ASCIICHAT_OK);
  buffer_release(plain);
  plain = NULL;
  CHECK(acip_packet_open(&server, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, old, old_length, &plain, &plain_length) ==
            ERROR_CRYPTO &&
        !plain);
  CHECK(crypto_record_retire_epoch_flow(&server, ACIP_TRAFFIC_AUDIO, 2) == ERROR_INVALID_STATE);
  CHECK(crypto_record_retire_epoch_flow(&server, ACIP_TRAFFIC_AUDIO, 1) == ASCIICHAT_OK);
  CHECK(acip_packet_open(&server, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, old, old_length, &plain, &plain_length) ==
            ERROR_CRYPTO &&
        !plain);
  CHECK(acip_packet_open(&server, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, stale, stale_length, &plain,
                         &plain_length) == ERROR_CRYPTO &&
        !plain);
  CHECK(acip_packet_open(&server, ACIP_TRAFFIC_VISUAL, ACIP_DELIVERY_RELIABLE, old_visual, old_visual_length, &plain,
                         &plain_length) == ASCIICHAT_OK);
  CHECK(plain_length == visual_length && !memcmp(plain, visual, visual_length));
  buffer_release(plain);
  plain = NULL;
  server_crypto.rekey_last_time = 0;
  CHECK(crypto_rekey_init(&server_crypto) == CRYPTO_OK);
  CHECK(crypto_record_prepare_epoch(&server) == ERROR_INVALID_STATE);
  crypto_rekey_abort(&server_crypto);
  for (unsigned flow = 0; flow < 3; ++flow) {
    CHECK(crypto_record_retire_epoch_flow(&client, (acip_traffic_class_t)flow, 1) == ASCIICHAT_OK);
    if (flow != ACIP_TRAFFIC_AUDIO)
      CHECK(crypto_record_retire_epoch_flow(&server, (acip_traffic_class_t)flow, 1) == ASCIICHAT_OK);
  }
  CHECK(!client.previous_valid && !server.previous_valid);
  crypto_context_t erased = {0};
  CHECK(!memcmp(&client.previous_crypto, &erased, sizeof(erased)));
  CHECK(!memcmp(&server.previous_crypto, &erased, sizeof(erased)));
  CHECK(client.send_sequence[ACIP_TRAFFIC_AUDIO][ACIP_DELIVERY_DATAGRAM] == 2 &&
        server.receive_high[ACIP_TRAFFIC_AUDIO][ACIP_DELIVERY_DATAGRAM] == 1);
  // A later rotation reuses the bounded old-key slot without resetting replay windows.
  CHECK(crypto_rekey_init(&server_crypto) == CRYPTO_OK);
  CHECK(crypto_rekey_process_request(&client_crypto, server_crypto.temp_public_key) == CRYPTO_OK);
  CHECK(crypto_rekey_process_response(&server_crypto, client_crypto.temp_public_key) == CRYPTO_OK);
  CHECK(crypto_record_prepare_epoch(&client) == ASCIICHAT_OK);
  CHECK(crypto_record_prepare_epoch(&server) == ASCIICHAT_OK);
  CHECK(crypto_record_commit_epoch(&server) == ASCIICHAT_OK);
  buffer_release(next);
  next = NULL;
  CHECK(acip_packet_seal(&server, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, packet, packet_length, &next,
                         &next_length) == ASCIICHAT_OK);
  CHECK(acip_packet_open(&client, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, next, next_length, &plain,
                         &plain_length) == ASCIICHAT_OK);
  buffer_release(plain);
  plain = NULL;
  CHECK(client.key_epoch == 2 && server.key_epoch == 2 &&
        client.receive_high[ACIP_TRAFFIC_AUDIO][ACIP_DELIVERY_DATAGRAM] == 2);
  CHECK(acip_packet_open(&server, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, stale, stale_length, &plain,
                         &plain_length) == ERROR_CRYPTO &&
        !plain);
  buffer_release(stale);
  buffer_release(old_visual);
  buffer_release(visual);
  crypto_record_cleanup(&client);
  crypto_record_cleanup(&server);
  CHECK(!client.crypto && !server.crypto);
  buffer_release(forged);
  buffer_release(next);
  buffer_release(old);
  buffer_release(packet);
  crypto_destroy(&client_crypto);
  crypto_destroy(&server_crypto);
}
