#include "connection.h"

#include <ascii-chat/common.h>
#include <ngtcp2/ngtcp2_crypto_ossl.h>
#include <sodium.h>
#include <string.h>

#define ACIP_QUIC_CONNECTION_ID_LENGTH 16
#define ACIP_QUIC_INITIAL_STREAM_WINDOW (5U * 1024U * 1024U)
#define ACIP_QUIC_INITIAL_CONNECTION_WINDOW (16U * 1024U * 1024U)
#define ACIP_QUIC_IDLE_TIMEOUT_NS (30ULL * 1000000000ULL)

struct acip_quic_connection {
  ngtcp2_conn *conn;
  ngtcp2_crypto_conn_ref reference;
  ngtcp2_crypto_ossl_ctx *crypto;
  SSL *ssl;
  acip_quic_tls_t *tls;
  uint64_t last_time_ns;
  bool failed;
  bool tls_ready;
  bool packet_pending;
};

static ngtcp2_conn *get_connection(ngtcp2_crypto_conn_ref *reference) {
  return ((acip_quic_connection_t *)reference->user_data)->conn;
}

static void random_bytes(uint8_t *output, size_t length, const ngtcp2_rand_ctx *context) {
  (void)context;
  randombytes_buf(output, length);
}

static int new_connection_id(ngtcp2_conn *conn, ngtcp2_cid *cid, ngtcp2_stateless_reset_token *token, size_t length,
                             void *user_data) {
  (void)conn;
  (void)user_data;
  if (length > sizeof(cid->data))
    return NGTCP2_ERR_CALLBACK_FAILURE;
  randombytes_buf(cid->data, length);
  cid->datalen = length;
  randombytes_buf(token->data, sizeof(token->data));
  return 0;
}

static ngtcp2_callbacks connection_callbacks(bool server) {
  ngtcp2_callbacks callbacks = {0};
  callbacks.client_initial = server ? NULL : ngtcp2_crypto_client_initial_cb;
  callbacks.recv_client_initial = server ? ngtcp2_crypto_recv_client_initial_cb : NULL;
  callbacks.recv_crypto_data = ngtcp2_crypto_recv_crypto_data_cb;
  callbacks.encrypt = ngtcp2_crypto_encrypt_cb;
  callbacks.decrypt = ngtcp2_crypto_decrypt_cb;
  callbacks.hp_mask = ngtcp2_crypto_hp_mask_cb;
  callbacks.recv_retry = server ? NULL : ngtcp2_crypto_recv_retry_cb;
  callbacks.rand = random_bytes;
  callbacks.update_key = ngtcp2_crypto_update_key_cb;
  callbacks.delete_crypto_aead_ctx = ngtcp2_crypto_delete_crypto_aead_ctx_cb;
  callbacks.delete_crypto_cipher_ctx = ngtcp2_crypto_delete_crypto_cipher_ctx_cb;
  callbacks.version_negotiation = ngtcp2_crypto_version_negotiation_cb;
  callbacks.get_new_connection_id2 = new_connection_id;
  callbacks.get_path_challenge_data2 = ngtcp2_crypto_get_path_challenge_data2_cb;
  return callbacks;
}

static bool valid_address(const ngtcp2_addr *address) {
  if (!address || !address->addr)
    return false;
  size_t length = (size_t)address->addrlen;
  if (length < sizeof(struct sockaddr) || length > sizeof(struct sockaddr_storage))
    return false;
  if (address->addr->sa_family == AF_INET)
    return length >= sizeof(struct sockaddr_in);
  if (address->addr->sa_family == AF_INET6)
    return length >= sizeof(struct sockaddr_in6);
  return false;
}

static bool valid_path(const ngtcp2_path *path) {
  return path && valid_address(&path->local) && valid_address(&path->remote);
}

static asciichat_error_t connection_create(acip_quic_tls_t *tls, const char *server_name, const ngtcp2_path *path,
                                           const ngtcp2_pkt_hd *initial, uint64_t now_ns,
                                           acip_quic_connection_t **out) {
  if (!out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing QUIC connection output");
  *out = NULL;
  if (!tls || !valid_path(path) || now_ns == UINT64_MAX || (tls->server != (initial != NULL)) ||
      (!tls->server && (!server_name || !*server_name)))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC connection configuration");
  if (sodium_init() < 0)
    return SET_ERRNO(ERROR_CRYPTO, "Cannot initialize QUIC random source");

  acip_quic_connection_t *connection = SAFE_CALLOC(1, sizeof(*connection), acip_quic_connection_t *);
  if (!connection)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate QUIC connection");
  connection->tls = tls;
  connection->last_time_ns = now_ns;
  connection->reference.get_conn = get_connection;
  connection->reference.user_data = connection;

  ngtcp2_cid dcid = {0}, scid = {0};
  scid.datalen = ACIP_QUIC_CONNECTION_ID_LENGTH;
  randombytes_buf(scid.data, scid.datalen);
  if (initial)
    dcid = initial->scid;
  else {
    dcid.datalen = ACIP_QUIC_CONNECTION_ID_LENGTH;
    randombytes_buf(dcid.data, dcid.datalen);
  }
  ngtcp2_settings settings;
  ngtcp2_settings_default(&settings);
  settings.initial_ts = now_ns;
  ngtcp2_transport_params params;
  ngtcp2_transport_params_default(&params);
  params.initial_max_streams_uni = 3;
  params.initial_max_stream_data_uni = ACIP_QUIC_INITIAL_STREAM_WINDOW;
  params.initial_max_data = ACIP_QUIC_INITIAL_CONNECTION_WINDOW;
  params.max_idle_timeout = ACIP_QUIC_IDLE_TIMEOUT_NS;
  if (initial) {
    params.original_dcid = initial->dcid;
    params.original_dcid_present = 1;
  }
  ngtcp2_callbacks callbacks = connection_callbacks(tls->server);
  int rc = initial ? ngtcp2_conn_server_new(&connection->conn, &dcid, &scid, path, initial->version, &callbacks,
                                            &settings, &params, NULL, connection)
                   : ngtcp2_conn_client_new(&connection->conn, &dcid, &scid, path, NGTCP2_PROTO_VER_V1, &callbacks,
                                            &settings, &params, NULL, connection);
  if (rc != 0) {
    asciichat_error_t result = SET_ERRNO(ERROR_NETWORK, "Cannot create QUIC connection: %s", ngtcp2_strerror(rc));
    acip_quic_connection_destroy(&connection);
    return result;
  }
  asciichat_error_t result = acip_quic_tls_session_create(tls, server_name, &connection->reference, &connection->ssl);
  if (result != ASCIICHAT_OK) {
    acip_quic_connection_destroy(&connection);
    return result;
  }
  rc = ngtcp2_crypto_ossl_ctx_new(&connection->crypto, connection->ssl);
  if (rc != 0) {
    result = SET_ERRNO(ERROR_CRYPTO, "Cannot create QUIC OpenSSL connection context: %d", rc);
    acip_quic_connection_destroy(&connection);
    return result;
  }
  ngtcp2_conn_set_tls_native_handle(connection->conn, connection->crypto);
  *out = connection;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_connection_client_create(acip_quic_tls_t *tls, const char *server_name,
                                                     const ngtcp2_path *path, uint64_t now_ns,
                                                     acip_quic_connection_t **out) {
  return connection_create(tls, server_name, path, NULL, now_ns, out);
}

asciichat_error_t acip_quic_connection_server_create(acip_quic_tls_t *tls, const ngtcp2_path *path, const void *initial,
                                                     size_t initial_length, uint64_t now_ns,
                                                     acip_quic_connection_t **out) {
  if (!out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing QUIC connection output");
  *out = NULL;
  if (!initial || !initial_length)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing QUIC Initial packet");
  ngtcp2_pkt_hd header;
  int rc = ngtcp2_accept(&header, initial, initial_length);
  if (rc != 0 || header.version != NGTCP2_PROTO_VER_V1)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid or unsupported QUIC Initial packet");
  return connection_create(tls, NULL, path, &header, now_ns, out);
}

static asciichat_error_t update_readiness(acip_quic_connection_t *connection) {
  if (!connection->tls_ready && ngtcp2_conn_get_handshake_completed2(connection->conn)) {
    asciichat_error_t result = acip_quic_tls_verify_ready(connection->tls, connection->ssl);
    if (result != ASCIICHAT_OK) {
      connection->failed = true;
      return result;
    }
    connection->tls_ready = true;
  }
  return ASCIICHAT_OK;
}

static asciichat_error_t update_time(acip_quic_connection_t *connection, uint64_t now_ns) {
  if (now_ns < connection->last_time_ns || now_ns == UINT64_MAX)
    return SET_ERRNO(ERROR_INVALID_PARAM, "QUIC monotonic time moved backwards or is invalid");
  connection->last_time_ns = now_ns;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_connection_receive(acip_quic_connection_t *connection, const ngtcp2_path *path,
                                               const void *packet, size_t length, uint64_t now_ns) {
  if (!connection || !connection->conn || connection->failed || !valid_path(path) || !packet || !length)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC packet input");
  asciichat_error_t result = update_time(connection, now_ns);
  if (result != ASCIICHAT_OK)
    return result;
  ngtcp2_pkt_info info = {0};
  int rc = ngtcp2_conn_read_pkt(connection->conn, path, &info, packet, length, now_ns);
  if (rc != 0) {
    connection->failed = true;
    return SET_ERRNO(rc == NGTCP2_ERR_CRYPTO ? ERROR_CRYPTO : ERROR_NETWORK_PROTOCOL, "QUIC receive failed: %s",
                     ngtcp2_strerror(rc));
  }
  return update_readiness(connection);
}

asciichat_error_t acip_quic_connection_write(acip_quic_connection_t *connection, ngtcp2_path_storage *path,
                                             void *packet, size_t capacity, size_t *length, uint64_t now_ns) {
  if (!length)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing QUIC packet length output");
  *length = 0;
  if (!connection || !connection->conn || connection->failed || !path || !packet || capacity < 1200)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC packet output");
  if (connection->packet_pending)
    return SET_ERRNO(ERROR_INVALID_STATE, "Previous QUIC packet has not been transmitted");
  asciichat_error_t result = update_time(connection, now_ns);
  if (result != ASCIICHAT_OK)
    return result;
  ngtcp2_path_storage_zero(path);
  ngtcp2_pkt_info info = {0};
  ngtcp2_ssize written = ngtcp2_conn_write_pkt(connection->conn, &path->path, &info, packet, capacity, now_ns);
  if (written < 0) {
    connection->failed = true;
    return SET_ERRNO(written == NGTCP2_ERR_CRYPTO ? ERROR_CRYPTO : ERROR_NETWORK, "QUIC write failed: %s",
                     ngtcp2_strerror((int)written));
  }
  *length = (size_t)written;
  connection->packet_pending = written > 0;
  return update_readiness(connection);
}

asciichat_error_t acip_quic_connection_packet_sent(acip_quic_connection_t *connection, uint64_t now_ns) {
  if (!connection || !connection->conn || connection->failed || !connection->packet_pending)
    return SET_ERRNO(ERROR_INVALID_STATE, "No QUIC packet awaiting transmission");
  asciichat_error_t result = update_time(connection, now_ns);
  if (result != ASCIICHAT_OK)
    return result;
  ngtcp2_conn_update_pkt_tx_time(connection->conn, now_ns);
  connection->packet_pending = false;
  return ASCIICHAT_OK;
}

uint64_t acip_quic_connection_expiry(const acip_quic_connection_t *connection) {
  return connection && connection->conn && !connection->failed ? ngtcp2_conn_get_expiry(connection->conn) : UINT64_MAX;
}

asciichat_error_t acip_quic_connection_handle_expiry(acip_quic_connection_t *connection, uint64_t now_ns) {
  if (!connection || !connection->conn || connection->failed)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC timeout state");
  asciichat_error_t result = update_time(connection, now_ns);
  if (result != ASCIICHAT_OK)
    return result;
  int rc = ngtcp2_conn_handle_expiry(connection->conn, now_ns);
  if (rc != 0) {
    connection->failed = true;
    return SET_ERRNO(ERROR_NETWORK_TIMEOUT, "QUIC timer failed: %s", ngtcp2_strerror(rc));
  }
  return update_readiness(connection);
}

bool acip_quic_connection_tls_ready(const acip_quic_connection_t *connection) {
  return connection && !connection->failed && connection->tls_ready;
}

void acip_quic_connection_destroy(acip_quic_connection_t **connection) {
  if (!connection || !*connection)
    return;
  ngtcp2_conn_del((*connection)->conn);
  (*connection)->conn = NULL;
  acip_quic_tls_session_destroy(&(*connection)->ssl);
  ngtcp2_crypto_ossl_ctx_del((*connection)->crypto);
  SAFE_FREE(*connection);
}
