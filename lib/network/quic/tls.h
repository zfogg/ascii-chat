#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <openssl/ssl.h>
#include <stdbool.h>

/* The context and its ALPN storage must outlive every SSL created from it. */
typedef struct {
  SSL_CTX *ssl_ctx;
  unsigned char alpn[256];
  unsigned int alpn_length;
  bool server;
  /* Set before creating connections; defaults to disabled. */
  bool enable_datagrams;
  bool webtransport;
  bool accept_native;
  char webtransport_origin[512];
  char webtransport_path[512];
  /**
   * @brief When code needs to work with acip quic tls, it keeps `ssl_ctx`, `alpn_length`, `server`, `enable_datagrams`
   * here instead of scattering it across globals. Initialization establishes the handles and sizes; teardown must
   * release any allocation, lock, thread, or callback context recorded in those fields.
   */
} acip_quic_tls_t;

/** Create a TLS 1.3 server context. Certificate and key are copied by OpenSSL. */
asciichat_error_t quic_tls_server_create(const char *alpn, const char *certificate_file, const char *private_key_file,
                                         acip_quic_tls_t **out);
/** Create a TLS 1.3 client context. NULL ca_file selects the system trust store. */
asciichat_error_t quic_tls_client_create(const char *alpn, const char *ca_file, acip_quic_tls_t **out);

/** Configure certificate identity verification, SNI and ALPN on a client SSL. */
asciichat_error_t quic_tls_client_configure(acip_quic_tls_t *tls, SSL *ssl, const char *server_name);

/** The connection reference is borrowed and must remain valid while SSL uses it.
 * The caller owns the SSL and must clear its app data before freeing it if the
 * ngtcp2 connection or reference can have been destroyed first. */
asciichat_error_t quic_tls_session_create(acip_quic_tls_t *tls, const char *server_name,
                                          ngtcp2_crypto_conn_ref *connection, SSL **out);
/** Call after the TLS handshake and before exposing the connection as ready.
 * Requires the configured ALPN; a client also requires a verified peer cert. */
asciichat_error_t quic_tls_verify_ready(acip_quic_tls_t *tls, SSL *ssl);
void quic_tls_session_destroy(SSL **ssl);
void quic_tls_destroy(acip_quic_tls_t **tls);
