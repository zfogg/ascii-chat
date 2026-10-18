#pragma once

#include <ascii-chat/network/acip/acds_client.h>

/** Event-driven discovery protocol. Callbacks consume borrowed data before returning.
 * The caller owns transport I/O, deadlines, and serialization of calls to this object.
 * No function waits for input or owns the transport.
 */
typedef asciichat_error_t (*acds_protocol_send_fn)(void *, packet_type_t, const void *, size_t);
typedef enum {
  ACDS_IDLE,
  ACDS_CREATING,
  ACDS_LOOKING_UP,
  ACDS_JOINING,
  ACDS_CREATED,
  ACDS_FOUND,
  ACDS_JOINED,
  ACDS_ENDED,
  ACDS_FAILED
} acds_protocol_state_t;

typedef struct {
  acds_protocol_send_fn send;
  void *user_data;
  acds_protocol_state_t state;
  bool terminal;
  bool auto_join;
  bool require_webrtc;
  char error[256];
  acds_session_join_params_t join;
  char name[49];
  acip_session_created_t created;
  acip_session_info_t info;
  acip_session_joined_t joined;
} acds_protocol_t;

void acds_protocol_init(acds_protocol_t *protocol, acds_protocol_send_fn send, void *user_data);
void acds_protocol_destroy(acds_protocol_t *protocol);
asciichat_error_t acds_protocol_create(acds_protocol_t *protocol, const acds_session_create_params_t *params,
                                       uint8_t total_keys, uint8_t key_index);
asciichat_error_t acds_protocol_lookup(acds_protocol_t *protocol, const char *name);
asciichat_error_t acds_protocol_join(acds_protocol_t *protocol, const acds_session_join_params_t *params);
/** Create then join, or look up then join, using the same identity throughout. */
asciichat_error_t acds_protocol_enter(acds_protocol_t *protocol, bool create, const char *name, const char *password);
asciichat_error_t acds_protocol_receive(acds_protocol_t *protocol, packet_type_t type, const void *payload,
                                        size_t length);
asciichat_error_t acds_protocol_leave(acds_protocol_t *protocol);

/** Handshake event steps shared by native receive loops and browser socket events.
 * Context must already be initialized and configured with any identity options.
 * Input remains caller-owned. */
asciichat_error_t acds_handshake_begin(acip_transport_t *transport, uint8_t mode);
asciichat_error_t acds_handshake_receive(crypto_handshake_context_t *context, acip_transport_t *transport,
                                         packet_type_t type, const void *payload, size_t length);
