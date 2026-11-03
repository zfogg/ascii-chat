#include <ascii-chat/common.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
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

static bool quic_handshake(acip_quic_tls_t *server_tls, acip_quic_tls_t *client_tls, const char *server_name,
                           bool drop_first_server_packet, bool *dropped, asciichat_error_t *failure) {
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
  CHECK(quic_handshake(server, client, "localhost", false, &dropped, &quic_failure) && quic_failure == ASCIICHAT_OK);
  CHECK(quic_handshake(server, client, "localhost", true, &dropped, &quic_failure) && dropped &&
        quic_failure == ASCIICHAT_OK);
  CHECK(!quic_handshake(server, client, "other.example", false, &dropped, &quic_failure) &&
        quic_failure == ERROR_CRYPTO);
  CHECK(!quic_handshake(server, untrusted, "localhost", false, &dropped, &quic_failure) &&
        quic_failure == ERROR_CRYPTO);
  CHECK(!quic_handshake(server, wrong_alpn, "localhost", false, &dropped, &quic_failure) &&
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
