#pragma once
#include <emscripten/bind.h>
#include "../peer_crypto.h"

EMSCRIPTEN_DECLARE_VAL_TYPE(PeerBytes);
EMSCRIPTEN_DECLARE_VAL_TYPE(QuicControlResult);
EMSCRIPTEN_DECLARE_VAL_TYPE(QuicPacketResult);

int network_peer_quic_begin(browser_peer_crypto_t *peer);
int network_peer_quic_init(browser_peer_crypto_t *peer);
int network_peer_quic_tick(browser_peer_crypto_t *peer);
double network_peer_quic_epoch(browser_peer_crypto_t *peer);
QuicControlResult network_peer_quic_output(browser_peer_crypto_t *peer);
QuicPacketResult network_peer_quic_packet(browser_peer_crypto_t *peer, bool seal, unsigned flow, unsigned delivery,
                                          PeerBytes packet);
