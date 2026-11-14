/* Each Catch2 case starts with a fresh set of splash initialization state. */
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#define main splash_standalone_main
#include "../../standalone/tui/splash_init.c"
#undef main

static void setup(void) {
  ASCIICHAT_REQUIRE_EQ(asciichat_shared_init(NULL, false, true), ASCIICHAT_OK);
  log_set_terminal_output(true);
}
TEST_CASE("splash_init/notification_first_call_is_serialized", "[splash_init]") {
  ASCIICHAT_REQUIRE(competing_initializers(false));
}

TEST_CASE("splash_init/discovery_first_call_is_serialized", "[splash_init]") {
  ASCIICHAT_REQUIRE(competing_initializers(true));
}
