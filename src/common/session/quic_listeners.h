#pragma once
#include <ascii-chat/network/quic/quic.h>
/* Open the shared native/HTTP3 endpoint in native; webtransport remains NULL
 * so socket ownership is never aliased. On failure close any created endpoint. */
asciichat_error_t session_quic_listeners_open(int port, quic_endpoint_t **native, quic_endpoint_t **webtransport);
asciichat_error_t session_quic_listeners_close(quic_endpoint_t **native, quic_endpoint_t **webtransport);
