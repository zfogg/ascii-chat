#pragma once
#include <ascii-chat/network/acip/quic.h>
/* Open the shared native/HTTP3 endpoint in native; webtransport remains NULL
 * so socket ownership is never aliased. On failure close any created endpoint. */
asciichat_error_t session_quic_listeners_open(int port, acip_quic_endpoint_t **native,
                                              acip_quic_endpoint_t **webtransport);
asciichat_error_t session_quic_listeners_close(acip_quic_endpoint_t **native, acip_quic_endpoint_t **webtransport);
