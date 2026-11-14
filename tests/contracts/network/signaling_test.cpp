#include <ascii-chat/tests/catch2_test_helpers.hpp>
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
  ASCIICHAT_REQUIRE_LE(length, sizeof(packet));
  memcpy(packet, data, length);
  packet_length = length;
  sends++;
  return send_result;
}
static const acip_transport_methods_t methods = {.send = capture};
static acip_transport_t transport = {.methods = &methods};

static void setup(void) {
  log_init(NULL, LOG_FATAL, false, false);
  ASCIICHAT_REQUIRE_EQ(options_state_init(), ASCIICHAT_OK);
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
static webrtc_signaling_callbacks_t callbacks(bool direct) {
  if (direct)
    return discovery_webrtc_get_direct_signaling_callbacks(&transport, session, local);
  webrtc_set_session_context(session, local);
  webrtc_set_acds_transport(&transport);
  return webrtc_get_signaling_callbacks();
}

static const uint8_t *payload(packet_type_t type, size_t length) {
  ASCIICHAT_REQUIRE_EQ(packet_length, sizeof(packet_header_t) + length);
  packet_header_t header;
  memcpy(&header, packet, sizeof(header));
  ASCIICHAT_REQUIRE_EQ(NET_TO_HOST_U16(header.type), type);
  ASCIICHAT_REQUIRE_EQ(NET_TO_HOST_U32(header.length), length);
  const uint8_t *data = packet + sizeof(header);
  ASCIICHAT_REQUIRE_EQ(NET_TO_HOST_U32(header.crc32), asciichat_crc32(data, length));
  return data;
}

TEST_CASE("signaling/sdp_offer_and_answer_preserve_identity_payload_and_network_byte_order", "[signaling]") {
  const char sdp[] = "v=0\r\no=- 123 1 IN IP4 127.0.0.1\r\ns=fixture\r\n";
  for (unsigned direct = 0; direct < 2; ++direct) {
    webrtc_signaling_callbacks_t cb = callbacks(direct);
    for (unsigned answer = 0; answer < 2; ++answer) {
      ASCIICHAT_REQUIRE_EQ(cb.send_sdp(session, remote, answer ? "answer" : "offer", sdp, cb.user_data), ASCIICHAT_OK);
      const uint8_t *data = payload(PACKET_TYPE_ACIP_WEBRTC_SDP, sizeof(acip_webrtc_sdp_t) + strlen(sdp));
      acip_webrtc_sdp_t header;
      memcpy(&header, data, sizeof(header));
      ASCIICHAT_REQUIRE_ARRAY_EQ(header.session_id, session, 16);
      ASCIICHAT_REQUIRE_ARRAY_EQ(header.sender_id, local, 16);
      const uint8_t broadcast[16] = {0};
      ASCIICHAT_REQUIRE_ARRAY_EQ(header.recipient_id, direct ? broadcast : remote, 16);
      ASCIICHAT_REQUIRE_EQ(header.sdp_type, answer);
      ASCIICHAT_REQUIRE_EQ(NET_TO_HOST_U16(header.sdp_len), strlen(sdp));
      ASCIICHAT_REQUIRE_ARRAY_EQ(data + sizeof(header), sdp, strlen(sdp));
    }
  }
}

TEST_CASE("signaling/ice_candidate_and_mid_have_separate_terminators_and_candidate_only_length", "[signaling]") {
  const char candidate[] = "candidate:1 1 UDP 2122260223 127.0.0.1 5000 typ host";
  const char mid[] = "data";
  for (unsigned direct = 0; direct < 2; ++direct) {
    webrtc_signaling_callbacks_t cb = callbacks(direct);
    ASCIICHAT_REQUIRE_EQ(cb.send_ice(session, remote, candidate, mid, cb.user_data), ASCIICHAT_OK);
    const uint8_t *data =
        payload(PACKET_TYPE_ACIP_WEBRTC_ICE, sizeof(acip_webrtc_ice_t) + sizeof(candidate) + sizeof(mid));
    acip_webrtc_ice_t header;
    memcpy(&header, data, sizeof(header));
    ASCIICHAT_REQUIRE_ARRAY_EQ(header.session_id, session, 16);
    ASCIICHAT_REQUIRE_ARRAY_EQ(header.sender_id, local, 16);
    ASCIICHAT_REQUIRE_EQ(NET_TO_HOST_U16(header.candidate_len), strlen(candidate));
    ASCIICHAT_REQUIRE_ARRAY_EQ(data + sizeof(header), candidate, sizeof(candidate));
    ASCIICHAT_REQUIRE_ARRAY_EQ(data + sizeof(header) + sizeof(candidate), mid, sizeof(mid));
  }
}

TEST_CASE("signaling/disconnected_and_unconfigured_signaling_cannot_send", "[signaling]") {
  webrtc_signaling_callbacks_t cb = webrtc_get_signaling_callbacks();
  ASCIICHAT_REQUIRE_EQ(cb.send_sdp(session, remote, "offer", "v=0", cb.user_data), ERROR_INVALID_STATE);
  ASCIICHAT_REQUIRE_EQ(cb.send_ice(session, remote, "candidate", "data", cb.user_data), ERROR_INVALID_STATE);
  webrtc_set_acds_transport(&transport);
  ASCIICHAT_REQUIRE_EQ(cb.send_sdp(session, remote, "offer", "v=0", cb.user_data), ERROR_INVALID_STATE);
  ASCIICHAT_REQUIRE_EQ(cb.send_ice(session, remote, "candidate", "data", cb.user_data), ERROR_INVALID_STATE);
  cb = discovery_webrtc_get_direct_signaling_callbacks(NULL, NULL, NULL);
  ASCIICHAT_REQUIRE_EQ(cb.send_sdp(session, remote, "offer", "v=0", cb.user_data), ERROR_INVALID_STATE);
  ASCIICHAT_REQUIRE_EQ(cb.send_ice(session, remote, "candidate", "data", cb.user_data), ERROR_INVALID_STATE);
  discovery_webrtc_set_tcp_transport(&transport);
  ASCIICHAT_REQUIRE_EQ(cb.send_sdp(session, remote, "offer", "v=0", cb.user_data), ERROR_INVALID_STATE);
  ASCIICHAT_REQUIRE_EQ(cb.send_ice(session, remote, "candidate", "data", cb.user_data), ERROR_INVALID_STATE);
  ASCIICHAT_REQUIRE_EQ(sends, 0);
}

TEST_CASE("signaling/invalid_or_oversized_payloads_are_rejected_and_transport_errors_propagate", "[signaling]") {
  char too_large[8193];
  memset(too_large, 'a', sizeof(too_large) - 1);
  too_large[sizeof(too_large) - 1] = '\0';
  for (unsigned direct = 0; direct < 2; ++direct) {
    webrtc_signaling_callbacks_t cb = callbacks(direct);
    size_t before = sends;
    ASCIICHAT_REQUIRE_EQ(cb.send_sdp(session, remote, "offer", NULL, cb.user_data), ERROR_INVALID_PARAM);
    ASCIICHAT_REQUIRE_EQ(cb.send_ice(session, remote, NULL, "data", cb.user_data), ERROR_INVALID_PARAM);
    ASCIICHAT_REQUIRE_EQ(cb.send_ice(session, remote, "candidate", NULL, cb.user_data), ERROR_INVALID_PARAM);
    ASCIICHAT_REQUIRE_EQ(cb.send_sdp(session, remote, "offer", too_large, cb.user_data), ERROR_INVALID_PARAM);
    ASCIICHAT_REQUIRE_EQ(cb.send_ice(session, remote, too_large, "data", cb.user_data), ERROR_INVALID_PARAM);
    ASCIICHAT_REQUIRE_EQ(sends, before);
    send_result = ERROR_NETWORK;
    ASCIICHAT_REQUIRE_EQ(cb.send_sdp(session, remote, "offer", "v=0", cb.user_data), ERROR_NETWORK);
    ASCIICHAT_REQUIRE_EQ(cb.send_ice(session, remote, "candidate", "data", cb.user_data), ERROR_NETWORK);
    send_result = ASCIICHAT_OK;
    ASCIICHAT_REQUIRE_EQ(cb.send_sdp(session, remote, "answer", "v=0", cb.user_data), ASCIICHAT_OK);
  }
}

TEST_CASE("signaling/data_channel_cleanup_does_not_release_the_borrowed_transport", "[signaling]") {
  ASCIICHAT_REQUIRE_NULL(webrtc_get_transport());
  webrtc_cleanup_transport();
  webrtc_set_transport(&transport);
  ASCIICHAT_REQUIRE_EQ(webrtc_get_transport(), &transport);
  webrtc_cleanup_transport();
  ASCIICHAT_REQUIRE_NULL(webrtc_get_transport());
  webrtc_set_transport(&transport);
  webrtc_set_transport(NULL);
  ASCIICHAT_REQUIRE_NULL(webrtc_get_transport());
  ASCIICHAT_REQUIRE_EQ(transport.methods, &methods);
}
