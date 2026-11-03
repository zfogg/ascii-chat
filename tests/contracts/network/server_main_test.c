#include <criterion/criterion.h>
#include <ascii-chat/options/rcu.h>
#include "../../../src/server/main.c"

atomic_t g_should_exit;
bool should_exit(void) {
  return atomic_load_bool(&g_should_exit);
}
void signal_exit(void) {
  atomic_store_bool(&g_should_exit, true);
}
void set_interrupt_callback(void (*callback)(void)) {
  (void)callback;
}

static unsigned contract_sends;
static packet_type_t contract_type;
static size_t contract_length;
static uint8_t contract_payload[16384];
static asciichat_error_t contract_send_result;
static asciichat_error_t contract_send(acip_transport_t *transport, const void *data, size_t length) {
  (void)transport;
  cr_assert_geq(length, sizeof(packet_header_t));
  packet_header_t header;
  memcpy(&header, data, sizeof(header));
  contract_type = NET_TO_HOST_U16(header.type);
  contract_length = length - sizeof(header);
  cr_assert_leq(contract_length, sizeof(contract_payload));
  memcpy(contract_payload, (const uint8_t *)data + sizeof(header), contract_length);
  ++contract_sends;
  return contract_send_result;
}
static const acip_transport_methods_t contract_methods = {.send = contract_send};
static acip_transport_t contract_transport = {.methods = &contract_methods};
static void contract_setup(void) {
  log_init(NULL, LOG_FATAL, false, false);
  log_set_terminal_output(false);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  contract_send_result = ASCIICHAT_OK;
}
static void contract_teardown(void) {
  g_acds_transport = NULL;
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TestSuite(server_main_contract, .init = contract_setup, .fini = contract_teardown, .timeout = 20);
Test(server_main_contract, sdp_relay_preserves_ids_type_and_exact_nonterminated_payload) {
  uint8_t session[16] = {1}, recipient[16] = {2};
  g_server_participant_id[0] = 3;
  cr_assert_eq(server_send_sdp(session, recipient, "offer", "v=0", NULL), ERROR_INVALID_STATE);
  CLEAR_ERRNO_ALL();
  g_acds_transport = &contract_transport;
  cr_assert_eq(server_send_sdp(session, recipient, "offer", "v=0", NULL), ASCIICHAT_OK);
  cr_assert_eq(contract_type, PACKET_TYPE_ACIP_WEBRTC_SDP);
  acip_webrtc_sdp_t header;
  memcpy(&header, contract_payload, sizeof(header));
  cr_assert_arr_eq(header.session_id, session, 16);
  cr_assert_arr_eq(header.recipient_id, recipient, 16);
  cr_assert_arr_eq(header.sender_id, g_server_participant_id, 16);
  cr_assert_eq(header.sdp_type, 0);
  cr_assert_eq(NET_TO_HOST_U16(header.sdp_len), 3);
  cr_assert_eq(contract_length, sizeof(header) + 3);
  cr_assert_arr_eq(contract_payload + sizeof(header), "v=0", 3);
  cr_assert_eq(server_send_sdp(session, recipient, "answer", "answer", NULL), ASCIICHAT_OK);
  memcpy(&header, contract_payload, sizeof(header));
  cr_assert_eq(header.sdp_type, 1);
  unsigned before = contract_sends;
  char oversized[8193];
  memset(oversized, 'x', sizeof(oversized) - 1);
  oversized[sizeof(oversized) - 1] = 0;
  cr_assert_eq(server_send_sdp(session, recipient, "offer", "", NULL), ERROR_INVALID_PARAM);
  CLEAR_ERRNO_ALL();
  cr_assert_eq(server_send_sdp(session, recipient, "offer", oversized, NULL), ERROR_INVALID_PARAM);
  CLEAR_ERRNO_ALL();
  cr_assert_eq(contract_sends, before);
  contract_send_result = ERROR_NETWORK;
  cr_assert_eq(server_send_sdp(session, recipient, "offer", "v=0", NULL), ERROR_NETWORK);
}
Test(server_main_contract, ice_relay_encodes_two_strings_and_bounds_combined_payload) {
  uint8_t session[16] = {4}, recipient[16] = {5};
  g_server_participant_id[0] = 6;
  cr_assert_eq(server_send_ice(session, recipient, "candidate", "0", NULL), ERROR_INVALID_STATE);
  CLEAR_ERRNO_ALL();
  g_acds_transport = &contract_transport;
  cr_assert_eq(server_send_ice(session, recipient, "candidate", "0", NULL), ASCIICHAT_OK);
  cr_assert_eq(contract_type, PACKET_TYPE_ACIP_WEBRTC_ICE);
  acip_webrtc_ice_t header;
  memcpy(&header, contract_payload, sizeof(header));
  cr_assert_eq(NET_TO_HOST_U16(header.candidate_len), 9);
  cr_assert_arr_eq(header.session_id, session, 16);
  cr_assert_arr_eq(header.recipient_id, recipient, 16);
  cr_assert_arr_eq(header.sender_id, g_server_participant_id, 16);
  cr_assert_eq(contract_length, sizeof(header) + 12);
  cr_assert_arr_eq(contract_payload + sizeof(header),
                   "candidate\0"
                   "0\0",
                   12);
  char oversized[8193];
  memset(oversized, 'x', sizeof(oversized) - 1);
  oversized[sizeof(oversized) - 1] = 0;
  unsigned before = contract_sends;
  cr_assert_eq(server_send_ice(session, recipient, oversized, "0", NULL), ERROR_INVALID_PARAM);
  CLEAR_ERRNO_ALL();
  cr_assert_eq(server_send_ice(session, recipient, "x", oversized, NULL), ERROR_INVALID_PARAM);
  CLEAR_ERRNO_ALL();
  cr_assert_eq(contract_sends, before);
  contract_send_result = ERROR_NETWORK;
  cr_assert_eq(server_send_ice(session, recipient, "candidate", "0", NULL), ERROR_NETWORK);
}
