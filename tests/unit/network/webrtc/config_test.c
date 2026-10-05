#include <ascii-chat/network/webrtc/webrtc.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/tests/logging.h>
#include <criterion/criterion.h>

static void setup(void) {
  cr_assert_eq(webrtc_init(), ASCIICHAT_OK);
}

TestSuite(webrtc_config, .init = setup, .fini = webrtc_destroy, .timeout = 10.0);

Test(webrtc_config, relay_requires_turn) {
  webrtc_config_t config = {.relay_only = true};
  webrtc_peer_connection_t *pc = NULL;
  cr_assert_eq(webrtc_create_peer_connection(&config, &pc), ERROR_INVALID_PARAM);
  cr_assert_null(pc);
}

Test(webrtc_config, turn_requires_complete_credentials) {
  turn_server_t turn = {.url = "turn:127.0.0.1:3478", .username = "test"};
  webrtc_config_t config = {.turn_servers = &turn, .turn_count = 1};
  webrtc_peer_connection_t *pc = NULL;
  cr_assert_eq(webrtc_create_peer_connection(&config, &pc), ERROR_INVALID_PARAM);
  cr_assert_null(pc);
}

Test(webrtc_config, rejects_invalid_turn_scheme) {
  turn_server_t turn = {.url = "https://127.0.0.1:3478", .username = "test", .credential = "test"};
  webrtc_config_t config = {.turn_servers = &turn, .turn_count = 1};
  webrtc_peer_connection_t *pc = NULL;
  cr_assert_eq(webrtc_create_peer_connection(&config, &pc), ERROR_INVALID_PARAM);
  cr_assert_null(pc);
}

Test(webrtc_config, accepts_full_discovery_credentials_with_reserved_characters) {
  turn_server_t turn = {.url = "turn:127.0.0.1:3478?transport=tcp"};
  webrtc_config_t config = {
      .turn_servers = &turn,
      .turn_count = 1,
      .relay_only = true,
      .turn_username = "1900000000:session-name-longer-than-wire-username",
      .turn_credential = "password:@%/+=",
  };
  webrtc_peer_connection_t *pc = NULL;
  cr_assert_eq(webrtc_create_peer_connection(&config, &pc), ASCIICHAT_OK);
  cr_assert_not_null(pc);
  webrtc_close_peer_connection(pc);
}
