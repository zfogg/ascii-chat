#include <criterion/criterion.h>
#include <gotel/testing.h>
#include <ascii-chat/otel.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
#include <math.h>
#include <string.h>

static const gotel_operation_t operations[] = {
    {"fixture.root", false}, {"fixture.hot", true}, {"fixture.child", false}};
static const double bounds[] = {.001, .01, 1};
static const gotel_metric_t metrics[] = {
    {"fixture.bytes", "By", OTLP_METRIC_COUNTER, NULL, 0},
    {"fixture.depth", "{item}", OTLP_METRIC_GAUGE, NULL, 0},
    {"fixture.duration", "s", OTLP_METRIC_HISTOGRAM, bounds, 3},
};
static const gotel_operation_t application_operations[] = {
#define ASCII_OTEL_OPERATION(id, name, realtime) {name, realtime},
#include <ascii-chat/otel_operations.def>
#undef ASCII_OTEL_OPERATION
};
static const gotel_metric_t application_metrics[] = {
#define ASCII_OTEL_METRIC(id, name, type, unit) {name, unit, OTLP_METRIC_##type, bounds, 3},
#include <ascii-chat/otel_metrics.def>
#undef ASCII_OTEL_METRIC
};
static gotel_host_t *host;
static gotel_test_events_t *events;
static bool adapter_options;
static gotel_options_t config(void) {
  gotel_options_t o = {0};
  o.exporter.service_name = "fixture";
  o.exporter.endpoint = "http://127.0.0.1:1/v1/traces";
  o.exporter.queue_capacity = 128;
  o.exporter.flush_timeout_ms = 100;
  o.exporter.backoff_initial_ms = 1;
  o.exporter.backoff_max_ms = 2;
  o.signals = GOTEL_ALL;
  o.realtime_sample_rate = 1;
  o.realtime_max_per_second = 2;
  o.metrics_interval_ms = 10;
  o.operations = operations;
  o.operation_count = 3;
  o.metrics = metrics;
  o.metric_count = 3;
  o.monotonic_ns = gotel_test_monotonic_ns;
  o.realtime_ns = gotel_test_realtime_ns;
  o.exporter_event = gotel_test_event_record;
  o.exporter_event_context = events;
  return o;
}
static void setup(void) {
  gotel_test_clock_set(UINT64_C(1000000000), UINT64_C(1700000000000000000));
  events = gotel_test_events_create();
  cr_assert_not_null(events);
}
static void cleanup(void) {
  if (host)
    gotel_destroy(host);
  host = NULL;
  ascii_otel_shutdown();
  if (adapter_options)
    options_state_destroy();
  adapter_options = false;
  gotel_test_events_destroy(events);
  events = NULL;
}
static void create(gotel_options_t o) {
  cr_assert_eq(gotel_test_create(&o, &host), OTLP_OK);
}
static otlp_exporter_stats_t stats(void) {
  otlp_exporter_stats_t result;
  cr_assert_eq(gotel_stats(host, &result), OTLP_OK);
  return result;
}
static void operation(size_t id) {
  gotel_scope_t scope;
  gotel_begin(host, &scope, id, NULL);
  gotel_end(&scope);
}
TestSuite(otel, .init = setup, .fini = cleanup, .timeout = 10);

Test(otel, all_signal_combinations) {
  for (unsigned mask = 0; mask <= GOTEL_ALL; ++mask) {
    gotel_options_t o = config();
    o.signals = mask;
    create(o);
    operation(0);
    gotel_log(host, OTLP_SEVERITY_INFO, "message", "fixture.c", 1, "fixture");
    cr_assert_eq(gotel_metric_record(host, 0, 3), OTLP_OK);
    cr_assert_eq(gotel_drain(host), OTLP_OK);
    otlp_exporter_stats_t s = stats();
    cr_assert_eq(s.sent, !!(mask & GOTEL_TRACES));
    cr_assert_eq(s.sent_logs, !!(mask & GOTEL_LOGS));
    cr_assert_eq(s.sent_metrics, mask & GOTEL_METRICS ? 5 : 0);
    gotel_destroy(host);
    host = NULL;
  }
}
Test(otel, scope_end_is_idempotent) {
  create(config());
  gotel_scope_t scope;
  gotel_begin(host, &scope, 0, NULL);
  gotel_end(&scope);
  gotel_end(&scope);
  cr_assert_eq(stats().emitted, 1);
  cr_assert_not(gotel_current_context().valid);
}
Test(otel, invalid_operation_preserves_context) {
  create(config());
  gotel_scope_t root, invalid;
  gotel_begin(host, &root, 0, NULL);
  gotel_context_t before = gotel_current_context();
  gotel_begin(host, &invalid, 999, NULL);
  gotel_end(&invalid);
  cr_assert_not(invalid.active);
  cr_assert_arr_eq(before.span_id, gotel_current_context().span_id, 8);
  gotel_end(&root);
}
Test(otel, nested_context_restored_and_nearest_error) {
  create(config());
  gotel_scope_t root, child;
  gotel_begin(host, &root, 0, NULL);
  gotel_context_t parent = gotel_current_context();
  gotel_begin(host, &child, 2, NULL);
  cr_assert_arr_eq(parent.trace_id, gotel_current_context().trace_id, 16);
  gotel_current_error("fixture.error", "failed");
  cr_assert(child.failed);
  cr_assert_not(root.failed);
  gotel_end(&child);
  cr_assert_arr_eq(parent.span_id, gotel_current_context().span_id, 8);
  gotel_end(&root);
  cr_assert_not(gotel_current_context().valid);
}
Test(otel, explicit_context_can_be_attached_by_value) {
  create(config());
  gotel_context_t parent = {.valid = true, .sampled = true, .realtime = true};
  memset(parent.trace_id, 11, sizeof(parent.trace_id));
  memset(parent.span_id, 22, sizeof(parent.span_id));
  gotel_scope_t child;
  gotel_begin(host, &child, 1, &parent);
  cr_assert(child.context.sampled);
  cr_assert_arr_eq(child.context.trace_id, parent.trace_id, 16);
  cr_assert_neq(memcmp(child.context.span_id, parent.span_id, 8), 0);
  gotel_end(&child);
}
Test(otel, unsampled_parent_cannot_be_resampled) {
  create(config());
  gotel_context_t parent = {.valid = true, .sampled = false, .realtime = true};
  gotel_scope_t child;
  gotel_begin(host, &child, 1, &parent);
  cr_assert_not(child.context.sampled);
  cr_assert_null(child.span);
  gotel_end(&child);
  cr_assert_eq(gotel_drain(host), OTLP_OK);
  cr_assert_eq(stats().sent, 0);
  cr_assert_eq(stats().sent_metrics, 4);
}
Test(otel, realtime_sampling_zero_retains_metrics) {
  gotel_options_t o = config();
  o.realtime_sample_rate = 0;
  create(o);
  for (int i = 0; i < 20; ++i)
    operation(1);
  cr_assert_eq(stats().emitted, 0);
  cr_assert_eq(gotel_drain(host), OTLP_OK);
  cr_assert_eq(stats().sent_metrics, 4);
}
Test(otel, root_rate_limit_resets_at_second_boundary) {
  create(config());
  operation(1);
  operation(1);
  operation(1);
  cr_assert_eq(stats().emitted, 2);
  cr_assert_eq(gotel_test_clock_advance(UINT64_C(1000000000)), OTLP_OK);
  operation(1);
  cr_assert_eq(stats().emitted, 3);
}
Test(otel, sampled_descendants_do_not_consume_root_budget) {
  create(config());
  gotel_scope_t root, child;
  gotel_begin(host, &root, 1, NULL);
  for (int i = 0; i < 10; ++i) {
    gotel_begin(host, &child, 1, NULL);
    cr_assert(child.context.sampled);
    gotel_end(&child);
  }
  gotel_end(&root);
  operation(1);
  operation(1);
  cr_assert_eq(stats().emitted, 12);
}
Test(otel, metric_validation_and_unused_instruments) {
  create(config());
  cr_assert_eq(gotel_metric_record(host, 0, -1), OTLP_ERR_INVALID_ARGUMENT);
  cr_assert_eq(gotel_metric_record(host, 1, NAN), OTLP_ERR_INVALID_ARGUMENT);
  cr_assert_eq(gotel_metric_record(host, 2, INFINITY), OTLP_ERR_INVALID_ARGUMENT);
  cr_assert_eq(gotel_metric_record(host, 3, 1), OTLP_ERR_INVALID_ARGUMENT);
  cr_assert_eq(gotel_metric_record(host, 1, -4), OTLP_OK);
  cr_assert_eq(gotel_drain(host), OTLP_OK);
  cr_assert_eq(stats().sent_metrics, 1);
}
Test(otel, zero_observations_are_exported) {
  create(config());
  cr_assert_eq(gotel_metric_record(host, 0, 0), OTLP_OK);
  cr_assert_eq(gotel_metric_record(host, 1, 0), OTLP_OK);
  cr_assert_eq(gotel_metric_record(host, 2, 0), OTLP_OK);
  cr_assert_eq(gotel_drain(host), OTLP_OK);
  cr_assert_eq(stats().sent_metrics, 3);
}
Test(otel, metric_interval_and_final_flush) {
  create(config());
  cr_assert_eq(gotel_metric_record(host, 0, 1), OTLP_OK);
  cr_assert_eq(gotel_tick(host), OTLP_OK);
  cr_assert_eq(stats().emitted_metrics, 0);
  cr_assert_eq(gotel_test_clock_advance(10000000), OTLP_OK);
  cr_assert_eq(gotel_tick(host), OTLP_OK);
  cr_assert_eq(stats().sent_metrics, 1);
  cr_assert_eq(gotel_metric_record(host, 1, 7), OTLP_OK);
  cr_assert_eq(gotel_drain(host), OTLP_OK);
  cr_assert_eq(stats().sent_metrics, 3);
}
Test(otel, retries_recover_without_losing_records) {
  create(config());
  const int responses[] = {503, 429, 200};
  gotel_test_script_t script = {responses, 3, 0, 200};
  cr_assert_eq(gotel_test_transport(host, &script), OTLP_OK);
  operation(0);
  cr_assert_eq(gotel_drain(host), OTLP_OK);
  cr_assert_eq(stats().sent, 1);
  cr_assert_eq(gotel_test_events_snapshot(events).retries, 2);
  cr_assert_eq(stats().dropped_err, 0);
}
Test(otel, permanent_http_failure_drops_without_retry) {
  gotel_options_t o = config();
  o.signals = GOTEL_TRACES;
  create(o);
  gotel_test_script_t script = {.fallback_status = 403};
  cr_assert_eq(gotel_test_transport(host, &script), OTLP_OK);
  operation(0);
  (void)gotel_drain(host);
  cr_assert_eq(stats().sent, 0);
  cr_assert_eq(stats().dropped_err, 1);
  cr_assert_eq(gotel_test_events_snapshot(events).retries, 0);
  cr_assert_eq(gotel_test_events_snapshot(events).dropped, 1);
}
Test(otel, bounded_queue_reports_loss) {
  gotel_options_t o = config();
  o.signals = GOTEL_TRACES;
  o.exporter.queue_capacity = 2;
  create(o);
  for (int i = 0; i < 20; ++i)
    operation(0);
  otlp_exporter_stats_t s = stats();
  cr_assert_gt(s.dropped_full, 0);
  cr_assert_eq(s.emitted + s.dropped_full, 20);
  cr_assert_eq(gotel_test_events_snapshot(events).queue_full, s.dropped_full);
  cr_assert_eq(gotel_drain(host), OTLP_OK);
  cr_assert_eq(stats().sent, s.emitted);
}
Test(otel, invalid_catalogs_are_rejected) {
  gotel_options_t o = config();
  gotel_metric_t duplicate[] = {metrics[0], metrics[0]};
  o.metrics = duplicate;
  o.metric_count = 2;
  cr_assert_eq(gotel_test_create(&o, &host), OTLP_ERR_INVALID_ARGUMENT);
  cr_assert_null(host);
  double reversed[] = {1, .1};
  gotel_metric_t bad = {"bad", "s", OTLP_METRIC_HISTOGRAM, reversed, 2};
  o.metrics = &bad;
  o.metric_count = 1;
  cr_assert_eq(gotel_test_create(&o, &host), OTLP_ERR_INVALID_ARGUMENT);
  o = config();
  o.realtime_sample_rate = NAN;
  cr_assert_eq(gotel_test_create(&o, &host), OTLP_ERR_INVALID_ARGUMENT);
}
Test(otel, signal_parser_rejects_ambiguous_lists) {
  unsigned mask = 0;
  cr_assert_eq(gotel_parse_signals("traces,logs,metrics", &mask), OTLP_OK);
  cr_assert_eq(mask, GOTEL_ALL);
  const char *bad[] = {"", "traces,", ",logs", "logs,logs", "traces, metrics", "ALL"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); ++i)
    cr_assert_neq(gotel_parse_signals(bad[i], &mask), OTLP_OK);
}
Test(otel, http_names_redact_secrets_and_bound_output) {
  char name[256];
  cr_assert_eq(gotel_http_name(name, sizeof(name), "fixture", false, "GET",
                               "https://user:password@host/a/123?q=secret#fragment"),
               OTLP_OK);
  cr_assert_str_eq(name, "fixture.http.client.GET host/a/{id}");
  cr_assert_eq(gotel_http_name(name, 4, "fixture", false, "GET", "https://host/a"), OTLP_ERR_OVERFLOW);
  cr_assert_eq(name[0], 0);
}
Test(otel, sqlite_lifecycle_unknown_and_detach) {
  create(config());
  sqlite3 *db = NULL;
  cr_assert_eq(sqlite3_open(":memory:", &db), SQLITE_OK);
  const gotel_query_t queries[] = {{"BoundSelect", "SELECT ? UNION ALL SELECT ?"}};
  gotel_sqlite_t *attachment = NULL;
  cr_assert_eq(gotel_sqlite_attach(host, db, "fixture.storage", queries, 1, &attachment), OTLP_OK);
  sqlite3_stmt *statement = NULL;
  cr_assert_eq(sqlite3_prepare_v2(db, queries[0].sql, -1, &statement, NULL), SQLITE_OK);
  cr_assert_eq(sqlite3_bind_text(statement, 1, "private", -1, SQLITE_STATIC), SQLITE_OK);
  cr_assert_eq(sqlite3_step(statement), SQLITE_ROW);
  cr_assert_eq(sqlite3_reset(statement), SQLITE_OK);
  cr_assert_eq(sqlite3_step(statement), SQLITE_ROW);
  cr_assert_eq(sqlite3_finalize(statement), SQLITE_OK);
  cr_assert_eq(sqlite3_exec(db, "SELECT 99", NULL, NULL, NULL), SQLITE_OK);
  cr_assert_eq(stats().emitted, 3);
  gotel_sqlite_detach(attachment);
  cr_assert_eq(sqlite3_exec(db, "SELECT 100", NULL, NULL, NULL), SQLITE_OK);
  cr_assert_eq(stats().emitted, 3);
  cr_assert_eq(sqlite3_close(db), SQLITE_OK);
  cr_assert_eq(gotel_drain(host), OTLP_OK);
  cr_assert_eq(stats().sent_metrics, 3);
}
Test(otel, sqlite_close_frees_attachment_and_duplicate_sql_rejected) {
  create(config());
  sqlite3 *db = NULL;
  cr_assert_eq(sqlite3_open(":memory:", &db), SQLITE_OK);
  gotel_query_t queries[] = {{"One", "SELECT 1"}, {"Two", "SELECT 1"}};
  gotel_sqlite_t *attachment = NULL;
  cr_assert_eq(gotel_sqlite_attach(host, db, "fixture", queries, 2, &attachment), OTLP_ERR_INVALID_ARGUMENT);
  cr_assert_null(attachment);
  cr_assert_eq(gotel_sqlite_attach(host, db, "fixture", queries, 1, &attachment), OTLP_OK);
  cr_assert_eq(sqlite3_close(db), SQLITE_OK);
}
Test(otel, sqlite_unsampled_parent_keeps_duration_metrics) {
  create(config());
  sqlite3 *db = NULL;
  cr_assert_eq(sqlite3_open(":memory:", &db), SQLITE_OK);
  gotel_sqlite_t *attachment = NULL;
  cr_assert_eq(gotel_sqlite_attach(host, db, "fixture", NULL, 0, &attachment), OTLP_OK);
  gotel_context_t parent = {.valid = true, .sampled = false, .realtime = true};
  gotel_scope_t scope;
  gotel_begin(host, &scope, 1, &parent);
  cr_assert_eq(sqlite3_exec(db, "SELECT 1", NULL, NULL, NULL), SQLITE_OK);
  gotel_end(&scope);
  cr_assert_eq(sqlite3_close(db), SQLITE_OK);
  cr_assert_eq(gotel_drain(host), OTLP_OK);
  cr_assert_eq(stats().sent, 0);
  cr_assert_eq(stats().sent_metrics, 5);
}
Test(otel, disabled_ascii_adapter_is_safe) {
  ascii_otel_scope_t scope = {0};
  ascii_otel_scope_begin(&scope, ASCII_OTEL_PIPELINE_CAPTURE);
  cr_assert_not(scope.retained);
  ascii_otel_scope_int(&scope, "key", 1);
  ascii_otel_scope_end(&scope);
  ascii_otel_metric(ASCII_OTEL_METRIC_FRAME_CAPTURED, 1);
  ascii_otel_error("ignored");
  cr_assert_not(ascii_otel_enabled());
}
static void adapter_start(const char *signals) {
  log_init(NULL, LOG_ERROR, false, false);
  options_t o = options_t_new();
  SAFE_STRNCPY(o.otel_endpoint, "http://127.0.0.1:1", sizeof(o.otel_endpoint));
  SAFE_STRNCPY(o.otel_enabled_telemetry, signals, sizeof(o.otel_enabled_telemetry));
  o.otel_timeout = 10;
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  adapter_options = true;
  cr_assert_eq(options_state_set(&o), ASCIICHAT_OK);
  char *args[] = {"fixture"};
  cr_assert_eq(ascii_otel_init("mirror", 1, args), ASCIICHAT_OK);
}
Test(otel, ascii_none_does_not_start_worker) {
  adapter_start("none");
  cr_assert_not(ascii_otel_enabled());
}
Test(otel, test_mode_cannot_start_adapter_with_explicit_collector) {
  adapter_start("traces,logs,metrics");
  cr_assert_not(ascii_otel_enabled());
  cr_assert_not(ascii_otel_context().valid);
  ascii_otel_scope_t scope = {0};
  ascii_otel_scope_begin(&scope, ASCII_OTEL_DATABASE_INIT);
  cr_assert_not(scope.retained);
  ascii_otel_scope_end(&scope);
}

Test(otel, bounded_children_keep_unsampled_parent_decision) {
  adapter_start("traces");
  gotel_context_t unsampled = {.valid = true, .sampled = false, .realtime = true};
  ascii_otel_scope_t operation_scope = {0}, child = {0};
  ascii_otel_scope_parent(&operation_scope, ASCII_OTEL_NETWORK_PACKET_DISPATCH, &unsampled);
  ascii_otel_scope_begin(&child, ASCII_OTEL_FFMPEG_VIDEO_DECODE);
  cr_assert_not(operation_scope.scope.context.sampled);
  cr_assert_not(child.scope.context.sampled);
  ascii_otel_scope_end(&child);
  ascii_otel_scope_end(&operation_scope);
}
Test(otel, ascii_http_in_test_mode_is_a_safe_noop) {
  adapter_start("traces,metrics");
  ascii_otel_scope_t scope = {0};
  ascii_otel_scope_begin(&scope, ASCII_OTEL_HTTP);
  cr_assert_not(scope.retained);
  ascii_otel_http(&scope, "example.invalid", "/items/123?q=secret");
  cr_assert_not(scope.scope.failed);
  ascii_otel_http_status(&scope, 200);
  cr_assert_not(scope.scope.failed);
  ascii_otel_scope_end(&scope);
  ascii_otel_exit(0);
  ascii_otel_shutdown();
  cr_assert_not(ascii_otel_enabled());
}

Test(otel, stats_share_canonical_metrics_without_mirror_series) {
  static const char *names[] = {
#define ASCII_OTEL_METRIC(id, name, type, unit) name,
#include <ascii-chat/otel_metrics.def>
#undef ASCII_OTEL_METRIC
  };
  for (unsigned kind = STATS_RECORD_COUNTER; kind <= STATS_RECORD_DURATION; ++kind) {
    unsigned count = kind == STATS_RECORD_COUNTER ? STATS_COUNTER_COUNT
                     : kind == STATS_RECORD_GAUGE ? STATS_GAUGE_COUNT
                                                  : STATS_DURATION_COUNT;
    for (unsigned id = 0; id < count; ++id) {
      ascii_otel_metric_t metric = ascii_otel_stats_metric(kind, id);
      cr_assert_lt(metric, ASCII_OTEL_METRIC_COUNT);
      cr_assert(strncmp(names[metric], "ascii-chat.stats.", 17) != 0);
    }
  }
  cr_assert_eq(ascii_otel_stats_metric(STATS_RECORD_COUNTER, STATS_COUNTER_FRAMES_CAPTURED),
               ASCII_OTEL_METRIC_FRAME_CAPTURED);
  cr_assert_eq(ascii_otel_stats_metric(STATS_RECORD_COUNTER, STATS_COUNTER_RECONNECTS), ASCII_OTEL_METRIC_RECONNECT);
  cr_assert_eq(ascii_otel_stats_metric(STATS_RECORD_COUNTER, STATS_COUNTER_BYTES_SENT), ASCII_OTEL_METRIC_NET_TX);
  cr_assert_eq(ascii_otel_stats_metric(STATS_RECORD_COUNTER, STATS_COUNTER_BYTES_RECEIVED), ASCII_OTEL_METRIC_NET_RX);
  cr_assert_eq(ascii_otel_stats_metric(STATS_RECORD_GAUGE, STATS_GAUGE_SESSIONS_ACTIVE), ASCII_OTEL_METRIC_SESSIONS);
  cr_assert_eq(ascii_otel_stats_metric(STATS_RECORD_DURATION, STATS_DURATION_RTT), ASCII_OTEL_METRIC_RTT);
  cr_assert_eq(ascii_otel_stats_metric(STATS_RECORD_COUNTER, STATS_COUNTER_COUNT), ASCII_OTEL_METRIC_COUNT);
  cr_assert_eq(ascii_otel_stats_metric(STATS_RECORD_COUNTER, 65536), ASCII_OTEL_METRIC_COUNT);
}

Test(otel, complete_application_catalog_exports_without_loss) {
  gotel_options_t o = config();
  o.operations = application_operations;
  o.operation_count = ASCII_OTEL_OPERATION_COUNT;
  o.metrics = application_metrics;
  o.metric_count = ASCII_OTEL_METRIC_COUNT;
  o.realtime_max_per_second = 0;
  o.exporter.queue_capacity = 1024;
  create(o);
  for (size_t i = 0; i < o.operation_count; ++i)
    operation(i);
  for (size_t i = 0; i < o.metric_count; ++i)
    cr_assert_eq(gotel_metric_record(host, i, 1), OTLP_OK);
  cr_assert_eq(gotel_drain(host), OTLP_OK);
  otlp_exporter_stats_t s = stats();
  cr_assert_eq(s.sent, o.operation_count);
  cr_assert_eq(s.sent_metrics, 4 * o.operation_count + o.metric_count);
  cr_assert_eq(s.dropped_full + s.dropped_err + s.dropped_metrics_full + s.dropped_metrics_err, 0);
}

Test(otel, exhausted_retry_budget_reports_permanent_loss) {
  gotel_options_t o = config();
  o.signals = GOTEL_TRACES;
  o.exporter.max_retries = 1;
  create(o);
  gotel_test_script_t script = {.fallback_status = 503};
  cr_assert_eq(gotel_test_transport(host, &script), OTLP_OK);
  operation(0);
  (void)gotel_drain(host);
  otlp_exporter_stats_t s = stats();
  cr_assert_eq(s.sent, 0);
  cr_assert_eq(s.dropped_err, 1);
  cr_assert_eq(s.http_5xx, 2);
  cr_assert_eq(gotel_test_events_snapshot(events).retries, 1);
}
Test(otel, null_inputs_are_rejected_or_noops) {
  cr_assert_eq(gotel_tick(NULL), OTLP_ERR_NULL);
  cr_assert_eq(gotel_drain(NULL), OTLP_ERR_NULL);
  cr_assert_eq(gotel_stats(NULL, NULL), OTLP_ERR_NULL);
  gotel_end(NULL);
  gotel_destroy(NULL);
  gotel_error(NULL, "error", "message");
  gotel_set_logger(NULL, NULL, NULL);
  cr_assert_eq(gotel_metric_record(NULL, 0, 1), OTLP_ERR_INVALID_ARGUMENT);
  cr_assert_eq(gotel_test_create(NULL, &host), OTLP_ERR_INVALID_ARGUMENT);
  cr_assert_null(host);
  cr_assert_eq(gotel_test_create(NULL, NULL), OTLP_ERR_NULL);
}
Test(otel, http_error_status_marks_scope_failed) {
  create(config());
  gotel_scope_t scope;
  gotel_begin(host, &scope, 0, NULL);
  gotel_http_attributes(&scope, "GET", 503);
  cr_assert(scope.failed);
  gotel_end(&scope);
  cr_assert_eq(gotel_drain(host), OTLP_OK);
  cr_assert_eq(stats().sent, 1);
}

Test(otel, classifies_actionable_failures_without_cancellation_or_incomplete_reads) {
  cr_assert_eq(ascii_otel_failure_operation(ERROR_CRYPTO_AUTH, ""), ASCII_OTEL_FAILURE_CRYPTO);
  cr_assert_eq(ascii_otel_failure_operation(ERROR_INVALID_SIGNATURE, ""), ASCII_OTEL_FAILURE_CRYPTO);
  cr_assert_eq(ascii_otel_failure_operation(ERROR_NETWORK_PROTOCOL, ""), ASCII_OTEL_FAILURE_PROTOCOL);
  cr_assert_eq(ascii_otel_failure_operation(ERROR_UNKNOWN_PACKET, ""), ASCII_OTEL_FAILURE_PROTOCOL);
  cr_assert_eq(ascii_otel_failure_operation(ERROR_SESSION_NOT_FOUND, ""), ASCII_OTEL_FAILURE_DISCOVERY);
  cr_assert_eq(ascii_otel_failure_operation(ERROR_NETWORK_CONNECT, "ascii-chat.discovery.connect"),
               ASCII_OTEL_FAILURE_DISCOVERY);
  cr_assert_eq(ascii_otel_failure_operation(ERROR_SIGNAL_INTERRUPT, "ascii-chat.discovery.connect"),
               ASCII_OTEL_OPERATION_COUNT);
  cr_assert_eq(ascii_otel_failure_operation(ERROR_NETWORK_INCOMPLETE, "ascii-chat.discovery.connect"),
               ASCII_OTEL_OPERATION_COUNT);
  cr_assert_eq(ascii_otel_failure_operation(ERROR_NETWORK, "ascii-chat.network.receive"), ASCII_OTEL_OPERATION_COUNT);
}
Test(otel, pipeline_stall_requires_recent_capture_and_an_expected_stage) {
  const uint64_t now = UINT64_C(20000000000), capture = UINT64_C(19500000000), progress = UINT64_C(10000000000);
  cr_assert_eq(ascii_otel_pipeline_age(now, capture, progress, true), 10);
  cr_assert_eq(ascii_otel_pipeline_age(now, capture, progress, false), 0);
  cr_assert_eq(ascii_otel_pipeline_age(now, UINT64_C(17000000000), progress, true), 0);
  cr_assert_eq(ascii_otel_pipeline_age(now, 0, progress, true), 0);
  cr_assert_eq(ascii_otel_pipeline_age(now, capture, 0, true), 0);
  cr_assert_eq(ascii_otel_pipeline_age(now, capture, now, true), 0);
}

Test(otel, mcp_render_context_survives_request_completion) {
  gotel_options_t o = config();
  o.operations = application_operations;
  o.operation_count = ASCII_OTEL_OPERATION_COUNT;
  o.metrics = application_metrics;
  o.metric_count = ASCII_OTEL_METRIC_COUNT;
  o.exporter.queue_capacity = 1024;
  create(o);
  gotel_scope_t request, render;
  gotel_begin(host, &request, ASCII_OTEL_MCP_REQUEST, NULL);
  gotel_context_t parent = request.context;
  gotel_end(&request);
  gotel_begin(host, &render, ASCII_OTEL_MCP_RENDER, &parent);
  cr_assert_arr_eq(render.context.trace_id, parent.trace_id, 16);
  cr_assert_arr_eq(render.parent_span_id, parent.span_id, 8);
  gotel_attr_string(&render, "mcp.tool", "render_file");
  cr_assert_eq(gotel_metric_record(host, ASCII_OTEL_METRIC_MCP_RENDERS, 1), OTLP_OK);
  cr_assert_eq(gotel_metric_record(host, ASCII_OTEL_METRIC_MCP_RENDER_DURATION, .25), OTLP_OK);
  gotel_end(&render);
  cr_assert_eq(gotel_drain(host), OTLP_OK);
  cr_assert_eq(stats().sent, 2);
}
