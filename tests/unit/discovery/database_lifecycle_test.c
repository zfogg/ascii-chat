#include <criterion/criterion.h>
#include <ascii-chat/discovery/database.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/platform/system.h>
#include <stdatomic.h>
#include <string.h>

static sqlite3 *db;
static acds_config_t config;
static acip_session_created_t session;

static void setup(void) {
  cr_assert_eq(database_init(":memory:", &db), ASCIICHAT_OK);
  memset(&config, 0, sizeof(config));
  acip_session_create_t request = {0};
  request.session_type = SESSION_TYPE_DIRECT_TCP;
  request.max_participants = 4;
  request.expose_ip_publicly = true;
  SAFE_STRNCPY(request.server_address, "127.0.0.1", sizeof(request.server_address));
  request.server_port = 27224;
  cr_assert_eq(database_session_create(db, &request, &config, &session), ASCIICHAT_OK);
}

static void teardown(void) {
  database_close(db);
  db = NULL;
}

TestSuite(database_lifecycle, .init = setup, .fini = teardown);

Test(database_lifecycle, shared_connection_is_serialized) {
  cr_assert_not_null(sqlite3_db_mutex(db), "Discovery's shared SQLite connection must have a mutex");
}

typedef struct {
  sqlite3 *db;
  const uint8_t *session_id;
  atomic_bool *start;
  bool success;
} concurrent_lookup_t;

static void *concurrent_session_lookup(void *data) {
  concurrent_lookup_t *lookup = data;
  while (!atomic_load(lookup->start))
    platform_sleep_ms(1);
  lookup->success = true;
  for (int i = 0; i < 100; ++i) {
    session_entry_t *entry = database_session_find_by_id(lookup->db, lookup->session_id);
    if (!entry) {
      lookup->success = false;
      break;
    }
    session_entry_destroy(entry);
  }
  return NULL;
}

Test(database_lifecycle, shared_connection_handles_concurrent_session_lookups) {
  enum { WORKERS = 4 };
  atomic_bool start = false;
  concurrent_lookup_t lookups[WORKERS] = {0};
  asciichat_thread_t threads[WORKERS];
  int created = 0;
  for (int i = 0; i < WORKERS; ++i) {
    lookups[i] = (concurrent_lookup_t){.db = db, .session_id = session.session_id, .start = &start};
    if (acthread_create(&threads[i], "discovery_lookup", concurrent_session_lookup, &lookups[i]) != ASCIICHAT_OK)
      break;
    ++created;
  }
  atomic_store(&start, true);
  for (int i = 0; i < created; ++i)
    cr_assert_eq(thread_join(&threads[i], NULL), ASCIICHAT_OK);
  cr_assert_eq(created, WORKERS, "Could not start all lookup workers");
  for (int i = 0; i < WORKERS; ++i)
    cr_assert(lookups[i].success, "Concurrent discovery lookup failed");
}

Test(database_lifecycle, key_rotation_is_ordered_bounded_and_revocation_cannot_be_undone_by_readding) {
  uint8_t first[32] = {1}, second[32] = {2}, missing[32] = {3}, keys[3][32] = {{0}};
  const char *name = session.session_string;
  cr_assert_not(database_session_verify_key(db, name, first));
  cr_assert_eq(database_session_add_key(db, name, second, 20), ASCIICHAT_OK);
  cr_assert_eq(database_session_add_key(db, name, first, 10), ASCIICHAT_OK);
  cr_assert(database_session_verify_key(db, name, first));
  cr_assert_not(database_session_verify_key(db, name, missing));
  size_t count = 99;
  cr_assert_eq(database_session_get_keys(db, name, keys, 1, &count), ASCIICHAT_OK);
  cr_assert_eq(count, 1);
  cr_assert_arr_eq(keys[0], first, 32);
  cr_assert_eq(database_session_get_keys(db, name, keys, 3, &count), ASCIICHAT_OK);
  cr_assert_eq(count, 2);
  cr_assert_arr_eq(keys[1], second, 32);
  cr_assert_eq(database_session_revoke_key(db, name, first), ASCIICHAT_OK);
  cr_assert_eq(database_session_add_key(db, name, first, 30), ASCIICHAT_OK);
  cr_assert_not(database_session_verify_key(db, name, first));
  cr_assert_eq(database_session_get_keys(db, name, keys, 3, &count), ASCIICHAT_OK);
  cr_assert_eq(count, 1);
  cr_assert_arr_eq(keys[0], second, 32);
  cr_assert_eq(database_session_get_keys(db, name, keys, 0, &count), ASCIICHAT_OK);
  cr_assert_eq(count, 0);
  cr_assert_eq(database_session_add_key(NULL, name, first, 1), ERROR_INVALID_PARAM);
  cr_assert_not(database_session_verify_key(NULL, name, first));
}

Test(database_lifecycle, host_migration_and_inactivity_cleanup_update_the_persisted_session) {
  uint8_t host[16] = {42};
  cr_assert_eq(database_session_update_host(db, session.session_id, host, "127.0.0.2", 30000, 0), ASCIICHAT_OK);
  session_entry_t *entry = database_session_find_by_id(db, session.session_id);
  cr_assert_not_null(entry);
  cr_assert(entry->host_established);
  cr_assert_str_eq(entry->host_address, "127.0.0.2");
  cr_assert_eq(entry->host_port, 30000);
  session_entry_destroy(entry);
  cr_assert_eq(database_session_start_migration(db, session.session_id), ASCIICHAT_OK);
  cr_assert(database_session_is_migration_ready(db, session.session_id, 0));
  cr_assert_eq(database_session_clear_host(db, session.session_id), ASCIICHAT_OK);
  entry = database_session_find_by_string(db, session.session_string);
  cr_assert_not_null(entry);
  cr_assert_not(entry->host_established);
  session_entry_destroy(entry);
  database_session_stats_t rows[2];
  size_t count;
  uint64_t sessions, participants;
  cr_assert_eq(database_stats_snapshot(db, rows, 2, &count, &sessions, &participants), ASCIICHAT_OK);
  cr_assert_eq(count, 1);
  cr_assert_eq(sessions, 1);
  cr_assert_str_eq(rows[0].session_string, session.session_string);
  cr_assert_eq(sqlite3_exec(db, "UPDATE sessions SET last_activity_at = 0", NULL, NULL, NULL), SQLITE_OK);
  database_session_cleanup_expired(db);
  cr_assert_null(database_session_find_by_id(db, session.session_id));
}
