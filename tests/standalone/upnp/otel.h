#pragma once

// Router lifecycle tests isolate the gateway implementation from telemetry.
#define ASCII_OTEL_SCOPE(operation) ((void)0)
#define ASCII_OTEL_ATTR_STRING(key, value) ((void)(key), (void)(value))
#define ASCII_OTEL_ATTR_INT(key, value) ((void)(key), (void)(value))

typedef enum {
  ASCII_OTEL_METRIC_NAT_MAP_SUCCESS,
  ASCII_OTEL_METRIC_NAT_MAP_FAILURE,
} ascii_otel_metric_t;

static inline void ascii_otel_metric(ascii_otel_metric_t metric, double value) {
  (void)metric;
  (void)value;
}
