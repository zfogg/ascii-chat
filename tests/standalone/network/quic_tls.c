#include <ascii-chat/common.h>
#include <ascii-chat/network/acip/backpressure.h>
#include <ascii-chat/network/acip/quic.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/acip/handlers.h>
#include <ascii-chat/audio/limits.h>
#include <ascii-chat/network/connection.h>
#include <ascii-chat/network/negotiation.h>
#include <ascii-chat/crypto/handshake/client.h>
#include <ascii-chat/crypto/handshake/server.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/buffer.h>
#include <network/quic/connection.h>
#include <network/quic/security.h>
#include <network/quic/tls.h>
#include <network/quic/udp.h>
#include <network/quic/worker.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <network/quic/transport.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression)                                                                                              \
  do {                                                                                                                 \
    if (!(expression))                                                                                                 \
      FATAL(ERROR_ASSERTION_FAILED, "line %d: %s", __LINE__, #expression);                                             \
  } while (0)

static EVP_PKEY *make_key(void) {
  EVP_PKEY *key = EVP_PKEY_Q_keygen(NULL, NULL, "EC", "P-256");
  CHECK(key);
  return key;
}

static X509 *make_certificate(EVP_PKEY *key) {
  X509 *cert = X509_new();
  CHECK(cert && X509_set_version(cert, 2) == 1);
  CHECK(ASN1_INTEGER_set(X509_get_serialNumber(cert), 1) == 1);
  CHECK(X509_gmtime_adj(X509_getm_notBefore(cert), -60));
  CHECK(X509_gmtime_adj(X509_getm_notAfter(cert), 3600));
  CHECK(X509_set_pubkey(cert, key) == 1);
  X509_NAME *subject = X509_get_subject_name(cert);
  CHECK(subject &&
        X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC, (const unsigned char *)"localhost", -1, -1, 0) == 1);
  CHECK(X509_set_issuer_name(cert, subject) == 1);
  X509V3_CTX extension_context;
  X509V3_set_ctx(&extension_context, cert, cert, NULL, NULL, 0);
  X509_EXTENSION *extension =
      X509V3_EXT_conf_nid(NULL, &extension_context, NID_subject_alt_name, "DNS:localhost,IP:127.0.0.1");
  CHECK(extension && X509_add_ext(cert, extension, -1) == 1);
  X509_EXTENSION_free(extension);
  extension = X509V3_EXT_conf_nid(NULL, &extension_context, NID_basic_constraints, "critical,CA:TRUE");
  CHECK(extension && X509_add_ext(cert, extension, -1) == 1);
  X509_EXTENSION_free(extension);
  CHECK(X509_sign(cert, key, EVP_sha256()) > 0);
  return cert;
}

static void write_bio(const char *path, BIO *bio) {
  char *data = NULL;
  long size = BIO_get_mem_data(bio, &data);
  CHECK(size > 0 && data);
  int fd = platform_open("quic_test_pem", path,
                         PLATFORM_O_WRONLY | PLATFORM_O_CREAT | PLATFORM_O_TRUNC | PLATFORM_O_BINARY, 0600);
  CHECK(fd >= 0);
  size_t offset = 0;
  while (offset < (size_t)size) {
    ssize_t written = platform_write(fd, data + offset, (size_t)size - offset);
    CHECK(written > 0);
    offset += (size_t)written;
  }
  CHECK(platform_close(fd) == 0);
  BIO_free(bio);
}

static void write_certificate(const char *path, X509 *cert) {
  BIO *bio = BIO_new(BIO_s_mem());
  CHECK(bio && PEM_write_bio_X509(bio, cert) == 1);
  write_bio(path, bio);
}

static void write_key(const char *path, EVP_PKEY *key) {
  BIO *bio = BIO_new(BIO_s_mem());
  CHECK(bio && PEM_write_bio_PrivateKey(bio, key, NULL, NULL, 0, NULL, NULL) == 1);
  write_bio(path, bio);
}

/* This uses TLS over an in-memory BIO to test certificate identity and ALPN.
 * The ngtcp2 callback configuration is checked separately below. */
static bool handshake(acip_quic_tls_t *server, acip_quic_tls_t *client, const char *server_name, bool omit_alpn,
                      bool omit_identity, bool change_identity_after_handshake) {
  SSL *client_ssl = SSL_new(client->ssl_ctx);
  SSL *server_ssl = SSL_new(server->ssl_ctx);
  CHECK(client_ssl && server_ssl);
  if (omit_alpn) {
    CHECK(SSL_set1_host(client_ssl, server_name) == 1);
    CHECK(SSL_set_tlsext_host_name(client_ssl, server_name) == 1);
  } else if (omit_identity) {
    CHECK(SSL_set_alpn_protos(client_ssl, client->alpn, client->alpn_length) == 0);
    CHECK(SSL_set_tlsext_host_name(client_ssl, server_name) == 1);
  } else {
    CHECK(acip_quic_tls_client_configure(client, client_ssl, server_name) == ASCIICHAT_OK);
  }
  BIO *client_bio = NULL, *server_bio = NULL;
  CHECK(BIO_new_bio_pair(&client_bio, 0, &server_bio, 0) == 1);
  SSL_set_bio(client_ssl, client_bio, client_bio);
  SSL_set_bio(server_ssl, server_bio, server_bio);
  SSL_set_connect_state(client_ssl);
  SSL_set_accept_state(server_ssl);
  bool client_done = false, server_done = false, failed = false;
  for (unsigned step = 0; step < 100 && !failed && !(client_done && server_done); ++step) {
    if (!client_done) {
      int result = SSL_do_handshake(client_ssl);
      if (result == 1)
        client_done = true;
      else {
        int error = SSL_get_error(client_ssl, result);
        failed = error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE;
      }
    }
    if (!server_done && !failed) {
      int result = SSL_do_handshake(server_ssl);
      if (result == 1)
        server_done = true;
      else {
        int error = SSL_get_error(server_ssl, result);
        failed = error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE;
      }
    }
  }
  bool success = !failed && client_done && server_done;
  if (success && change_identity_after_handshake)
    CHECK(SSL_set1_host(client_ssl, "other.example") == 1);
  if (success && (acip_quic_tls_verify_ready(server, server_ssl) != ASCIICHAT_OK ||
                  acip_quic_tls_verify_ready(client, client_ssl) != ASCIICHAT_OK))
    success = false;
  if (success) {
    const unsigned char *selected = NULL;
    unsigned int selected_length = 0;
    SSL_get0_alpn_selected(client_ssl, &selected, &selected_length);
    CHECK(SSL_version(client_ssl) == TLS1_3_VERSION && SSL_get_verify_result(client_ssl) == X509_V_OK);
    CHECK(selected_length == server->alpn_length - 1 && memcmp(selected, server->alpn + 1, selected_length) == 0);
  }
  SSL_free(client_ssl);
  SSL_free(server_ssl);
  return success;
}

static ngtcp2_conn *unused_connection(ngtcp2_crypto_conn_ref *ref) {
  (void)ref;
  return NULL;
}

typedef struct {
  const uint8_t *packets[6];
  size_t lengths[6];
  unsigned count, visual_count;
  acip_traffic_class_t order[6];
} media_capture_t;

static asciichat_error_t capture_quic_packet(acip_traffic_class_t traffic_class, const void *packet, size_t length,
                                             void *arg) {
  media_capture_t *capture = arg;
  if ((unsigned)traffic_class >= 3 || capture->count >= 6)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unexpected QUIC stream packet");
  size_t index = traffic_class == ACIP_TRAFFIC_VISUAL ? 2 + capture->visual_count++ : (size_t)traffic_class;
  if (index >= 6 || length != capture->lengths[index] || memcmp(packet, capture->packets[index], length) != 0)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unexpected QUIC stream packet");
  capture->order[capture->count++] = traffic_class;
  return ASCIICHAT_OK;
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

static void quic_secure_framing(void) {
  crypto_context_t client_crypto, server_crypto;
  CHECK(crypto_init(&client_crypto) == CRYPTO_OK && crypto_init(&server_crypto) == CRYPTO_OK);
  CHECK(crypto_set_peer_public_key(&client_crypto, server_crypto.public_key) == CRYPTO_OK);
  CHECK(crypto_set_peer_public_key(&server_crypto, client_crypto.public_key) == CRYPTO_OK);
  CHECK(memcmp(server_crypto.shared_key, client_crypto.shared_key, sizeof(client_crypto.shared_key)) == 0);
  acip_quic_security_t client = {0}, server = {0};
  CHECK(acip_quic_security_init(&client, &client_crypto, false) == ERROR_INVALID_STATE);
  client_crypto.handshake_complete = server_crypto.handshake_complete = true;
  CHECK(acip_quic_security_init(&client, &client_crypto, false) == ASCIICHAT_OK);
  CHECK(acip_quic_security_init(&server, &server_crypto, true) == ASCIICHAT_OK);
  CHECK(acip_quic_security_init(&server, &server_crypto, true) == ERROR_INVALID_STATE);
  size_t audio_length = 0, control_length = 0;
  uint8_t *audio = make_packet(PACKET_TYPE_AUDIO_BATCH, 16, &audio_length);
  uint8_t *control = make_packet(PACKET_TYPE_PING, 8, &control_length);
  uint8_t *wire[3] = {NULL, NULL, NULL}, *plain = NULL;
  size_t wire_length[3] = {0}, plain_length = 0;
  for (size_t i = 0; i < 3; ++i)
    CHECK(acip_quic_security_seal(&client, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, audio, audio_length, &wire[i],
                                  &wire_length[i]) == ASCIICHAT_OK);
  CHECK(memcmp(wire[0] + sizeof(packet_header_t), wire[1] + sizeof(packet_header_t), CRYPTO_NONCE_SIZE) != 0);
  CHECK(acip_quic_security_open(&server, ACIP_TRAFFIC_VISUAL, ACIP_DELIVERY_DATAGRAM, wire[1], wire_length[1], &plain,
                                &plain_length) == ERROR_NETWORK_PROTOCOL &&
        !plain);
  CHECK(acip_quic_security_open(&server, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_RELIABLE, wire[1], wire_length[1], &plain,
                                &plain_length) == ERROR_NETWORK_PROTOCOL &&
        !plain);
  CHECK(acip_quic_security_open(&client, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, wire[1], wire_length[1], &plain,
                                &plain_length) == ERROR_NETWORK_PROTOCOL &&
        !plain);
  CHECK(acip_quic_security_open(&server, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, wire[1], wire_length[1], &plain,
                                &plain_length) == ASCIICHAT_OK);
  CHECK(plain_length == audio_length && memcmp(plain, audio, audio_length) == 0);
  buffer_release(plain);
  plain = NULL;
  CHECK(acip_quic_security_open(&server, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, wire[0], wire_length[0], &plain,
                                &plain_length) == ASCIICHAT_OK);
  buffer_release(plain);
  plain = NULL;
  CHECK(acip_quic_security_open(&server, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, wire[1], wire_length[1], &plain,
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
  CHECK(acip_quic_security_open(&server, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, tampered, wire_length[2], &plain,
                                &plain_length) == ERROR_CRYPTO &&
        !plain);
  CHECK(acip_quic_security_open(&server, ACIP_TRAFFIC_AUDIO, ACIP_DELIVERY_DATAGRAM, wire[2], wire_length[2], &plain,
                                &plain_length) == ASCIICHAT_OK);
  buffer_release(plain);
  buffer_release(tampered);
  for (size_t i = 0; i < 3; ++i)
    buffer_release(wire[i]);
  wire[0] = NULL;
  CHECK(acip_quic_security_seal(&client, ACIP_TRAFFIC_CONTROL, ACIP_DELIVERY_DATAGRAM, control, control_length,
                                &wire[0], &wire_length[0]) == ERROR_INVALID_STATE);
  CHECK(acip_quic_security_seal(&client, ACIP_TRAFFIC_CONTROL, ACIP_DELIVERY_RELIABLE, control, control_length,
                                &wire[0], &wire_length[0]) == ASCIICHAT_OK);
  CHECK(acip_quic_security_open(&server, ACIP_TRAFFIC_CONTROL, ACIP_DELIVERY_RELIABLE, wire[0], wire_length[0], &plain,
                                &plain_length) == ASCIICHAT_OK);
  buffer_release(plain);
  plain = NULL;
  CHECK(acip_quic_security_open(&server, ACIP_TRAFFIC_CONTROL, ACIP_DELIVERY_RELIABLE, wire[0], wire_length[0], &plain,
                                &plain_length) == ERROR_CRYPTO &&
        !plain);
  uint8_t *reply = NULL;
  size_t reply_length = 0;
  CHECK(acip_quic_security_seal(&server, ACIP_TRAFFIC_CONTROL, ACIP_DELIVERY_RELIABLE, control, control_length, &reply,
                                &reply_length) == ASCIICHAT_OK);
  CHECK(acip_quic_security_open(&client, ACIP_TRAFFIC_CONTROL, ACIP_DELIVERY_RELIABLE, reply, reply_length, &plain,
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
    CHECK(acip_quic_security_seal(&client, ACIP_TRAFFIC_VISUAL, ACIP_DELIVERY_DATAGRAM, visual, visual_length, &next,
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
  CHECK(acip_quic_security_open(&server, ACIP_TRAFFIC_VISUAL, ACIP_DELIVERY_DATAGRAM, last, last_length, &plain,
                                &plain_length) == ASCIICHAT_OK);
  buffer_release(plain);
  plain = NULL;
  CHECK(acip_quic_security_open(&server, ACIP_TRAFFIC_VISUAL, ACIP_DELIVERY_DATAGRAM, first, first_length, &plain,
                                &plain_length) == ERROR_CRYPTO &&
        !plain);
  CHECK(acip_quic_security_open(&server, ACIP_TRAFFIC_VISUAL, ACIP_DELIVERY_DATAGRAM, within_window,
                                within_window_length, &plain, &plain_length) == ASCIICHAT_OK);
  buffer_release(plain);
  plain = NULL;
  CHECK(acip_quic_security_open(&server, ACIP_TRAFFIC_VISUAL, ACIP_DELIVERY_DATAGRAM, within_window,
                                within_window_length, &plain, &plain_length) == ERROR_CRYPTO &&
        !plain);
  buffer_release(first);
  buffer_release(within_window);
  buffer_release(last);
  buffer_release(visual);
  server_crypto.rekey_in_progress = true;
  CHECK(acip_quic_security_open(&server, ACIP_TRAFFIC_CONTROL, ACIP_DELIVERY_RELIABLE, wire[0], wire_length[0], &plain,
                                &plain_length) == ERROR_INVALID_STATE &&
        !plain);
  server_crypto.rekey_in_progress = false;
  ++server_crypto.rekey_count;
  CHECK(acip_quic_security_open(&server, ACIP_TRAFFIC_CONTROL, ACIP_DELIVERY_RELIABLE, wire[0], wire_length[0], &plain,
                                &plain_length) == ERROR_INVALID_STATE &&
        !plain);
  buffer_release(wire[0]);
  buffer_release(audio);
  buffer_release(control);
  crypto_destroy(&server_crypto);
  crypto_destroy(&client_crypto);
}

static bool contains_bytes(const uint8_t *bytes, size_t length, const uint8_t *needle, size_t needle_length) {
  if (needle_length > length)
    return false;
  for (size_t i = 0; i <= length - needle_length; ++i)
    if (memcmp(bytes + i, needle, needle_length) == 0)
      return true;
  return false;
}

static void drive_barrier_step(acip_quic_connection_t *client, acip_quic_connection_t *server,
                               const ngtcp2_path *client_path, const ngtcp2_path *server_path, uint64_t now,
                               bool *drop_client_control) {
  acip_quic_connection_t *senders[] = {client, server};
  acip_quic_connection_t *receivers[] = {server, client};
  const ngtcp2_path *paths[] = {server_path, client_path};
  for (size_t i = 0; i < 2; ++i) {
    uint8_t packet[1452];
    size_t length = 0;
    ngtcp2_path_storage path;
    CHECK(acip_quic_connection_write(senders[i], &path, packet, sizeof(packet), &length, now) == ASCIICHAT_OK);
    if (length) {
      acip_traffic_class_t traffic_class;
      bool drop = i == 0 && drop_client_control && *drop_client_control &&
                  acip_quic_connection_pending_packet_class(senders[i], &traffic_class) &&
                  traffic_class == ACIP_TRAFFIC_CONTROL;
      CHECK(acip_quic_connection_packet_sent(senders[i], now) == ASCIICHAT_OK);
      if (drop)
        *drop_client_control = false;
      else
        CHECK(acip_quic_connection_receive(receivers[i], paths[i], packet, length, now) == ASCIICHAT_OK);
    }
    if (acip_quic_connection_expiry(senders[i]) <= now)
      CHECK(acip_quic_connection_handle_expiry(senders[i], now) == ASCIICHAT_OK);
  }
}

typedef struct {
  unsigned received, requests;
} visual_recovery_capture_t;

static asciichat_error_t capture_recovery_packet(acip_traffic_class_t flow, const void *packet, size_t length,
                                                 void *context) {
  visual_recovery_capture_t *capture = context;
  if (flow != ACIP_TRAFFIC_VISUAL || length < sizeof(packet_header_t))
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unexpected recovery packet");
  (void)packet;
  ++capture->received;
  return ASCIICHAT_OK;
}

static void capture_keyframe_request(uint16_t type, uint32_t id, void *context) {
  visual_recovery_capture_t *capture = context;
  if (type == PACKET_TYPE_IMAGE_FRAME_H264 && id == 0)
    ++capture->requests;
}

static void set_recovery_keyframe(uint8_t *packet, size_t length, bool keyframe) {
  packet_header_t header;
  memcpy(&header, packet, sizeof(header));
  packet[sizeof(header)] = keyframe ? 1 : 0;
  header.crc32 = HOST_TO_NET_U32(asciichat_crc32(packet + sizeof(header), length - sizeof(header)));
  memcpy(packet, &header, sizeof(header));
}

static void quic_media_exchange(acip_quic_connection_t *client, acip_quic_connection_t *server,
                                const ngtcp2_path *client_path, const ngtcp2_path *server_path, uint64_t *clock_ns,
                                bool drop_control, crypto_context_t *client_crypto, crypto_context_t *server_crypto) {
  uint8_t *packets[6];
  size_t lengths[6];
  packets[0] = make_packet(PACKET_TYPE_PING, 8, &lengths[0]);
  packets[1] = make_packet(PACKET_TYPE_AUDIO_BATCH, 48, &lengths[1]);
  packets[2] = make_packet(PACKET_TYPE_IMAGE_FRAME, 65536, &lengths[2]);
  packets[3] = make_packet(PACKET_TYPE_ASCII_FRAME, 256, &lengths[3]);
  packets[4] = make_packet(PACKET_TYPE_IMAGE_FRAME_H264, 1024, &lengths[4]);
  packets[5] = make_packet(PACKET_TYPE_IMAGE_FRAME_H265, 1024, &lengths[5]);
  media_capture_t at_client = {0}, at_server = {0};
  for (size_t i = 0; i < 6; ++i) {
    at_client.packets[i] = at_server.packets[i] = packets[i];
    at_client.lengths[i] = at_server.lengths[i] = lengths[i];
  }
  CHECK(acip_quic_connection_set_receiver(client, capture_quic_packet, &at_client) == ASCIICHAT_OK);
  CHECK(acip_quic_connection_set_receiver(server, capture_quic_packet, &at_server) == ASCIICHAT_OK);
  CHECK(acip_quic_connection_submit(client, ACIP_TRAFFIC_AUDIO, packets[1], lengths[1]) == ERROR_INVALID_STATE);
  CHECK(acip_quic_connection_allow_media(client) == ERROR_INVALID_STATE);
  CHECK(acip_quic_connection_enable_security(client, client_crypto) == ASCIICHAT_OK);
  CHECK(acip_quic_connection_allow_media(client) == ERROR_INVALID_STATE);
  CHECK(acip_quic_connection_submit(client, ACIP_TRAFFIC_CONTROL, packets[0], lengths[0]) == ERROR_INVALID_STATE);
  bool drop_barrier_packet = drop_control;
  for (unsigned step = 0; step < 100; ++step, *clock_ns += NS_PER_MS_INT)
    drive_barrier_step(client, server, client_path, server_path, *clock_ns, &drop_barrier_packet);
  CHECK(acip_quic_connection_enable_security(server, server_crypto) == ASCIICHAT_OK);
  for (unsigned step = 0;
       step < 10000 && (!acip_quic_connection_security_ready(client) || !acip_quic_connection_security_ready(server));
       ++step, *clock_ns += NS_PER_MS_INT)
    drive_barrier_step(client, server, client_path, server_path, *clock_ns, &drop_barrier_packet);
  CHECK(acip_quic_connection_security_ready(client) && acip_quic_connection_security_ready(server));
  CHECK(!drop_barrier_packet);
  size_t reserved_length = 0;
  uint8_t *reserved = make_packet(PACKET_TYPE_QUIC_READY, 5, &reserved_length);
  CHECK(acip_quic_connection_submit(client, ACIP_TRAFFIC_CONTROL, reserved, reserved_length) == ERROR_INVALID_PARAM);
  buffer_release(reserved);
  CHECK(acip_quic_connection_allow_media(client) == ASCIICHAT_OK);
  CHECK(acip_quic_connection_allow_media(server) == ASCIICHAT_OK);
  acip_quic_connection_t *peers[] = {client, server};
  for (size_t peer = 0; peer < 2; ++peer) {
    for (size_t visual = 2; visual < 6; ++visual)
      CHECK(acip_quic_connection_submit(peers[peer], ACIP_TRAFFIC_VISUAL, packets[visual], lengths[visual]) ==
            ASCIICHAT_OK);
    CHECK(acip_quic_connection_submit(peers[peer], ACIP_TRAFFIC_AUDIO, packets[1], lengths[1]) == ASCIICHAT_OK);
    CHECK(acip_quic_connection_submit(peers[peer], ACIP_TRAFFIC_CONTROL, packets[0], lengths[0]) == ASCIICHAT_OK);
    CHECK(acip_quic_connection_submit(peers[peer], ACIP_TRAFFIC_CONTROL, packets[2], lengths[2]) ==
          ERROR_NETWORK_PROTOCOL);
  }

  bool control_dropped = false, audio_duplicated = false;
  for (unsigned step = 0; step < 30000; ++step, *clock_ns += NS_PER_MS_INT) {
    uint8_t packet[1452];
    size_t length;
    ngtcp2_path_storage path;
    CHECK(acip_quic_connection_write(client, &path, packet, sizeof(packet), &length, *clock_ns) == ASCIICHAT_OK);
    if (length) {
      CHECK(!contains_bytes(packet, length, packets[0], lengths[0]));
      CHECK(!contains_bytes(packet, length, packets[2] + sizeof(packet_header_t), 32));
      acip_traffic_class_t packet_class;
      bool has_class = acip_quic_connection_pending_packet_class(client, &packet_class);
      bool drop = drop_control && !control_dropped && has_class && packet_class == ACIP_TRAFFIC_CONTROL;
      CHECK(acip_quic_connection_packet_sent(client, *clock_ns) == ASCIICHAT_OK);
      if (drop)
        control_dropped = true;
      else {
        CHECK(acip_quic_connection_receive(server, server_path, packet, length, *clock_ns) == ASCIICHAT_OK);
        if (!audio_duplicated && has_class && packet_class == ACIP_TRAFFIC_AUDIO) {
          CHECK(acip_quic_connection_receive(server, server_path, packet, length, *clock_ns) == ASCIICHAT_OK);
          audio_duplicated = true;
        }
      }
    }
    /* Hold server packets until an independent stream arrives, so their ACKs
     * cannot trigger control retransmission before the assertion is observed. */
    if (!drop_control || !control_dropped || at_server.count) {
      CHECK(acip_quic_connection_write(server, &path, packet, sizeof(packet), &length, *clock_ns) == ASCIICHAT_OK);
      if (length) {
        CHECK(!contains_bytes(packet, length, packets[0], lengths[0]));
        CHECK(!contains_bytes(packet, length, packets[2] + sizeof(packet_header_t), 32));
        CHECK(acip_quic_connection_packet_sent(server, *clock_ns) == ASCIICHAT_OK);
        CHECK(acip_quic_connection_receive(client, client_path, packet, length, *clock_ns) == ASCIICHAT_OK);
      }
    }
    if (at_client.count == 6 && at_server.count == 6 && !acip_quic_connection_pending_bytes(client) &&
        !acip_quic_connection_pending_bytes(server))
      break;
    if (acip_quic_connection_expiry(client) <= *clock_ns)
      CHECK(acip_quic_connection_handle_expiry(client, *clock_ns) == ASCIICHAT_OK);
    if (acip_quic_connection_expiry(server) <= *clock_ns)
      CHECK(acip_quic_connection_handle_expiry(server, *clock_ns) == ASCIICHAT_OK);
  }
  CHECK(at_client.count == 6 && at_server.count == 6);
  CHECK(audio_duplicated);
  CHECK(!drop_control || control_dropped);
  CHECK(!acip_quic_connection_pending_bytes(client) && !acip_quic_connection_pending_bytes(server));
  CHECK(acip_quic_connection_finish_stream(server, ACIP_TRAFFIC_AUDIO) == ASCIICHAT_OK);
  CHECK(acip_quic_connection_finish_stream(server, ACIP_TRAFFIC_AUDIO) == ERROR_INVALID_STATE);
  CHECK(acip_quic_connection_submit(server, ACIP_TRAFFIC_AUDIO, packets[1], lengths[1]) == ERROR_INVALID_STATE);
  bool fin_dropped = false;
  for (unsigned step = 0; step < 10000 && !acip_quic_connection_received_fin(client, ACIP_TRAFFIC_AUDIO);
       ++step, *clock_ns += NS_PER_MS_INT) {
    uint8_t datagram[1452];
    size_t datagram_length;
    ngtcp2_path_storage path;
    CHECK(acip_quic_connection_write(client, &path, datagram, sizeof(datagram), &datagram_length, *clock_ns) ==
          ASCIICHAT_OK);
    if (datagram_length) {
      CHECK(acip_quic_connection_packet_sent(client, *clock_ns) == ASCIICHAT_OK);
      CHECK(acip_quic_connection_receive(server, server_path, datagram, datagram_length, *clock_ns) == ASCIICHAT_OK);
    }
    CHECK(acip_quic_connection_write(server, &path, datagram, sizeof(datagram), &datagram_length, *clock_ns) ==
          ASCIICHAT_OK);
    if (datagram_length) {
      acip_traffic_class_t packet_class;
      bool drop = !fin_dropped && acip_quic_connection_pending_packet_class(server, &packet_class) &&
                  packet_class == ACIP_TRAFFIC_AUDIO;
      CHECK(acip_quic_connection_packet_sent(server, *clock_ns) == ASCIICHAT_OK);
      if (drop)
        fin_dropped = true;
      else
        CHECK(acip_quic_connection_receive(client, client_path, datagram, datagram_length, *clock_ns) == ASCIICHAT_OK);
    }
    if (acip_quic_connection_expiry(client) <= *clock_ns)
      CHECK(acip_quic_connection_handle_expiry(client, *clock_ns) == ASCIICHAT_OK);
    if (acip_quic_connection_expiry(server) <= *clock_ns)
      CHECK(acip_quic_connection_handle_expiry(server, *clock_ns) == ASCIICHAT_OK);
  }
  CHECK(fin_dropped && acip_quic_connection_received_fin(client, ACIP_TRAFFIC_AUDIO));
  CHECK(!acip_quic_connection_received_fin(client, ACIP_TRAFFIC_CONTROL));
  for (size_t i = 0; i < 6; ++i) {
    acip_traffic_class_t expected = i < 2 ? (acip_traffic_class_t)i : ACIP_TRAFFIC_VISUAL;
    CHECK(at_client.order[i] == expected);
    if (!drop_control)
      CHECK(at_server.order[i] == expected);
  }
  if (drop_control && at_server.order[0] != ACIP_TRAFFIC_AUDIO)
    FATAL(ERROR_ASSERTION_FAILED, "Unexpected receive order %d, %d, %d after dropping control", at_server.order[0],
          at_server.order[1], at_server.order[2]);
  CHECK(acip_quic_connection_clear_receiver(client, &at_client) == ASCIICHAT_OK);
  CHECK(acip_quic_connection_clear_receiver(server, &at_server) == ASCIICHAT_OK);
  media_capture_t datagram_client = {0}, datagram_server = {0};
  datagram_client.packets[1] = datagram_server.packets[1] = packets[1];
  datagram_client.lengths[1] = datagram_server.lengths[1] = lengths[1];
  datagram_client.packets[2] = datagram_server.packets[2] = packets[2];
  datagram_client.lengths[2] = datagram_server.lengths[2] = lengths[2];
  CHECK(acip_quic_connection_set_receiver(client, capture_quic_packet, &datagram_client) == ASCIICHAT_OK);
  CHECK(acip_quic_connection_set_receiver(server, capture_quic_packet, &datagram_server) == ASCIICHAT_OK);
  CHECK(acip_quic_connection_datagram_payload(client) > 0 && acip_quic_connection_datagram_payload(server) > 0);
  for (size_t peer = 0; peer < 2; ++peer) {
    CHECK(acip_quic_connection_submit_datagram(peers[peer], ACIP_TRAFFIC_AUDIO, packets[1], lengths[1], 1) ==
          ERROR_NETWORK_TIMEOUT);
    CHECK(acip_quic_connection_submit_datagram(peers[peer], ACIP_TRAFFIC_AUDIO, packets[1], lengths[1], 0) ==
          ASCIICHAT_OK);
    CHECK(acip_quic_connection_submit_datagram(peers[peer], ACIP_TRAFFIC_VISUAL, packets[2], lengths[2], 0) ==
          ASCIICHAT_OK);
  }
  for (unsigned step = 0; step < 10000 && (datagram_client.count < 2 || datagram_server.count < 2);
       ++step, *clock_ns += NS_PER_MS_INT)
    drive_barrier_step(client, server, client_path, server_path, *clock_ns, NULL);
  CHECK(datagram_client.count == 2 && datagram_server.count == 2);
  datagram_server.count = datagram_server.visual_count = 0;
  CHECK(acip_quic_connection_submit_datagram(client, ACIP_TRAFFIC_VISUAL, packets[2], lengths[2], 0) == ASCIICHAT_OK);
  CHECK(acip_quic_connection_submit_datagram(client, ACIP_TRAFFIC_AUDIO, packets[1], lengths[1], 0) == ASCIICHAT_OK);
  bool visual_dropped = false;
  for (unsigned step = 0; step < 2500; ++step, *clock_ns += NS_PER_MS_INT) {
    uint8_t output[1452];
    size_t output_length = 0;
    ngtcp2_path_storage output_path;
    CHECK(acip_quic_connection_write(client, &output_path, output, sizeof(output), &output_length, *clock_ns) ==
          ASCIICHAT_OK);
    if (output_length) {
      acip_traffic_class_t flow;
      bool drop =
          !visual_dropped && acip_quic_connection_pending_packet_class(client, &flow) && flow == ACIP_TRAFFIC_VISUAL;
      CHECK(acip_quic_connection_packet_sent(client, *clock_ns) == ASCIICHAT_OK);
      if (drop)
        visual_dropped = true;
      else
        CHECK(acip_quic_connection_receive(server, server_path, output, output_length, *clock_ns) == ASCIICHAT_OK);
    }
    CHECK(acip_quic_connection_write(server, &output_path, output, sizeof(output), &output_length, *clock_ns) ==
          ASCIICHAT_OK);
    if (output_length) {
      CHECK(acip_quic_connection_packet_sent(server, *clock_ns) == ASCIICHAT_OK);
      CHECK(acip_quic_connection_receive(client, client_path, output, output_length, *clock_ns) == ASCIICHAT_OK);
    }
    if (acip_quic_connection_expiry(client) <= *clock_ns)
      CHECK(acip_quic_connection_handle_expiry(client, *clock_ns) == ASCIICHAT_OK);
    if (acip_quic_connection_expiry(server) <= *clock_ns)
      CHECK(acip_quic_connection_handle_expiry(server, *clock_ns) == ASCIICHAT_OK);
  }
  CHECK(visual_dropped && datagram_server.count == 1 && datagram_server.order[0] == ACIP_TRAFFIC_AUDIO);
  CHECK(acip_quic_connection_submit_datagram(client, ACIP_TRAFFIC_VISUAL, packets[2], lengths[2], 0) == ASCIICHAT_OK);
  for (unsigned step = 0; step < 10000 && datagram_server.count < 2; ++step, *clock_ns += NS_PER_MS_INT)
    drive_barrier_step(client, server, client_path, server_path, *clock_ns, NULL);
  CHECK(datagram_server.count == 2 && datagram_server.order[1] == ACIP_TRAFFIC_VISUAL);
  CHECK(acip_quic_connection_clear_receiver(client, &datagram_client) == ASCIICHAT_OK);
  CHECK(acip_quic_connection_clear_receiver(server, &datagram_server) == ASCIICHAT_OK);
  visual_recovery_capture_t recovery = {0};
  CHECK(acip_quic_connection_set_receiver(server, capture_recovery_packet, &recovery) == ASCIICHAT_OK);
  CHECK(acip_quic_connection_set_receiver(client, capture_recovery_packet, &recovery) == ASCIICHAT_OK);
  acip_quic_connection_set_keyframe_notifier(client, capture_keyframe_request, &recovery);
  size_t encoded_length = 0;
  uint8_t *encoded = make_packet(PACKET_TYPE_IMAGE_FRAME_H264, 32, &encoded_length);
  set_recovery_keyframe(encoded, encoded_length, true);
  CHECK(acip_quic_connection_submit_datagram(client, ACIP_TRAFFIC_VISUAL, encoded, encoded_length, 0) == ASCIICHAT_OK);
  for (unsigned step = 0; step < 10000 && recovery.received < 1; ++step, *clock_ns += NS_PER_MS_INT)
    drive_barrier_step(client, server, client_path, server_path, *clock_ns, NULL);
  CHECK(recovery.received == 1);
  set_recovery_keyframe(encoded, encoded_length, false);
  CHECK(acip_quic_connection_submit_datagram(client, ACIP_TRAFFIC_VISUAL, encoded, encoded_length, 0) == ASCIICHAT_OK);
  bool dropped_encoded = false;
  for (unsigned step = 0; step < 10000 && !dropped_encoded; ++step, *clock_ns += NS_PER_MS_INT) {
    uint8_t output[1452];
    size_t output_length = 0;
    ngtcp2_path_storage output_path;
    CHECK(acip_quic_connection_write(client, &output_path, output, sizeof(output), &output_length, *clock_ns) ==
          ASCIICHAT_OK);
    if (output_length) {
      acip_traffic_class_t flow;
      bool drop = acip_quic_connection_pending_packet_class(client, &flow) && flow == ACIP_TRAFFIC_VISUAL;
      CHECK(acip_quic_connection_packet_sent(client, *clock_ns) == ASCIICHAT_OK);
      if (drop)
        dropped_encoded = true;
      else
        CHECK(acip_quic_connection_receive(server, server_path, output, output_length, *clock_ns) == ASCIICHAT_OK);
    }
    if (acip_quic_connection_expiry(client) <= *clock_ns)
      CHECK(acip_quic_connection_handle_expiry(client, *clock_ns) == ASCIICHAT_OK);
  }
  CHECK(dropped_encoded);
  CHECK(acip_quic_connection_submit_datagram(client, ACIP_TRAFFIC_VISUAL, encoded, encoded_length, 0) == ASCIICHAT_OK);
  for (unsigned step = 0; step < 10000 && recovery.requests < 1; ++step, *clock_ns += NS_PER_MS_INT)
    drive_barrier_step(client, server, client_path, server_path, *clock_ns, NULL);
  CHECK(recovery.requests == 1 && recovery.received == 1);
  set_recovery_keyframe(encoded, encoded_length, true);
  CHECK(acip_quic_connection_submit_datagram(client, ACIP_TRAFFIC_VISUAL, encoded, encoded_length, 0) == ASCIICHAT_OK);
  for (unsigned step = 0; step < 10000 && recovery.received < 2; ++step, *clock_ns += NS_PER_MS_INT)
    drive_barrier_step(client, server, client_path, server_path, *clock_ns, NULL);
  CHECK(recovery.received == 2);
  buffer_release(encoded);
  acip_quic_connection_set_keyframe_notifier(client, NULL, NULL);
  CHECK(acip_quic_connection_clear_receiver(client, &recovery) == ASCIICHAT_OK);
  CHECK(acip_quic_connection_clear_receiver(server, &recovery) == ASCIICHAT_OK);
  if (!drop_control) {
    size_t large_length = 0;
    uint8_t *large = make_packet(PACKET_TYPE_IMAGE_FRAME, 512 * 1024, &large_length);
    bool full = false;
    for (unsigned i = 0; i < 64; ++i) {
      size_t before = acip_quic_connection_pending_bytes(client);
      asciichat_error_t result = acip_quic_connection_submit(client, ACIP_TRAFFIC_VISUAL, large, large_length);
      if (result == ERROR_BUFFER_FULL) {
        CHECK(acip_quic_connection_pending_bytes(client) == before);
        full = true;
        break;
      }
      CHECK(result == ASCIICHAT_OK);
    }
    CHECK(full);
    CHECK(acip_quic_connection_submit(client, ACIP_TRAFFIC_AUDIO, at_client.packets[1], lengths[1]) == ASCIICHAT_OK);
    CHECK(acip_quic_connection_submit(client, ACIP_TRAFFIC_CONTROL, at_client.packets[0], lengths[0]) == ASCIICHAT_OK);
    CHECK(acip_quic_connection_pending_bytes(client) <= ACIP_OUTBOUND_BUDGET);
    buffer_release(large);
  }
  for (size_t i = 0; i < 6; ++i)
    buffer_release(packets[i]);
}

static bool quic_handshake(acip_quic_tls_t *server_tls, acip_quic_tls_t *client_tls, const char *server_name,
                           bool drop_first_server_packet, bool drop_control, bool *dropped,
                           asciichat_error_t *failure) {
  struct sockaddr_in client_address = {
      .sin_family = AF_INET, .sin_port = HOST_TO_NET_U16(23456), .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  struct sockaddr_in server_address = {
      .sin_family = AF_INET, .sin_port = HOST_TO_NET_U16(27224), .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  ngtcp2_path client_path = {.local = {.addr = (struct sockaddr *)&client_address, .addrlen = sizeof(client_address)},
                             .remote = {.addr = (struct sockaddr *)&server_address, .addrlen = sizeof(server_address)}};
  ngtcp2_path server_path = {.local = {.addr = (struct sockaddr *)&server_address, .addrlen = sizeof(server_address)},
                             .remote = {.addr = (struct sockaddr *)&client_address, .addrlen = sizeof(client_address)}};
  acip_quic_connection_t *client = NULL, *server = NULL;
  server_tls->enable_datagrams = client_tls->enable_datagrams = true;
  *failure = ASCIICHAT_OK;
  *dropped = false;
  uint64_t now = NS_PER_SEC_INT;
  ngtcp2_path invalid_path = client_path;
  invalid_path.local.addrlen = 1;
  CHECK(acip_quic_connection_client_create(client_tls, server_name, &invalid_path, now, &client) ==
            ERROR_INVALID_PARAM &&
        !client);
  const uint8_t invalid_initial[16] = {0};
  CHECK(acip_quic_connection_server_create(server_tls, &server_path, invalid_initial, sizeof(invalid_initial), now,
                                           &server) == ERROR_NETWORK_PROTOCOL &&
        !server);
  CHECK(acip_quic_connection_client_create(client_tls, server_name, &client_path, now, &client) == ASCIICHAT_OK);
  CHECK(!acip_quic_connection_tls_ready(client));
  bool failed = false;
  for (unsigned step = 0; step < 10000 && !failed; ++step, now += NS_PER_MS_INT) {
    uint8_t packet[1452];
    size_t length = 0;
    ngtcp2_path_storage selected_path;
    asciichat_error_t result = acip_quic_connection_write(client, &selected_path, packet, sizeof(packet), &length, now);
    if (result != ASCIICHAT_OK) {
      *failure = result;
      failed = true;
      break;
    }
    if (length) {
      size_t second_length = 1;
      uint8_t second_packet[1452];
      ngtcp2_path_storage second_path;
      CHECK(acip_quic_connection_write(client, &second_path, second_packet, sizeof(second_packet), &second_length,
                                       now) == ERROR_INVALID_STATE &&
            second_length == 0);
      CHECK(acip_quic_connection_packet_sent(client, now - 1) == ERROR_INVALID_PARAM);
      CHECK(acip_quic_connection_packet_sent(client, now) == ASCIICHAT_OK);
      CHECK(acip_quic_connection_packet_sent(client, now) == ERROR_INVALID_STATE);
      if (!server)
        CHECK(acip_quic_connection_server_create(server_tls, &server_path, packet, length, now, &server) ==
              ASCIICHAT_OK);
      result = acip_quic_connection_receive(server, &server_path, packet, length, now);
      if (result != ASCIICHAT_OK) {
        *failure = result;
        failed = true;
        break;
      }
    }
    if (server) {
      result = acip_quic_connection_write(server, &selected_path, packet, sizeof(packet), &length, now);
      if (result == ASCIICHAT_OK && length) {
        CHECK(acip_quic_connection_packet_sent(server, now) == ASCIICHAT_OK);
        if (drop_first_server_packet && !*dropped)
          *dropped = true;
        else
          result = acip_quic_connection_receive(client, &client_path, packet, length, now);
      }
      if (result != ASCIICHAT_OK) {
        *failure = result;
        failed = true;
        break;
      }
    }
    if (server && acip_quic_connection_tls_ready(server) && acip_quic_connection_tls_ready(client))
      break;
    if (acip_quic_connection_expiry(client) <= now) {
      *failure = acip_quic_connection_handle_expiry(client, now);
      failed = *failure != ASCIICHAT_OK;
    }
    if (!failed && server && acip_quic_connection_expiry(server) <= now) {
      *failure = acip_quic_connection_handle_expiry(server, now);
      failed = *failure != ASCIICHAT_OK;
    }
  }
  bool success = !failed && server && acip_quic_connection_tls_ready(server) && acip_quic_connection_tls_ready(client);
  crypto_context_t client_crypto = {0}, server_crypto = {0};
  bool crypto_initialized = false;
  if (success && !drop_first_server_packet) {
    CHECK(crypto_init(&client_crypto) == CRYPTO_OK && crypto_init(&server_crypto) == CRYPTO_OK);
    CHECK(crypto_set_peer_public_key(&client_crypto, server_crypto.public_key) == CRYPTO_OK);
    CHECK(crypto_set_peer_public_key(&server_crypto, client_crypto.public_key) == CRYPTO_OK);
    client_crypto.handshake_complete = server_crypto.handshake_complete = true;
    crypto_initialized = true;
    quic_media_exchange(client, server, &client_path, &server_path, &now, drop_control, &client_crypto, &server_crypto);
  }
  acip_quic_connection_destroy(&server);
  acip_quic_connection_destroy(&client);
  server_tls->enable_datagrams = client_tls->enable_datagrams = false;
  if (crypto_initialized) {
    crypto_destroy(&server_crypto);
    crypto_destroy(&client_crypto);
  }
  return success;
}

typedef struct {
  acip_quic_connection_t *peers[2];
  unsigned accepted;
  unsigned received;
  const uint8_t *packet;
  size_t length;
} udp_test_state_t;

static asciichat_error_t udp_capture(acip_traffic_class_t traffic_class, const void *packet, size_t length,
                                     void *context) {
  udp_test_state_t *state = context;
  if (traffic_class != ACIP_TRAFFIC_CONTROL || length != state->length || memcmp(packet, state->packet, length))
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unexpected UDP test packet");
  ++state->received;
  return ASCIICHAT_OK;
}

static asciichat_error_t udp_accept(acip_quic_connection_t *connection, void *context) {
  udp_test_state_t *state = context;
  if (state->accepted >= 2)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unexpected UDP test connection");
  state->peers[state->accepted++] = connection;
  return acip_quic_connection_set_receiver(connection, udp_capture, state);
}

typedef struct {
  acip_quic_connection_t *peer;
  media_capture_t capture;
} udp_secure_test_state_t;

typedef struct {
  mutex_t *lane;
  acip_quic_connection_t *connection;
  acip_transport_t *transport;
  bool reject_once;
  unsigned rejected;
} quic_worker_accept_test_t;

static asciichat_error_t quic_worker_accept(acip_quic_connection_t *connection, void *context) {
  quic_worker_accept_test_t *state = context;
  if (state->reject_once) {
    state->reject_once = false;
    ++state->rejected;
    return ERROR_BUFFER_FULL;
  }
  if (state->connection)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unexpected QUIC worker connection");
  asciichat_error_t result = acip_quic_transport_attach_on_lane(connection, NULL, state->lane, &state->transport);
  if (result == ASCIICHAT_OK)
    state->connection = connection;
  return result;
}

static void quic_worker_accept_test(acip_quic_tls_t *server_tls, acip_quic_tls_t *client_tls) {
  CHECK(socket_init() == ASCIICHAT_OK);
  mutex_t client_lane, server_lane;
  CHECK(mutex_init(&client_lane, "quic_accept_client") == 0);
  CHECK(mutex_init(&server_lane, "quic_accept_server") == 0);
  struct sockaddr_in loopback = {.sin_family = AF_INET, .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  quic_worker_accept_test_t accepted = {.lane = &server_lane, .reject_once = true};
  acip_quic_udp_endpoint_t *server = NULL, *client = NULL;
  CHECK(acip_quic_udp_endpoint_create((struct sockaddr *)&loopback, sizeof(loopback), server_tls, quic_worker_accept,
                                      &accepted, &server) == ASCIICHAT_OK);
  CHECK(acip_quic_udp_endpoint_create((struct sockaddr *)&loopback, sizeof(loopback), NULL, NULL, NULL, &client) ==
        ASCIICHAT_OK);
  struct sockaddr_storage address;
  socklen_t address_length = 0;
  CHECK(acip_quic_udp_endpoint_address(server, &address, &address_length) == ASCIICHAT_OK);
  acip_quic_connection_t *connection = NULL;
  CHECK(acip_quic_udp_connect(client, client_tls, "localhost", (struct sockaddr *)&address, address_length,
                              time_get_ns(), &connection) == ASCIICHAT_OK);
  acip_transport_t *client_transport = NULL;
  CHECK(acip_quic_transport_attach_serialized(connection, NULL, &client_lane, &client_transport) == ASCIICHAT_OK);
  acip_quic_worker_t *client_worker = NULL, *server_worker = NULL;
  CHECK(acip_quic_worker_start(server, &server_lane, NULL, NULL, &server_worker) == ASCIICHAT_OK);
  CHECK(acip_quic_worker_start(client, &client_lane, NULL, NULL, &client_worker) == ASCIICHAT_OK);
  CHECK(acip_quic_transport_set_waker(client_transport, acip_quic_worker_wake, client_worker) == ASCIICHAT_OK);
  uint64_t start = time_get_ns();
  bool client_ready = false, server_ready = false;
  acip_transport_t *server_transport = NULL;
  while (time_get_ns() - start < 10 * NS_PER_SEC_INT) {
    mutex_lock(&client_lane);
    client_ready = acip_quic_connection_tls_ready(connection);
    mutex_unlock(&client_lane);
    mutex_lock(&server_lane);
    server_transport = accepted.transport;
    server_ready = accepted.connection && acip_quic_connection_tls_ready(accepted.connection);
    mutex_unlock(&server_lane);
    if (client_ready && server_ready && server_transport)
      break;
    time_sleep_ns(NS_PER_MS_INT);
  }
  if (!client_ready || !server_ready || !server_transport) {
    CHECK(acip_quic_transport_set_waker(client_transport, NULL, NULL) == ASCIICHAT_OK);
    asciichat_error_t client_worker_result = acip_quic_worker_stop(&client_worker);
    asciichat_error_t server_worker_result = acip_quic_worker_stop(&server_worker);
    FATAL(ERROR_ASSERTION_FAILED,
          "QUIC retry failed: client_ready=%d server_ready=%d accepted=%d rejected=%u "
          "client_worker=%d server_worker=%d",
          client_ready, server_ready, server_transport != NULL, accepted.rejected, client_worker_result,
          server_worker_result);
  }
  mutex_lock(&server_lane);
  CHECK(accepted.rejected == 1);
  mutex_unlock(&server_lane);
  size_t packet_length = 0;
  uint8_t *packet = make_packet(PACKET_TYPE_CRYPTO_CAPABILITIES, 16, &packet_length);
  CHECK(acip_transport_send(client_transport, packet, packet_length) == ASCIICHAT_OK);
  server_transport->receive_timeout_ns = 2 * NS_PER_SEC_INT;
  void *received = NULL, *allocation = NULL;
  size_t received_length = 0;
  CHECK(acip_transport_recv(server_transport, &received, &received_length, &allocation) == ASCIICHAT_OK);
  CHECK(received_length == packet_length && memcmp(received, packet, packet_length) == 0);
  buffer_release(allocation);
  buffer_release(packet);
  crypto_context_t client_crypto, server_crypto;
  CHECK(crypto_init(&client_crypto) == CRYPTO_OK && crypto_init(&server_crypto) == CRYPTO_OK);
  CHECK(crypto_set_peer_public_key(&client_crypto, server_crypto.public_key) == CRYPTO_OK);
  CHECK(crypto_set_peer_public_key(&server_crypto, client_crypto.public_key) == CRYPTO_OK);
  client_crypto.handshake_complete = server_crypto.handshake_complete = true;
  CHECK(acip_quic_transport_set_waker(server_transport, acip_quic_worker_wake, server_worker) == ASCIICHAT_OK);
  CHECK(acip_quic_transport_begin_security(client_transport, &client_crypto) == ASCIICHAT_OK);
  CHECK(acip_quic_transport_begin_security(server_transport, &server_crypto) == ASCIICHAT_OK);
  start = time_get_ns();
  while ((!acip_quic_transport_security_ready(client_transport) ||
          !acip_quic_transport_security_ready(server_transport)) &&
         time_get_ns() - start < 5 * NS_PER_SEC_INT)
    time_sleep_ns(NS_PER_MS_INT);
  CHECK(acip_quic_transport_security_ready(client_transport) && acip_quic_transport_security_ready(server_transport));
  CHECK(acip_quic_transport_allow_media(client_transport) == ASCIICHAT_OK);
  CHECK(acip_quic_transport_allow_media(server_transport) == ASCIICHAT_OK);
  packet = make_packet(PACKET_TYPE_AUDIO_BATCH, 64, &packet_length);
  CHECK(acip_transport_send(client_transport, packet, packet_length) == ASCIICHAT_OK);
  CHECK(acip_transport_recv(server_transport, &received, &received_length, &allocation) == ASCIICHAT_OK);
  CHECK(received_length == packet_length && memcmp(received, packet, packet_length) == 0);
  buffer_release(allocation);
  buffer_release(packet);
  CHECK(acip_quic_transport_set_waker(client_transport, NULL, NULL) == ASCIICHAT_OK);
  CHECK(acip_quic_transport_set_waker(server_transport, NULL, NULL) == ASCIICHAT_OK);
  CHECK(acip_quic_worker_stop(&client_worker) == ASCIICHAT_OK);
  CHECK(acip_quic_worker_stop(&server_worker) == ASCIICHAT_OK);
  acip_transport_destroy(client_transport);
  acip_transport_destroy(server_transport);
  acip_quic_udp_endpoint_destroy(&client);
  acip_quic_udp_endpoint_destroy(&server);
  crypto_destroy(&client_crypto);
  crypto_destroy(&server_crypto);
  mutex_destroy(&client_lane);
  mutex_destroy(&server_lane);
  socket_cleanup();
}

typedef struct {
  acip_quic_udp_endpoint_t *endpoint;
  acip_transport_t *transport;
  mutex_t *lane;
  acip_quic_connection_t *expected;
  atomic_t handled;
} quic_worker_failure_test_t;

static bool quic_worker_failure(acip_quic_connection_t *connection, asciichat_error_t reason, void *context) {
  quic_worker_failure_test_t *state = context;
  if (connection != state->expected || reason != ERROR_INVALID_STATE ||
      acip_quic_transport_fail(state->transport, reason) != ASCIICHAT_OK)
    return false;
  mutex_lock(state->lane);
  asciichat_error_t result = acip_quic_udp_remove(state->endpoint, connection);
  mutex_unlock(state->lane);
  if (result != ASCIICHAT_OK)
    return false;
  atomic_store_u64(&state->handled, 1);
  return true;
}

static asciichat_error_t udp_secure_accept(acip_quic_connection_t *connection, void *context) {
  udp_secure_test_state_t *state = context;
  if (state->peer)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unexpected secure UDP test connection");
  state->peer = connection;
  return ASCIICHAT_OK;
}

static asciichat_error_t quic_handshake_dispatch(acip_transport_t *transport, crypto_handshake_context_t *ctx,
                                                 bool server) {
  while (acip_transport_has_pending_data(transport)) {
    void *packet = NULL, *allocation = NULL;
    size_t length = 0;
    CHECK(acip_transport_recv(transport, &packet, &length, &allocation) == ASCIICHAT_OK);
    CHECK(length >= sizeof(packet_header_t));
    packet_header_t header;
    memcpy(&header, packet, sizeof(header));
    packet_type_t type = NET_TO_HOST_U16(header.type);
    const uint8_t *payload = (const uint8_t *)packet + sizeof(header);
    size_t payload_length = length - sizeof(header);
    asciichat_error_t result = ERROR_NETWORK_PROTOCOL;
    if (!server && type == PACKET_TYPE_CRYPTO_PARAMETERS) {
      CHECK(payload_length == sizeof(crypto_parameters_packet_t));
      crypto_parameters_packet_t params;
      memcpy(&params, payload, sizeof(params));
      result = crypto_handshake_set_parameters(ctx, &params);
    } else if (!server && type == PACKET_TYPE_CRYPTO_KEY_EXCHANGE_INIT) {
      result = crypto_handshake_client_key_exchange(ctx, transport, type, payload, payload_length);
    } else if (!server &&
               (type == PACKET_TYPE_CRYPTO_AUTH_CHALLENGE || type == PACKET_TYPE_CRYPTO_HANDSHAKE_COMPLETE)) {
      result = crypto_handshake_client_auth_response(ctx, transport, type, payload, payload_length);
    } else if (!server && (type == PACKET_TYPE_CRYPTO_SERVER_AUTH_RESP || type == PACKET_TYPE_CRYPTO_AUTH_FAILED)) {
      result = crypto_handshake_client_complete(ctx, transport, type, payload, payload_length);
    } else if (server && type == PACKET_TYPE_CRYPTO_KEY_EXCHANGE_RESP) {
      result = crypto_handshake_server_auth_challenge(ctx, transport, type, payload, payload_length);
    } else if (server && type == PACKET_TYPE_CRYPTO_AUTH_RESPONSE) {
      result = crypto_handshake_server_complete(ctx, transport, type, payload, payload_length);
    }
    buffer_release(allocation);
    if (result != ASCIICHAT_OK)
      return result;
  }
  return ASCIICHAT_OK;
}

typedef struct {
  const uint8_t *payload;
  size_t length;
  bool received;
} quic_network_capture_t;

typedef struct {
  acip_transport_t *transport;
  crypto_handshake_context_t *handshake;
  asciichat_error_t result;
} quic_client_negotiation_t;

static void *quic_client_negotiate_thread(void *context) {
  quic_client_negotiation_t *state = context;
  state->result =
      network_client_negotiate(state->transport, state->handshake, ACIP_CRYPTO_FULL, NETWORK_PROTOCOL_MEDIA);
  return NULL;
}

static asciichat_error_t quic_network_packet(network_connection_t *connection, packet_type_t type, const void *payload,
                                             size_t length, void *context) {
  quic_network_capture_t *capture = context;
  if (type != PACKET_TYPE_AUDIO_BATCH || length != capture->length || memcmp(payload, capture->payload, length))
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unexpected adopted QUIC packet");
  capture->received = true;
  network_connection_request_stop(connection);
  return ASCIICHAT_OK;
}

typedef struct {
  network_negotiation_t negotiation;
  atomic_t received;
} quic_server_fixture_t;

static void check_audio_samples(const audio_batch_packet_t *header, const float *samples, size_t count) {
  CHECK(header && header->batch_count == 1 && header->total_samples == 4 && header->sample_rate == AUDIO_SAMPLE_RATE &&
        header->channels == 1 && count == 4);
  CHECK(samples[0] == 0.0f && samples[1] > 0.999f && samples[2] < -0.999f && samples[3] > 0.499f &&
        samples[3] < 0.501f);
}

static void quic_server_audio(const audio_batch_packet_t *header, const float *samples, size_t count, void *client,
                              void *context) {
  (void)client;
  check_audio_samples(header, samples, count);
  atomic_store_bool(&((quic_server_fixture_t *)context)->received, true);
}

static void quic_client_audio(const audio_batch_packet_t *header, const float *samples, size_t count, void *context) {
  check_audio_samples(header, samples, count);
  *(bool *)context = true;
}

typedef struct {
  network_server_t *server;
  asciichat_error_t result;
} quic_server_runner_t;

static void *quic_server_run(void *context) {
  quic_server_runner_t *runner = context;
  runner->result = network_server_run(runner->server);
  return NULL;
}

static asciichat_error_t quic_server_open(network_connection_t *connection, void *context) {
  quic_server_fixture_t *fixture = context;
  if (strcmp(network_connection_peer(connection), "127.0.0.1") != 0)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "QUIC server received an incorrect peer address");
  crypto_handshake_context_t *handshake = network_connection_handshake(connection);
  asciichat_error_t result =
      crypto_handshake_init_with_password("quic-network-server", handshake, true, "test-password");
  if (result != ASCIICHAT_OK)
    return result;
  fixture->negotiation = (network_negotiation_t){.protocol = NETWORK_PROTOCOL_MEDIA,
                                                 .handshake = handshake,
                                                 .initialized = true,
                                                 .password = "test-password",
                                                 .require_auth = true,
                                                 .require_encryption = true};
  return ASCIICHAT_OK;
}

static asciichat_error_t quic_server_packet(network_connection_t *connection, packet_type_t type, const void *payload,
                                            size_t length, void *context) {
  quic_server_fixture_t *fixture = context;
  if (network_connection_state(connection) == NETWORK_NEGOTIATING) {
    asciichat_error_t result = network_negotiation_receive(
        &fixture->negotiation, network_connection_transport(connection), type, payload, length);
    if (result == ASCIICHAT_OK && fixture->negotiation.ready)
      network_connection_ready(connection);
    return result;
  }
  if (type != PACKET_TYPE_AUDIO_BATCH)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unexpected QUIC server audio");
  acip_server_callbacks_t callbacks = {.on_audio_batch = quic_server_audio, .app_ctx = fixture};
  asciichat_error_t result =
      acip_handle_server_packet(network_connection_transport(connection), type, payload, length, NULL, &callbacks);
  if (result != ASCIICHAT_OK)
    return result;
  return packet_send_via_transport(network_connection_transport(connection), type, payload, length, 0);
}

static void quic_network_server_test(const char *certificate, const char *private_key, bool datagrams) {
  CHECK(socket_init() == ASCIICHAT_OK);
  struct sockaddr_in loopback = {.sin_family = AF_INET, .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  acip_quic_config_t server_config = {.certificate_file = certificate,
                                      .private_key_file = private_key,
                                      .enable_datagrams = datagrams,
                                      .prefer_audio_datagrams = true};
  acip_quic_config_t client_config = {.ca_file = certificate, .enable_datagrams = true, .prefer_audio_datagrams = true};
  acip_quic_endpoint_t *server = NULL, *client = NULL;
  CHECK(acip_quic_endpoint_open((struct sockaddr *)&loopback, sizeof(loopback), true, &server_config, &server) ==
        ASCIICHAT_OK);
  CHECK(acip_quic_endpoint_open((struct sockaddr *)&loopback, sizeof(loopback), false, &client_config, &client) ==
        ASCIICHAT_OK);
  struct sockaddr_storage address;
  socklen_t address_length = 0;
  CHECK(acip_quic_endpoint_address(server, &address, &address_length) == ASCIICHAT_OK);
  acip_transport_t *outbound = NULL;
  CHECK(acip_quic_connect(client, (struct sockaddr *)&address, address_length, "localhost", &outbound) == ASCIICHAT_OK);
  uint64_t start = time_get_ns();
  acip_transport_capabilities_t capabilities;
  bool ready = false;
  while (time_get_ns() - start < 10 * NS_PER_SEC_INT) {
    if (acip_transport_get_capabilities(outbound, &capabilities) == ASCIICHAT_OK) {
      ready = true;
      break;
    }
    time_sleep_ns(NS_PER_MS_INT);
  }
  CHECK(ready && capabilities.datagrams == datagrams);
  float samples[] = {0.0f, 1.0f, -1.0f, 0.5f};
  quic_server_fixture_t fixture = {0};
  network_server_config_t config = {.port = 0,
                                    .ipv4_address = "127.0.0.1",
                                    .bind_ipv4 = true,
                                    .callbacks = {.open = quic_server_open, .packet = quic_server_packet},
                                    .user_data = &fixture,
                                    .quic_endpoint = server};
  network_server_t *network_server = NULL;
  CHECK(network_server_create(&config, &network_server) == ASCIICHAT_OK);
  CHECK(network_server_start(network_server) == ASCIICHAT_OK);
  quic_server_runner_t runner = {.server = network_server};
  asciichat_thread_t server_thread;
  CHECK(acthread_create(&server_thread, "quic_server_loop", quic_server_run, &runner) == ASCIICHAT_OK);
  crypto_handshake_context_t client_handshake;
  CHECK(crypto_handshake_init_with_password("quic-network-client", &client_handshake, false, "test-password") ==
        ASCIICHAT_OK);
  SAFE_STRNCPY(client_handshake.server_ip, "127.0.0.1", sizeof(client_handshake.server_ip));
  client_handshake.server_port = 27224;
  CHECK(network_client_negotiate(outbound, &client_handshake, ACIP_CRYPTO_FULL, NETWORK_PROTOCOL_MEDIA) ==
        ASCIICHAT_OK);
  CHECK(outbound->prefer_audio_datagrams);
  CHECK(acip_send_audio_batch(outbound, samples, 4, 1) == ASCIICHAT_OK);
  outbound->receive_timeout_ns = 5 * NS_PER_SEC_INT;
  void *echo = NULL, *allocation = NULL;
  size_t echo_length = 0;
  CHECK(acip_transport_recv(outbound, &echo, &echo_length, &allocation) == ASCIICHAT_OK);
  CHECK(atomic_load_bool(&fixture.received) &&
        echo_length == sizeof(packet_header_t) + sizeof(audio_batch_packet_t) + sizeof(samples));
  bool client_received = false;
  acip_client_callbacks_t callbacks = {.on_audio_batch = quic_client_audio, .app_ctx = &client_received};
  CHECK(acip_handle_client_packet(outbound, PACKET_TYPE_AUDIO_BATCH, (uint8_t *)echo + sizeof(packet_header_t),
                                  echo_length - sizeof(packet_header_t), &callbacks) == ASCIICHAT_OK);
  CHECK(client_received);
  buffer_release(allocation);
  network_server_request_stop(network_server);
  CHECK(thread_join(&server_thread, NULL) == ASCIICHAT_OK && runner.result == ASCIICHAT_OK);
  CHECK(network_server_stop(network_server) == ASCIICHAT_OK);
  network_server_destroy(&network_server);
  CHECK(!network_server);
  CHECK(acip_quic_endpoint_stop(server) == ASCIICHAT_OK);
  CHECK(acip_quic_endpoint_destroy(&server) == ASCIICHAT_OK && !server);
  CHECK(acip_quic_endpoint_stop(client) == ASCIICHAT_OK);
  CHECK(acip_quic_release(client, &outbound) == ASCIICHAT_OK && !outbound);
  CHECK(acip_quic_endpoint_destroy(&client) == ASCIICHAT_OK && !client);
  crypto_handshake_destroy(&client_handshake);
  socket_cleanup();
}

static void quic_public_endpoint_test(const char *certificate, const char *private_key) {
  CHECK(socket_init() == ASCIICHAT_OK);
  struct sockaddr_in loopback = {.sin_family = AF_INET, .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  acip_quic_config_t server_config = {
      .certificate_file = certificate, .private_key_file = private_key, .enable_datagrams = true};
  acip_quic_config_t client_config = {.ca_file = certificate, .enable_datagrams = true};
  acip_quic_endpoint_t *server = NULL, *client = NULL;
  CHECK(acip_quic_endpoint_open((struct sockaddr *)&loopback, sizeof(loopback), true, &server_config, &server) ==
        ASCIICHAT_OK);
  CHECK(acip_quic_endpoint_open((struct sockaddr *)&loopback, sizeof(loopback), false, &client_config, &client) ==
        ASCIICHAT_OK);
  CHECK(acip_quic_endpoint_destroy(&server) == ERROR_INVALID_STATE && server);
  struct sockaddr_storage address;
  socklen_t address_length = 0;
  CHECK(acip_quic_endpoint_address(server, &address, &address_length) == ASCIICHAT_OK);
  acip_transport_t *outbound = NULL, *inbound = NULL;
  CHECK(acip_quic_accept(server, &inbound) == ERROR_NETWORK_TIMEOUT && !inbound);
  CHECK(acip_quic_connect(client, (struct sockaddr *)&address, address_length, "localhost", &outbound) == ASCIICHAT_OK);
  uint64_t start = time_get_ns();
  acip_transport_capabilities_t client_caps, server_caps;
  bool ready = false;
  while (time_get_ns() - start < 10 * NS_PER_SEC_INT) {
    if (!inbound) {
      asciichat_error_t result = acip_quic_accept(server, &inbound);
      CHECK(result == ASCIICHAT_OK || result == ERROR_NETWORK_TIMEOUT);
    }
    if (inbound && acip_transport_get_capabilities(outbound, &client_caps) == ASCIICHAT_OK &&
        acip_transport_get_capabilities(inbound, &server_caps) == ASCIICHAT_OK) {
      ready = true;
      break;
    }
    time_sleep_ns(NS_PER_MS_INT);
  }
  CHECK(ready && client_caps.datagrams && server_caps.datagrams);
  struct sockaddr_storage client_address, observed_peer;
  socklen_t client_address_length = 0, peer_length = 0;
  CHECK(acip_quic_endpoint_address(client, &client_address, &client_address_length) == ASCIICHAT_OK);
  CHECK(acip_quic_transport_peer_address(inbound, &observed_peer, &peer_length) == ASCIICHAT_OK);
  CHECK(peer_length == client_address_length && observed_peer.ss_family == AF_INET &&
        ((struct sockaddr_in *)&observed_peer)->sin_port == ((struct sockaddr_in *)&client_address)->sin_port &&
        ((struct sockaddr_in *)&observed_peer)->sin_addr.s_addr ==
            ((struct sockaddr_in *)&client_address)->sin_addr.s_addr);
  CHECK(acip_quic_transport_wait_security(outbound, 0) == ERROR_NETWORK_TIMEOUT);
  CHECK(acip_quic_release(client, &inbound) == ERROR_INVALID_PARAM && inbound);
  network_connection_t *adopted = NULL;
  CHECK(network_connection_adopt_negotiating(outbound, "localhost", true, &adopted) == ASCIICHAT_OK && adopted);
  CHECK(network_connection_state(adopted) == NETWORK_NEGOTIATING);
  crypto_handshake_context_t *client_handshake = network_connection_handshake(adopted);
  crypto_handshake_context_t server_handshake;
  CHECK(crypto_handshake_init_with_password("public-client", client_handshake, false, "test-password") == ASCIICHAT_OK);
  CHECK(crypto_handshake_init_with_password("public-server", &server_handshake, true, "test-password") == ASCIICHAT_OK);
  SAFE_STRNCPY(client_handshake->server_ip, "127.0.0.1", sizeof(client_handshake->server_ip));
  client_handshake->server_port = 27224;
  CHECK(network_client_negotiate(outbound, client_handshake, ACIP_CRYPTO_NONE, NETWORK_PROTOCOL_MEDIA) == ERROR_CRYPTO);
  network_negotiation_t server_negotiation = {.protocol = NETWORK_PROTOCOL_MEDIA,
                                              .handshake = &server_handshake,
                                              .initialized = true,
                                              .password = "test-password",
                                              .require_auth = true,
                                              .require_encryption = true};
  protocol_version_packet_t insecure_version = {.protocol_version = HOST_TO_NET_U16(1),
                                                .supports_encryption = ACIP_CRYPTO_NONE};
  CHECK(network_negotiation_receive(&server_negotiation, inbound, PACKET_TYPE_PROTOCOL_VERSION, &insecure_version,
                                    sizeof(insecure_version)) == ERROR_CRYPTO);
  CHECK(!server_negotiation.ready && server_negotiation.step == 0);
  quic_client_negotiation_t client_negotiation = {.transport = outbound, .handshake = client_handshake};
  asciichat_thread_t negotiation_thread;
  CHECK(acthread_create(&negotiation_thread, "quic_negotiate", quic_client_negotiate_thread, &client_negotiation) ==
        ASCIICHAT_OK);
  start = time_get_ns();
  while (time_get_ns() - start < 10 * NS_PER_SEC_INT && !server_negotiation.ready) {
    if (acip_transport_has_pending_data(inbound)) {
      void *packet = NULL, *packet_allocation = NULL;
      size_t packet_length = 0;
      CHECK(acip_transport_recv(inbound, &packet, &packet_length, &packet_allocation) == ASCIICHAT_OK);
      CHECK(packet_length >= sizeof(packet_header_t));
      packet_header_t header;
      memcpy(&header, packet, sizeof(header));
      asciichat_error_t result =
          network_negotiation_receive(&server_negotiation, inbound, NET_TO_HOST_U16(header.type),
                                      (const uint8_t *)packet + sizeof(header), packet_length - sizeof(header));
      buffer_release(packet_allocation);
      CHECK(result == ASCIICHAT_OK);
    }
    time_sleep_ns(NS_PER_MS_INT);
  }
  CHECK(thread_join(&negotiation_thread, NULL) == ASCIICHAT_OK);
  CHECK(server_negotiation.ready && client_negotiation.result == ASCIICHAT_OK);
  CHECK(crypto_handshake_is_ready(client_handshake) && crypto_handshake_is_ready(&server_handshake));
  CHECK(acip_quic_transport_security_ready(outbound) && acip_quic_transport_security_ready(inbound));
  CHECK(acip_quic_transport_wait_security(outbound, 0) == ASCIICHAT_OK);
  size_t audio_length = 0;
  uint8_t *audio = make_packet(PACKET_TYPE_AUDIO_BATCH, 48, &audio_length);
  socket_buffer_t slice = {audio, audio_length};
  acip_send_options_t options = {.traffic_class = ACIP_TRAFFIC_AUDIO, .delivery = ACIP_DELIVERY_DATAGRAM};
  CHECK(acip_transport_sendv_ex(outbound, &slice, 1, &options) == ASCIICHAT_OK);
  inbound->receive_timeout_ns = 2 * NS_PER_SEC_INT;
  void *received = NULL, *allocation = NULL;
  size_t received_length = 0;
  CHECK(acip_transport_recv(inbound, &received, &received_length, &allocation) == ASCIICHAT_OK);
  CHECK(received_length == audio_length && memcmp(received, audio, audio_length) == 0);
  buffer_release(allocation);
  // An invalid migration keeps the old path and its authenticated state usable.
  struct sockaddr invalid_local = {0};
  CHECK(acip_quic_endpoint_migrate(server, (struct sockaddr *)&loopback, sizeof(loopback)) == ERROR_INVALID_PARAM);
  CHECK(acip_quic_endpoint_migrate(client, &invalid_local, sizeof(invalid_local)) == ERROR_INVALID_PARAM);
  CHECK(acip_quic_endpoint_migrate(client, (struct sockaddr *)&client_address, client_address_length) == ERROR_NETWORK);
  struct sockaddr_storage unchanged_address;
  socklen_t unchanged_length = 0;
  CHECK(acip_quic_endpoint_address(client, &unchanged_address, &unchanged_length) == ASCIICHAT_OK);
  CHECK(unchanged_length == client_address_length && !memcmp(&unchanged_address, &client_address, unchanged_length));
  struct sockaddr_in new_local = {.sin_family = AF_INET, .sin_addr.s_addr = HOST_TO_NET_U32(0x7f000002)};
  socket_t migration_probe = socket_create("migration_probe", AF_INET, SOCK_DGRAM, 0);
  CHECK(migration_probe != INVALID_SOCKET_VALUE);
  if (socket_bind(migration_probe, (struct sockaddr *)&new_local, sizeof(new_local)) != 0)
    new_local = loopback;
  socket_close(migration_probe);
  start = time_get_ns();
  asciichat_error_t migration = ERROR_INVALID_STATE;
  do {
    migration = acip_quic_endpoint_migrate(client, (struct sockaddr *)&new_local, sizeof(new_local));
    CHECK(migration == ASCIICHAT_OK || migration == ERROR_INVALID_STATE || migration == ERROR_BUFFER_FULL);
    if (migration == ASCIICHAT_OK)
      break;
    time_sleep_ns(NS_PER_MS_INT);
  } while (time_get_ns() - start < 5 * NS_PER_SEC_INT);
  CHECK(migration == ASCIICHAT_OK);
  CHECK(acip_quic_endpoint_address(client, &client_address, &client_address_length) == ASCIICHAT_OK);
  CHECK(((struct sockaddr_in *)&client_address)->sin_addr.s_addr == new_local.sin_addr.s_addr);
  CHECK(memcmp(&client_address, &unchanged_address, client_address_length) != 0);
  start = time_get_ns();
  bool migrated = false;
  do {
    CHECK(acip_quic_transport_peer_address(inbound, &observed_peer, &peer_length) == ASCIICHAT_OK);
    migrated = peer_length == client_address_length && !memcmp(&observed_peer, &client_address, peer_length);
    if (migrated)
      break;
    time_sleep_ns(NS_PER_MS_INT);
  } while (time_get_ns() - start < 5 * NS_PER_SEC_INT);
  CHECK(migrated && acip_quic_transport_security_ready(outbound) && acip_quic_transport_security_ready(inbound));
  CHECK(acip_transport_sendv_ex(outbound, &slice, 1, &options) == ASCIICHAT_OK);
  CHECK(acip_transport_recv(inbound, &received, &received_length, &allocation) == ASCIICHAT_OK);
  CHECK(received_length == audio_length && memcmp(received, audio, audio_length) == 0);
  buffer_release(allocation);
  CHECK(acip_send_ping(outbound) == ASCIICHAT_OK);
  CHECK(acip_transport_recv(inbound, &received, &received_length, &allocation) == ASCIICHAT_OK);
  CHECK(received_length == sizeof(packet_header_t) &&
        NET_TO_HOST_U16(((packet_header_t *)received)->type) == PACKET_TYPE_PING);
  buffer_release(allocation);
  network_connection_ready(adopted);
  outbound = NULL;
  CHECK(acip_transport_get_type(network_connection_transport(adopted)) == ACIP_TRANSPORT_QUIC);
  CHECK(acip_transport_sendv_ex(inbound, &slice, 1, &options) == ASCIICHAT_OK);
  quic_network_capture_t network_capture = {.payload = audio + sizeof(packet_header_t),
                                            .length = audio_length - sizeof(packet_header_t)};
  network_connection_callbacks_t callbacks = {.packet = quic_network_packet};
  CHECK(network_connection_run(adopted, &callbacks, &network_capture) == ASCIICHAT_OK && network_capture.received);
  network_connection_destroy(&adopted);
  CHECK(!adopted);
  buffer_release(audio);

  acip_transport_t *bad = NULL;
  CHECK(acip_quic_connect(client, (struct sockaddr *)&address, address_length, "other.example", &bad) == ASCIICHAT_OK);
  start = time_get_ns();
  while (time_get_ns() - start < 10 * NS_PER_SEC_INT && acip_transport_is_connected(bad))
    time_sleep_ns(NS_PER_MS_INT);
  CHECK(!acip_transport_is_connected(bad));
  CHECK(acip_quic_release(client, &bad) == ASCIICHAT_OK && !bad);
  acip_transport_t *unclaimed = NULL;
  CHECK(acip_quic_connect(client, (struct sockaddr *)&address, address_length, "localhost", &unclaimed) ==
        ASCIICHAT_OK);
  start = time_get_ns();
  while (time_get_ns() - start < 10 * NS_PER_SEC_INT &&
         acip_transport_get_capabilities(unclaimed, &client_caps) != ASCIICHAT_OK)
    time_sleep_ns(NS_PER_MS_INT);
  CHECK(acip_transport_get_capabilities(unclaimed, &client_caps) == ASCIICHAT_OK);
  CHECK(acip_quic_endpoint_stop(server) == ASCIICHAT_OK);
  CHECK(acip_quic_transport_wait_security(inbound, 0) == ERROR_NETWORK);
  CHECK(acip_quic_endpoint_destroy(&server) == ERROR_INVALID_STATE && server);
  CHECK(acip_quic_release(server, &inbound) == ASCIICHAT_OK && !inbound);
  CHECK(acip_quic_endpoint_destroy(&server) == ASCIICHAT_OK && !server);
  CHECK(acip_quic_endpoint_stop(client) == ASCIICHAT_OK);
  CHECK(acip_quic_release(client, &unclaimed) == ASCIICHAT_OK && !unclaimed);
  CHECK(acip_quic_endpoint_destroy(&client) == ASCIICHAT_OK && !client);
  crypto_handshake_destroy(&server_handshake);
  socket_cleanup();
}

typedef struct {
  acip_transport_t *transport;
  const uint8_t *packet;
  size_t length;
  asciichat_error_t result;
} quic_concurrent_sender_t;

typedef struct {
  acip_transport_t *transport;
  void *packet, *allocation;
  size_t length;
  asciichat_error_t result;
  atomic_t started, done;
} quic_concurrent_receiver_t;

static void *quic_concurrent_receive(void *arg) {
  quic_concurrent_receiver_t *receiver = arg;
  atomic_store_u64(&receiver->started, 1);
  receiver->result =
      acip_transport_recv(receiver->transport, &receiver->packet, &receiver->length, &receiver->allocation);
  atomic_store_u64(&receiver->done, 1);
  return NULL;
}

static void *quic_concurrent_send(void *arg) {
  quic_concurrent_sender_t *sender = arg;
  sender->result = ASCIICHAT_OK;
  for (unsigned i = 0; i < 32; ++i) {
    sender->result = acip_transport_send(sender->transport, sender->packet, sender->length);
    if (sender->result != ASCIICHAT_OK)
      break;
  }
  return NULL;
}

static void quic_udp_secure_exchange(acip_quic_tls_t *server_tls, acip_quic_tls_t *client_tls, bool wrong_password) {
  server_tls->enable_datagrams = client_tls->enable_datagrams = true;
  CHECK(socket_init() == ASCIICHAT_OK);
  struct sockaddr_in loopback = {.sin_family = AF_INET, .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  acip_quic_udp_endpoint_t *server = NULL, *client = NULL;
  mutex_t client_lane, server_lane;
  CHECK(mutex_init(&client_lane, "quic_client_lane") == 0);
  CHECK(mutex_init(&server_lane, "quic_server_lane") == 0);
  udp_secure_test_state_t server_state = {0};
  media_capture_t client_capture = {0};
  packet_type_t types[] = {PACKET_TYPE_PING,        PACKET_TYPE_AUDIO_BATCH,      PACKET_TYPE_IMAGE_FRAME,
                           PACKET_TYPE_ASCII_FRAME, PACKET_TYPE_IMAGE_FRAME_H264, PACKET_TYPE_IMAGE_FRAME_H265};
  uint8_t *packets[6];
  size_t lengths[6];
  for (size_t i = 0; i < 6; ++i) {
    packets[i] = make_packet(types[i], i == 2 ? 65536 : 64, &lengths[i]);
    client_capture.packets[i] = server_state.capture.packets[i] = packets[i];
    client_capture.lengths[i] = server_state.capture.lengths[i] = lengths[i];
  }
  crypto_handshake_context_t client_handshake, server_handshake;
  CHECK(crypto_handshake_init_with_password("quic-client", &client_handshake, false,
                                            wrong_password ? "wrong-password" : "test-password") == ASCIICHAT_OK);
  CHECK(crypto_handshake_init_with_password("quic-server", &server_handshake, true, "test-password") == ASCIICHAT_OK);
  SAFE_STRNCPY(client_handshake.server_ip, "127.0.0.1", sizeof(client_handshake.server_ip));
  client_handshake.server_port = 27224;
  CHECK(acip_quic_udp_endpoint_create((struct sockaddr *)&loopback, sizeof(loopback), server_tls, udp_secure_accept,
                                      &server_state, &server) == ASCIICHAT_OK);
  CHECK(acip_quic_udp_endpoint_create((struct sockaddr *)&loopback, sizeof(loopback), NULL, NULL, NULL, &client) ==
        ASCIICHAT_OK);
  struct sockaddr_storage address;
  socklen_t address_length = 0;
  CHECK(acip_quic_udp_endpoint_address(server, &address, &address_length) == ASCIICHAT_OK);
  acip_quic_connection_t *client_connection = NULL;
  acip_transport_t *client_transport = NULL, *server_transport = NULL;
  uint64_t start = time_get_ns();
  CHECK(acip_quic_udp_connect(client, client_tls, "localhost", (struct sockaddr *)&address, address_length, start,
                              &client_connection) == ASCIICHAT_OK);
  uint64_t wait_ns = 0;
  bool want_write = true;
  uint64_t expiry = acip_quic_connection_expiry(client_connection);
  CHECK(acip_quic_udp_wait_parameters(client, start, 5 * NS_PER_SEC_INT, &wait_ns, &want_write) == ASCIICHAT_OK);
  uint64_t expected_wait = expiry <= start ? 0 : expiry - start;
  if (expected_wait > 5 * NS_PER_SEC_INT)
    expected_wait = 5 * NS_PER_SEC_INT;
  CHECK(wait_ns == expected_wait && !want_write);
  bool handshake_started = false, security_started = false, submitted = false, rejected = false,
       client_rejected = false;
  while (time_get_ns() - start < 10 * NS_PER_SEC_INT) {
    uint64_t now = time_get_ns();
    acip_quic_connection_t *failed = NULL;
    mutex_lock(&client_lane);
    asciichat_error_t client_pump = acip_quic_udp_pump(client, now, &failed);
    mutex_unlock(&client_lane);
    CHECK(client_pump == ASCIICHAT_OK && !failed);
    mutex_lock(&server_lane);
    asciichat_error_t server_pump = acip_quic_udp_pump(server, now, &failed);
    mutex_unlock(&server_lane);
    CHECK(server_pump == ASCIICHAT_OK && !failed);
    if (!handshake_started && server_state.peer && acip_quic_connection_tls_ready(client_connection) &&
        acip_quic_connection_tls_ready(server_state.peer)) {
      CHECK(acip_quic_transport_attach_serialized(client_connection, &client_handshake.crypto_ctx, &client_lane,
                                                  &client_transport) == ASCIICHAT_OK);
      CHECK(acip_quic_transport_attach_serialized(server_state.peer, &server_handshake.crypto_ctx, &server_lane,
                                                  &server_transport) == ASCIICHAT_OK);
      acip_transport_t *duplicate = NULL;
      CHECK(acip_quic_transport_attach(client_connection, &client_handshake.crypto_ctx, &duplicate) ==
                ERROR_INVALID_PARAM &&
            !duplicate);
      CHECK(acip_transport_get_type(client_transport) == ACIP_TRANSPORT_QUIC);
      CHECK(acip_transport_get_socket(client_transport) == INVALID_SOCKET_VALUE);
      acip_transport_capabilities_t caps;
      CHECK(acip_transport_get_capabilities(client_transport, &caps) == ASCIICHAT_OK && caps.independent_streams &&
            caps.datagrams && caps.max_datagram_payload > 28);
      client_transport->receive_timeout_ns = 5 * NS_PER_MS_INT;
      void *empty = NULL, *allocation = NULL;
      size_t empty_length = 0;
      CHECK(acip_transport_recv(client_transport, &empty, &empty_length, &allocation) == ERROR_NETWORK_TIMEOUT);
      acip_send_options_t unsupported = {.traffic_class = ACIP_TRAFFIC_AUDIO, .delivery = ACIP_DELIVERY_DATAGRAM};
      socket_buffer_t unsupported_slice = {packets[1], lengths[1]};
      CHECK(acip_transport_sendv_ex(client_transport, &unsupported_slice, 1, &unsupported) == ERROR_INVALID_STATE);
      CHECK(crypto_handshake_server_send_parameters(&server_handshake, server_transport) == ASCIICHAT_OK);
      CHECK(crypto_handshake_server_start(&server_handshake, server_transport) == ASCIICHAT_OK);
      handshake_started = true;
    }
    if (handshake_started && !security_started) {
      asciichat_error_t client_result = quic_handshake_dispatch(client_transport, &client_handshake, false);
      asciichat_error_t server_result =
          rejected ? ASCIICHAT_OK : quic_handshake_dispatch(server_transport, &server_handshake, true);
      if (server_result != ASCIICHAT_OK) {
        CHECK(wrong_password && server_result == ERROR_NETWORK && client_result == ASCIICHAT_OK);
        rejected = true;
      }
      if (client_result != ASCIICHAT_OK) {
        CHECK(wrong_password && rejected && client_result == ERROR_CRYPTO_AUTH);
        client_rejected = true;
        break;
      }
    }
    if (handshake_started && !security_started && crypto_handshake_is_ready(&client_handshake) &&
        crypto_handshake_is_ready(&server_handshake)) {
      CHECK(acip_quic_connection_enable_security(client_connection, &client_handshake.crypto_ctx) == ASCIICHAT_OK);
      CHECK(acip_quic_connection_enable_security(server_state.peer, &server_handshake.crypto_ctx) == ASCIICHAT_OK);
      security_started = true;
    }
    if (security_started && !submitted && acip_quic_connection_security_ready(client_connection) &&
        acip_quic_connection_security_ready(server_state.peer)) {
      CHECK(acip_quic_connection_allow_media(client_connection) == ASCIICHAT_OK);
      CHECK(acip_quic_connection_allow_media(server_state.peer) == ASCIICHAT_OK);
      acip_transport_t *peers[] = {client_transport, server_transport};
      for (size_t peer = 0; peer < 2; ++peer) {
        for (size_t i = 0; i < 6; ++i) {
          if (i == 1) {
            socket_buffer_t slice = {packets[i], lengths[i]};
            acip_send_options_t options = {.traffic_class = ACIP_TRAFFIC_AUDIO, .delivery = ACIP_DELIVERY_DATAGRAM};
            CHECK(acip_transport_sendv_ex(peers[peer], &slice, 1, &options) == ASCIICHAT_OK);
          } else if (i == 2) {
            socket_buffer_t slices[] = {{packets[i], sizeof(packet_header_t)},
                                        {packets[i] + sizeof(packet_header_t), lengths[i] - sizeof(packet_header_t)}};
            CHECK(acip_transport_sendv(peers[peer], slices, 2) == ASCIICHAT_OK);
          } else {
            CHECK(acip_transport_send(peers[peer], packets[i], lengths[i]) == ASCIICHAT_OK);
          }
        }
      }
      submitted = true;
    }
    if (submitted) {
      acip_transport_t *peers[] = {client_transport, server_transport};
      media_capture_t *captures[] = {&client_capture, &server_state.capture};
      for (size_t peer = 0; peer < 2; ++peer) {
        while (acip_transport_has_pending_data(peers[peer])) {
          void *packet = NULL, *allocation = NULL;
          size_t length = 0;
          CHECK(acip_transport_recv(peers[peer], &packet, &length, &allocation) == ASCIICHAT_OK);
          CHECK(length >= sizeof(packet_header_t));
          packet_header_t header;
          memcpy(&header, packet, sizeof(header));
          acip_traffic_class_t traffic_class = acip_packet_traffic_class(NET_TO_HOST_U16(header.type));
          CHECK(capture_quic_packet(traffic_class, packet, length, captures[peer]) == ASCIICHAT_OK);
          buffer_release(allocation);
        }
      }
    }
    if (submitted && client_capture.count == 6 && server_state.capture.count == 6)
      break;
    time_sleep_ns(NS_PER_MS_INT);
  }
  if (wrong_password) {
    CHECK(rejected && client_rejected && !crypto_handshake_is_ready(&client_handshake) &&
          !crypto_handshake_is_ready(&server_handshake));
    CHECK(acip_quic_connection_allow_media(client_connection) == ERROR_INVALID_STATE);
    CHECK(acip_quic_connection_allow_media(server_state.peer) == ERROR_INVALID_STATE);
  } else {
    CHECK(submitted && client_capture.count == 6 && server_state.capture.count == 6);
    acip_quic_worker_t *client_worker = NULL, *server_worker = NULL;
    quic_worker_failure_test_t failure_state = {
        .endpoint = server, .transport = server_transport, .lane = &server_lane, .expected = server_state.peer};
    CHECK(acip_quic_worker_start(client, &client_lane, NULL, NULL, &client_worker) == ASCIICHAT_OK);
    CHECK(acip_quic_worker_start(server, &server_lane, quic_worker_failure, &failure_state, &server_worker) ==
          ASCIICHAT_OK);
    CHECK(acip_quic_transport_set_waker(client_transport, acip_quic_worker_wake, client_worker) == ASCIICHAT_OK);
    CHECK(acip_quic_transport_set_waker(server_transport, acip_quic_worker_wake, server_worker) == ASCIICHAT_OK);
    server_transport->receive_timeout_ns = 2 * NS_PER_SEC_INT;
    CHECK(acip_outbound_create(client_transport) == ASCIICHAT_OK);
    socket_buffer_t audio_slice = {packets[1], lengths[1]};
    acip_send_options_t audio_options = {.traffic_class = ACIP_TRAFFIC_AUDIO,
                                         .delivery = ACIP_DELIVERY_DATAGRAM,
                                         .deadline_ns = time_get_ns() + 2 * NS_PER_SEC_INT};
    CHECK(acip_transport_sendv_ex(client_transport, &audio_slice, 1, &audio_options) == ASCIICHAT_OK);
    void *audio_received = NULL, *audio_allocation = NULL;
    size_t audio_length = 0;
    CHECK(acip_transport_recv(server_transport, &audio_received, &audio_length, &audio_allocation) == ASCIICHAT_OK);
    CHECK(audio_length == lengths[1] && memcmp(audio_received, packets[1], audio_length) == 0);
    buffer_release(audio_allocation);
    server_transport->receive_timeout_ns = 200 * NS_PER_MS_INT;
    quic_concurrent_sender_t sender = {.transport = client_transport, .packet = packets[0], .length = lengths[0]};
    asciichat_thread_t thread;
    CHECK(acthread_create(&thread, "quic_concurrent_sender", quic_concurrent_send, &sender) == 0);
    unsigned received = 0;
    start = time_get_ns();
    while (received < 32 && time_get_ns() - start < 10 * NS_PER_SEC_INT) {
      void *packet = NULL, *allocation = NULL;
      size_t length = 0;
      asciichat_error_t result = acip_transport_recv(server_transport, &packet, &length, &allocation);
      if (result == ASCIICHAT_OK) {
        CHECK(length == lengths[0] && memcmp(packet, packets[0], length) == 0);
        buffer_release(allocation);
        ++received;
      } else
        CHECK(result == ERROR_NETWORK_TIMEOUT);
    }
    CHECK(thread_join(&thread, NULL) == 0);
    CHECK(sender.result == ASCIICHAT_OK && received == 32);
    server_transport->receive_timeout_ns = 2 * NS_PER_SEC_INT;
    quic_concurrent_receiver_t receiver = {.transport = server_transport};
    CHECK(acthread_create(&thread, "quic_concurrent_receiver", quic_concurrent_receive, &receiver) == 0);
    while (!atomic_load_u64(&receiver.started))
      time_sleep_ns(NS_PER_MS_INT);
    CHECK(acip_transport_send(client_transport, packets[0], lengths[0]) == ASCIICHAT_OK);
    start = time_get_ns();
    while (!atomic_load_u64(&receiver.done) && time_get_ns() - start < 2 * NS_PER_SEC_INT)
      time_sleep_ns(NS_PER_MS_INT);
    CHECK(thread_join(&thread, NULL) == 0);
    CHECK(receiver.result == ASCIICHAT_OK && receiver.length == lengths[0] &&
          memcmp(receiver.packet, packets[0], receiver.length) == 0);
    buffer_release(receiver.allocation);
    receiver = (quic_concurrent_receiver_t){.transport = server_transport};
    CHECK(acthread_create(&thread, "quic_interrupted_receiver", quic_concurrent_receive, &receiver) == 0);
    while (!atomic_load_u64(&receiver.started))
      time_sleep_ns(NS_PER_MS_INT);
    mutex_lock(&server_lane);
    server_handshake.crypto_ctx.rekey_in_progress = true;
    mutex_unlock(&server_lane);
    CHECK(acip_transport_send(client_transport, packets[0], lengths[0]) == ASCIICHAT_OK);
    CHECK(thread_join(&thread, NULL) == 0);
    CHECK(receiver.result == ERROR_INVALID_STATE && !receiver.allocation);
    start = time_get_ns();
    while (!atomic_load_u64(&failure_state.handled) && time_get_ns() - start < 2 * NS_PER_SEC_INT)
      time_sleep_ns(NS_PER_MS_INT);
    CHECK(atomic_load_u64(&failure_state.handled));
    server_state.peer = NULL;
    CHECK(server_transport->methods->queued_bytes(server_transport) == 0);
    CHECK(acip_transport_send(server_transport, packets[0], lengths[0]) == ERROR_INVALID_STATE);
    receiver = (quic_concurrent_receiver_t){.transport = client_transport};
    client_transport->receive_timeout_ns = 2 * NS_PER_SEC_INT;
    CHECK(acthread_create(&thread, "quic_interrupted_receiver", quic_concurrent_receive, &receiver) == 0);
    while (!atomic_load_u64(&receiver.started))
      time_sleep_ns(NS_PER_MS_INT);
    acip_transport_interrupt(client_transport);
    CHECK(thread_join(&thread, NULL) == 0);
    CHECK(receiver.result == ERROR_NETWORK && !receiver.allocation);
    CHECK(acip_quic_worker_stop(&client_worker) == ASCIICHAT_OK);
    CHECK(acip_quic_worker_stop(&server_worker) == ASCIICHAT_OK);
  }
  CHECK(acip_transport_close(client_transport) == ASCIICHAT_OK);
  CHECK(acip_transport_close(client_transport) == ASCIICHAT_OK);
  CHECK(!acip_transport_is_connected(client_transport));
  acip_transport_destroy(client_transport);
  acip_transport_destroy(server_transport);
  acip_quic_udp_endpoint_destroy(&client);
  acip_quic_udp_endpoint_destroy(&server);
  mutex_destroy(&client_lane);
  mutex_destroy(&server_lane);
  crypto_handshake_destroy(&client_handshake);
  crypto_handshake_destroy(&server_handshake);
  for (size_t i = 0; i < 6; ++i)
    buffer_release(packets[i]);
  server_tls->enable_datagrams = client_tls->enable_datagrams = false;
  socket_cleanup();
}

static void quic_udp_exchange(acip_quic_tls_t *server_tls, acip_quic_tls_t *client_tls) {
  client_tls->enable_datagrams = true;
  server_tls->enable_datagrams = false;
  CHECK(socket_init() == ASCIICHAT_OK);
  struct sockaddr_in loopback = {.sin_family = AF_INET, .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  acip_quic_udp_endpoint_t *server = NULL, *client = NULL;
  udp_test_state_t server_state = {0}, client_state = {0};
  size_t length = 0;
  uint8_t *packet = make_packet(PACKET_TYPE_CRYPTO_CAPABILITIES, 64, &length);
  server_state.packet = client_state.packet = packet;
  server_state.length = client_state.length = length;
  CHECK(acip_quic_udp_endpoint_create((struct sockaddr *)&loopback, sizeof(loopback), server_tls, udp_accept,
                                      &server_state, &server) == ASCIICHAT_OK);
  CHECK(acip_quic_udp_endpoint_create((struct sockaddr *)&loopback, sizeof(loopback), NULL, NULL, NULL, &client) ==
        ASCIICHAT_OK);
  struct sockaddr_storage server_address;
  socklen_t server_length = 0;
  CHECK(acip_quic_udp_endpoint_address(server, &server_address, &server_length) == ASCIICHAT_OK);
  acip_quic_connection_t *clients[2] = {NULL, NULL};
  uint64_t start = time_get_ns();
  for (size_t i = 0; i < 2; ++i) {
    CHECK(acip_quic_udp_connect(client, client_tls, "localhost", (struct sockaddr *)&server_address, server_length,
                                start, &clients[i]) == ASCIICHAT_OK);
    CHECK(acip_quic_connection_set_receiver(clients[i], udp_capture, &client_state) == ASCIICHAT_OK);
  }
  bool submitted = false;
  while (time_get_ns() - start < 10 * NS_PER_SEC_INT) {
    uint64_t now = time_get_ns();
    acip_quic_connection_t *failed = NULL;
    CHECK(acip_quic_udp_pump(client, now, &failed) == ASCIICHAT_OK && !failed);
    CHECK(acip_quic_udp_pump(server, now, &failed) == ASCIICHAT_OK && !failed);
    if (!submitted && server_state.accepted == 2 && acip_quic_connection_tls_ready(clients[0]) &&
        acip_quic_connection_tls_ready(clients[1]) && acip_quic_connection_tls_ready(server_state.peers[0]) &&
        acip_quic_connection_tls_ready(server_state.peers[1])) {
      size_t unauthenticated_length = 0;
      uint8_t *unauthenticated = make_packet(PACKET_TYPE_PING, 8, &unauthenticated_length);
      CHECK(acip_quic_connection_submit(clients[0], ACIP_TRAFFIC_CONTROL, unauthenticated, unauthenticated_length) ==
            ERROR_CRYPTO);
      buffer_release(unauthenticated);
      unauthenticated = make_packet(PACKET_TYPE_CRYPTO_NO_ENCRYPTION, 8, &unauthenticated_length);
      CHECK(acip_quic_connection_submit(clients[0], ACIP_TRAFFIC_CONTROL, unauthenticated, unauthenticated_length) ==
            ERROR_CRYPTO);
      buffer_release(unauthenticated);
      unauthenticated = make_packet(PACKET_TYPE_CRYPTO_REKEY_REQUEST, 8, &unauthenticated_length);
      CHECK(acip_quic_connection_submit(clients[0], ACIP_TRAFFIC_CONTROL, unauthenticated, unauthenticated_length) ==
            ERROR_CRYPTO);
      buffer_release(unauthenticated);
      for (size_t i = 0; i < 2; ++i) {
        CHECK(acip_quic_connection_datagram_payload(clients[i]) == 0);
        CHECK(acip_quic_connection_submit_datagram(clients[i], ACIP_TRAFFIC_AUDIO, packet, length, 0) ==
              ERROR_NOT_SUPPORTED);
        CHECK(acip_quic_connection_submit(clients[i], ACIP_TRAFFIC_CONTROL, packet, length) == ASCIICHAT_OK);
        CHECK(acip_quic_connection_submit(server_state.peers[i], ACIP_TRAFFIC_CONTROL, packet, length) == ASCIICHAT_OK);
      }
      CHECK(acip_quic_connection_finish_stream(clients[0], ACIP_TRAFFIC_CONTROL) == ASCIICHAT_OK);
      submitted = true;
    }
    if (submitted && client_state.received == 2 && server_state.received == 2 &&
        (acip_quic_connection_received_fin(server_state.peers[0], ACIP_TRAFFIC_CONTROL) ||
         acip_quic_connection_received_fin(server_state.peers[1], ACIP_TRAFFIC_CONTROL)))
      break;
    time_sleep_ns(NS_PER_MS_INT);
  }
  CHECK(submitted && client_state.received == 2 && server_state.received == 2);
  CHECK(acip_quic_connection_received_fin(server_state.peers[0], ACIP_TRAFFIC_CONTROL) !=
        acip_quic_connection_received_fin(server_state.peers[1], ACIP_TRAFFIC_CONTROL));
  CHECK(acip_quic_udp_remove(client, clients[0]) == ASCIICHAT_OK);
  CHECK(acip_quic_connection_submit(clients[1], ACIP_TRAFFIC_CONTROL, packet, length) == ASCIICHAT_OK);
  for (size_t i = 0; i < 2; ++i)
    CHECK(acip_quic_connection_submit(server_state.peers[i], ACIP_TRAFFIC_CONTROL, packet, length) == ASCIICHAT_OK);
  start = time_get_ns();
  while (time_get_ns() - start < 10 * NS_PER_SEC_INT && (client_state.received < 3 || server_state.received < 3)) {
    uint64_t now = time_get_ns();
    acip_quic_connection_t *failed = NULL;
    CHECK(acip_quic_udp_pump(client, now, &failed) == ASCIICHAT_OK && !failed);
    CHECK(acip_quic_udp_pump(server, now, &failed) == ASCIICHAT_OK && !failed);
    time_sleep_ns(NS_PER_MS_INT);
  }
  CHECK(client_state.received == 3 && server_state.received == 3);
  acip_quic_udp_endpoint_destroy(&client);
  acip_quic_udp_endpoint_destroy(&server);
  buffer_release(packet);
  client_tls->enable_datagrams = false;
  socket_cleanup();
}

static void quic_udp_bad_identity(acip_quic_tls_t *server_tls, acip_quic_tls_t *client_tls) {
  CHECK(socket_init() == ASCIICHAT_OK);
  struct sockaddr_in loopback = {.sin_family = AF_INET, .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  acip_quic_udp_endpoint_t *server = NULL, *client = NULL;
  udp_test_state_t state = {0};
  CHECK(acip_quic_udp_endpoint_create((struct sockaddr *)&loopback, sizeof(loopback), server_tls, udp_accept, &state,
                                      &server) == ASCIICHAT_OK);
  CHECK(acip_quic_udp_endpoint_create((struct sockaddr *)&loopback, sizeof(loopback), NULL, NULL, NULL, &client) ==
        ASCIICHAT_OK);
  struct sockaddr_storage address;
  socklen_t address_length = 0;
  CHECK(acip_quic_udp_endpoint_address(server, &address, &address_length) == ASCIICHAT_OK);
  acip_quic_connection_t *connection = NULL;
  uint64_t start = time_get_ns();
  CHECK(acip_quic_udp_connect(client, client_tls, "other.example", (struct sockaddr *)&address, address_length, start,
                              &connection) == ASCIICHAT_OK);
  bool rejected = false;
  while (time_get_ns() - start < 10 * NS_PER_SEC_INT) {
    uint64_t now = time_get_ns();
    acip_quic_connection_t *failed = NULL;
    asciichat_error_t result = acip_quic_udp_pump(client, now, &failed);
    if (result != ASCIICHAT_OK) {
      CHECK(result == ERROR_CRYPTO && failed == connection);
      rejected = true;
      break;
    }
    CHECK(!failed);
    CHECK(acip_quic_udp_pump(server, now, &failed) == ASCIICHAT_OK && !failed);
    time_sleep_ns(NS_PER_MS_INT);
  }
  CHECK(rejected && !acip_quic_connection_tls_ready(connection));
  acip_quic_udp_endpoint_destroy(&client);
  acip_quic_udp_endpoint_destroy(&server);
  socket_cleanup();
}

typedef struct {
  acip_quic_udp_endpoint_t *endpoint;
  atomic_t started;
  asciichat_error_t result;
  bool ready;
} quic_udp_wait_test_t;

static void *quic_udp_wait_thread(void *arg) {
  quic_udp_wait_test_t *state = arg;
  atomic_store_u64(&state->started, 1);
  state->result = acip_quic_udp_wait(state->endpoint, 2 * NS_PER_SEC_INT, false, &state->ready);
  return NULL;
}

static void quic_udp_wakeup_test(void) {
  CHECK(socket_init() == ASCIICHAT_OK);
  struct sockaddr_in loopback = {.sin_family = AF_INET, .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  acip_quic_udp_endpoint_t *endpoint = NULL;
  CHECK(acip_quic_udp_endpoint_create((struct sockaddr *)&loopback, sizeof(loopback) + 1, NULL, NULL, NULL,
                                      &endpoint) == ERROR_INVALID_PARAM &&
        !endpoint);
  CHECK(acip_quic_udp_endpoint_create((struct sockaddr *)&loopback, sizeof(loopback), NULL, NULL, NULL, &endpoint) ==
        ASCIICHAT_OK);
  uint64_t wait_ns = 0;
  bool want_write = true, ready = true;
  CHECK(acip_quic_udp_wait_parameters(endpoint, time_get_ns(), 10 * NS_PER_MS_INT, &wait_ns, &want_write) ==
        ASCIICHAT_OK);
  CHECK(wait_ns == 10 * NS_PER_MS_INT && !want_write);
  CHECK(acip_quic_udp_wait(endpoint, wait_ns, want_write, &ready) == ASCIICHAT_OK && !ready);
  quic_udp_wait_test_t state = {.endpoint = endpoint};
  asciichat_thread_t thread;
  CHECK(acthread_create(&thread, "quic_udp_wait", quic_udp_wait_thread, &state) == 0);
  while (!atomic_load_u64(&state.started))
    time_sleep_ns(NS_PER_MS_INT);
  CHECK(acip_quic_udp_wake(endpoint) == ASCIICHAT_OK);
  CHECK(thread_join(&thread, NULL) == 0);
  CHECK(state.result == ASCIICHAT_OK && state.ready);
  acip_quic_connection_t *failed = NULL;
  CHECK(acip_quic_udp_pump(endpoint, time_get_ns(), &failed) == ASCIICHAT_OK && !failed);
  CHECK(acip_quic_udp_wait(endpoint, 0, false, &ready) == ASCIICHAT_OK && !ready);
  acip_quic_udp_endpoint_destroy(&endpoint);
  socket_cleanup();
}

static void quic_retry_token_test(void) {
  uint8_t secret[32];
  for (size_t i = 0; i < sizeof(secret); ++i)
    secret[i] = (uint8_t)(i + 7);
  ngtcp2_cid retry = {.datalen = 16}, original = {.datalen = 16}, recovered;
  memset(retry.data, 0x31, retry.datalen);
  memset(original.data, 0x42, original.datalen);
  struct sockaddr_in address = {
      .sin_family = AF_INET, .sin_port = HOST_TO_NET_U16(12345), .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  uint8_t token[NGTCP2_CRYPTO_MAX_RETRY_TOKENLEN2];
  uint64_t now = 100 * NS_PER_SEC_INT;
  ngtcp2_ssize token_length =
      ngtcp2_crypto_generate_retry_token2(token, secret, sizeof(secret), NGTCP2_PROTO_VER_V1,
                                          (struct sockaddr *)&address, sizeof(address), &retry, &original, now);
  CHECK(token_length > 0);
  CHECK(ngtcp2_crypto_verify_retry_token2(&recovered, token, (size_t)token_length, secret, sizeof(secret),
                                          NGTCP2_PROTO_VER_V1, (struct sockaddr *)&address, sizeof(address), &retry,
                                          10 * NS_PER_SEC_INT, now + NS_PER_SEC_INT) == 0);
  CHECK(recovered.datalen == original.datalen && memcmp(recovered.data, original.data, original.datalen) == 0);
  struct sockaddr_in wrong_address = address;
  wrong_address.sin_port = HOST_TO_NET_U16(12346);
  CHECK(ngtcp2_crypto_verify_retry_token2(&recovered, token, (size_t)token_length, secret, sizeof(secret),
                                          NGTCP2_PROTO_VER_V1, (struct sockaddr *)&wrong_address, sizeof(wrong_address),
                                          &retry, 10 * NS_PER_SEC_INT, now + NS_PER_SEC_INT) != 0);
  CHECK(ngtcp2_crypto_verify_retry_token2(&recovered, token, (size_t)token_length, secret, sizeof(secret),
                                          NGTCP2_PROTO_VER_V1, (struct sockaddr *)&address, sizeof(address), &retry,
                                          10 * NS_PER_SEC_INT, now + 11 * NS_PER_SEC_INT) != 0);
  retry.data[0] ^= 1;
  CHECK(ngtcp2_crypto_verify_retry_token2(&recovered, token, (size_t)token_length, secret, sizeof(secret),
                                          NGTCP2_PROTO_VER_V1, (struct sockaddr *)&address, sizeof(address), &retry,
                                          10 * NS_PER_SEC_INT, now + NS_PER_SEC_INT) != 0);
  retry.data[0] ^= 1;
  token[(size_t)token_length - 1] ^= 1;
  CHECK(ngtcp2_crypto_verify_retry_token2(&recovered, token, (size_t)token_length, secret, sizeof(secret),
                                          NGTCP2_PROTO_VER_V1, (struct sockaddr *)&address, sizeof(address), &retry,
                                          10 * NS_PER_SEC_INT, now + NS_PER_SEC_INT) != 0);
}

static void quic_cli_probe(const char *certificate, const char *port_text) {
  char *end = NULL;
  unsigned long port = strtoul(port_text, &end, 10);
  CHECK(end && !*end && port > 0 && port <= 65535);
  CHECK(socket_init() == ASCIICHAT_OK);
  struct sockaddr_in local = {.sin_family = AF_INET, .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  struct sockaddr_in remote = local;
  remote.sin_port = HOST_TO_NET_U16((uint16_t)port);
  acip_quic_config_t config = {.ca_file = certificate, .enable_datagrams = true};
  acip_quic_endpoint_t *endpoint = NULL;
  acip_transport_t *transport = NULL;
  CHECK(acip_quic_endpoint_open((struct sockaddr *)&local, sizeof(local), false, &config, &endpoint) == ASCIICHAT_OK);
  CHECK(acip_quic_connect(endpoint, (struct sockaddr *)&remote, sizeof(remote), "localhost", &transport) ==
        ASCIICHAT_OK);
  uint64_t start = time_get_ns();
  acip_transport_capabilities_t caps;
  while (time_get_ns() - start < 10 * NS_PER_SEC_INT &&
         acip_transport_get_capabilities(transport, &caps) != ASCIICHAT_OK)
    time_sleep_ns(NS_PER_MS_INT);
  CHECK(acip_transport_get_capabilities(transport, &caps) == ASCIICHAT_OK);
  crypto_handshake_context_t handshake;
  CHECK(crypto_handshake_init_with_password("quic-cli-probe", &handshake, false, "test-password") == ASCIICHAT_OK);
  SAFE_STRNCPY(handshake.server_ip, "127.0.0.1", sizeof(handshake.server_ip));
  handshake.server_port = (uint16_t)port;
  CHECK(network_client_negotiate(transport, &handshake, ACIP_CRYPTO_FULL, NETWORK_PROTOCOL_MEDIA) == ASCIICHAT_OK);
  transport->receive_timeout_ns = 5 * NS_PER_SEC_INT;
  void *packet = NULL, *allocation = NULL;
  size_t length = 0;
  CHECK(acip_transport_recv(transport, &packet, &length, &allocation) == ASCIICHAT_OK);
  CHECK(length >= sizeof(packet_header_t));
  packet_header_t header;
  memcpy(&header, packet, sizeof(header));
  CHECK(NET_TO_HOST_U16(header.type) == PACKET_TYPE_SERVER_STATE);
  buffer_release(allocation);
  CHECK(acip_quic_endpoint_stop(endpoint) == ASCIICHAT_OK);
  CHECK(acip_quic_release(endpoint, &transport) == ASCIICHAT_OK);
  CHECK(acip_quic_endpoint_destroy(&endpoint) == ASCIICHAT_OK);
  crypto_handshake_destroy(&handshake);
  socket_cleanup();
}

static void quic_http3_probe_server(const char *certificate, const char *key, const char *port_text,
                                    const char *origin) {
  char *end = NULL;
  unsigned long port = strtoul(port_text, &end, 10);
  CHECK(end && !*end && port > 0 && port <= 65535);
  CHECK(socket_init() == ASCIICHAT_OK);
  struct sockaddr_in local = {.sin_family = AF_INET,
                              .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK),
                              .sin_port = HOST_TO_NET_U16((uint16_t)port)};
  acip_quic_config_t config = {.certificate_file = certificate,
                               .private_key_file = key,
                               .enable_datagrams = true,
                               .webtransport_origin = origin,
                               .webtransport_path = "/acip"};
  acip_quic_endpoint_t *endpoint = NULL;
  CHECK(acip_quic_endpoint_open((struct sockaddr *)&local, sizeof(local), true, &config, &endpoint) == ASCIICHAT_OK);
  log_info("HTTP/3 probe server ready");
  uint64_t started = time_get_ns();
  acip_transport_t *transport = NULL;
  while (time_get_ns() - started < 30 * NS_PER_SEC_INT && !transport) {
    asciichat_error_t result = acip_quic_accept(endpoint, &transport);
    CHECK(result == ASCIICHAT_OK || result == ERROR_NETWORK_TIMEOUT);
    if (!transport)
      time_sleep_ns(10 * NS_PER_MS_INT);
  }
  CHECK(transport);
  void *packet = NULL, *allocation = NULL;
  size_t length = 0;
  while (time_get_ns() - started < 30 * NS_PER_SEC_INT && !packet) {
    asciichat_error_t result = acip_transport_recv(transport, &packet, &length, &allocation);
    CHECK(result == ASCIICHAT_OK || result == ERROR_NETWORK_TIMEOUT);
  }
  CHECK(packet && acip_transport_send(transport, packet, length) == ASCIICHAT_OK);
  buffer_release(allocation);
  log_info("HTTP/3 probe echoed ACIP control packet");
  while (time_get_ns() - started < 30 * NS_PER_SEC_INT)
    time_sleep_ns(10 * NS_PER_MS_INT);
  CHECK(acip_quic_endpoint_stop(endpoint) == ASCIICHAT_OK);
  CHECK(acip_quic_release(endpoint, &transport) == ASCIICHAT_OK);
  CHECK(acip_quic_endpoint_destroy(&endpoint) == ASCIICHAT_OK);
  socket_cleanup();
}

int main(int argc, char **argv) {
  if (argc == 6 && strcmp(argv[1], "--http3-server") == 0) {
    log_init(NULL, LOG_DEBUG, false, false);
    quic_http3_probe_server(argv[2], argv[3], argv[4], argv[5]);
    log_destroy();
    return 0;
  }
  if (argc == 4 && strcmp(argv[1], "--cli-probe") == 0) {
    log_init(NULL, LOG_ERROR, false, false);
    errno_suppress(true);
    quic_cli_probe(argv[2], argv[3]);
    log_destroy();
    return 0;
  }
  CHECK(argc == 5);
  log_init(NULL, LOG_ERROR, false, false);
  errno_suppress(true);
  quic_secure_framing();
  EVP_PKEY *key = make_key(), *other_key = make_key();
  X509 *cert = make_certificate(key), *other_cert = make_certificate(other_key);
  write_certificate(argv[1], cert);
  write_key(argv[2], key);
  write_certificate(argv[3], other_cert);
  write_key(argv[4], other_key);
  X509_free(cert);
  X509_free(other_cert);
  EVP_PKEY_free(key);
  EVP_PKEY_free(other_key);

  acip_quic_tls_t *server = NULL, *client = NULL, *untrusted = NULL, *wrong_alpn = NULL;
  CHECK(acip_quic_tls_server_create("acip/1", argv[1], argv[2], &server) == ASCIICHAT_OK);
  CHECK(acip_quic_tls_client_create("acip/1", argv[1], &client) == ASCIICHAT_OK);
  CHECK(acip_quic_tls_client_create("acip/1", argv[3], &untrusted) == ASCIICHAT_OK);
  CHECK(acip_quic_tls_client_create("h3", argv[1], &wrong_alpn) == ASCIICHAT_OK);
  CHECK(SSL_CTX_get_min_proto_version(client->ssl_ctx) == TLS1_3_VERSION);
  CHECK(SSL_CTX_get_max_proto_version(client->ssl_ctx) == TLS1_3_VERSION);
  CHECK(SSL_CTX_get_max_early_data(client->ssl_ctx) == 0);
  CHECK(handshake(server, client, "localhost", false, false, false));
  CHECK(handshake(server, client, "127.0.0.1", false, false, false));
  CHECK(!handshake(server, client, "other.example", false, false, false));
  CHECK(!handshake(server, untrusted, "localhost", false, false, false));
  CHECK(!handshake(server, wrong_alpn, "localhost", false, false, false));
  CHECK(!handshake(server, client, "localhost", true, false, false));
  CHECK(!handshake(server, client, "localhost", false, true, false));
  CHECK(!handshake(server, client, "localhost", false, false, true));
  asciichat_error_t quic_failure = ASCIICHAT_OK;
  bool dropped = false;
  CHECK(quic_handshake(server, client, "localhost", false, false, &dropped, &quic_failure) &&
        quic_failure == ASCIICHAT_OK);
  CHECK(quic_handshake(server, client, "localhost", false, true, &dropped, &quic_failure) &&
        quic_failure == ASCIICHAT_OK);
  CHECK(quic_handshake(server, client, "localhost", true, false, &dropped, &quic_failure) && dropped &&
        quic_failure == ASCIICHAT_OK);
  CHECK(!quic_handshake(server, client, "other.example", false, false, &dropped, &quic_failure) &&
        quic_failure == ERROR_CRYPTO);
  CHECK(!quic_handshake(server, untrusted, "localhost", false, false, &dropped, &quic_failure) &&
        quic_failure == ERROR_CRYPTO);
  CHECK(!quic_handshake(server, wrong_alpn, "localhost", false, false, &dropped, &quic_failure) &&
        quic_failure == ERROR_CRYPTO);
  quic_udp_exchange(server, client);
  quic_udp_bad_identity(server, client);
  quic_udp_wakeup_test();
  quic_retry_token_test();
  quic_worker_accept_test(server, client);
  quic_udp_secure_exchange(server, client, false);
  quic_udp_secure_exchange(server, client, true);
  quic_public_endpoint_test(argv[1], argv[2]);
  quic_network_server_test(argv[1], argv[2], true);
  quic_network_server_test(argv[1], argv[2], false);

  acip_quic_tls_t *invalid = (acip_quic_tls_t *)server;
  CHECK(acip_quic_tls_server_create("acip/1", argv[1], argv[4], &invalid) == ERROR_CRYPTO && !invalid);
  invalid = (acip_quic_tls_t *)server;
  CHECK(acip_quic_tls_client_create("acip/1", "", &invalid) == ERROR_INVALID_PARAM && !invalid);
  CHECK(acip_quic_tls_client_create("", argv[1], &invalid) == ERROR_INVALID_PARAM && !invalid);
  invalid = (acip_quic_tls_t *)server;
  CHECK(acip_quic_tls_server_create("acip/1", NULL, argv[2], &invalid) == ERROR_INVALID_PARAM && !invalid);
  invalid = (acip_quic_tls_t *)server;
  CHECK(acip_quic_tls_client_create("acip/1", argv[2], &invalid) == ERROR_CRYPTO && !invalid);

  ngtcp2_crypto_conn_ref ref = {.get_conn = unused_connection};
  SSL *ssl = NULL;
  CHECK(acip_quic_tls_session_create(client, "localhost", NULL, &ssl) == ERROR_INVALID_PARAM && !ssl);
  CHECK(acip_quic_tls_session_create(client, "localhost", &ref, &ssl) == ASCIICHAT_OK);
  CHECK(ssl && SSL_get_app_data(ssl) == &ref);
  CHECK(acip_quic_tls_verify_ready(client, ssl) == ERROR_INVALID_STATE);
  acip_quic_tls_session_destroy(&ssl);
  CHECK(!ssl);
  CHECK(acip_quic_tls_session_create(server, NULL, &ref, &ssl) == ASCIICHAT_OK);
  acip_quic_tls_session_destroy(&ssl);
  acip_quic_tls_destroy(&wrong_alpn);
  acip_quic_tls_destroy(&untrusted);
  acip_quic_tls_destroy(&client);
  acip_quic_tls_destroy(&server);
  log_destroy();
  return 0;
}
