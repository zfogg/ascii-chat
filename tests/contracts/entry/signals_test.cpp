#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/common.h>
#include <libplatform/system.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/options/rcu.h>
#include <setjmp.h>
static bool help_active;
static unsigned help_toggles, interrupts;
static int forced_exit;
static jmp_buf forced_jump;
static char signal_console[1024];
static void entry_console(log_level_t level, const char *file, int line, const char *function, const char *message) {
  (void)level;
  (void)file;
  (void)line;
  (void)function;
  SAFE_STRNCPY(signal_console, message, sizeof(signal_console));
}
static bool entry_help_active(void) {
  return help_active;
}
static void entry_help_toggle(void) {
  help_active = !help_active;
  ++help_toggles;
}
static void entry_force_exit(int code) {
  forced_exit = code;
  longjmp(forced_jump, 1);
}
#define log_console_impl entry_console
#define keyboard_help_is_active_global entry_help_active
#define keyboard_help_toggle_global entry_help_toggle
#define platform_force_exit entry_force_exit
#define main fixture_application_main
#include "../../../src/main.c"
#undef main
#undef log_console_impl
#undef platform_force_exit
#undef keyboard_help_toggle_global
#undef keyboard_help_is_active_global
// The dispatcher table is retained, but no mode is launched by these contracts.
int server_main(void) {
  return ASCIICHAT_OK;
}
int client_main(void) {
  return ASCIICHAT_OK;
}
int mirror_main(void) {
  return ASCIICHAT_OK;
}
int acds_main(void) {
  return ASCIICHAT_OK;
}
int discovery_main(void) {
  return ASCIICHAT_OK;
}
int mcp_server_main(void) {
  return ASCIICHAT_OK;
}
asciichat_error_t mcp_stdio_reserve(void) {
  return ASCIICHAT_OK;
}
void mcp_stdio_release(void) {}
static void interrupted(void) {
  ++interrupts;
}
static void setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  ASCIICHAT_REQUIRE_EQ(options_state_init(), ASCIICHAT_OK);
  set_interrupt_callback(interrupted);
}
static void teardown(void) {
  set_interrupt_callback(NULL);
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TEST_CASE("entry_signals/signal_exit_sets_the_global_flag_before_notifying_the_mode_owner", "[entry_signals]") {
  ASCIICHAT_REQUIRE_NOT(should_exit());
  signal_exit();
  ASCIICHAT_REQUIRE(should_exit());
  ASCIICHAT_REQUIRE_EQ(interrupts, 1);
  set_interrupt_callback(NULL);
  atomic_store_bool(&g_should_exit, false);
  signal_exit();
  ASCIICHAT_REQUIRE(should_exit());
  ASCIICHAT_REQUIRE_EQ(interrupts, 1);
}
TEST_CASE("entry_signals/console_close_and_break_follow_help_dismissal_then_shutdown_then_forced_exit", "[entry_signals]") {
  ASCIICHAT_REQUIRE_NOT(console_ctrl_handler((console_ctrl_event_t)999));
  ASCIICHAT_REQUIRE_NOT(should_exit());
  ASCIICHAT_REQUIRE_EQ(interrupts, 0);
  help_active = true;
  ASCIICHAT_REQUIRE(console_ctrl_handler(CONSOLE_CTRL_C));
  ASCIICHAT_REQUIRE_NOT(help_active);
  ASCIICHAT_REQUIRE_EQ(help_toggles, 1);
  ASCIICHAT_REQUIRE_NOT(should_exit());
  ASCIICHAT_REQUIRE(console_ctrl_handler(CONSOLE_CTRL_BREAK));
  ASCIICHAT_REQUIRE(should_exit());
  ASCIICHAT_REQUIRE_EQ(interrupts, 1);
  if (setjmp(forced_jump) == 0) {
    console_ctrl_handler(CONSOLE_CLOSE);
    ASCIICHAT_FAIL("Repeated console interrupt did not force exit");
  }
  ASCIICHAT_REQUIRE_EQ(forced_exit, 1);
  ASCIICHAT_REQUIRE_EQ(interrupts, 1);
}
#ifndef _WIN32
TEST_CASE("entry_signals/posix_interrupt_dismisses_help_and_termination_interrupts_the_owner", "[entry_signals]") {
  help_active = true;
  handle_sigterm(SIGINT);
  ASCIICHAT_REQUIRE_NOT(should_exit());
  ASCIICHAT_REQUIRE_EQ(help_toggles, 1);
  const int signals[] = {SIGINT, SIGTERM, SIGHUP};
  const char *names[] = {"SIGINT", "SIGTERM", "SIGHUP"};
  for (unsigned i = 0; i < ARRAY_SIZE(signals); ++i) {
    atomic_store_bool(&g_should_exit, false);
    handle_sigterm(signals[i]);
    ASCIICHAT_REQUIRE(should_exit());
    ASCIICHAT_REQUIRE_EQ(interrupts, i + 1);
    ASCIICHAT_REQUIRE(strstr(signal_console, names[i]), "Missing signal name in shutdown diagnostic: %s", signal_console);
    ASCIICHAT_REQUIRE(strstr(signal_console, "shutting down"));
  }
}
#endif
TEST_CASE("entry_signals/mode_lookup_rejects_out_of_range_values_and_log_paths_keep_mode_names", "[entry_signals]") {
  const char *names[] = {"server.log", "client.log", "mirror.log", "acds.log", "discovery.log", "mcp-server.log"};
  for (int mode = MODE_SERVER; mode < MODE_INVALID; ++mode) {
    ASCIICHAT_REQUIRE_NOT_NULL(find_mode((asciichat_mode_t)mode));
    ASCIICHAT_REQUIRE_STR_EQ(get_mode_log_filename((asciichat_mode_t)mode), names[mode]);
  }
  ASCIICHAT_REQUIRE_NULL(find_mode(MODE_INVALID));
  ASCIICHAT_REQUIRE_NULL(find_mode((asciichat_mode_t)-1));
  ASCIICHAT_REQUIRE_NULL(find_mode((asciichat_mode_t)INT_MAX));
  ASCIICHAT_REQUIRE_STR_EQ(get_mode_log_filename(MODE_INVALID), "ascii-chat.log");
  char path[PLATFORM_MAX_PATH_LENGTH];
  generate_default_log_path(MODE_DISCOVERY, path, sizeof(path));
  ASCIICHAT_REQUIRE(strstr(path, "discovery.log"));
  path[0] = 'X';
  generate_default_log_path(MODE_SERVER, path, 0);
  ASCIICHAT_REQUIRE_EQ(path[0], 'X');
  generate_default_log_path(MODE_SERVER, NULL, sizeof(path));
}
