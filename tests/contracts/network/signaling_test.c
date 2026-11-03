#include <criterion/criterion.h>
#include "client/webrtc.h"
#include "discovery/webrtc.h"
#include <ascii-chat/common.h>
#include <ascii-chat/network/acip/acds.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/util/endian.h>
#include <string.h>

static uint8_t packet[9000];
static size_t packet_length, sends;
static asciichat_error_t send_result;
static const uint8_t session[16] = {1, 2, 3}, local[16] = {4, 5, 6}, remote[16] = {7, 8, 9};

static asciichat_error_t capture(acip_transport_t *transport, const void *data, size_t length) {
  (void)transport;
  cr_assert_leq(length, sizeof(packet));
  memcpy(packet, data, length);
  packet_length = length;
  sends++;
  return send_result;
}
static const acip_transport_methods_t methods = {.send = capture};
static acip_transport_t transport = {.methods = &methods};

static void setup(void) {
  log_init(NULL, LOG_FATAL, false, false);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  send_result = ASCIICHAT_OK;
}
static void teardown(void) {
  webrtc_set_acds_transport(NULL);
  webrtc_cleanup_transport();
  discovery_webrtc_cleanup_transport();
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TestSuite(signaling, .init = setup, .fini = teardown);

static webrtc_signaling_callbacks_t callbacks(bool direct) {
  if (direct)
    return discovery_webrtc_get_direct_signaling_callbacks(&transport, session, local);
  webrtc_set_session_context(session, local);
  webrtc_set_acds_transport(&transport);
  return webrtc_get_signaling_callbacks();
}

static const uint8_t *payload(packet_type_t type, size_t length) {
  cr_assert_eq(packet_length, sizeof(packet_header_t) + length);
  packet_header_t header;
  memcpy(&header, packet, sizeof(header));
  cr_assert_eq(NET_TO_HOST_U16(header.type), type);
  cr_assert_eq(NET_TO_HOST_U32(header.length), length);
  const uint8_t *data = packet + sizeof(header);
  cr_assert_eq(NET_TO_HOST_U32(header.crc32), asciichat_crc32(data, length));
  return data;
}

Test(signaling, sdp_offer_and_answer_preserve_identity_payload_and_network_byte_order) {
  const char sdp[] = "v=0\r\no=- 123 1 IN IP4 127.0.0.1\r\ns=fixture\r\n";
  for (unsigned direct = 0; direct < 2; ++direct) {
    webrtc_signaling_callbacks_t cb = callbacks(direct);
    for (unsigned answer = 0; answer < 2; ++answer) {
      cr_assert_eq(cb.send_sdp(session, remote, answer ? "answer" : "offer", sdp, cb.user_data), ASCIICHAT_OK);
      const uint8_t *data = payload(PACKET_TYPE_ACIP_WEBRTC_SDP, sizeof(acip_webrtc_sdp_t) + strlen(sdp));
      acip_webrtc_sdp_t header;
      memcpy(&header, data, sizeof(header));
      cr_assert_arr_eq(header.session_id, session, 16);
      cr_assert_arr_eq(header.sender_id, local, 16);
      const uint8_t broadcast[16] = {0};
      cr_assert_arr_eq(header.recipient_id, direct ? broadcast : remote, 16);
      cr_assert_eq(header.sdp_type, answer);
      cr_assert_eq(NET_TO_HOST_U16(header.sdp_len), strlen(sdp));
      cr_assert_arr_eq(data + sizeof(header), sdp, strlen(sdp));
    }
  }
}

Test(signaling, ice_candidate_and_mid_have_separate_terminators_and_candidate_only_length) {
  const char candidate[] = "candidate:1 1 UDP 2122260223 127.0.0.1 5000 typ host";
  const char mid[] = "data";
  for (unsigned direct = 0; direct < 2; ++direct) {
    webrtc_signaling_callbacks_t cb = callbacks(direct);
    cr_assert_eq(cb.send_ice(session, remote, candidate, mid, cb.user_data), ASCIICHAT_OK);
    const uint8_t *data =
        payload(PACKET_TYPE_ACIP_WEBRTC_ICE, sizeof(acip_webrtc_ice_t) + sizeof(candidate) + sizeof(mid));
    acip_webrtc_ice_t header;
    memcpy(&header, data, sizeof(header));
    cr_assert_arr_eq(header.session_id, session, 16);
    cr_assert_arr_eq(header.sender_id, local, 16);
    cr_assert_eq(NET_TO_HOST_U16(header.candidate_len), strlen(candidate));
    cr_assert_arr_eq(data + sizeof(header), candidate, sizeof(candidate));
    cr_assert_arr_eq(data + sizeof(header) + sizeof(candidate), mid, sizeof(mid));
  }
}

Test(signaling, disconnected_and_unconfigured_signaling_cannot_send) {
  webrtc_signaling_callbacks_t cb = webrtc_get_signaling_callbacks();
  cr_assert_eq(cb.send_sdp(session, remote, "offer", "v=0", cb.user_data), ERROR_INVALID_STATE);
  cr_assert_eq(cb.send_ice(session, remote, "candidate", "data", cb.user_data), ERROR_INVALID_STATE);
  webrtc_set_acds_transport(&transport);
  cr_assert_eq(cb.send_sdp(session, remote, "offer", "v=0", cb.user_data), ERROR_INVALID_STATE);
  cr_assert_eq(cb.send_ice(session, remote, "candidate", "data", cb.user_data), ERROR_INVALID_STATE);
  cb = discovery_webrtc_get_direct_signaling_callbacks(NULL, NULL, NULL);
  cr_assert_eq(cb.send_sdp(session, remote, "offer", "v=0", cb.user_data), ERROR_INVALID_STATE);
  cr_assert_eq(cb.send_ice(session, remote, "candidate", "data", cb.user_data), ERROR_INVALID_STATE);
  discovery_webrtc_set_tcp_transport(&transport);
  cr_assert_eq(cb.send_sdp(session, remote, "offer", "v=0", cb.user_data), ERROR_INVALID_STATE);
  cr_assert_eq(cb.send_ice(session, remote, "candidate", "data", cb.user_data), ERROR_INVALID_STATE);
  cr_assert_eq(sends, 0);
}

Test(signaling, invalid_or_oversized_payloads_are_rejected_and_transport_errors_propagate) {
  char too_large[8193];
  memset(too_large, 'a', sizeof(too_large) - 1);
  too_large[sizeof(too_large) - 1] = '\0';
  for (unsigned direct = 0; direct < 2; ++direct) {
    webrtc_signaling_callbacks_t cb = callbacks(direct);
    size_t before = sends;
    cr_assert_eq(cb.send_sdp(session, remote, "offer", NULL, cb.user_data), ERROR_INVALID_PARAM);
    cr_assert_eq(cb.send_ice(session, remote, NULL, "data", cb.user_data), ERROR_INVALID_PARAM);
    cr_assert_eq(cb.send_ice(session, remote, "candidate", NULL, cb.user_data), ERROR_INVALID_PARAM);
    cr_assert_eq(cb.send_sdp(session, remote, "offer", too_large, cb.user_data), ERROR_INVALID_PARAM);
    cr_assert_eq(cb.send_ice(session, remote, too_large, "data", cb.user_data), ERROR_INVALID_PARAM);
    cr_assert_eq(sends, before);
    send_result = ERROR_NETWORK;
    cr_assert_eq(cb.send_sdp(session, remote, "offer", "v=0", cb.user_data), ERROR_NETWORK);
    cr_assert_eq(cb.send_ice(session, remote, "candidate", "data", cb.user_data), ERROR_NETWORK);
    send_result = ASCIICHAT_OK;
    cr_assert_eq(cb.send_sdp(session, remote, "answer", "v=0", cb.user_data), ASCIICHAT_OK);
  }
}

Test(signaling, data_channel_cleanup_does_not_release_the_borrowed_transport) {
  cr_assert_null(webrtc_get_transport());
  webrtc_cleanup_transport();
  webrtc_set_transport(&transport);
  cr_assert_eq(webrtc_get_transport(), &transport);
  webrtc_cleanup_transport();
  cr_assert_null(webrtc_get_transport());
  webrtc_set_transport(&transport);
  webrtc_set_transport(NULL);
  cr_assert_null(webrtc_get_transport());
  cr_assert_eq(transport.methods, &methods);
}
