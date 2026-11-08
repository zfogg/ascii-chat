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

Test(database_lifecycle, transport_endpoints_are_hidden_after_host_changes) {
  uint8_t metadata[10] = {'A', 'C', 'Q', 'T', 1}, output[10], new_host[16] = {42};
  size_t length = 0;
  cr_assert_eq(database_session_transport_set(db, session.session_id, metadata, sizeof(metadata)), ASCIICHAT_OK);
  cr_assert_eq(database_session_transport_get(db, session.session_id, output, sizeof(output), &length), ASCIICHAT_OK);
  cr_assert_eq(length, sizeof(metadata));
  cr_assert_arr_eq(output, metadata, sizeof(metadata));
  cr_assert_eq(database_session_update_host(db, session.session_id, new_host, "127.0.0.2", 30000, 0), ASCIICHAT_OK);
  cr_assert_eq(database_session_transport_get(db, session.session_id, output, sizeof(output), &length), ASCIICHAT_OK);
  cr_assert_eq(length, 0);
}

Test(database_lifecycle, transport_storage_failure_rolls_back_session_creation) {
  cr_assert_eq(sqlite3_exec(db,
                            "CREATE TRIGGER reject_transport BEFORE INSERT ON session_transports BEGIN SELECT "
                            "RAISE(FAIL, 'test rejection'); END;",
                            NULL, NULL, NULL),
               SQLITE_OK);
  acip_session_create_t request = {.session_type = SESSION_TYPE_WEBRTC, .max_participants = 4};
  uint8_t keys[1][32] = {{0}}, metadata[10] = {'A', 'C', 'Q', 'T', 1};
  acip_session_created_t response;
  cr_assert_neq(database_session_create_keys_with_transport(db, &request, keys, 1, &config, &response, metadata,
                                                            sizeof(metadata)),
                ASCIICHAT_OK);
  cr_assert_eq(response.session_string[0], 0);
  sqlite3_stmt *statement = NULL;
  cr_assert_eq(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM sessions", -1, &statement, NULL), SQLITE_OK);
  cr_assert_eq(sqlite3_step(statement), SQLITE_ROW);
  cr_assert_eq(sqlite3_column_int(statement, 0), 1);
  sqlite3_finalize(statement);
}

Test(database_lifecycle, successor_transports_follow_the_host_and_failed_refresh_rolls_back) {
  uint8_t old_metadata[10] = {'A', 'C', 'Q', 'T', 1, 0, 0x75, 0x31, 0, 0};
  uint8_t fresh_metadata[10] = {'A', 'C', 'Q', 'T', 1, 0, 0x75, 0x32, 0, 0};
  uint8_t first[16] = {42}, second[16] = {43}, output[10];
  size_t length = 0;
  cr_assert_eq(database_session_transport_set(db, session.session_id, old_metadata, 10), ASCIICHAT_OK);
  cr_assert_eq(database_session_update_host(db, session.session_id, first, "127.0.0.2", 30000, 0), ASCIICHAT_OK);
  cr_assert_eq(database_session_transport_get(db, session.session_id, output, 10, &length), ASCIICHAT_OK);
  cr_assert_eq(length, 0);
  cr_assert_eq(database_session_update_host_with_transport(db, session.session_id, first, "127.0.0.2", 30000, 0,
                                                           fresh_metadata, 10),
               ASCIICHAT_OK);
  cr_assert_eq(database_session_transport_get(db, session.session_id, output, 10, &length), ASCIICHAT_OK);
  cr_assert_eq(length, 10);
  cr_assert_arr_eq(output, fresh_metadata, 10);
  cr_assert_eq(sqlite3_exec(db,
                            "CREATE TRIGGER reject_host_transport BEFORE INSERT ON session_transport_hosts "
                            "BEGIN SELECT RAISE(FAIL, 'test rejection'); END;",
                            NULL, NULL, NULL),
               SQLITE_OK);
  cr_assert_neq(database_session_update_host_with_transport(db, session.session_id, second, "127.0.0.3", 31000, 0,
                                                            old_metadata, 10),
                ASCIICHAT_OK);
  session_entry_t *stored = database_session_find_by_id(db, session.session_id);
  cr_assert_not_null(stored);
  cr_assert_arr_eq(stored->host_participant_id, first, 16);
  cr_assert_eq(stored->host_port, 30000);
  session_entry_destroy(stored);
  cr_assert_eq(database_session_transport_get(db, session.session_id, output, 10, &length), ASCIICHAT_OK);
  cr_assert_arr_eq(output, fresh_metadata, 10);
  cr_assert_eq(database_session_start_migration(db, session.session_id), ASCIICHAT_OK);
  cr_assert_eq(database_session_transport_get(db, session.session_id, output, 10, &length), ASCIICHAT_OK);
  cr_assert_eq(length, 0);
  cr_assert_eq(database_session_clear_host(db, session.session_id), ASCIICHAT_OK);
  cr_assert_eq(database_session_transport_get(db, session.session_id, output, 10, &length), ASCIICHAT_OK);
  cr_assert_eq(length, 0);
}

Test(database_lifecycle, clearing_a_failed_host_cannot_republish_its_old_transport) {
  uint8_t metadata[10] = {'A', 'C', 'Q', 'T', 1}, host[16] = {42}, output[10];
  size_t length = 0;
  cr_assert_eq(
      database_session_update_host_with_transport(db, session.session_id, host, "127.0.0.2", 30000, 0, metadata, 10),
      ASCIICHAT_OK);
  cr_assert_eq(database_session_clear_host(db, session.session_id), ASCIICHAT_OK);
  cr_assert_eq(database_session_transport_get(db, session.session_id, output, 10, &length), ASCIICHAT_OK);
  cr_assert_eq(length, 0);
  sqlite3_stmt *statement = NULL;
  cr_assert_eq(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM session_transports", -1, &statement, NULL), SQLITE_OK);
  cr_assert_eq(sqlite3_step(statement), SQLITE_ROW);
  cr_assert_eq(sqlite3_column_int(statement, 0), 0);
  sqlite3_finalize(statement);
}

// A failed host refresh must not roll back another caller's successful creation
// on the same connection, even when the two callers repeatedly contend.
enum { CONCURRENT_HOST_REFRESHES = 128 };
typedef struct {
  atomic_bool start;
  bool refresh_ok, creation_ok;
  uint8_t created[CONCURRENT_HOST_REFRESHES][SESSION_ID_SIZE];
} host_refresh_race_t;

static void *concurrent_failed_host_refresh(void *data) {
  host_refresh_race_t *race = data;
  while (!atomic_load(&race->start))
    platform_sleep_ms(1);
  race->refresh_ok = true;
  uint8_t host[SESSION_ID_SIZE] = {42};
  uint8_t metadata[10] = {'A', 'C', 'Q', 'T', 1};
  for (int i = 0; i < CONCURRENT_HOST_REFRESHES; ++i) {
    if (database_session_update_host_with_transport(db, session.session_id, host, "127.0.0.2", 30000, 0, metadata,
                                                    sizeof(metadata)) == ASCIICHAT_OK) {
      race->refresh_ok = false;
      break;
    }
  }
  return NULL;
}

static void *concurrent_session_creation(void *data) {
  host_refresh_race_t *race = data;
  while (!atomic_load(&race->start))
    platform_sleep_ms(1);
  race->creation_ok = true;
  acip_session_create_t request = {.session_type = SESSION_TYPE_WEBRTC, .max_participants = 4};
  uint8_t keys[1][32] = {{0}};
  for (int i = 0; i < CONCURRENT_HOST_REFRESHES; ++i) {
    acip_session_created_t response;
    if (database_session_create_keys(db, &request, keys, 1, &config, &response) != ASCIICHAT_OK) {
      race->creation_ok = false;
      break;
    }
    memcpy(race->created[i], response.session_id, SESSION_ID_SIZE);
  }
  return NULL;
}

Test(database_lifecycle, failed_host_refresh_preserves_concurrent_session_creations) {
  cr_assert_eq(sqlite3_exec(db,
                            "CREATE TRIGGER reject_host_refresh BEFORE INSERT ON session_transport_hosts "
                            "BEGIN SELECT RAISE(FAIL, 'test rejection'); END;",
                            NULL, NULL, NULL),
               SQLITE_OK);
  host_refresh_race_t race = {0};
  asciichat_thread_t refresh, creation;
  cr_assert_eq(acthread_create(&refresh, "host_refresh", concurrent_failed_host_refresh, &race), ASCIICHAT_OK);
  asciichat_error_t result = acthread_create(&creation, "session_create", concurrent_session_creation, &race);
  atomic_store(&race.start, true);
  cr_assert_eq(thread_join(&refresh, NULL), ASCIICHAT_OK);
  if (result == ASCIICHAT_OK)
    cr_assert_eq(thread_join(&creation, NULL), ASCIICHAT_OK);
  cr_assert_eq(result, ASCIICHAT_OK);
  cr_assert(race.refresh_ok, "The injected host refresh failure was not observed");
  cr_assert(race.creation_ok, "A concurrent session creation failed");
  for (int i = 0; i < CONCURRENT_HOST_REFRESHES; ++i) {
    session_entry_t *stored = database_session_find_by_id(db, race.created[i]);
    cr_assert_not_null(stored, "Successful session creation %d was rolled back by another caller", i);
    session_entry_destroy(stored);
  }
}
