/** @file otel.c Native telemetry lifecycle and ascii-chat instrumentation. */
#include <ascii-chat/otel.h>
#ifdef ASCIICHAT_WITH_OTEL
#include <ascii-chat/common.h>
#include <ascii-chat/version.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/log/log.h>
#include <stdatomic.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const gotel_operation_t operations[] = {
#define ASCII_OTEL_OPERATION(id, name, realtime) {name, realtime},
#include <ascii-chat/otel_operations.def>
#undef ASCII_OTEL_OPERATION
};
static _Atomic(gotel_host_t *) host;
static atomic_uint readers;
static atomic_bool stopping;
static asciichat_thread_t worker;
static bool worker_started;
static _Thread_local bool in_log;
static int minimum_log_level;
static gotel_scope_t command_scope;
static uint64_t shutdown_timeout_ns;

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
static void exporter_diagnostic(void *context, otlp_log_level_t level, const char *message) {
  (void)context;
  (void)level;
  bool previous = in_log;
  in_log = true;
  log_debug("OTLP exporter: %s", message);
  in_log = previous;
}
static void *export_worker(void *arg) {
  gotel_host_t *h = arg;
  while (!atomic_load_explicit(&stopping, memory_order_acquire)) {
    (void)gotel_tick(h);
    platform_sleep_ms(10);
  }
  return NULL;
}
void ascii_otel_scope_begin(ascii_otel_scope_t *s, ascii_otel_operation_t op) {
  ascii_otel_scope_parent(s, op, NULL);
}
void ascii_otel_scope_parent(ascii_otel_scope_t *s, ascii_otel_operation_t op, const gotel_context_t *parent) {
  gotel_host_t *h = retain();
  s->retained = h != NULL;
  if (h)
    gotel_begin(h, &s->scope, (size_t)op, parent);
}
void ascii_otel_scope_end(ascii_otel_scope_t *s) {
  if (!s || !s->retained)
    return;
  gotel_end(&s->scope);
  s->retained = false;
  release();
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
  otlp_status_t result = gotel_drain(h);
  otlp_exporter_stats_t stats = {0};
  (void)gotel_stats(h, &stats);
  // exit() can bypass scope cleanup in a producer. Do not free its host while
  // it remains borrowed; the process will reclaim it after atexit handlers.
  if (!producers_active)
    gotel_destroy(h);
  else
    log_warn("Telemetry producer still active during process shutdown; exporter retained");
  if (result != OTLP_OK)
    log_warn("Telemetry flush deadline reached; pending records discarded");
  log_debug("Telemetry exporter stopped: spans=%llu/%llu logs=%llu/%llu metrics=%llu/%llu network_errors=%llu "
            "http_4xx=%llu dropped=%llu rejected=%llu",
            (unsigned long long)stats.sent, (unsigned long long)stats.emitted, (unsigned long long)stats.sent_logs,
            (unsigned long long)stats.emitted_logs, (unsigned long long)stats.sent_metrics,
            (unsigned long long)stats.emitted_metrics, (unsigned long long)stats.network_err,
            (unsigned long long)stats.http_4xx, (unsigned long long)(stats.dropped_full + stats.dropped_err),
            (unsigned long long)stats.rejected_spans);
}
asciichat_error_t ascii_otel_init(const char *mode, int argc, char **argv) {
  const options_t *o = options_get();
  const char *endpoint = o->otel_endpoint;
  if (!*endpoint)
    endpoint = SAFE_GETENV("OTEL_EXPORTER_OTLP_ENDPOINT");
  if (!endpoint || !*endpoint)
    return ASCIICHAT_OK;
  unsigned signals;
  if (gotel_parse_signals(o->otel_enabled_telemetry, &signals) != OTLP_OK)
    return SET_ERRNO(ERROR_CONFIG, "otel-enabled-telemetry must contain traces,logs,metrics or none");
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
  if (strncmp(endpoint, "http://", 7) || strchr(endpoint, '?') || strchr(endpoint, '#') || strchr(endpoint, '@'))
    return SET_ERRNO(ERROR_CONFIG, "otel-endpoint requires an HTTP collector base URL without credentials or query");
  char traces[OPTIONS_BUFF_SIZE];
  size_t length = strlen(endpoint);
  while (length && endpoint[length - 1] == '/')
    --length;
  int n = safe_snprintf(traces, sizeof(traces), "%.*s/v1/traces", (int)length, endpoint);
  if (n < 0 || (size_t)n >= sizeof(traces))
    return SET_ERRNO(ERROR_CONFIG, "Telemetry endpoint too long");
  gotel_options_t config = {0};
  otlp_env_storage_t env;
  if (otlp_exporter_opts_apply_env(&config.exporter, &env) != OTLP_OK)
    return SET_ERRNO(ERROR_CONFIG, "Invalid OTEL exporter environment");
  config.exporter.endpoint = traces;
  config.exporter.service_name = "ascii-chat";
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
  gotel_host_t *h = NULL;
  if (gotel_create(&config, &h) != OTLP_OK)
    return SET_ERRNO(ERROR_CONFIG, "Cannot initialize telemetry exporter");
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
    otlp_value_t *args = SAFE_CALLOC((size_t)argc, sizeof(*args), otlp_value_t *);
    typedef char otel_arg_name_t[128];
    otel_arg_name_t *names = SAFE_CALLOC((size_t)argc, sizeof(*names), otel_arg_name_t *);
    if (args && names) {
      for (int i = 0; i < argc; ++i) {
        const char *value = "[REDACTED]";
        if (!i)
          value = "ascii-chat";
        else if (!strcmp(argv[i], mode))
          value = mode;
        else if (!strncmp(argv[i], "--", 2)) {
          size_t len = strcspn(argv[i], "=");
          if (len < sizeof(names[i])) {
            memcpy(names[i], argv[i], len);
            names[i][len] = 0;
            value = names[i];
          }
        }
        args[i].type = OTLP_VALUE_STRING;
        args[i].v.string_val = value;
      }
      otlp_span_set_attribute_array(command_scope.span, "process.command_args", args, (size_t)argc);
    }
    SAFE_FREE(args);
    SAFE_FREE(names);
  }

  log_info("Telemetry enabled: %s (%s)", endpoint, o->otel_enabled_telemetry);
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
      otlp_span_set_status(command_scope.span, OTLP_STATUS_CODE_UNSET, "");
  }
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
      gotel_http_name(name, sizeof(name), "ascii-chat", false, "GET", url) == OTLP_OK) {
    if (s->scope.span)
      otlp_span_set_name(s->scope.span, name);
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
        otlp_span_set_status(s->scope.span, OTLP_STATUS_CODE_UNSET, "");
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
  if (gotel_sqlite_attach(h, db, "ascii-chat.storage", queries, count, &a) != OTLP_OK)
    log_warn("Could not attach SQLite telemetry");
  release();
}
#endif
