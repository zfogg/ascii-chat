#pragma once

#include <ascii-chat/otel.h>
#include "queries_generated.h"

/* Gotel reads the generated records directly without imposing its descriptor
 * layout on the application catalog. The generated table has static lifetime. */
static bool database_query_get(const void *context, size_t index, gotel_query_t *out) {
  const struct database_query *queries = context;
  if (!queries || !out || index >= DATABASE_QUERY_COUNT)
    return false;
  *out = (gotel_query_t){queries[index].name, queries[index].sql};
  return true;
}
