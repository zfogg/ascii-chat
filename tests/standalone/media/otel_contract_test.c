/** Exercise the real application adapter with an in-process Gotel host.
 * Including the implementation gives this fixture access to its private host;
 * normal TESTING initialization stays disabled and no worker or socket starts. */
#include "../../../lib/otel.c"
#include <gotel/testing.h>
#include <ascii-chat/discovery/database.h>
#include <ascii-chat/network/rate_limit/rate_limit.h>
#include "../../../lib/discovery/queries.h"

#ifndef CHECK
#define CHECK(value)                                                                                                   \
  do {                                                                                                                 \
    if (!(value))                                                                                                      \
      FATAL(ERROR_ASSERTION_FAILED, "%s", #value);                                                                     \
  } while (0)
#endif

static gotel_test_capture_t *capture;

/* Fixture storage in exporter records and retained protobuf bytes. */
enum {
  OTEL_CONTRACT_EXPORT_QUEUE_CAPACITY = 1024,
  OTEL_CONTRACT_CAPTURE_MAX_BYTES = 8 * 1024 * 1024,
};

static gotel_host_t *start(double rate) {
  gotel_options_t options = {0};
  options.exporter.service_name = "ascii-chat-contract";
  options.exporter.endpoint = "http://127.0.0.1:1";
  options.exporter.queue_capacity = OTEL_CONTRACT_EXPORT_QUEUE_CAPACITY;
  options.signals = GOTEL_TRACES | GOTEL_METRICS;
  options.operations = operations;
  options.operation_count = ASCII_OTEL_OPERATION_COUNT;
  options.metrics = metrics;
  options.metric_count = ASCII_OTEL_METRIC_COUNT;
  options.realtime_sample_rate = rate;
  options.realtime_max_per_second = 100;
  options.monotonic_ns = gotel_test_monotonic_ns;
  options.realtime_ns = gotel_test_realtime_ns;
  gotel_test_clock_set(UINT64_C(1000000000), UINT64_C(1700000000000000000));
  gotel_host_t *result = NULL;
  CHECK(gotel_test_create(&options, &result) == OTLP_OK);
  if (capture)
    CHECK(gotel_test_capture_attach(result, capture) == OTLP_OK);
  atomic_store(&host, result);
  return result;
}

static void finish(gotel_host_t *h) {
  CHECK(current_scope == NULL);
  CHECK(atomic_load(&readers) == 0);
  CHECK(gotel_drain(h) == OTLP_OK);
  atomic_store(&host, NULL);
  gotel_destroy(h);
}

static void bounded_sampling(void) {
  gotel_host_t *h = start(0);
  gotel_begin(h, &command_scope, ASCII_OTEL_CLI, NULL);
  ascii_otel_scope_t operation = {0}, child = {0};
  ascii_otel_scope_begin(&operation, ASCII_OTEL_HTTP);
  CHECK(operation.scope.context.sampled);
  CHECK(memcmp(operation.scope.context.trace_id, command_scope.context.trace_id, 16) != 0);
  ascii_otel_scope_begin(&child, ASCII_OTEL_FFMPEG_VIDEO_DECODE);
  CHECK(child.scope.context.sampled);
  CHECK(memcmp(child.scope.context.trace_id, operation.scope.context.trace_id, 16) == 0);
  CHECK(memcmp(child.scope.parent_span_id, operation.scope.context.span_id, 8) == 0);
  ascii_otel_scope_end(&child);
  ascii_otel_scope_end(&operation);

  ascii_otel_scope_begin(&operation, ASCII_OTEL_SESSION_ATTEMPT);
  ascii_otel_scope_begin(&child, ASCII_OTEL_FFMPEG_VIDEO_DECODE);
  CHECK(!child.scope.context.sampled);
  CHECK(child.scope.span == NULL);
  ascii_otel_scope_end(&child);
  ascii_otel_scope_end(&operation);

  gotel_context_t transferred = {.valid = true, .sampled = true, .realtime = true};
  memset(transferred.trace_id, 11, sizeof(transferred.trace_id));
  memset(transferred.span_id, 22, sizeof(transferred.span_id));
  ascii_otel_scope_parent(&child, ASCII_OTEL_H265_ENCODE, &transferred);
  CHECK(child.scope.context.sampled);
  CHECK(memcmp(child.scope.context.trace_id, transferred.trace_id, 16) == 0);
  CHECK(memcmp(child.scope.parent_span_id, transferred.span_id, 8) == 0);
  ascii_otel_scope_end(&child);
  CHECK(memcmp(gotel_current_context().span_id, command_scope.context.span_id, 8) == 0);
  gotel_end(&command_scope);
  finish(h);
}

static void dropped_errors(void) {
  gotel_host_t *h = start(0);
  ascii_otel_scope_t operation = {0}, child = {0};
  ascii_otel_scope_begin(&operation, ASCII_OTEL_NETWORK_PACKET_DISPATCH);
  CHECK(!operation.scope.context.sampled);
  ascii_otel_scope_begin(&child, ASCII_OTEL_FFMPEG_VIDEO_DECODE);
  CHECK(!child.scope.context.sampled);
  ascii_otel_scope_end(&child);
  ascii_otel_error_code(ERROR_NETWORK_INCOMPLETE, "incomplete fixture");
  ascii_otel_error_code(ERROR_SIGNAL_INTERRUPT, "cancelled fixture");
  ascii_otel_error_code(ERROR_CRYPTO_AUTH, "authentication fixture");
  ascii_otel_frame_rejected("fixture", "invalid dimensions", 12, 0, 0, ERROR_NETWORK_PROTOCOL);
  CHECK(!operation.scope.context.sampled);
  CHECK(operation.scope.span == NULL);
  ascii_otel_scope_end(&operation);
  otlp_exporter_stats_t stats = {0};
  CHECK(gotel_stats(h, &stats) == OTLP_OK);
  CHECK(stats.emitted == 2);
  finish(h);
}

static void sqlite_prepare_contract(void) {
  gotel_host_t *h = start(1);
  sqlite3 *db = NULL;
  CHECK(sqlite3_open(":memory:", &db) == SQLITE_OK);
  const gotel_query_t queries[] = {{"BoundValue", "SELECT ?"},
                                   {"Malformed", "SELECT 'fixture-secret"},
                                   {"LiteralValue", "SELECT 17, 'fixture-literal-secret'"}};
  ascii_otel_sqlite(db, queries, 3);
  sqlite3_stmt *statement = NULL;
  CHECK(ascii_otel_sqlite_prepare(db, queries[0].sql, &statement) == SQLITE_OK);
  otlp_exporter_stats_t stats = {0};
  CHECK(gotel_stats(h, &stats) == OTLP_OK && stats.emitted == 0);
  CHECK(sqlite3_bind_text(statement, 1, "fixture-bound-secret", -1, SQLITE_STATIC) == SQLITE_OK);
  CHECK(sqlite3_step(statement) == SQLITE_ROW);
  CHECK(strcmp((const char *)sqlite3_column_text(statement, 0), "fixture-bound-secret") == 0);
  CHECK(sqlite3_finalize(statement) == SQLITE_OK);
  CHECK(gotel_stats(h, &stats) == OTLP_OK && stats.emitted == 1);
  CHECK(ascii_otel_sqlite_prepare(db, queries[2].sql, &statement) == SQLITE_OK);
  CHECK(sqlite3_step(statement) == SQLITE_ROW);
  CHECK(sqlite3_column_int(statement, 0) == 17);
  CHECK(strcmp((const char *)sqlite3_column_text(statement, 1), "fixture-literal-secret") == 0);
  CHECK(sqlite3_finalize(statement) == SQLITE_OK);
  statement = NULL;
  CHECK(ascii_otel_sqlite_prepare(db, queries[1].sql, &statement) == SQLITE_ERROR);
  CHECK(statement == NULL && sqlite3_errcode(db) == SQLITE_ERROR);
  CHECK(gotel_stats(h, &stats) == OTLP_OK && stats.emitted == 3);
  CHECK(sqlite3_close(db) == SQLITE_OK);
  finish(h);
}

static void retained_budget(void) {
  gotel_host_t *h = start(0);
  ascii_otel_scope_t operation = {0};
  ascii_otel_scope_begin(&operation, ASCII_OTEL_NETWORK_PACKET_DISPATCH);
  for (unsigned i = 0; i < 105; ++i)
    ascii_otel_error_code(ERROR_NETWORK_PROTOCOL, "protocol fixture");
  otlp_exporter_stats_t stats = {0};
  CHECK(gotel_stats(h, &stats) == OTLP_OK && stats.emitted == 100);
  CHECK(!operation.scope.context.sampled && operation.scope.span == NULL);
  CHECK(gotel_test_clock_advance(NS_PER_SEC_INT) == OTLP_OK);
  ascii_otel_error_code(ERROR_NETWORK_PROTOCOL, "protocol fixture");
  CHECK(gotel_stats(h, &stats) == OTLP_OK && stats.emitted == 101);
  ascii_otel_scope_end(&operation);
  finish(h);
}

static void catalog_contract(void) {
  gotel_host_t *h = start(1);
  sqlite3 *db = NULL;
  CHECK(database_init(":memory:", &db) == ASCIICHAT_OK);
  CHECK(db != NULL);
  // Opening the real database checks schema/migration names and attachment
  // uniqueness. Every prepared application query must compile in that schema.
  for (size_t i = 0; i < sizeof(database_queries) / sizeof(database_queries[0]); ++i) {
    if (i == QUERY_ImportLegacySessionKeys)
      continue; // The legacy table intentionally does not exist in a new DB.
    sqlite3_stmt *statement = NULL;
    CHECK(ascii_otel_sqlite_prepare(db, database_queries[i].sql, &statement) == SQLITE_OK);
    CHECK(statement != NULL);
    CHECK(sqlite3_finalize(statement) == SQLITE_OK);
  }
  rate_limiter_t *limiter = rate_limiter_create_sqlite(NULL);
  CHECK(limiter != NULL);
  rate_limiter_set_sqlite_db(limiter, db);
  rate_limit_config_t limit = {.max_events = 1, .window_secs = 60};
  bool allowed = false;
  CHECK(rate_limiter_check(limiter, "192.0.2.44", RATE_EVENT_SESSION_CREATE, &limit, &allowed) == ASCIICHAT_OK);
  CHECK(allowed);
  CHECK(rate_limiter_record(limiter, "192.0.2.44", RATE_EVENT_SESSION_CREATE) == ASCIICHAT_OK);
  CHECK(rate_limiter_check(limiter, "192.0.2.44", RATE_EVENT_SESSION_CREATE, &limit, &allowed) == ASCIICHAT_OK);
  CHECK(!allowed);
  CHECK(rate_limiter_check(limiter, "192.0.2.45", RATE_EVENT_SESSION_CREATE, &limit, &allowed) == ASCIICHAT_OK);
  CHECK(allowed);
  CHECK(rate_limiter_prune(limiter, 3600) == ASCIICHAT_OK);
  rate_limiter_destroy(limiter);
  database_close(db);
  finish(h);
}

int main(int argc, char **argv) {
  CHECK(argc == 1 || argc == 2);
  log_init(NULL, LOG_ERROR, false, false);
  if (argc == 2) {
    capture = gotel_test_capture_create(OTEL_CONTRACT_CAPTURE_MAX_BYTES);
    CHECK(capture != NULL);
  }
  bounded_sampling();
  dropped_errors();
  retained_budget();
  sqlite_prepare_contract();
  catalog_contract();
  if (capture) {
    const uint8_t *data = NULL;
    size_t bytes = 0;
    CHECK(gotel_test_capture_read(capture, &data, &bytes) == OTLP_OK && bytes > 0);
    FILE *output = fopen(argv[1], "wb");
    CHECK(output != NULL);
    CHECK(fwrite(data, 1, bytes, output) == bytes);
    CHECK(fclose(output) == 0);
    gotel_test_capture_destroy(capture);
  }
  return 0;
}
