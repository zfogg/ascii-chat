/** Shared packet fixtures and production transport interoperability. */
#include <ascii-chat/network/acip/prepare.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/tcp/async.h>
#include <ascii-chat/network/network.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <string.h>

#define CHECK(expr)                                                                                                    \
  do {                                                                                                                 \
    if (!(expr))                                                                                                       \
      FATAL(ERROR_ASSERTION_FAILED, "line %d: %s", __LINE__, #expr);                                                   \
  } while (0)

static void crypto_pair(crypto_context_t *a, crypto_context_t *b) {
  CHECK(crypto_init(a) == CRYPTO_OK);
  CHECK(crypto_init(b) == CRYPTO_OK);
  CHECK(crypto_set_peer_public_key(a, b->public_key) == CRYPTO_OK);
  CHECK(crypto_set_peer_public_key(b, a->public_key) == CRYPTO_OK);
}

static packet_header_t inner_header(size_t size) {
  return (packet_header_t){.magic = HOST_TO_NET_U64(PACKET_MAGIC),
                           .type = HOST_TO_NET_U16(PACKET_TYPE_PING),
                           .length = HOST_TO_NET_U32((uint32_t)size),
                           .client_id = 7};
}

static void fixtures(void) {
  crypto_context_t crypto, peer;
  crypto_pair(&crypto, &peer);
  packet_header_t header = inner_header(9);
  header.crc32 = HOST_TO_NET_U32(asciichat_crc32("123456789", 9));
  socket_buffer_t slices[] = {{&header, sizeof(header)}, {"123", 3}, {NULL, 0}, {"456789", 6}};
  uint8_t original[sizeof(header) + 9];
  memcpy(original, &header, sizeof(header));
  memcpy(original + sizeof(header), "123456789", 9);
  socket_buffer_t contiguous = {original, sizeof(original)};
  acip_packet_plan_t plan;
  buffer_stats_t before, after;
  buffer_domain_stats(buffer_default_domain(), &before);
  CHECK(acip_packet_plan(NULL, slices, 4, &plan) == ASCIICHAT_OK);
  CHECK(!plan.encrypt && plan.buffers == slices && plan.count == 4);
  buffer_domain_stats(buffer_default_domain(), &after);
  CHECK(before.allocations == after.allocations && before.hits == after.hits);

  // Ready keys alone must not encrypt authentication-only connections.
  {
    crypto.encrypt_data = false;
    CHECK(acip_packet_plan(&crypto, slices, 4, &plan) == ASCIICHAT_OK);
    CHECK(!plan.encrypt);
    crypto.encrypt_data = true;
    crypto.key_exchange_complete = false;
    CHECK(acip_packet_plan(&crypto, slices, 4, &plan) == ASCIICHAT_OK && !plan.encrypt);
    crypto.key_exchange_complete = true;
    // Exercise every handshake exception, rather than duplicating its allowlist.
    unsigned exceptions = 0;
    for (unsigned type = 0; type <= UINT16_MAX; ++type) {
      if (!packet_is_handshake_type((packet_type_t)type))
        continue;
      header.type = HOST_TO_NET_U16(type);
      CHECK(acip_packet_plan(&crypto, slices, 4, &plan) == ASCIICHAT_OK && !plan.encrypt);
      ++exceptions;
    }
    CHECK(exceptions > 0);
    header.type = HOST_TO_NET_U16(PACKET_TYPE_PING);
    for (int vector = 0; vector < 2; ++vector) {
      CHECK(acip_packet_plan(&crypto, vector ? slices : &contiguous, vector ? 4 : 1, &plan) == ASCIICHAT_OK);
      CHECK(plan.encrypt && plan.plaintext_size == sizeof(original));
      uint8_t ciphertext[sizeof(original) + CRYPTO_NONCE_SIZE + CRYPTO_MAC_SIZE];
      uint8_t expected[sizeof(ciphertext)], plaintext[sizeof(original)];
      packet_header_t outer;
      size_t length = 0, expected_length = 0, plain_length = 0;
      crypto_context_t reference = crypto;
      CHECK(crypto_encrypt(&reference, original, sizeof(original), expected, sizeof(expected), &expected_length) ==
            CRYPTO_OK);
      CHECK(acip_packet_encrypt(&crypto, &plan, NULL, ciphertext, sizeof(ciphertext), &outer, &length) == ASCIICHAT_OK);
      CHECK(length == expected_length && memcmp(ciphertext, expected, length) == 0);
      // Exclusive wire buffers decrypt in place, while shared wire owners
      // retain unchanged ciphertext and use the allocation fallback.
      for (unsigned mode = 0; mode < 3; ++mode) {
        size_t wire_size = sizeof(outer) + length;
        uint8_t *wire = buffer_alloc(NULL, wire_size);
        CHECK(wire);
        memcpy(wire, &outer, sizeof(outer));
        memcpy(wire + sizeof(outer), ciphertext, length);
        const void *shared = mode == 1 ? buffer_share(wire) : NULL;
        if (mode == 2)
          wire[wire_size - 1] ^= 1;
        packet_envelope_t envelope = {.type = PACKET_TYPE_ENCRYPTED,
                                      .data = wire + sizeof(outer),
                                      .len = length,
                                      .allocated_buffer = wire,
                                      .allocated_size = wire_size};
        asciichat_error_t result = packet_decrypt_envelope(&envelope, &peer);
        if (mode == 2) {
          CHECK(result == ERROR_CRYPTO && envelope.allocated_buffer == wire);
          CLEAR_ERRNO_ALL();
        } else {
          CHECK(result == ASCIICHAT_OK);
          CHECK((envelope.allocated_buffer == wire) == (mode == 0));
          CHECK(envelope.len == 9 && memcmp(envelope.data, "123456789", 9) == 0);
          if (shared)
            CHECK(memcmp((const uint8_t *)shared + sizeof(outer), ciphertext, length) == 0);
        }
        buffer_release(shared);
        buffer_release(envelope.allocated_buffer);
      }
      CHECK(NET_TO_HOST_U64(outer.magic) == PACKET_MAGIC && NET_TO_HOST_U16(outer.type) == PACKET_TYPE_ENCRYPTED);
      CHECK(NET_TO_HOST_U32(outer.length) == length && outer.client_id == 0);
      CHECK(NET_TO_HOST_U32(outer.crc32) == asciichat_crc32(ciphertext, length));
      CHECK(crypto_decrypt(&peer, ciphertext, length, plaintext, sizeof(plaintext), &plain_length) == CRYPTO_OK);
      CHECK(plain_length == sizeof(original) && memcmp(plaintext, original, plain_length) == 0);
    }
  }

  CHECK(acip_packet_plan(&crypto, slices, 4, &plan) == ASCIICHAT_OK);
  uint8_t output[128];
  packet_header_t outer;
  size_t length = 123;
  uint64_t nonce = crypto.nonce_counter;
  CHECK(acip_packet_encrypt(&crypto, &plan, NULL, output, 1, &outer, &length) == ERROR_NETWORK_SIZE);
  CHECK(length == 0 && crypto.nonce_counter == nonce);
  buffer_domain_t *limited = NULL;
  buffer_domain_config_t config = {.limit_bytes = 1};
  CHECK(buffer_domain_create(&config, &limited) == ASCIICHAT_OK);
  CHECK(acip_packet_encrypt(&crypto, &plan, limited, output, sizeof(output), &outer, &length) == ERROR_MEMORY);
  CHECK(length == 0 && crypto.nonce_counter == nonce);
  buffer_domain_stats(limited, &after);
  CHECK(after.outstanding == 0 && after.failures > 0);
  buffer_domain_close(&limited);
  crypto.nonce_counter = UINT64_MAX;
  CHECK(acip_packet_encrypt(&crypto, &plan, NULL, output, sizeof(output), &outer, &length) == ERROR_CRYPTO);
  CHECK(length == 0);
  crypto.nonce_counter = nonce;
  socket_buffer_t huge[] = {{&header, sizeof(header)}, {"x", CRYPTO_MAX_PLAINTEXT_SIZE - sizeof(header)}};
  CHECK(acip_packet_plan(&crypto, huge, 2, &plan) == ASCIICHAT_OK);
  ++huge[1].len;
  CHECK(acip_packet_plan(&crypto, huge, 2, &plan) == ERROR_NETWORK_SIZE);
  huge[1].len = SIZE_MAX;
  CHECK(acip_packet_plan(NULL, huge, 2, &plan) == ERROR_NETWORK_SIZE);
  CHECK(acip_packet_plan(NULL, NULL, 0, &plan) == ERROR_INVALID_PARAM);
  slices[1] = (socket_buffer_t){NULL, 1};
  CHECK(acip_packet_plan(NULL, slices, 4, &plan) == ERROR_NETWORK_SIZE);
  CHECK(!plan.buffers && !plan.encrypt);
  crypto_destroy(&crypto);
  crypto_destroy(&peer);
  CLEAR_ERRNO_ALL();
}

static void socket_pair(socket_t sockets[2]) {
  socket_t listener = socket_create("prepare-listener", AF_INET, SOCK_STREAM, 0);
  CHECK(listener != INVALID_SOCKET_VALUE);
  struct sockaddr_in addr = {.sin_family = AF_INET, .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  CHECK(socket_bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == 0);
  CHECK(socket_listen(listener, 1) == 0);
  socklen_t length = sizeof(addr);
  CHECK(socket_getsockname(listener, (struct sockaddr *)&addr, &length) == 0);
  sockets[0] = socket_create("prepare-client", AF_INET, SOCK_STREAM, 0);
  CHECK(socket_connect(sockets[0], (struct sockaddr *)&addr, sizeof(addr)) == 0);
  sockets[1] = socket_accept(listener, NULL, NULL, "prepare-peer");
  CHECK(sockets[1] != INVALID_SOCKET_VALUE);
  socket_close(listener);
}

static void tcp_fixture(bool async, bool encrypted) {
  socket_t sockets[2];
  socket_pair(sockets);
  crypto_context_t crypto, peer;
  crypto_pair(&crypto, &peer);
  acip_transport_t *sender = async ? acip_async_tcp_client_create(&sockets[0], encrypted ? &crypto : NULL)
                                   : acip_tcp_transport_create("prepare-send", sockets[0], encrypted ? &crypto : NULL);
  CHECK(sender);
  socket_buffer_t payload[] = {{"123", 3}, {NULL, 0}, {"456789", 6}};
  CHECK(packet_send_via_transportv(sender, PACKET_TYPE_PING, payload, 3, 7) == ASCIICHAT_OK);
  packet_envelope_t envelope = {0};
  CHECK(receive_packet_secure_with_timeout(sockets[1], encrypted ? &peer : NULL, encrypted, &envelope,
                                           5 * NS_PER_SEC_INT) == PACKET_RECV_SUCCESS);
  CHECK(envelope.type == PACKET_TYPE_PING && envelope.len == (encrypted ? 9 : sizeof(packet_header_t) + 9));
  CHECK(memcmp((uint8_t *)envelope.allocated_buffer + sizeof(packet_header_t), "123456789", 9) == 0);
  buffer_release(envelope.allocated_buffer);
  // A rejected frame must not insert a fatal error before the next valid packet.
  packet_header_t header = inner_header(0);
  socket_buffer_t invalid[] = {{&header, sizeof(header)}, {"x", SIZE_MAX}};
  CHECK(acip_transport_sendv(sender, invalid, 2) == ERROR_NETWORK_SIZE);
  CHECK(acip_transport_is_connected(sender));
  CHECK(packet_send_via_transportv(sender, PACKET_TYPE_PING, payload, 3, 7) == ASCIICHAT_OK);
  CHECK(receive_packet_secure_with_timeout(sockets[1], encrypted ? &peer : NULL, encrypted, &envelope,
                                           5 * NS_PER_SEC_INT) == PACKET_RECV_SUCCESS);
  CHECK(envelope.type == PACKET_TYPE_PING && envelope.len == (encrypted ? 9 : sizeof(packet_header_t) + 9));
  CHECK(memcmp((uint8_t *)envelope.allocated_buffer + sizeof(packet_header_t), "123456789", 9) == 0);
  buffer_release(envelope.allocated_buffer);
  acip_transport_destroy(sender);
  if (sockets[0] != INVALID_SOCKET_VALUE)
    socket_close(sockets[0]);
  socket_close(sockets[1]);
  crypto_destroy(&crypto);
  crypto_destroy(&peer);
  CLEAR_ERRNO_ALL();
}

// The Python fixture echoes WebSocket messages; decrypt here to verify the exact
// production transport envelope, including the original header and client ID.
static void retained_async_receive(void) {
  socket_t sockets[2];
  socket_pair(sockets);
  crypto_context_t crypto, peer;
  crypto_pair(&crypto, &peer);
  acip_transport_t *sender = acip_tcp_transport_create("retained-send", sockets[0], &crypto);
  acip_transport_t *receiver = acip_async_tcp_client_create(&sockets[1], &peer);
  CHECK(sender && receiver);
  void *owners[12] = {0};
  for (unsigned i = 0; i < 12; ++i) {
    uint8_t value = (uint8_t)(42 + i);
    CHECK(packet_send_via_transport(sender, PACKET_TYPE_PING, &value, 1, 0) == ASCIICHAT_OK);
    void *data = NULL;
    size_t length = 0;
    CHECK(acip_transport_recv(receiver, &data, &length, &owners[i]) == ASCIICHAT_OK);
    CHECK(length == sizeof(packet_header_t) + 1 && data == owners[i]);
    // More owners than cache slots: no live receiver reference may be reused.
    for (unsigned j = 0; j <= i; ++j)
      CHECK(((uint8_t *)owners[j])[sizeof(packet_header_t)] == 42 + j);
  }
  for (unsigned i = 0; i < 12; ++i)
    buffer_release(owners[i]);
  acip_transport_destroy(receiver);
  acip_transport_destroy(sender);
  socket_close(sockets[0]);
  crypto_destroy(&crypto);
  crypto_destroy(&peer);
}

static void websocket_fixture(const char *url) {
  crypto_context_t crypto, peer;
  crypto_pair(&crypto, &peer);
  acip_transport_t *transport = acip_websocket_client_transport_create("prepare-websocket", url, &crypto);
  CHECK(transport);
  CHECK(packet_send_via_transport(transport, PACKET_TYPE_PING, "123456789", 9, 7) == ASCIICHAT_OK);
  void *data = NULL, *owner = NULL;
  size_t size = 0;
  CHECK(acip_transport_recv(transport, &data, &size, &owner) == ASCIICHAT_OK);
  packet_header_t outer;
  CHECK(size > sizeof(outer));
  memcpy(&outer, data, sizeof(outer));
  CHECK(NET_TO_HOST_U16(outer.type) == PACKET_TYPE_ENCRYPTED);
  CHECK(NET_TO_HOST_U32(outer.length) == size - sizeof(outer));
  CHECK(NET_TO_HOST_U32(outer.crc32) == asciichat_crc32((uint8_t *)data + sizeof(outer), size - sizeof(outer)));
  uint8_t plain[128];
  size_t length = 0;
  CHECK(crypto_decrypt(&peer, (uint8_t *)data + sizeof(outer), size - sizeof(outer), plain, sizeof(plain), &length) ==
        CRYPTO_OK);
  CHECK(length == sizeof(outer) + 9 && memcmp(plain + sizeof(outer), "123456789", 9) == 0);
  buffer_release(owner);
  acip_transport_destroy(transport);
  crypto_destroy(&crypto);
  crypto_destroy(&peer);
}

int main(int argc, char **argv) {
  log_init(NULL, LOG_WARN, false, false);
  asciichat_errno_suppress(true);
  CHECK(socket_init() == ASCIICHAT_OK);
  if (argc == 2) {
    websocket_fixture(argv[1]);
  } else {
    fixtures();
    retained_async_receive();
    for (int async = 0; async < 2; ++async)
      for (int encrypted = 0; encrypted < 2; ++encrypted)
        tcp_fixture(async != 0, encrypted != 0);
  }
  buffer_stats_t stats;
  buffer_domain_stats(buffer_default_domain(), &stats);
  CHECK(stats.outstanding == 0);
  buffer_system_cleanup();
  CHECK(buffer_process_reserved() == 0);
  socket_cleanup();
  log_warn("ACIP preparation checks passed");
  log_destroy();
  return 0;
}
