#include <criterion/criterion.h>
#include <ascii-chat/discovery/database.h>
#include <ascii-chat/discovery/key_set.h>
#include <ascii-chat/network/acip/acds_client.h>
#include <ascii-chat/network/acip/acds_handlers.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <sodium.h>
#include <string.h>

static sqlite3 *db;
static acds_config_t config;
static uint8_t keys[3][32], secrets[3][64];
static char db_path[1024];

static void setup(void) {
  CLEAR_ERRNO_ALL();
  cr_assert_geq(sodium_init(), 0);
  memset(&config, 0, sizeof(config));
  db_path[0] = 0;
  cr_assert_eq(database_init(":memory:", &db), ASCIICHAT_OK);
  for (size_t i = 0; i < 3; ++i)
    cr_assert_eq(crypto_sign_keypair(keys[i], secrets[i]), 0);
}
static void teardown(void) {
  database_close(db);
  if (db_path[0])
    platform_delete_temp_file(db_path);
  sodium_memzero(secrets, sizeof(secrets));
  CLEAR_ERRNO_ALL();
}
TestSuite(discovery_key_set, .init = setup, .fini = teardown);

static acip_session_create_t create_request(size_t index, uint8_t total) {
  acip_session_create_t req = {0};
  memcpy(req.identity_pubkey, keys[index], 32);
  req.timestamp = time_ns_to_ms(time_get_realtime_ns());
  req.capabilities = 3;
  req.max_participants = 8;
  req.session_type = SESSION_TYPE_WEBRTC;
  req.total_keys = total;
  req.key_index = (uint8_t)index;
  cr_assert_eq(
      acds_sign_session_create(secrets[index], req.timestamp, req.capabilities, req.max_participants, req.signature),
      ASCIICHAT_OK);
  return req;
}
static acip_session_join_t join_request(const acip_session_created_t *session, size_t index) {
  acip_session_join_t req = {0};
  req.session_string_len = session->session_string_len;
  memcpy(req.session_string, session->session_string, req.session_string_len);
  memcpy(req.identity_pubkey, keys[index], 32);
  req.timestamp = time_ns_to_ms(time_get_realtime_ns());
  cr_assert_eq(acds_sign_session_join(secrets[index], req.timestamp, req.session_string, req.signature), ASCIICHAT_OK);
  return req;
}
static void expect_join(acip_session_join_t *req, bool success) {
  acip_session_joined_t response;
  cr_assert_eq(database_session_join(db, req, &config, &response), ASCIICHAT_OK);
  cr_assert_eq(response.success, success);
  if (!success)
    cr_assert_eq(response.error_code, ERROR_CRYPTO_VERIFICATION);
}

Test(discovery_key_set, ordered_proofs_and_any_active_member_can_join) {
  acds_create_key_set_t set = {0};
  bool complete;
  for (size_t i = 0; i < 2; ++i) {
    acip_session_create_t req = create_request(i, 2);
    cr_assert_eq(acds_create_key_set_collect(&set, &req, false, &complete), ASCIICHAT_OK);
    cr_assert_eq(complete, i == 1);
  }
  acip_session_created_t session;
  cr_assert_eq(database_session_create_keys(db, &set.request, set.keys, set.count, &config, &session), ASCIICHAT_OK);
  uint8_t stored[3][32];
  size_t count;
  cr_assert_eq(database_session_get_keys(db, session.session_string, stored, 3, &count), ASCIICHAT_OK);
  cr_assert_eq(count, 2);
  cr_assert_arr_eq(stored[0], keys[0], 32);
  cr_assert_arr_eq(stored[1], keys[1], 32);
  for (size_t i = 0; i < 2; ++i) {
    acip_session_join_t req = join_request(&session, i);
    expect_join(&req, true);
  }
  acip_session_join_t outsider = join_request(&session, 2);
  expect_join(&outsider, false);
  acip_session_join_t forged = join_request(&session, 0);
  forged.signature[0] ^= 1;
  expect_join(&forged, false);
  forged = join_request(&session, 0);
  forged.timestamp = 0;
  expect_join(&forged, false);
  cr_assert_eq(database_session_revoke_key(db, session.session_string, keys[1]), ASCIICHAT_OK);
  cr_assert_eq(database_session_add_key(db, session.session_string, keys[1], 5), ASCIICHAT_OK);
  acip_session_join_t revoked = join_request(&session, 1);
  expect_join(&revoked, false);
  session_entry_t *entry = database_session_find_by_id(db, session.session_id);
  cr_assert_not_null(entry);
  cr_assert_eq(entry->current_participants, 2, "Rejected joins must not mutate participant counts");
  session_entry_destroy(entry);
}

Test(discovery_key_set, malformed_sequences_clear_partial_state) {
  for (int scenario = 0; scenario < 9; ++scenario) {
    acds_create_key_set_t set = {0};
    bool complete;
    acip_session_create_t first = create_request(0, 2), next = create_request(1, 2);
    cr_assert_eq(acds_create_key_set_collect(&set, &first, false, &complete), ASCIICHAT_OK);
    cr_assert_not(complete);
    switch (scenario) {
    case 0:
      next.key_index = 0;
      break;
    case 1:
      next.key_index = 2;
      break;
    case 2:
      next.total_keys = MAX_IDENTITY_KEYS + 1;
      break;
    case 3:
      next.total_keys = 3;
      break;
    case 4:
      next = first;
      next.key_index = 1;
      break;
    case 5:
      next.signature[0] ^= 1;
      break;
    case 6:
      next.timestamp = 0;
      break;
    case 7:
      next.total_keys = 0;
      break;
    case 8:
      next.capabilities ^= 1;
      break;
    }
    cr_assert_neq(acds_create_key_set_collect(&set, &next, false, &complete), ASCIICHAT_OK,
                  "Malformed sequence scenario %d", scenario);
    cr_assert_eq(set.count, 0);
    cr_assert_not(complete);
    CLEAR_ERRNO_ALL();
  }
  acds_create_key_set_t set = {0};
  acip_session_create_t req = create_request(1, 2);
  bool complete;
  cr_assert_eq(acds_create_key_set_collect(&set, &req, false, &complete), ERROR_INVALID_PARAM);
}

Test(discovery_key_set, single_key_policy_and_required_identity_are_preserved) {
  for (uint8_t total = 0; total <= 1; ++total) {
    acds_create_key_set_t set = {0};
    acip_session_create_t req = create_request(0, total);
    bool complete;
    req.signature[0] ^= 1;
    cr_assert_eq(acds_create_key_set_collect(&set, &req, true, &complete), ERROR_CRYPTO_VERIFICATION);
    CLEAR_ERRNO_ALL();
    cr_assert_eq(acds_create_key_set_collect(&set, &req, false, &complete), ASCIICHAT_OK);
    cr_assert(complete);
    acip_session_created_t session;
    cr_assert_eq(database_session_create_keys(db, &set.request, set.keys, 1, &config, &session), ASCIICHAT_OK);
    acip_session_join_t outsider = join_request(&session, 2);
    expect_join(&outsider, true);
    config.require_client_identity = true;
    outsider.signature[0] ^= 1;
    expect_join(&outsider, false);
    config.require_client_identity = false;
  }
}

Test(discovery_key_set, invalid_key_sets_and_storage_failures_leave_no_partial_session) {
  acip_session_create_t req = create_request(0, 2);
  acip_session_created_t session;
  uint8_t duplicate[2][32];
  memcpy(duplicate[0], keys[0], 32);
  memcpy(duplicate[1], keys[0], 32);
  cr_assert_eq(database_session_create_keys(db, &req, duplicate, 2, &config, &session), ERROR_INVALID_PARAM);
  memset(duplicate[1], 0, 32);
  cr_assert_eq(database_session_create_keys(db, &req, duplicate, 2, &config, &session), ERROR_INVALID_PARAM);
  cr_assert_eq(database_session_create_keys(db, &req, keys, 0, &config, &session), ERROR_INVALID_PARAM);
  cr_assert_eq(database_session_create_keys(db, &req, keys, MAX_IDENTITY_KEYS + 1, &config, &session),
               ERROR_INVALID_PARAM);
  cr_assert_eq(sqlite3_exec(db,
                            "CREATE TRIGGER fail_key BEFORE INSERT ON session_identity_keys "
                            "BEGIN SELECT RAISE(ABORT, 'injected key failure'); END",
                            NULL, NULL, NULL),
               SQLITE_OK);
  cr_assert_neq(database_session_create_keys(db, &req, keys, 2, &config, &session), ASCIICHAT_OK);
  int64_t sessions;
  cr_assert_eq(database_stats_session_count(db, &sessions), ASCIICHAT_OK);
  cr_assert_eq(sessions, 0);
  cr_assert_eq(session.session_string_len, 0);
}

Test(discovery_key_set, migration_retains_old_sessions_keys_revocations_and_admission_on_reopen) {
  int fd;
  cr_assert_eq(platform_create_temp_file(db_path, sizeof(db_path), "acds-key-migration", &fd), 0);
  if (fd >= 0)
    platform_close(fd);
  database_close(db);
  cr_assert_eq(database_init(db_path, &db), ASCIICHAT_OK);
  acip_session_create_t req = create_request(0, 0);
  acip_session_created_t session;
  cr_assert_eq(database_session_create(db, &req, &config, &session), ASCIICHAT_OK);
  // Reconstruct the pre-migration schema and retain a revoked host key plus a
  // second legacy rotation key. The old tables are left in place by migration.
  sqlite3_stmt *stmt;
  cr_assert_eq(
      sqlite3_prepare_v2(db,
                         "INSERT INTO session_keys "
                         "(session_string, identity_pubkey, key_version, added_at, revoked) VALUES (?, ?, 7, 123, ?)",
                         -1, &stmt, NULL),
      SQLITE_OK);
  for (size_t i = 0; i < 2; ++i) {
    sqlite3_bind_text(stmt, 1, session.session_string, -1, SQLITE_STATIC);
    sqlite3_bind_blob(stmt, 2, keys[i], 32, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, i == 0);
    cr_assert_eq(sqlite3_step(stmt), SQLITE_DONE);
    sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);
  cr_assert_eq(sqlite3_exec(db, "DROP TABLE session_identity_keys; DROP TABLE session_key_policy;", NULL, NULL, NULL),
               SQLITE_OK);
  database_close(db);
  cr_assert_eq(database_init(db_path, &db), ASCIICHAT_OK);
  cr_assert_not(database_session_verify_key(db, session.session_string, keys[0]));
  cr_assert(database_session_verify_key(db, session.session_string, keys[1]));
  acip_session_join_t outsider = join_request(&session, 2);
  expect_join(&outsider, true);
  cr_assert_eq(database_session_revoke_key(db, session.session_string, keys[1]), ASCIICHAT_OK);
  database_close(db);
  cr_assert_eq(database_init(db_path, &db), ASCIICHAT_OK);
  cr_assert_not(database_session_verify_key(db, session.session_string, keys[1]));
  session_entry_t *entry = database_session_find_by_id(db, session.session_id);
  cr_assert_not_null(entry);
  cr_assert_arr_eq(entry->host_pubkey, keys[0], 32);
  cr_assert_eq(entry->current_participants, 1);
  session_entry_destroy(entry);
}

Test(discovery_key_set, malformed_join_names_are_rejected_before_authentication) {
  acip_session_join_t req = {0};
  acip_session_joined_t resp;
  req.session_string_len = 255;
  cr_assert_eq(database_session_join(db, &req, &config, &resp), ERROR_INVALID_PARAM);
  req.session_string_len = 5;
  cr_assert_eq(database_session_join(db, &req, &config, &resp), ERROR_INVALID_PARAM);
}

Test(discovery_key_set, restricted_sessions_still_require_passwords_and_typed_storage) {
  acip_session_create_t req = create_request(0, 2);
  req.has_password = 1;
  cr_assert_eq(
      crypto_pwhash_str((char *)req.password_hash, "secret", 6, crypto_pwhash_OPSLIMIT_MIN, crypto_pwhash_MEMLIMIT_MIN),
      0);
  acip_session_created_t session;
  cr_assert_eq(database_session_create_keys(db, &req, keys, 2, &config, &session), ASCIICHAT_OK);
  acip_session_join_t join = join_request(&session, 1);
  acip_session_joined_t response;
  cr_assert_eq(database_session_join(db, &join, &config, &response), ASCIICHAT_OK);
  cr_assert_not(response.success);
  cr_assert_eq(response.error_code, ACIP_ERROR_INVALID_PASSWORD);
  join.has_password = 1;
  SAFE_STRNCPY(join.password, "wrong", sizeof(join.password));
  cr_assert_eq(database_session_join(db, &join, &config, &response), ASCIICHAT_OK);
  cr_assert_not(response.success);
  SAFE_STRNCPY(join.password, "secret", sizeof(join.password));
  expect_join(&join, true);
  cr_assert_eq(sqlite3_exec(db,
                            "INSERT INTO session_identity_keys "
                            "SELECT session_string, 'x25519', host_pubkey, 0, 0, 0 FROM sessions",
                            NULL, NULL, NULL),
               SQLITE_CONSTRAINT);
  cr_assert_eq(sqlite3_exec(db,
                            "INSERT INTO session_identity_keys "
                            "SELECT session_string, 'ed25519', X'01', 0, 0, 0 FROM sessions",
                            NULL, NULL, NULL),
               SQLITE_CONSTRAINT);
  cr_assert_eq(database_session_add_key(db, session.session_string, keys[0], 99), ASCIICHAT_OK);
  uint8_t stored[3][32];
  size_t count;
  cr_assert_eq(database_session_get_keys(db, session.session_string, stored, 3, &count), ASCIICHAT_OK);
  cr_assert_eq(count, 2);
}

static void count_create_callback(const acip_session_create_t *req, acip_transport_t *transport, const char *ip,
                                  void *ctx) {
  (void)req;
  (void)transport;
  (void)ip;
  ++*(size_t *)ctx;
}
Test(discovery_key_set, malformed_wire_fields_never_reach_create_callback) {
  size_t callbacks_called = 0;
  acip_acds_callbacks_t callbacks = {.on_session_create = count_create_callback, .app_ctx = &callbacks_called};
  acip_session_create_t req = create_request(0, 0);
  cr_assert_eq(
      acip_handle_acds_packet(NULL, PACKET_TYPE_ACIP_SESSION_CREATE, &req, sizeof(req), "127.0.0.1", &callbacks),
      ASCIICHAT_OK);
  cr_assert_eq(callbacks_called, 1);
  req.reserved_string_len = 4;
  cr_assert_eq(
      acip_handle_acds_packet(NULL, PACKET_TYPE_ACIP_SESSION_CREATE, &req, sizeof(req), "127.0.0.1", &callbacks),
      ERROR_INVALID_PARAM);
  req.reserved_string_len = 0;
  memset(req.server_address, 'x', sizeof(req.server_address));
  cr_assert_eq(
      acip_handle_acds_packet(NULL, PACKET_TYPE_ACIP_SESSION_CREATE, &req, sizeof(req), "127.0.0.1", &callbacks),
      ERROR_INVALID_PARAM);
  req.server_address[0] = 0;
  req.has_password = 1;
  memset(req.password_hash, 'x', sizeof(req.password_hash));
  cr_assert_eq(
      acip_handle_acds_packet(NULL, PACKET_TYPE_ACIP_SESSION_CREATE, &req, sizeof(req), "127.0.0.1", &callbacks),
      ERROR_INVALID_PARAM);
  cr_assert_eq(callbacks_called, 1);
}

Test(discovery_key_set, host_only_legacy_database_is_backfilled_and_new_restrictions_survive_reopen) {
  int fd;
  cr_assert_eq(platform_create_temp_file(db_path, sizeof(db_path), "acds-host-migration", &fd), 0);
  if (fd >= 0)
    platform_close(fd);
  database_close(db);
  cr_assert_eq(database_init(db_path, &db), ASCIICHAT_OK);
  acip_session_create_t req = create_request(0, 0);
  acip_session_created_t legacy, restricted;
  cr_assert_eq(database_session_create(db, &req, &config, &legacy), ASCIICHAT_OK);
  cr_assert_eq(sqlite3_exec(db, "DROP TABLE session_identity_keys; DROP TABLE session_key_policy", NULL, NULL, NULL),
               SQLITE_OK);
  database_close(db);
  cr_assert_eq(database_init(db_path, &db), ASCIICHAT_OK);
  cr_assert(database_session_verify_key(db, legacy.session_string, keys[0]));
  req = create_request(0, 2);
  cr_assert_eq(database_session_create_keys(db, &req, keys, 2, &config, &restricted), ASCIICHAT_OK);
  database_close(db);
  cr_assert_eq(database_init(db_path, &db), ASCIICHAT_OK);
  acip_session_join_t join = join_request(&restricted, 2);
  expect_join(&join, false);
  join = join_request(&restricted, 1);
  expect_join(&join, true);
  cr_assert_eq(sqlite3_exec(db, "DELETE FROM sessions", NULL, NULL, NULL), SQLITE_OK);
  cr_assert_not(database_session_verify_key(db, restricted.session_string, keys[1]));
}
