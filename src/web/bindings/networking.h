#pragma once
#include <emscripten/bind.h>
extern "C" {
#include <ascii-chat/crypto/session.h>
}

EMSCRIPTEN_DECLARE_VAL_TYPE(PeerBytes);
EMSCRIPTEN_DECLARE_VAL_TYPE(QuicControlResult);
EMSCRIPTEN_DECLARE_VAL_TYPE(QuicPacketResult);

QuicControlResult network_quic_output(crypto_context_t *context);
QuicPacketResult network_quic_packet(crypto_context_t *context, bool seal, unsigned flow, unsigned delivery,
                                     PeerBytes packet);
