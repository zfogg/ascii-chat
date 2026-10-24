/** @file otel.c Native telemetry lifecycle and ascii-chat instrumentation. */
#define ASCIICHAT_OTEL_IMPLEMENTATION
#include <ascii-chat/otel.h>
#ifdef ASCIICHAT_WITH_OTEL
#include <ascii-chat/common.h>
#include <ascii-chat/version.h>
#include <ascii-chat/network/acip/messages.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/common/protocol_constants.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/platform/process.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/debug/mutex.h>
#include <ascii-chat/tests/test_env.h>
#include <stdatomic.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static _Thread_local bool in_log;

// Keep library status codes inside this boundary and retain their diagnostic text.
static asciichat_error_t otel_status(otlp_status_t status, const char *operation) {
  if (status == OTLP_OK)
    return ASCIICHAT_OK;
  asciichat_error_t error;
  switch (status) {
  case OTLP_ERR_NOMEM:
    error = ERROR_MEMORY;
    break;
  case OTLP_ERR_INVALID_ARGUMENT:
  case OTLP_ERR_NULL:
    error = ERROR_INVALID_PARAM;
    break;
  case OTLP_ERR_OVERFLOW:
    error = ERROR_BUFFER_OVERFLOW;
    break;
  case OTLP_ERR_UTF8:
    error = ERROR_STRING;
    break;
  case OTLP_ERR_NETWORK:
  case OTLP_ERR_DNS:
  case OTLP_ERR_WRITE:
  case OTLP_ERR_READ:
    error = ERROR_NETWORK;
    break;
  case OTLP_ERR_CONNECT:
    error = ERROR_NETWORK_CONNECT;
    break;
  case OTLP_ERR_TIMEOUT:
    error = ERROR_NETWORK_TIMEOUT;
    break;
  case OTLP_ERR_PROTOCOL:
  case OTLP_ERR_INVALID_RESPONSE:
  case OTLP_ERR_HTTP_STATUS:
  case OTLP_ERR_SERVER:
    error = ERROR_NETWORK_PROTOCOL;
    break;
  case OTLP_ERR_THROTTLED:
    error = ERROR_RESOURCE_EXHAUSTED;
    break;
  case OTLP_ERR_BUFFER_FULL:
    error = ERROR_BUFFER_FULL;
    break;
  case OTLP_ERR_SHUTDOWN:
    error = ERROR_INVALID_STATE;
    break;
  case OTLP_ERR_WOULDBLOCK:
    error = ERROR_NETWORK_INCOMPLETE;
    break;
  case OTLP_ERR_NOT_IMPLEMENTED:
    error = ERROR_NOT_SUPPORTED;
    break;
  default:
    error = ERROR_INTERNAL;
    break;
  }
  // Error recording logs locally; do not export that diagnostic back into a failing exporter.
  bool previous = in_log;
  in_log = true;
  asciichat_error_t result =
      SET_ERRNO(error, "%s failed: %s (OTLP status %d)", operation, otlp_strerror(status), (int)status);
  in_log = previous;
  return result;
}

static const gotel_operation_t operations[] = {
#define ASCII_OTEL_OPERATION(id, name, realtime) {name, realtime},
#include <ascii-chat/otel_operations.def>
#undef ASCII_OTEL_OPERATION
};
static const double metric_bounds[] = {.0001, .001, .005, .01, .025, .05,  .1,   .25,   .5,    1,      2,
                                       5,     10,   50,   100, 500,  1000, 5000, 10000, 50000, 100000, 1000000};
static const gotel_metric_t metrics[] = {
#define ASCII_OTEL_METRIC(id, name, type, unit)                                                                        \
  {name, unit, OTLP_METRIC_##type, metric_bounds, sizeof(metric_bounds) / sizeof(metric_bounds[0])},
#include <ascii-chat/otel_metrics.def>
#undef ASCII_OTEL_METRIC
};
static _Atomic(gotel_host_t *) host;
static atomic_uint readers;
static atomic_bool stopping;
static asciichat_thread_t worker;
static bool worker_started;
static asciichat_error_t drain_result;
static int minimum_log_level;
static gotel_scope_t command_scope;
static _Thread_local ascii_otel_scope_t *current_scope;
static uint64_t shutdown_timeout_ns;
static atomic_uint_fast64_t exporter_retries, exporter_batches;
static void exporter_event(void *context, const otlp_event_t *event) {
  (void)context;
  if (event->code == OTLP_EVT_RETRY_ARMED)
    atomic_fetch_add(&exporter_retries, 1);
  if (event->code == OTLP_EVT_BATCH_SENT)
    atomic_fetch_add(&exporter_batches, 1);
}

static uint64_t telemetry_realtime_ns(void) {
  uint64_t now = time_get_realtime_ns();
#ifdef _WIN32
  // The platform clock returns FILETIME nanoseconds since 1601; OTLP uses Unix time.
  return now - UINT64_C(11644473600000000000);
#else
  return now;
#endif
}
static gotel_host_t *retain(void) {
  if (!atomic_load_explicit(&host, memory_order_acquire))
    return NULL;
  atomic_fetch_add_explicit(&readers, 1, memory_order_seq_cst);
  gotel_host_t *h = atomic_load_explicit(&host, memory_order_seq_cst);
  if (!h)
    atomic_fetch_sub_explicit(&readers, 1, memory_order_seq_cst);
  return h;
}
static void release(void) {
  atomic_fetch_sub_explicit(&readers, 1, memory_order_seq_cst);
}
const char *ascii_otel_current_operation(void) {
  return current_scope && current_scope->scope.operation < ASCII_OTEL_OPERATION_COUNT
             ? operations[current_scope->scope.operation].name
             : "";
}
bool ascii_otel_enabled(void) {
  return atomic_load_explicit(&host, memory_order_acquire) != NULL;
}
void ascii_otel_metric(ascii_otel_metric_t metric, double value) {
  gotel_host_t *h = retain();
  if (h) {
    (void)otel_status(gotel_metric_record(h, metric, value), "gotel_metric_record");
    release();
  }
}
enum {
  stats_metric_count = 0
#define ASCII_STATS_COUNTER(id, metric_id, name, unit) +1
#define ASCII_STATS_GAUGE(id, metric_id, name, unit) +1
#define ASCII_STATS_DURATION(id, metric_id, name, unit) +1
#include <ascii-chat/stats/telemetry.def>
#undef ASCII_STATS_COUNTER
#undef ASCII_STATS_GAUGE
#undef ASCII_STATS_DURATION
};
_Static_assert(stats_metric_count == STATS_COUNTER_COUNT + STATS_GAUGE_COUNT + STATS_DURATION_COUNT,
               "Every typed stat needs a telemetry metric");

ascii_otel_metric_t ascii_otel_stats_metric(stats_record_kind_t kind, unsigned id) {
  if (!((kind == STATS_RECORD_COUNTER && id < STATS_COUNTER_COUNT) ||
        (kind == STATS_RECORD_GAUGE && id < STATS_GAUGE_COUNT) ||
        (kind == STATS_RECORD_DURATION && id < STATS_DURATION_COUNT)))
    return ASCII_OTEL_METRIC_COUNT;
  ascii_otel_metric_t metric;
  switch (((unsigned)kind << 16) | id) {
#define ASCII_STATS_COUNTER(id, metric_id, name, unit)                                                                            \
  case (STATS_RECORD_COUNTER << 16) | STATS_COUNTER_##id:                                                              \
    metric = ASCII_OTEL_METRIC_##metric_id;                                                                     \
    break;
#define ASCII_STATS_GAUGE(id, metric_id, name, unit)                                                                              \
  case (STATS_RECORD_GAUGE << 16) | STATS_GAUGE_##id:                                                                  \
    metric = ASCII_OTEL_METRIC_##metric_id;                                                                       \
    break;
#define ASCII_STATS_DURATION(id, metric_id, name, unit)                                                                           \
  case (STATS_RECORD_DURATION << 16) | STATS_DURATION_##id:                                                            \
    metric = ASCII_OTEL_METRIC_##metric_id;                                                                    \
    break;
#include <ascii-chat/stats/telemetry.def>
#undef ASCII_STATS_COUNTER
#undef ASCII_STATS_GAUGE
#undef ASCII_STATS_DURATION
  default:
    return ASCII_OTEL_METRIC_COUNT;
  }
  return metric;
}

static void stats_metric_record(stats_record_kind_t kind, unsigned id, uint64_t value, void *context) {
  (void)context;
  ascii_otel_metric_t metric = ascii_otel_stats_metric(kind, id);
  if (metric == ASCII_OTEL_METRIC_COUNT)
    return;
  ascii_otel_metric(metric, kind == STATS_RECORD_DURATION ? (double)value / 1e9 : (double)value);
}

void ascii_otel_stats_attach(stats_scope_t *scope) {
  stats_scope_set_observer(scope, stats_metric_record, NULL);
}
static void exporter_diagnostic(void *context, otlp_log_level_t level, const char *message) {
  (void)context;
  (void)level;
  bool previous = in_log;
  in_log = true;
  log_debug("OTLP exporter: %s", message);
  in_log = previous;
}
#ifndef NDEBUG
static mutex_wait_snapshot_t watchdog_detail;
static bool watchdog_snapshot(void *context, gotel_watchdog_snapshot_t *out) {
  (void)context;
  static mutex_wait_snapshot_t waits[256];
  size_t count = 0;
  if (!mutex_stack_try_snapshot(waits, 256, &count, &out->limited))
    return false;
  uint64_t now = time_get_ns();
  for (size_t i = 0; i < count; ++i) {
    if (waits[i].cycle) {
      if (!out->blocked_threads) {
        watchdog_detail = waits[i];
        memcpy(out->parent.trace_id, waits[i].trace_id, sizeof(out->parent.trace_id));
        memcpy(out->parent.span_id, waits[i].span_id, sizeof(out->parent.span_id));
        out->parent.valid = waits[i].context_valid;
        out->parent.sampled = waits[i].context_sampled;
        out->parent.realtime = true;
        out->parent_snapshot.context = out->parent;
        memcpy(out->parent_snapshot.parent_span_id, waits[i].parent_span_id, sizeof(waits[i].parent_span_id));
        out->parent_snapshot.start_unix_ns = waits[i].span_start_unix_ns;
        SAFE_STRNCPY(out->parent_snapshot.name, waits[i].span_name, sizeof(out->parent_snapshot.name));
      }
      ++out->blocked_threads;
      uint64_t age = now >= waits[i].since_ns ? now - waits[i].since_ns : 0;
      if (!out->wait_ns || age < out->wait_ns)
        out->wait_ns = age;
    }
  }
  return true;
}
static void watchdog_decorate(void *context, gotel_scope_t *scope) {
  (void)context;
  gotel_attr_string(scope, "thread.blocked.span.name", watchdog_detail.span_name);
  gotel_attr_int(scope, "asciichat_errno.code", watchdog_detail.error_code);
  gotel_attr_string(scope, "asciichat_errno.code_message", asciichat_error_string(watchdog_detail.error_code));
  gotel_attr_string(scope, "asciichat_errno.message", watchdog_detail.error_message);
}
#endif
static atomic_uint_fast64_t pipeline_capture_ns, pipeline_display_ns, pipeline_encode_ns;
static atomic_uint pipeline_expected;
void ascii_otel_pipeline_progress(unsigned stage, unsigned expected) {
  if (!ascii_otel_enabled()) return;
  uint64_t now = time_get_ns();
  if (stage == 0) {
    if (!atomic_load(&pipeline_capture_ns)) {
      atomic_store(&pipeline_display_ns, now);
      atomic_store(&pipeline_encode_ns, now);
    }
    if (!(expected & 1)) atomic_store(&pipeline_display_ns, now);
    if (!(expected & 2)) atomic_store(&pipeline_encode_ns, now);
    atomic_store(&pipeline_expected, expected);
    atomic_store(&pipeline_capture_ns, now);
  } else if (stage == 1) atomic_store(&pipeline_display_ns, now);
  else if (stage == 2) atomic_store(&pipeline_encode_ns, now);
  else {
    atomic_store(&pipeline_expected, 0);
    atomic_store(&pipeline_capture_ns, 0);
    ascii_otel_metric(ASCII_OTEL_METRIC_PIPELINE_DISPLAY_STALL, 0);
    ascii_otel_metric(ASCII_OTEL_METRIC_PIPELINE_ENCODE_STALL, 0);
  }
}
double ascii_otel_pipeline_age(uint64_t now, uint64_t capture, uint64_t progress, bool expected) {
  return expected && capture && progress && now >= capture && now >= progress && now-capture < 2 * NS_PER_SEC_INT
      ? (double)(now-progress)/1e9 : 0;
}
static void pipeline_health(gotel_host_t *h, uint64_t now) {
  uint64_t capture = atomic_load(&pipeline_capture_ns);
  unsigned expected = atomic_load(&pipeline_expected);
  uint64_t display = atomic_load(&pipeline_display_ns), encode = atomic_load(&pipeline_encode_ns);
  (void)gotel_metric_record(h, ASCII_OTEL_METRIC_PIPELINE_DISPLAY_STALL,
      ascii_otel_pipeline_age(now, capture, display, (expected & 1) != 0));
  (void)gotel_metric_record(h, ASCII_OTEL_METRIC_PIPELINE_ENCODE_STALL,
      ascii_otel_pipeline_age(now, capture, encode, (expected & 2) != 0));
}
static void *export_worker(void *arg) {
  gotel_host_t *h = arg;
  uint64_t last_health = 0;
  uint64_t last_failure_warning = 0, observed_failures = 0, observed_sent = 0;
  bool collector_failed = false;
  while (!atomic_load_explicit(&stopping, memory_order_acquire)) {
    uint64_t start = time_get_ns();
    (void)otel_status(gotel_tick(h), "gotel_tick");
    (void)otel_status(gotel_metric_record(h, ASCII_OTEL_METRIC_EXPORT_LATENCY, (double)(time_get_ns() - start) / 1e9),
                      "gotel_metric_record");
    if (!last_health || start - last_health >= NS_PER_SEC_INT) {
      last_health = start;
      pipeline_health(h, time_get_ns());
      platform_process_stats_t process;
      if (platform_process_stats(&process) == ASCIICHAT_OK) {
        (void)otel_status(gotel_metric_record(h, ASCII_OTEL_METRIC_CPU, process.cpu_seconds), "gotel_metric_record");
        (void)otel_status(gotel_metric_record(h, ASCII_OTEL_METRIC_RSS, (double)process.resident_bytes),
                          "gotel_metric_record");
        (void)otel_status(gotel_metric_record(h, ASCII_OTEL_METRIC_THREADS, (double)process.threads),
                          "gotel_metric_record");
      }
      otlp_exporter_stats_t stats = {0};
      (void)otel_status(gotel_stats(h, &stats), "gotel_stats");
      uint64_t accepted = stats.emitted + stats.emitted_logs + stats.emitted_metrics;
      uint64_t sent = stats.sent + stats.sent_logs + stats.sent_metrics;
      uint64_t failures = stats.network_err + stats.http_4xx + stats.http_5xx;
      // Exporter health logs stay local to avoid feeding failures back into its queue.
      bool previous = in_log;
      in_log = true;
      if (failures > observed_failures) {
        collector_failed = true;
        if (!last_failure_warning || start - last_failure_warning >= 30 * NS_PER_SEC_INT) {
          log_warn("Telemetry collector export failed (%llu failures); export continues in background",
                   (unsigned long long)failures);
          last_failure_warning = start;
        }
      } else if (collector_failed && sent > observed_sent) {
        log_info("Telemetry collector export recovered");
        collector_failed = false;
        last_failure_warning = 0;
      }
      in_log = previous;
      observed_failures = failures;
      observed_sent = sent;
      uint64_t failed = stats.dropped_err + stats.dropped_logs_err + stats.dropped_metrics_err;
      uint64_t pending = accepted > sent + failed ? accepted - sent - failed : 0;
      (void)otel_status(gotel_metric_record(h, ASCII_OTEL_METRIC_EXPORT_QUEUE,
                                            (double)pending / (3.0 * options_get()->otel_queue_capacity)),
                        "gotel_metric_record");
      (void)otel_status(gotel_metric_record(h, ASCII_OTEL_METRIC_EXPORT_SENT, (double)sent), "gotel_metric_record");
      (void)otel_status(gotel_metric_record(h, ASCII_OTEL_METRIC_EXPORT_DROP,
                                            (double)(failed + stats.dropped_full + stats.dropped_logs_full +
                                                     stats.dropped_metrics_full)),
                        "gotel_metric_record");
      (void)otel_status(gotel_metric_record(h, ASCII_OTEL_METRIC_EXPORT_RETRY, (double)stats.network_err),
                        "gotel_metric_record");
      (void)otel_status(gotel_metric_record(h, ASCII_OTEL_METRIC_EXPORT_REJECT, (double)stats.rejected_spans),
                        "gotel_metric_record");
      (void)otel_status(
          gotel_metric_record(h, ASCII_OTEL_METRIC_EXPORT_RETRIES, (double)atomic_load(&exporter_retries)),
          "gotel_metric_record");
      (void)otel_status(
          gotel_metric_record(h, ASCII_OTEL_METRIC_EXPORT_BATCHES, (double)atomic_load(&exporter_batches)),
          "gotel_metric_record");
    }
    platform_sleep_ms(10);
  }
  // Keep final connection setup (including synchronous DNS) off the exit thread.
  drain_result = otel_status(gotel_drain(h), "Drain telemetry exporter");
  return NULL;
}
void ascii_otel_scope_begin(ascii_otel_scope_t *s, ascii_otel_operation_t op) {
  ascii_otel_scope_parent(s, op, NULL);
}
void ascii_otel_scope_parent(ascii_otel_scope_t *s, ascii_otel_operation_t op, const gotel_context_t *parent) {
  gotel_host_t *h = retain();
  s->retained = h != NULL;
  if (h) {
    gotel_context_t root = {0};
    gotel_context_t context = gotel_current_context();
    bool lifetime_parent = current_scope && (current_scope->scope.operation == ASCII_OTEL_SESSION_CLIENT_LIKE_RUN ||
                                             current_scope->scope.operation == ASCII_OTEL_SESSION_ATTEMPT);
    // Lifetime spans finish only at shutdown. Completed operations must remain
    // inspectable while a server is running, including after abrupt termination.
    if (!parent && context.valid) {
      if (lifetime_parent || memcmp(context.span_id, command_scope.context.span_id, sizeof(context.span_id)) == 0) {
        parent = &root;
      } else {
        // A bounded operation owns one sampling decision for all its stages.
        context.realtime = true;
        parent = &context;
      }
    }
    gotel_begin(h, &s->scope, (size_t)op, parent);
    s->previous = current_scope;
    current_scope = s;
  }
}
void ascii_otel_scope_end(ascii_otel_scope_t *s) {
  if (!s || !s->retained)
    return;
  gotel_end(&s->scope);
  if (current_scope == s)
    current_scope = s->previous;
  s->retained = false;
  release();
}
void ascii_otel_ui_usage(const char *screen, const char *event, uint64_t duration_ns) {
  // UI transitions must not inherit realtime media sampling decisions.
  gotel_context_t root = {0};
  ASCII_OTEL_SCOPE_PARENT(UI_USAGE, &root);
  ASCII_OTEL_ATTR_STRING("ascii_chat.ui.screen", screen);
  ASCII_OTEL_ATTR_STRING("ascii_chat.ui.event", event);
  ASCII_OTEL_ATTR_INT("ascii_chat.ui.duration_ns", (int64_t)duration_ns);
}

void ascii_otel_call_session_used(const char *session_string) {
  if (!session_string || !session_string[0])
    return;
  // Call usage must not inherit a realtime packet's sampling decision.
  gotel_context_t root = {0};
  ASCII_OTEL_SCOPE_PARENT(CALL_SESSION_USED, &root);
  ASCII_OTEL_ATTR_STRING("ascii-chat.session.string", session_string);
}
void ascii_otel_packet(ascii_otel_scope_t *scope, uint16_t type, size_t payload_size, const char *action,
                       int transport) {
  if (!scope->retained || !scope->scope.span)
    return;
  const char *packet = acip_packet_type_name(type);
  char name[256], version[32];
  safe_snprintf(name, sizeof(name), "%s.%s", operations[scope->scope.operation].name, packet);
  safe_snprintf(version, sizeof(version), "%d.%d.%d", PROTOCOL_VERSION_MAJOR, PROTOCOL_VERSION_MINOR,
                PROTOCOL_VERSION_PATCH);
  (void)otel_status(otlp_span_set_name(scope->scope.span, name), "otlp_span_set_name");
  gotel_attr_string(&scope->scope, "network.protocol.name", "acip");
  gotel_attr_string(&scope->scope, "network.protocol.version", version);
  gotel_attr_string(&scope->scope, "network.packet.name", packet);
  gotel_attr_int(&scope->scope, "network.packet.type", type);
  gotel_attr_int(&scope->scope, "network.packet.size", (int64_t)payload_size);
  gotel_attr_string(&scope->scope, "network.packet.operation", action);
  const char *carrier = transport == ACIP_TRANSPORT_TCP         ? "tcp"
                        : transport == ACIP_TRANSPORT_WEBSOCKET ? "websocket"
                        : transport == ACIP_TRANSPORT_WEBRTC    ? "webrtc"
                                                                : "unknown";
  gotel_attr_string(&scope->scope, "acip.transport", carrier);
}
void ascii_otel_shutdown(void) {
  gotel_host_t *h = atomic_exchange_explicit(&host, NULL, memory_order_seq_cst);
  if (!h)
    return;
  gotel_end(&command_scope);
  uint64_t started = time_get_ns();
  while (atomic_load_explicit(&readers, memory_order_seq_cst) && time_get_ns() - started < shutdown_timeout_ns)
    platform_sleep_ms(1);
  bool producers_active = atomic_load_explicit(&readers, memory_order_seq_cst) != 0;
  atomic_store_explicit(&stopping, true, memory_order_release);
  if (worker_started) {
    if (thread_join_timeout(&worker, NULL, shutdown_timeout_ns) != 0) {
      log_warn("Telemetry worker did not stop within its deadline; exporter retained until process exit");
      return;
    }
    worker_started = false;
  }
  asciichat_error_t result = drain_result;
  otlp_exporter_stats_t stats = {0};
  (void)otel_status(gotel_stats(h, &stats), "gotel_stats");
  // exit() can bypass scope cleanup in a producer. Do not free its host while
  // it remains borrowed; the process will reclaim it after atexit handlers.
  if (!producers_active)
    gotel_destroy(h);
  else
    log_warn("Telemetry producer still active during process shutdown; exporter retained");
  if (result != ASCIICHAT_OK)
    log_warn("Telemetry flush deadline reached; pending records discarded");
  log_debug("Telemetry exporter stopped: spans=%llu/%llu logs=%llu/%llu metrics=%llu/%llu network_errors=%llu "
            "http_4xx=%llu dropped=%llu rejected=%llu",
            (unsigned long long)stats.sent, (unsigned long long)stats.emitted, (unsigned long long)stats.sent_logs,
            (unsigned long long)stats.emitted_logs, (unsigned long long)stats.sent_metrics,
            (unsigned long long)stats.emitted_metrics, (unsigned long long)stats.network_err,
            (unsigned long long)stats.http_4xx, (unsigned long long)(stats.dropped_full + stats.dropped_err),
            (unsigned long long)stats.rejected_spans);
}
static const char *telemetry_argument(const char *argument, int index, const char *mode, char *name, size_t size) {
  if (!index)
    return "ascii-chat";
  if (!strcmp(argument, mode))
    return mode;
  if (!strncmp(argument, "--", 2)) {
    size_t length = strcspn(argument, "=");
    if (length < size) {
      memcpy(name, argument, length);
      name[length] = 0;
      return name;
    }
  }
  return "[REDACTED]";
}

asciichat_error_t ascii_otel_init(const char *mode, int argc, char **argv) {
  // Test mode is a hard export prohibition, including explicit endpoints and CLI overrides.
  if (is_test_environment())
    return ASCIICHAT_OK;
  const options_t *o = options_get();
  const char *endpoint = o->otel_endpoint;
  bool endpoint_cli = false, timeout_cli = false;
  for (int i = 1; i < argc; ++i) {
    endpoint_cli |= !strcmp(argv[i], "--otel-endpoint") || !strncmp(argv[i], "--otel-endpoint=", 16);
    timeout_cli |= !strcmp(argv[i], "--otel-timeout") || !strncmp(argv[i], "--otel-timeout=", 15);
  }
  unsigned signals;
  asciichat_error_t parsed = otel_status(gotel_parse_signals(o->otel_enabled_telemetry, &signals),
                                         "Parse otel-enabled-telemetry (traces,logs,metrics or none)");
  if (parsed != ASCIICHAT_OK)
    return parsed;
  if (!isfinite(o->otel_realtime_sample_rate) || o->otel_realtime_sample_rate < 0 || o->otel_realtime_sample_rate > 1 ||
      o->otel_queue_capacity < 2 || (o->otel_queue_capacity & (o->otel_queue_capacity - 1)) || o->otel_timeout < 1 ||
      o->otel_metrics_interval < 1 || o->otel_realtime_max_per_second < 1)
    return SET_ERRNO(ERROR_CONFIG, "Invalid telemetry sampling, queue capacity, or interval");
  const char *log_names[] = {"dev", "debug", "info", "warn", "error", "fatal"};
  const int log_levels[] = {LOG_DEV, LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL};
  minimum_log_level = -1;
  for (size_t i = 0; i < sizeof(log_levels) / sizeof(log_levels[0]); ++i)
    if (strcmp(o->otel_log_level, log_names[i]) == 0)
      minimum_log_level = log_levels[i];
  if (minimum_log_level < 0)
    return SET_ERRNO(ERROR_CONFIG, "Invalid otel-log-level");
  if (!signals)
    return ASCIICHAT_OK;
  gotel_options_t config = {0};
  char service_name[OPTIONS_BUFF_SIZE];
  int n = safe_snprintf(service_name, sizeof(service_name), "ascii-chat-%s", mode);
  if (n < 0 || (size_t)n >= sizeof(service_name))
    return SET_ERRNO(ERROR_CONFIG, "Telemetry service name too long");
  config.exporter.service_name = service_name;
  config.exporter.batch_ms = 250;
  config.exporter.queue_capacity = (size_t)o->otel_queue_capacity;
  config.exporter.connect_timeout_ms = (uint32_t)o->otel_timeout;
  config.exporter.read_timeout_ms = (uint32_t)o->otel_timeout;
  config.exporter.flush_timeout_ms = (uint32_t)o->otel_timeout;
  otlp_resource_attr_t resources[] = {
      {.key = "ascii_chat.mode", .value = {.type = OTLP_VALUE_STRING, .v = {.string_val = mode}}},
      {.key = "service.version", .value = {.type = OTLP_VALUE_STRING, .v = {.string_val = ASCII_CHAT_VERSION_FULL}}},
  };
  config.exporter.resource_attributes = resources;
  config.exporter.n_resource_attributes = sizeof(resources) / sizeof(resources[0]);
  config.signals = signals;
  config.realtime_sample_rate = o->otel_realtime_sample_rate;
  config.realtime_max_per_second = (uint32_t)o->otel_realtime_max_per_second;
  config.metrics_interval_ms = (uint32_t)o->otel_metrics_interval;
  config.monotonic_ns = time_get_ns;
  config.realtime_ns = telemetry_realtime_ns;
  config.operations = operations;
  config.operation_count = ASCII_OTEL_OPERATION_COUNT;
  config.exporter_event = exporter_event;
  config.metrics = metrics;
  config.metric_count = ASCII_OTEL_METRIC_COUNT;
#ifndef NDEBUG
  config.watchdog.snapshot = watchdog_snapshot;
  config.watchdog.decorate = watchdog_decorate;
  config.watchdog.operation = ASCII_OTEL_THREAD_DEADLOCK;
  config.watchdog.metric = ASCII_OTEL_METRIC_DEADLOCK_THREADS;
#endif
  typedef char otel_arg_name_t[128];
  size_t argument_count = argc > 0 ? (size_t)argc : 1;
  const char **resource_args = SAFE_CALLOC(argument_count, sizeof(*resource_args), const char **);
  otel_arg_name_t *resource_names = SAFE_CALLOC(argument_count, sizeof(*resource_names), otel_arg_name_t *);
  for (int i = 0; i < argc; ++i)
    resource_args[i] = telemetry_argument(argv[i], i, mode, resource_names[i], sizeof(resource_names[i]));
  config.argc = argc;
  config.argv = resource_args;
  gotel_env_storage_t env;
  asciichat_error_t env_status =
      otel_status(gotel_options_apply_env(&config, &env, OPT_OTEL_ENDPOINT_DEFAULT), "Configure OTEL environment");
  if (env_status == ASCIICHAT_OK &&
      (endpoint_cli || SAFE_GETENV("ASCII_CHAT_OTEL_ENDPOINT") || strcmp(endpoint, OPT_OTEL_ENDPOINT_DEFAULT)))
    env_status = otel_status(gotel_options_set_endpoint(&config, &env, endpoint), "Configure telemetry endpoint");
  if (timeout_cli || SAFE_GETENV("ASCII_CHAT_OTEL_TIMEOUT")) {
    config.exporter.connect_timeout_ms = (uint32_t)o->otel_timeout;
    config.exporter.read_timeout_ms = (uint32_t)o->otel_timeout;
    config.exporter.flush_timeout_ms = (uint32_t)o->otel_timeout;
  }
  if (*o->otel_ca_file)
    config.tls_ca_file = o->otel_ca_file;
  if (env_status != ASCIICHAT_OK || !config.signals || !config.exporter.endpoint) {
    SAFE_FREE(resource_args);
    SAFE_FREE(resource_names);
    return env_status;
  }
  gotel_host_t *h = NULL;
  asciichat_error_t created = otel_status(gotel_create(&config, &h), "Initialize telemetry exporter");
  SAFE_FREE(resource_args);
  SAFE_FREE(resource_names);
  if (created != ASCIICHAT_OK)
    return created;
  gotel_set_logger(h, exporter_diagnostic, NULL);
  shutdown_timeout_ns = (uint64_t)o->otel_timeout * NS_PER_MS_INT;
  atomic_store(&stopping, false);
  if (acthread_create(&worker, "otel_export", export_worker, h) != 0) {
    gotel_destroy(h);
    return SET_ERRNO(ERROR_THREAD, "Cannot start telemetry exporter");
  }
  worker_started = true;
  (void)atexit(ascii_otel_shutdown);
  atomic_store_explicit(&host, h, memory_order_release);
  /* Only option names are exported for arbitrary value-bearing flags. This also
   * handles -Psecret and future credential options without a fragile denylist. */
  gotel_begin(h, &command_scope, ASCII_OTEL_CLI, NULL);
  gotel_attr_string(&command_scope, "process.command", "ascii-chat");
  gotel_attr_string(&command_scope, "cli.command.name", mode);
  if (command_scope.span) {
    otlp_value_t *args = SAFE_CALLOC(argument_count, sizeof(*args), otlp_value_t *);
    otel_arg_name_t *names = SAFE_CALLOC(argument_count, sizeof(*names), otel_arg_name_t *);
    if (args && names) {
      for (int i = 0; i < argc; ++i) {
        args[i].type = OTLP_VALUE_STRING;
        args[i].v.string_val = telemetry_argument(argv[i], i, mode, names[i], sizeof(names[i]));
      }
      (void)otel_status(otlp_span_set_attribute_array(command_scope.span, "process.command_args", args, (size_t)argc),
                        "otlp_span_set_attribute_array");
    }
    SAFE_FREE(args);
    SAFE_FREE(names);
  }

  log_info("Telemetry enabled: %s (%s)", config.exporter.endpoint, o->otel_enabled_telemetry);
  return ASCIICHAT_OK;
}
void ascii_otel_log(int level, const char *file, int line, const char *function, const char *format, va_list args) {
  if (in_log || level < minimum_log_level || !atomic_load_explicit(&host, memory_order_acquire))
    return;
  in_log = true;
  gotel_host_t *h = retain();
  if (h) {
    char body[4096];
    int n = safe_vsnprintf(body, sizeof(body), format, args);
    if (n >= 0) {
      otlp_severity_t severity = level >= LOG_FATAL   ? OTLP_SEVERITY_FATAL
                                 : level >= LOG_ERROR ? OTLP_SEVERITY_ERROR
                                 : level >= LOG_WARN  ? OTLP_SEVERITY_WARN
                                 : level >= LOG_INFO  ? OTLP_SEVERITY_INFO
                                 : level >= LOG_DEBUG ? OTLP_SEVERITY_DEBUG
                                                      : OTLP_SEVERITY_TRACE;
      gotel_log(h, severity, body, file, line, function);
    }
    release();
  }
  in_log = false;
}
gotel_context_t ascii_otel_context(void) {
  return gotel_current_context();
}
void ascii_otel_scope_int(ascii_otel_scope_t *s, const char *key, int64_t value) {
  if (s->retained)
    gotel_attr_int(&s->scope, key, value);
}
void ascii_otel_exit(int code) {
  gotel_attr_int(&command_scope, "process.exit.code", code);
  if (code)
    gotel_error(&command_scope, "ascii_chat.exit", "Command failed");
  else {
    command_scope.failed = false;
    if (command_scope.span)
      (void)otel_status(otlp_span_set_status(command_scope.span, OTLP_STATUS_CODE_UNSET, ""), "otlp_span_set_status");
  }
}
void ascii_otel_frame_rejected(const char *validator, const char *reason, size_t bytes,
                               uint32_t width, uint32_t height, asciichat_error_t code) {
  ascii_otel_metric(ASCII_OTEL_METRIC_FRAME_VALIDATION_REJECTED, 1);
  gotel_context_t parent = ascii_otel_context();
  // Rejections must remain inspectable even when the media operation was sampled out.
  if (!parent.sampled)
    parent = (gotel_context_t){0};
  ASCII_OTEL_SCOPE_PARENT(FRAME_VALIDATION_REJECTED, &parent);
  ASCII_OTEL_ATTR_STRING("frame.validation.validator", validator);
  ASCII_OTEL_ATTR_STRING("frame.validation.reason", reason);
  ASCII_OTEL_ATTR_INT("frame.size_bytes", (int64_t)bytes);
  ASCII_OTEL_ATTR_INT("frame.width", width);
  ASCII_OTEL_ATTR_INT("frame.height", height);
  ASCII_OTEL_ATTR_INT("asciichat_errno.code", code);
  ASCII_OTEL_ATTR_STRING("asciichat_errno.code_message", asciichat_error_string(code));
  if (otel_scope.retained)
    gotel_error(&otel_scope.scope, "ascii_chat.invalid_frame", reason);
}
ascii_otel_operation_t ascii_otel_failure_operation(asciichat_error_t code, const char *origin) {
  if (code == ASCIICHAT_OK || code == ERROR_SIGNAL_INTERRUPT || code == ERROR_NETWORK_INCOMPLETE)
    return ASCII_OTEL_OPERATION_COUNT;
  if ((code >= ERROR_CRYPTO && code <= ERROR_CRYPTO_VERIFICATION) || code == ERROR_CRYPTO_INIT ||
      code == ERROR_INVALID_PASSWORD || code == ERROR_INVALID_SIGNATURE)
    return ASCII_OTEL_FAILURE_CRYPTO;
  if (code == ERROR_NETWORK_PROTOCOL || code == ERROR_NETWORK_SIZE || code == ERROR_UNKNOWN_PACKET)
    return ASCII_OTEL_FAILURE_PROTOCOL;
  if ((code >= ERROR_SESSION_NOT_FOUND && code <= ERROR_SESSION_FULL) ||
      code == ERROR_ACDS_STRING_TAKEN || code == ERROR_ACDS_STRING_INVALID ||
      (origin && !strncmp(origin, "ascii-chat.discovery.", 21)))
    return ASCII_OTEL_FAILURE_DISCOVERY;
  return ASCII_OTEL_OPERATION_COUNT;
}
void ascii_otel_error_code(asciichat_error_t code, const char *message) {
  if (code == ASCIICHAT_OK || code == ERROR_SIGNAL_INTERRUPT || code == ERROR_NETWORK_INCOMPLETE) return;
  ascii_otel_error(message);
  if (!ascii_otel_enabled()) return;
  const char *origin = ascii_otel_current_operation();
  ascii_otel_operation_t operation = ascii_otel_failure_operation(code, origin);
  if (operation == ASCII_OTEL_OPERATION_COUNT) return;
  const char *category = operation == ASCII_OTEL_FAILURE_CRYPTO ? "crypto" :
                         operation == ASCII_OTEL_FAILURE_PROTOCOL ? "protocol" : "discovery";
  gotel_context_t parent = ascii_otel_context();
  if (!parent.sampled) parent = (gotel_context_t){0};
  ascii_otel_scope_t report __attribute__((cleanup(ascii_otel_scope_end))) = {0};
  ascii_otel_scope_parent(&report, operation, &parent);
  gotel_attr_string(&report.scope, "ascii_chat.failure.category", category);
  gotel_attr_string(&report.scope, "ascii_chat.failure.operation", origin);
  gotel_attr_int(&report.scope, "asciichat_errno.code", code);
  gotel_attr_string(&report.scope, "asciichat_errno.code_message", asciichat_error_string(code));
  gotel_error(&report.scope, category, message);
}
void ascii_otel_error(const char *message) {
  gotel_current_error("ascii_chat.error", message);
}
void ascii_otel_http(ascii_otel_scope_t *s, const char *hostname, const char *path) {
  if (!s->retained || !hostname || !path)
    return;
  gotel_error(&s->scope, "http.transport", "HTTP operation did not complete");
  char url[2048], name[1200];
  int n = safe_snprintf(url, sizeof(url), "https://%s%s", hostname, path);
  if (n >= 0 && (size_t)n < sizeof(url) &&
      otel_status(gotel_http_name(name, sizeof(name), "ascii-chat", false, "GET", url), "Name HTTP telemetry span") ==
          ASCIICHAT_OK) {
    if (s->scope.span)
      (void)otel_status(otlp_span_set_name(s->scope.span, name), "otlp_span_set_name");
    gotel_http_attributes(&s->scope, "GET", 0);
    gotel_attr_string(&s->scope, "server.address", hostname);
    gotel_attr_string(&s->scope, "url.scheme", "https");
  }
}
void ascii_otel_http_status(ascii_otel_scope_t *s, int status) {
  if (s->retained) {
    if (status > 0 && status < 400) {
      s->scope.failed = false;
      if (s->scope.span) {
        (void)otel_status(otlp_span_set_status(s->scope.span, OTLP_STATUS_CODE_UNSET, ""), "otlp_span_set_status");
        gotel_attr_string(&s->scope, "error.type", "");
      }
    }
    gotel_http_attributes(&s->scope, "GET", status);
  }
}
void ascii_otel_sqlite(sqlite3 *db, const gotel_query_t *queries, size_t count) {
  gotel_host_t *h = retain();
  if (!h)
    return;
  gotel_sqlite_t *a = NULL;
  if (otel_status(gotel_sqlite_attach(h, db, "ascii-chat.storage", queries, count, &a), "Attach SQLite telemetry") !=
      ASCIICHAT_OK)
    log_warn("Could not attach SQLite telemetry");
  release();
}
#endif
