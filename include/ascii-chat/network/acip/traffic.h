#pragma once

#include <stdint.h>

typedef enum {
  ACIP_TRAFFIC_CONTROL,
  ACIP_TRAFFIC_AUDIO,
  ACIP_TRAFFIC_VISUAL,
} acip_traffic_class_t;

typedef enum {
  ACIP_DELIVERY_RELIABLE,
  ACIP_DELIVERY_DATAGRAM,
} acip_delivery_t;

typedef struct {
  acip_traffic_class_t traffic_class;
  acip_delivery_t delivery;
  uint64_t deadline_ns;
} acip_send_options_t;

/** Classify a plaintext ACIP packet type before any transport encryption. */
acip_traffic_class_t acip_packet_traffic_class(uint16_t type);
