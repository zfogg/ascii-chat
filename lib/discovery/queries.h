#pragma once

#include <ascii-chat/otel.h>

// Base SELECT for all session fields
#define SELECT_SESSION_BASE                                                                                            \
  "SELECT\n"                                                                                                           \
  "  session_string,\n"                                                                                                \
  "  session_id,\n"                                                                                                    \
  "  host_pubkey,\n"                                                                                                   \
  "  password_hash,\n"                                                                                                 \
  "  max_participants,\n"                                                                                              \
  "  current_participants,\n"                                                                                          \
  "  capabilities,\n"                                                                                                  \
  "  has_password,\n"                                                                                                  \
  "  expose_ip_publicly,\n"                                                                                            \
  "  session_type,\n"                                                                                                  \
  "  server_address,\n"                                                                                                \
  "  server_port,\n"                                                                                                   \
  "  created_at,\n"                                                                                                    \
  "  expires_at,\n"                                                                                                    \
  "  last_activity_at,\n"                                                                                              \
  "  initiator_id,\n"                                                                                                  \
  "  host_established,\n"                                                                                              \
  "  host_participant_id,\n"                                                                                           \
  "  host_address,\n"                                                                                                  \
  "  host_port,\n"                                                                                                     \
  "  host_connection_type,\n"                                                                                          \
  "  in_migration,\n"                                                                                                  \
  "  migration_start_ns\n"                                                                                             \
  "FROM sessions"

#ifndef ASCIICHAT_WITH_OTEL
typedef struct {
  const char *name;
  const char *sql;
} gotel_query_t;
#endif
typedef enum {
#define DATABASE_QUERY(name, sql) QUERY_##name,
#include "queries.def"
#undef DATABASE_QUERY
} database_query_id_t;
static const gotel_query_t database_queries[] = {
#define DATABASE_QUERY(name, sql) [QUERY_##name] = {#name, sql},
#include "queries.def"
#undef DATABASE_QUERY
};
