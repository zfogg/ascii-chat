#pragma once
#include <ascii-chat/network/acip/security.h>
#include <ascii-chat/crypto/session.h>

asciichat_error_t browser_security_init(crypto_context_t *);
asciichat_error_t browser_security_packet(crypto_context_t *, bool, uint32_t, uint32_t, const uint8_t *, size_t,
                                          uint8_t *, size_t, size_t *, uint64_t *);
asciichat_error_t browser_security_tick(crypto_context_t *);
asciichat_error_t browser_security_output(crypto_context_t *, uint8_t *, size_t, size_t *, uint32_t *);
double browser_security_epoch(crypto_context_t *);
