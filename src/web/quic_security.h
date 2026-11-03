#pragma once
#include "../../lib/network/quic/rekey.h"
#define BROWSER_QUIC_REKEY_QUEUE 16
typedef struct {
  acip_quic_security_t security;
  acip_quic_rekey_t rekey;
  uint8_t *output[BROWSER_QUIC_REKEY_QUEUE];
  size_t length[BROWSER_QUIC_REKEY_QUEUE];
  acip_traffic_class_t flow[BROWSER_QUIC_REKEY_QUEUE];
  size_t count;
  asciichat_error_t failure;
} browser_quic_security_t;
void browser_quic_cleanup(browser_quic_security_t *state);
asciichat_error_t browser_quic_receive(browser_quic_security_t *state, acip_traffic_class_t flow,
                                       acip_delivery_t delivery, const void *packet, size_t length, bool *consumed);
