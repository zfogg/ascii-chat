#include "tls.h"

#include <ascii-chat/common.h>
#include <ascii-chat/util/ip.h>
#include <ngtcp2/ngtcp2_crypto_ossl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <string.h>

static asciichat_error_t tls_error(const char *operation) {
  char message[256] = {0};
  unsigned long code = ERR_get_error();
  if (code)
    ERR_error_string_n(code, message, sizeof(message));
  return SET_ERRNO(ERROR_CRYPTO, "%s: %s", operation, code ? message : "OpenSSL rejected the operation");
}

static int select_alpn(SSL *ssl, const unsigned char **out, unsigned char *out_length, const unsigned char *offered,
                       unsigned int offered_length, void *arg) {
  (void)ssl;
  acip_quic_tls_t *tls = arg;
  unsigned char *selected = NULL;
  unsigned char selected_length = 0;
  if (SSL_select_next_proto(&selected, &selected_length, tls->alpn, tls->alpn_length, offered, offered_length) !=
      OPENSSL_NPN_NEGOTIATED)
    return SSL_TLSEXT_ERR_ALERT_FATAL;
  *out = selected;
  *out_length = selected_length;
  return SSL_TLSEXT_ERR_OK;
}

static asciichat_error_t create_context(bool server, const char *alpn, acip_quic_tls_t **out) {
  if (!out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing QUIC TLS context or ALPN");
  *out = NULL;
  if (!alpn)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing QUIC TLS context or ALPN");
  size_t length = strnlen(alpn, sizeof(((acip_quic_tls_t *)0)->alpn));
  if (!length || length > 255)
    return SET_ERRNO(ERROR_INVALID_PARAM, "QUIC ALPN must contain 1 to 255 bytes");

  acip_quic_tls_t *tls = SAFE_CALLOC(1, sizeof(*tls), acip_quic_tls_t *);
  if (!tls)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate QUIC TLS context");
  tls->server = server;
  tls->alpn[0] = (unsigned char)length;
  memcpy(tls->alpn + 1, alpn, length);
  tls->alpn_length = (unsigned int)length + 1;
  ERR_clear_error();
  tls->ssl_ctx = SSL_CTX_new(server ? TLS_server_method() : TLS_client_method());
  if (!tls->ssl_ctx || SSL_CTX_set_min_proto_version(tls->ssl_ctx, TLS1_3_VERSION) != 1 ||
      SSL_CTX_set_max_proto_version(tls->ssl_ctx, TLS1_3_VERSION) != 1 ||
      SSL_CTX_set_max_early_data(tls->ssl_ctx, 0) != 1) {
    asciichat_error_t result = tls_error("Cannot configure QUIC TLS 1.3");
    acip_quic_tls_destroy(&tls);
    return result;
  }
  SSL_CTX_set_options(tls->ssl_ctx, SSL_OP_NO_COMPRESSION | SSL_OP_NO_RENEGOTIATION);
  if (server)
    SSL_CTX_set_alpn_select_cb(tls->ssl_ctx, select_alpn, tls);
  else
    SSL_CTX_set_verify(tls->ssl_ctx, SSL_VERIFY_PEER, NULL);
  *out = tls;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_tls_server_create(const char *alpn, const char *certificate_file,
                                              const char *private_key_file, acip_quic_tls_t **out) {
  if (!out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "QUIC server requires a certificate and private key");
  *out = NULL;
  if (!certificate_file || !*certificate_file || !private_key_file || !*private_key_file)
    return SET_ERRNO(ERROR_INVALID_PARAM, "QUIC server requires a certificate and private key");
  acip_quic_tls_t *tls = NULL;
  asciichat_error_t result = create_context(true, alpn, &tls);
  if (result != ASCIICHAT_OK)
    return result;
  ERR_clear_error();
  if (SSL_CTX_use_certificate_chain_file(tls->ssl_ctx, certificate_file) != 1 ||
      SSL_CTX_use_PrivateKey_file(tls->ssl_ctx, private_key_file, SSL_FILETYPE_PEM) != 1 ||
      SSL_CTX_check_private_key(tls->ssl_ctx) != 1) {
    result = tls_error("Cannot load QUIC server certificate or private key");
    acip_quic_tls_destroy(&tls);
    return result;
  }
  *out = tls;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_tls_client_create(const char *alpn, const char *ca_file, acip_quic_tls_t **out) {
  if (!out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC client trust configuration");
  *out = NULL;
  if (ca_file && !*ca_file)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC client trust configuration");
  acip_quic_tls_t *tls = NULL;
  asciichat_error_t result = create_context(false, alpn, &tls);
  if (result != ASCIICHAT_OK)
    return result;
  ERR_clear_error();
  int loaded = ca_file ? SSL_CTX_load_verify_locations(tls->ssl_ctx, ca_file, NULL)
                       : SSL_CTX_set_default_verify_paths(tls->ssl_ctx);
  if (loaded != 1) {
    result = tls_error("Cannot load QUIC client trust roots");
    acip_quic_tls_destroy(&tls);
    return result;
  }
  *out = tls;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_tls_client_configure(acip_quic_tls_t *tls, SSL *ssl, const char *server_name) {
  if (!tls || tls->server || !ssl || SSL_get_SSL_CTX(ssl) != tls->ssl_ctx || !server_name || !*server_name)
    return SET_ERRNO(ERROR_INVALID_PARAM, "QUIC client requires the expected server identity");
  ERR_clear_error();
  X509_VERIFY_PARAM *verify = SSL_get0_param(ssl);
  X509_VERIFY_PARAM_set_hostflags(verify, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
  if (SSL_set_alpn_protos(ssl, tls->alpn, tls->alpn_length) != 0 ||
      (is_valid_ip(server_name)
           ? X509_VERIFY_PARAM_set1_ip_asc(verify, server_name) != 1
           : (SSL_set_tlsext_host_name(ssl, server_name) != 1 || SSL_set1_host(ssl, server_name) != 1)))
    return tls_error("Cannot configure QUIC server identity");
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_tls_session_create(acip_quic_tls_t *tls, const char *server_name,
                                               ngtcp2_crypto_conn_ref *connection, SSL **out) {
  if (!out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC TLS session arguments");
  *out = NULL;
  if (!tls || !tls->ssl_ctx || !connection || !connection->get_conn || (tls->server && server_name))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC TLS session arguments");
  ERR_clear_error();
  SSL *ssl = SSL_new(tls->ssl_ctx);
  if (!ssl)
    return tls_error("Cannot create QUIC TLS session");
  if (SSL_set_app_data(ssl, connection) != 1) {
    asciichat_error_t result = tls_error("Cannot attach QUIC connection to TLS session");
    SSL_free(ssl);
    return result;
  }
  asciichat_error_t result = ASCIICHAT_OK;
  if (tls->server) {
    SSL_set_accept_state(ssl);
    if (ngtcp2_crypto_ossl_configure_server_session(ssl) != 0)
      result = tls_error("Cannot configure server QUIC TLS callbacks");
  } else {
    SSL_set_connect_state(ssl);
    result = acip_quic_tls_client_configure(tls, ssl, server_name);
    if (result == ASCIICHAT_OK && ngtcp2_crypto_ossl_configure_client_session(ssl) != 0)
      result = tls_error("Cannot configure client QUIC TLS callbacks");
  }
  if (result != ASCIICHAT_OK) {
    acip_quic_tls_session_destroy(&ssl);
    return result;
  }
  *out = ssl;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_tls_verify_ready(acip_quic_tls_t *tls, SSL *ssl) {
  if (!tls || !ssl || SSL_get_SSL_CTX(ssl) != tls->ssl_ctx || !SSL_is_init_finished(ssl))
    return SET_ERRNO(ERROR_INVALID_STATE, "QUIC TLS handshake is not complete");
  const unsigned char *selected = NULL;
  unsigned int length = 0;
  SSL_get0_alpn_selected(ssl, &selected, &length);
  bool negotiated = false;
  for (size_t offset = 0; offset < tls->alpn_length;) {
    size_t protocol_length = tls->alpn[offset++];
    if (protocol_length > tls->alpn_length - offset)
      break;
    if (selected && protocol_length == (size_t)length && !memcmp(selected, tls->alpn + offset, protocol_length))
      negotiated = true;
    offset += protocol_length;
  }
  if (!negotiated)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "QUIC TLS ALPN was not negotiated");
  if (!tls->server) {
    X509_VERIFY_PARAM *verify = SSL_get0_param(ssl);
    const char *host = X509_VERIFY_PARAM_get0_host(verify, 0);
    char *ip = X509_VERIFY_PARAM_get1_ip_asc(verify);
    X509 *peer = SSL_get0_peer_certificate(ssl);
    bool identity_matches =
        peer && (host || ip) &&
        (!host || X509_check_host(peer, host, 0, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS, NULL) == 1) &&
        (!ip || X509_check_ip_asc(peer, ip, 0) == 1);
    OPENSSL_free(ip);
    if (!identity_matches || !(SSL_get_verify_mode(ssl) & SSL_VERIFY_PEER) || SSL_get_verify_result(ssl) != X509_V_OK)
      return SET_ERRNO(ERROR_CRYPTO, "QUIC TLS peer certificate or identity was not verified");
  }
  return ASCIICHAT_OK;
}

void acip_quic_tls_session_destroy(SSL **ssl) {
  if (ssl && *ssl) {
    SSL_set_app_data(*ssl, NULL);
    SSL_free(*ssl);
    *ssl = NULL;
  }
}

void acip_quic_tls_destroy(acip_quic_tls_t **tls) {
  if (tls && *tls) {
    SSL_CTX_free((*tls)->ssl_ctx);
    SAFE_FREE(*tls);
  }
}
