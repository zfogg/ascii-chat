#pragma once
#include <ascii-chat/asciichat_errno.h>
#include <stdarg.h>
#include <stdbool.h>
typedef enum {
#define ASCII_OTEL_OPERATION(id, name, realtime) ASCII_OTEL_##id,
#include <ascii-chat/otel_operations.def>
#undef ASCII_OTEL_OPERATION
  ASCII_OTEL_OPERATION_COUNT
} ascii_otel_operation_t;
typedef enum {
#define ASCII_OTEL_METRIC(id, name, type, unit) ASCII_OTEL_METRIC_##id,
#include <ascii-chat/otel_metrics.def>
#undef ASCII_OTEL_METRIC
  ASCII_OTEL_METRIC_COUNT
} ascii_otel_metric_t;
#ifdef ASCIICHAT_WITH_OTEL
#include <gotel/gotel.h>
typedef struct {
  gotel_scope_t scope;
  bool retained;
} ascii_otel_scope_t;
asciichat_error_t ascii_otel_init(const char *mode, int argc, char **argv);
void ascii_otel_shutdown(void);
bool ascii_otel_enabled(void);
void ascii_otel_metric(ascii_otel_metric_t metric, double value);
void ascii_otel_exit(int code);
gotel_context_t ascii_otel_context(void);
void ascii_otel_scope_int(ascii_otel_scope_t *scope, const char *key, int64_t value);
void ascii_otel_scope_parent(ascii_otel_scope_t *scope, ascii_otel_operation_t operation,
                             const gotel_context_t *parent);
void ascii_otel_scope_begin(ascii_otel_scope_t *scope, ascii_otel_operation_t operation);
void ascii_otel_scope_end(ascii_otel_scope_t *scope);
void ascii_otel_log(int level, const char *file, int line, const char *function, const char *format, va_list args);
void ascii_otel_error(const char *message);
void ascii_otel_http(ascii_otel_scope_t *scope, const char *hostname, const char *path);
void ascii_otel_http_status(ascii_otel_scope_t *scope, int status);
void ascii_otel_sqlite(sqlite3 *db, const gotel_query_t *queries, size_t count);
#define ASCII_OTEL_ATTR_STRING(key, value) gotel_attr_string(&otel_scope.scope, key, value)
#define ASCII_OTEL_ATTR_INT(key, value) gotel_attr_int(&otel_scope.scope, key, value)
#define ASCII_OTEL_SCOPE_PARENT(id, parent)                                                                            \
  ascii_otel_scope_t otel_scope __attribute__((cleanup(ascii_otel_scope_end))) = {0};                                  \
  ascii_otel_scope_parent(&otel_scope, ASCII_OTEL_##id, parent)
#define ASCII_OTEL_SCOPE(id)                                                                                           \
  ascii_otel_scope_t otel_scope __attribute__((cleanup(ascii_otel_scope_end))) = {0};                                  \
  ascii_otel_scope_begin(&otel_scope, ASCII_OTEL_##id)
#else
#define ASCII_OTEL_ATTR_STRING(key, value) ((void)(key), (void)(value))
#define ASCII_OTEL_ATTR_INT(key, value) ((void)(key), (void)(value))
#define ascii_otel_enabled() false
#define ascii_otel_metric(metric, value) ((void)(metric), (void)(value))
#define ASCII_OTEL_SCOPE_PARENT(id, parent) ((void)0)
#define ASCII_OTEL_SCOPE(id) ((void)0)
#endif
