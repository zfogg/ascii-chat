#include <catch2/catch_test_macros.hpp>
/* Each Catch2 case starts with a fresh set of splash initialization state. */
#include <catch2/catch_all.hpp>
#define main splash_standalone_main
#include "../../standalone/tui/splash_init.c"
#undef main

static void setup(void) {
  REQUIRE((asciichat_shared_init(NULL, false, true)) == (ASCIICHAT_OK));
  log_set_terminal_output(true);
}
TEST_CASE("splash_init/notification_first_call_is_serialized", "[splash_init]") {
  REQUIRE(competing_initializers(false));
}

TEST_CASE("splash_init/discovery_first_call_is_serialized", "[splash_init]") {
  REQUIRE(competing_initializers(true));
}
