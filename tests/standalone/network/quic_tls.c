#include <ascii-chat/common.h>
#include <ascii-chat/network/acip/backpressure.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/buffer.h>
#include <network/quic/connection.h>
#include <network/quic/tls.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
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

static bool contains_bytes(const uint8_t *bytes, size_t length, const uint8_t *needle, size_t needle_length) {
  if (needle_length > length)
    return false;
  for (size_t i = 0; i <= length - needle_length; ++i)
    if (memcmp(bytes + i, needle, needle_length) == 0)
      return true;
  return false;
}

static void quic_media_exchange(acip_quic_connection_t *client, acip_quic_connection_t *server,
                                const ngtcp2_path *client_path, const ngtcp2_path *server_path, uint64_t *clock_ns,
                                bool drop_control) {
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
  for (size_t i = 0; i < 6; ++i) {
    acip_traffic_class_t expected = i < 2 ? (acip_traffic_class_t)i : ACIP_TRAFFIC_VISUAL;
    CHECK(at_client.order[i] == expected);
    if (!drop_control)
      CHECK(at_server.order[i] == expected);
  }
  if (drop_control && at_server.order[0] != ACIP_TRAFFIC_AUDIO)
    FATAL(ERROR_ASSERTION_FAILED, "Unexpected receive order %d, %d, %d after dropping control", at_server.order[0],
          at_server.order[1], at_server.order[2]);
  if (!drop_control) {
    size_t large_length = 0;
    uint8_t *large = make_packet(PACKET_TYPE_IMAGE_FRAME, 1024 * 1024, &large_length);
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
  if (success && !drop_first_server_packet)
    quic_media_exchange(client, server, &client_path, &server_path, &now, drop_control);
  acip_quic_connection_destroy(&server);
  acip_quic_connection_destroy(&client);
  return success;
}

int main(int argc, char **argv) {
  CHECK(argc == 5);
  log_init(NULL, LOG_ERROR, false, false);
  errno_suppress(true);
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
