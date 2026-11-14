#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include "common/session/consensus.h"
#include <ascii-chat/network/consensus/state.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/util/time.h>
#include <string.h>

static session_consensus_t *consensus;
static uint8_t members[MAX_SESSION_PARTICIPANTS][SESSION_ID_SIZE];
static unsigned elections, notifications;
static bool invalid_choice, reject_election;

static asciichat_error_t capture_packet(void *context, const uint8_t id[SESSION_ID_SIZE], const uint8_t *data,
                                        size_t size) {
  (void)context;
  (void)id;
  (void)data;
  (void)size;
  return ASCIICHAT_OK;
}
static asciichat_error_t own_metrics(void *context, const uint8_t id[SESSION_ID_SIZE], participant_metrics_t *metric) {
  (void)context;
  *metric = (participant_metrics_t){.nat_tier = 0, .upload_kbps = 5000, .stun_probe_success_pct = 100};
  memcpy(metric->participant_id, id, SESSION_ID_SIZE);
  return ASCIICHAT_OK;
}
static asciichat_error_t notified(void *context, const uint8_t host[SESSION_ID_SIZE],
                                  const char address[ACIP_ADDRESS_SIZE], uint16_t port,
                                  const uint8_t backup[SESSION_ID_SIZE], const char backup_address[ACIP_ADDRESS_SIZE],
                                  uint16_t backup_port) {
  ASCIICHAT_REQUIRE_EQ(context, &notifications);
  ASCIICHAT_REQUIRE_ARRAY_EQ(host, members[1], SESSION_ID_SIZE);
  ASCIICHAT_REQUIRE_ARRAY_EQ(backup, members[2], SESSION_ID_SIZE);
  ASCIICHAT_REQUIRE_STR_EQ(address, "127.0.0.2");
  ASCIICHAT_REQUIRE_STR_EQ(backup_address, "127.0.0.3");
  ASCIICHAT_REQUIRE_EQ(port, 27002);
  ASCIICHAT_REQUIRE_EQ(backup_port, 27003);
  ++notifications;
  return reject_election ? ERROR_NETWORK : ASCIICHAT_OK;
}
static asciichat_error_t choose(void *context, const participant_metrics_t *metrics, int count, int *best,
                                int *backup) {
  ASCIICHAT_REQUIRE_EQ(context, &notifications);
  ASCIICHAT_REQUIRE_EQ(count, 3);
  ++elections;
  *best = *backup = -1;
  for (int i = 0; i < count; ++i) {
    if (!memcmp(metrics[i].participant_id, members[1], SESSION_ID_SIZE))
      *best = i;
    if (!memcmp(metrics[i].participant_id, members[2], SESSION_ID_SIZE))
      *backup = i;
  }
  if (invalid_choice)
    *best = count;
  return reject_election ? ERROR_NETWORK : ASCIICHAT_OK;
}
static session_consensus_callbacks_t callbacks(bool custom) {
  return (session_consensus_callbacks_t){.send_packet = capture_packet,
                                         .on_election = notified,
                                         .get_metrics = own_metrics,
                                         .election = custom ? choose : NULL,
                                         .context = &notifications};
}
static void setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  for (unsigned i = 0; i < 3; ++i)
    members[i][0] = (uint8_t)(3 - i); // The ring leader is the greatest UUID.
}
static void teardown(void) {
  session_consensus_destroy(consensus);
  CLEAR_ERRNO_ALL();
  log_destroy();
}
static void create(bool custom) {
  session_consensus_callbacks_t cb = callbacks(custom);
  ASCIICHAT_REQUIRE_EQ(session_consensus_create(members[0], true, members, 3, &cb, &consensus), ASCIICHAT_OK);
}
static void collect(void) {
  ASCIICHAT_REQUIRE_EQ(session_consensus_on_collection_start(consensus, 7, time_get_ns() - 1), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(session_consensus_get_metrics_count(consensus), 1);
  participant_metrics_t peers[2];
  ASCIICHAT_REQUIRE_EQ(own_metrics(NULL, members[1], &peers[0]), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(own_metrics(NULL, members[2], &peers[1]), ASCIICHAT_OK);
  peers[0].nat_tier = 1;
  peers[1].nat_tier = 2;
  ASCIICHAT_REQUIRE_EQ(session_consensus_on_stats_update(consensus, members[1], peers, 2), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(session_consensus_get_metrics_count(consensus), 3);
  ASCIICHAT_REQUIRE_EQ(session_consensus_process(consensus, 0), ASCIICHAT_OK);
}
static void expect_hosts(const uint8_t *expected_host, const uint8_t *expected_backup) {
  uint8_t host[SESSION_ID_SIZE], backup[SESSION_ID_SIZE];
  char address[ACIP_ADDRESS_SIZE], backup_address[ACIP_ADDRESS_SIZE];
  uint16_t port = 1, backup_port = 1;
  ASCIICHAT_REQUIRE_EQ(
      session_consensus_get_elected_host(consensus, host, address, &port, backup, backup_address, &backup_port),
      ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_ARRAY_EQ(host, expected_host, SESSION_ID_SIZE);
  ASCIICHAT_REQUIRE_ARRAY_EQ(backup, expected_backup, SESSION_ID_SIZE);
  // The coordinator currently stores identities, without endpoint metadata.
  ASCIICHAT_REQUIRE_EQ(port, 0);
  ASCIICHAT_REQUIRE_EQ(backup_port, 0);
  ASCIICHAT_REQUIRE_STR_EMPTY(address);
  ASCIICHAT_REQUIRE_STR_EMPTY(backup_address);
  ASCIICHAT_REQUIRE(session_consensus_is_ready(consensus));
}

TEST_CASE("session_consensus_rounds/custom_election_receives_all_metrics_and_publishes_its_selected_hosts", "[session_consensus_rounds]") {
  create(true);
  collect();
  ASCIICHAT_REQUIRE_EQ(elections, 1);
  expect_hosts(members[1], members[2]);
}
TEST_CASE("session_consensus_rounds/default_election_prefers_the_better_observed_network", "[session_consensus_rounds]") {
  create(false);
  collect();
  expect_hosts(members[0], members[1]);
  ASCIICHAT_REQUIRE_EQ(elections, 0);
}
TEST_CASE("session_consensus_rounds/invalid_custom_result_does_not_publish_a_host", "[session_consensus_rounds]") {
  create(true);
  invalid_choice = true;
  collect();
  ASCIICHAT_REQUIRE_EQ(elections, 1);
  ASCIICHAT_REQUIRE_NOT(session_consensus_is_ready(consensus));
}
TEST_CASE("session_consensus_rounds/election_notification_propagates_callback_errors_and_retains_host_identity", "[session_consensus_rounds]") {
  create(false);
  reject_election = true;
  ASCIICHAT_REQUIRE_EQ(
      session_consensus_on_election_result(consensus, members[1], "127.0.0.2", 27002, members[2], "127.0.0.3", 27003),
      ERROR_NETWORK);
  ASCIICHAT_REQUIRE_EQ(notifications, 1);
  expect_hosts(members[1], members[2]);
}
TEST_CASE("session_consensus_rounds/topology_replacement_rejects_missing_local_identity_without_losing_the_old_ring", "[session_consensus_rounds]") {
  create(true);
  uint8_t invalid[MAX_SESSION_PARTICIPANTS][SESSION_ID_SIZE] = {{99}};
  ASCIICHAT_REQUIRE_NE(session_consensus_set_topology(consensus, invalid, 1), ASCIICHAT_OK);
  CLEAR_ERRNO_ALL();
  collect();
  expect_hosts(members[1], members[2]);
}
TEST_CASE("session_consensus_rounds/topology_change_during_collection_discards_old_metrics_and_accepts_a_new_round", "[session_consensus_rounds]") {
  create(true);
  ASCIICHAT_REQUIRE_EQ(session_consensus_on_collection_start(consensus, 1, time_get_ns() + NS_PER_SEC_INT), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(session_consensus_get_metrics_count(consensus), 1);
  ASCIICHAT_REQUIRE_EQ(session_consensus_set_topology(consensus, members, 3), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(session_consensus_get_state(consensus), CONSENSUS_STATE_IDLE);
  ASCIICHAT_REQUIRE_EQ(session_consensus_get_metrics_count(consensus), 0);
  collect();
  expect_hosts(members[1], members[2]);
}
