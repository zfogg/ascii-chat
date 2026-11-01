#include <criterion/criterion.h>
#include <ascii-chat/platform/cond.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/network/acip/acds_client.h>
#include <ascii-chat/util/time.h>
#include <sodium.h>

// Compile the production database implementation with only the expensive Argon2
// boundary replaced, so a real second thread can inspect/mutate the shared DB
// during password verification without timing assumptions about hash duration.
static int password_verify_probe(const char *, const char *, unsigned long long);
#define crypto_pwhash_str_verify password_verify_probe
#include "../../../lib/discovery/database.c"
#undef crypto_pwhash_str_verify

static sqlite3 *db;
static acds_config_t config;
static acip_session_created_t session;
static uint8_t keys[2][32], secrets[2][64];
static mutex_t gate;
static cond_t changed;
static bool verifying, completed, progressed;
static unsigned int password_calls;
static const char *mutation;
static asciichat_error_t lookup_result;
static int mutation_result;

static void setup(void) {
  CLEAR_ERRNO_ALL();
  cr_assert_geq(sodium_init(), 0);
  cr_assert_eq(mutex_init(&gate, "password-probe"), 0);
  cr_assert_eq(cond_init(&changed, "password-probe"), 0);
  cr_assert_eq(database_init(":memory:", &db), ASCIICHAT_OK);
  memset(&config, 0, sizeof(config));
  verifying = completed = progressed = false;
  password_calls = 0;
  mutation = NULL;
  mutation_result = SQLITE_OK;
  for (size_t i = 0; i < 2; ++i)
    cr_assert_eq(crypto_sign_keypair(keys[i], secrets[i]), 0);
  acip_session_create_t create = {.total_keys = 2, .max_participants = 2, .has_password = 1};
  memcpy(create.identity_pubkey, keys[0], sizeof(create.identity_pubkey));
  memcpy(create.password_hash, "probe-hash", sizeof("probe-hash"));
  cr_assert_eq(database_session_create_keys(db, &create, keys, 2, &config, &session), ASCIICHAT_OK);
}
static void teardown(void) {
  database_close(db);
  cond_destroy(&changed);
  mutex_destroy(&gate);
  sodium_memzero(secrets, sizeof(secrets));
  CLEAR_ERRNO_ALL();
}
TestSuite(discovery_password_lock, .init = setup, .fini = teardown);

static int password_verify_probe(const char *hash, const char *password, unsigned long long length) {
  (void)hash;
  (void)password;
  (void)length;
  password_calls++;
  mutex_lock(&gate);
  verifying = true;
  cond_signal(&changed);
  uint64_t deadline = time_get_ns() + 5 * NS_PER_SEC_INT;
  while (!completed) {
    uint64_t now = time_get_ns();
    if (now >= deadline)
      break;
    cond_timedwait(&changed, &gate, deadline - now);
  }
  progressed = completed;
  mutex_unlock(&gate);
  return 0;
}
static void *other_client(void *unused) {
  (void)unused;
  mutex_lock(&gate);
  while (!verifying)
    cond_wait(&changed, &gate);
  mutex_unlock(&gate);
  acip_session_info_t info;
  lookup_result = database_session_lookup(db, session.session_string, &config, &info);
  if (mutation)
    mutation_result = sqlite3_exec(db, mutation, NULL, NULL, NULL);
  mutex_lock(&gate);
  completed = true;
  cond_signal(&changed);
  mutex_unlock(&gate);
  return NULL;
}
static acip_session_join_t request(void) {
  acip_session_join_t join = {.has_password = 1};
  join.session_string_len = session.session_string_len;
  memcpy(join.session_string, session.session_string, (size_t)join.session_string_len);
  memcpy(join.identity_pubkey, keys[0], sizeof(join.identity_pubkey));
  SAFE_STRNCPY(join.password, "secret", sizeof(join.password));
  join.timestamp = time_ns_to_ms(time_get_realtime_ns());
  cr_assert_eq(acds_sign_session_join(secrets[0], join.timestamp, join.session_string, join.signature), ASCIICHAT_OK);
  return join;
}
static void run_join(uint8_t expected) {
  acip_session_join_t join = request();
  asciichat_thread_t worker;
  cr_assert_eq(acthread_create(&worker, "other-client", other_client, NULL), 0);
  acip_session_joined_t response;
  asciichat_error_t result = database_session_join(db, &join, &config, &response);
  cr_assert_eq(thread_join(&worker, NULL), 0);
  cr_assert_eq(result, ASCIICHAT_OK);
  cr_assert(progressed, "Unrelated SQLite work must finish while password verification is still blocked");
  cr_assert_eq(lookup_result, ASCIICHAT_OK);
  cr_assert_eq(mutation_result, SQLITE_OK);
  cr_assert_eq(password_calls, 1);
  cr_assert_eq(response.error_code, expected);
  cr_assert_eq(response.success, expected == ACIP_ERROR_NONE);
  session_entry_t *entry = database_session_find_by_string(db, session.session_string);
  cr_assert_not_null(entry);
  cr_assert_eq(entry->current_participants, expected == ACIP_ERROR_NONE           ? 1
                                            : expected == ACIP_ERROR_SESSION_FULL ? 2
                                                                                  : 0);
  session_entry_destroy(entry);
}
Test(discovery_password_lock, unrelated_lookup_proceeds_during_password_verification) {
  run_join(ACIP_ERROR_NONE);
}
Test(discovery_password_lock, revocation_during_password_verification_is_rechecked) {
  mutation = "UPDATE session_identity_keys SET revoked = 1";
  run_join(ACIP_ERROR_INVALID_SIGNATURE);
}
Test(discovery_password_lock, capacity_during_password_verification_is_rechecked) {
  mutation = "UPDATE sessions SET current_participants = max_participants";
  run_join(ACIP_ERROR_SESSION_FULL);
}
Test(discovery_password_lock, replaced_session_cannot_inherit_verified_password) {
  mutation = "UPDATE sessions SET session_id = randomblob(16)";
  run_join(ACIP_ERROR_SESSION_NOT_FOUND);
}
Test(discovery_password_lock, changed_password_requires_a_retry) {
  mutation = "UPDATE sessions SET password_hash = 'different-hash'";
  run_join(ACIP_ERROR_INVALID_PASSWORD);
}
Test(discovery_password_lock, unlisted_identity_cannot_trigger_password_work) {
  acip_session_join_t join = request();
  memset(join.identity_pubkey, 0, sizeof(join.identity_pubkey));
  acip_session_joined_t response;
  cr_assert_eq(database_session_join(db, &join, &config, &response), ASCIICHAT_OK);
  cr_assert_eq(response.error_code, ACIP_ERROR_INVALID_SIGNATURE);
  cr_assert_eq(password_calls, 0);
}
