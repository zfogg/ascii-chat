/* Each Criterion case starts with a fresh set of splash initialization state. */
#include <criterion/criterion.h>
#define main splash_standalone_main
#include "../../standalone/tui/splash_init.c"
#undef main

static void setup(void) {
  cr_assert_eq(asciichat_shared_init(NULL, false, true), ASCIICHAT_OK);
  log_set_terminal_output(true);
}

TestSuite(splash_init, .init = setup);

Test(splash_init, notification_first_call_is_serialized) {
  cr_assert(competing_initializers(false));
}

Test(splash_init, discovery_first_call_is_serialized) {
  cr_assert(competing_initializers(true));
}
