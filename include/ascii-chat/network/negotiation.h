#pragma once
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/crypto/handshake/common.h>

typedef enum { NETWORK_PROTOCOL_MEDIA, NETWORK_PROTOCOL_DISCOVERY } network_protocol_t;

/** Wire profiles preserve deployed media and discovery handshakes. Credentials
 * @brief When code needs to work with network negotiation, it keeps `protocol`, `handshake`, `identity_keys`, `identity_key_count` here instead of scattering it across globals. Initialization establishes the handles and sizes; teardown must release any allocation, lock, thread, or callback context recorded in those fields.
 * are borrowed from server configuration until all connections have drained.
 * The application context retains handshake storage until the closed callback.
 */
typedef struct {
  network_protocol_t protocol;
  crypto_handshake_context_t *handshake;
  const private_key_t *identity_keys;
  size_t identity_key_count;
  public_key_t *whitelist;
  size_t whitelist_count;
  const char *password;
  bool secure, require_auth, require_identity, require_encryption, forbid_encryption;
  bool initialized, ready, encrypt_data;
  unsigned step;
} network_negotiation_t;

asciichat_error_t network_negotiation_receive(network_negotiation_t *, acip_transport_t *, packet_type_t, const void *,
                                              size_t);

/** Drive a client wire profile using connection-owned security storage.
 * A single absolute deadline covers negotiation; the caller retains transport
 * and credentials until cancellation and all users have drained.
 */
asciichat_error_t network_client_negotiate(acip_transport_t *, crypto_handshake_context_t *, uint8_t mode,
                                           network_protocol_t protocol);
