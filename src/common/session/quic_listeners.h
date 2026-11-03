#pragma once
#include <ascii-chat/network/acip/quic.h>
/* Snapshot the configured native and HTTP/3 listeners into owned endpoints.
 * On failure the caller must close any partially created endpoints. */
asciichat_error_t session_quic_listeners_open(int port, acip_quic_endpoint_t **native,
                                              acip_quic_endpoint_t **webtransport);
asciichat_error_t session_quic_listeners_close(acip_quic_endpoint_t **native, acip_quic_endpoint_t **webtransport);
