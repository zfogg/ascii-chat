#include <criterion/criterion.h>
#include "../../../src/discovery/negotiate.h"
#include <ascii-chat/network/acip/acds.h>
#include <ascii-chat/common.h>
#include <string.h>

static void setup(void) {
  log_init(NULL, LOG_FATAL, false, false);
}
static void teardown(void) {
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TestSuite(negotiate, .init = setup, .fini = teardown);

Test(negotiate, initialization_copies_identity_and_discards_previous_election) {
  uint8_t session[16] = {1, 2}, participant[16] = {3, 4};
  negotiate_ctx_t context;
  memset(&context, 0xa5, sizeof(context));
  negotiate_init(&context, session, participant, true);
  cr_assert_arr_eq(context.session_id, session, 16);
  cr_assert_arr_eq(context.participant_id, participant, 16);
  session[0] = participant[0] = 0;
  cr_assert_eq(context.session_id[0], 1);
  cr_assert_eq(context.participant_id[0], 3);
  cr_assert_eq(context.state, NEGOTIATE_STATE_INIT);
  cr_assert(context.is_initiator);
  cr_assert_not(context.we_are_host || context.peer_quality_received || context.our_quality.detection_complete);
  cr_assert_eq(context.host_address[0], 0);
  cr_assert_eq(context.host_port, 0);
  negotiate_init(&context, NULL, NULL, false);
  cr_assert_eq(context.session_id[0], 0);
  cr_assert_eq(context.participant_id[0], 0);
  cr_assert_not(context.is_initiator);
  negotiate_init(NULL, session, participant, true);
}

Test(negotiate, incomplete_observations_cannot_publish_a_host) {
  cr_assert_eq(negotiate_determine_result(NULL), ERROR_INVALID_PARAM);
  for (unsigned flags = 0; flags < 3; ++flags) {
    negotiate_ctx_t context;
    negotiate_init(&context, NULL, NULL, true);
    context.our_quality.detection_complete = (flags & 1) != 0;
    context.peer_quality_received = (flags & 2) != 0;
    cr_assert_eq(negotiate_determine_result(&context), ERROR_INVALID_STATE);
    cr_assert_eq(context.state, NEGOTIATE_STATE_INIT);
    cr_assert_not(context.we_are_host);
    cr_assert_eq(context.host_port, 0);
  }
}

Test(negotiate, elected_endpoint_uses_public_address_mapping_and_connection_type) {
  for (unsigned host_is_local = 0; host_is_local < 2; ++host_is_local) {
    for (unsigned kind = 0; kind < 3; ++kind) {
      negotiate_ctx_t context;
      negotiate_init(&context, NULL, NULL, host_is_local);
      context.our_quality.detection_complete = context.peer_quality_received = true;
      nat_quality_t *host = host_is_local ? &context.our_quality : &context.peer_quality;
      host->has_public_ip = kind == 0;
      host->upnp_available = kind == 1;
      host->upnp_mapped_port = 34567;
      SAFE_STRNCPY(host->public_address, "203.0.113.7", sizeof(host->public_address));
      cr_assert_eq(negotiate_determine_result(&context), ASCIICHAT_OK);
      cr_assert_eq(context.state, NEGOTIATE_STATE_COMPLETE);
      cr_assert_eq(context.we_are_host, host_is_local != 0);
      cr_assert_str_eq(context.host_address, "203.0.113.7");
      cr_assert_eq(context.host_port, kind == 1 ? 34567 : ACIP_HOST_DEFAULT_PORT);
      const uint8_t expected[] = {ACIP_CONNECTION_TYPE_DIRECT_PUBLIC, ACIP_CONNECTION_TYPE_UPNP,
                                  ACIP_CONNECTION_TYPE_STUN};
      cr_assert_eq(context.connection_type, expected[kind]);
    }
  }
}

Test(negotiate, equal_quality_peers_choose_the_initiator_and_local_host_has_loopback_fallback) {
  for (unsigned initiator = 0; initiator < 2; ++initiator) {
    negotiate_ctx_t context;
    negotiate_init(&context, NULL, NULL, initiator);
    context.our_quality.detection_complete = context.peer_quality_received = true;
    cr_assert_eq(negotiate_determine_result(&context), ASCIICHAT_OK);
    cr_assert_eq(context.we_are_host, initiator != 0);
    cr_assert_eq(context.connection_type, ACIP_CONNECTION_TYPE_STUN);
    cr_assert_eq(context.host_port, ACIP_HOST_DEFAULT_PORT);
    if (initiator)
      cr_assert_str_eq(context.host_address, "127.0.0.1");
  }
}
