/** Explicit, bounded local probe: only two read-only statements in an in-memory DB. */
#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/otel.h>
#include <stdio.h>

#define CHECK(value)                                                                                                   \
  do {                                                                                                                 \
    if (!(value)) {                                                                                                    \
      fprintf(stderr, "Diagnostic probe failed: %s\n", #value);                                                        \
      return 1;                                                                                                        \
    }                                                                                                                  \
  } while (0)

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s http://127.0.0.1:24318\n", argv[0]);
    return 2;
  }
  log_init(NULL, LOG_ERROR, false, false);
  options_t options = options_t_new();
  SAFE_STRNCPY(options.otel_endpoint, argv[1], sizeof(options.otel_endpoint));
  SAFE_STRNCPY(options.otel_enabled_telemetry, "traces", sizeof(options.otel_enabled_telemetry));
  CHECK(options_state_init() == ASCIICHAT_OK);
  CHECK(options_state_set(&options) == ASCIICHAT_OK);
  CHECK(ascii_otel_init("discovery-service", argc, argv) == ASCIICHAT_OK);
  CHECK(ascii_otel_enabled());
  ascii_otel_scope_t probe_scope = {0};
  ascii_otel_scope_begin(&probe_scope, ASCII_OTEL_DATABASE_INIT);
  gotel_context_t context = ascii_otel_context();
  sqlite3 *db = NULL;
  CHECK(sqlite3_open(":memory:", &db) == SQLITE_OK);
  ascii_otel_sqlite(db, NULL, 0);
  sqlite3_stmt *statement = NULL;
  CHECK(ascii_otel_sqlite_prepare(db, "SELECT 1", &statement) == SQLITE_OK);
  CHECK(sqlite3_step(statement) == SQLITE_ROW);
  CHECK(sqlite3_finalize(statement) == SQLITE_OK);
  statement = NULL;
  CHECK(ascii_otel_sqlite_prepare(db, "SELECT 'probe-private-value", &statement) == SQLITE_ERROR);
  CHECK(statement == NULL);
  CHECK(sqlite3_close(db) == SQLITE_OK);
  ascii_otel_scope_end(&probe_scope);
  ascii_otel_exit(0);
  ascii_otel_shutdown();
  options_state_destroy();
  printf("trace_id=");
  for (size_t i = 0; i < sizeof(context.trace_id); ++i)
    printf("%02x", context.trace_id[i]);
  printf("\n");
  return 0;
}
