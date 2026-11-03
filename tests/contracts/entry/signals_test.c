#include <criterion/criterion.h>
#include <ascii-chat/common.h>
#include <ascii-chat/platform/system.h>
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
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  set_interrupt_callback(interrupted);
}
static void teardown(void) {
  set_interrupt_callback(NULL);
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TestSuite(entry_signals, .init = setup, .fini = teardown, .timeout = 20);
Test(entry_signals, signal_exit_sets_the_global_flag_before_notifying_the_mode_owner) {
  cr_assert_not(should_exit());
  signal_exit();
  cr_assert(should_exit());
  cr_assert_eq(interrupts, 1);
  set_interrupt_callback(NULL);
  atomic_store_bool(&g_should_exit, false);
  signal_exit();
  cr_assert(should_exit());
  cr_assert_eq(interrupts, 1);
}
Test(entry_signals, console_close_and_break_follow_help_dismissal_then_shutdown_then_forced_exit) {
  cr_assert_not(console_ctrl_handler((console_ctrl_event_t)999));
  cr_assert_not(should_exit());
  cr_assert_eq(interrupts, 0);
  help_active = true;
  cr_assert(console_ctrl_handler(CONSOLE_CTRL_C));
  cr_assert_not(help_active);
  cr_assert_eq(help_toggles, 1);
  cr_assert_not(should_exit());
  cr_assert(console_ctrl_handler(CONSOLE_CTRL_BREAK));
  cr_assert(should_exit());
  cr_assert_eq(interrupts, 1);
  if (setjmp(forced_jump) == 0) {
    console_ctrl_handler(CONSOLE_CLOSE);
    cr_assert_fail("Repeated console interrupt did not force exit");
  }
  cr_assert_eq(forced_exit, 1);
  cr_assert_eq(interrupts, 1);
}
#ifndef _WIN32
Test(entry_signals, posix_interrupt_dismisses_help_and_termination_interrupts_the_owner) {
  help_active = true;
  handle_sigterm(SIGINT);
  cr_assert_not(should_exit());
  cr_assert_eq(help_toggles, 1);
  const int signals[] = {SIGINT, SIGTERM, SIGHUP};
  const char *names[] = {"SIGINT", "SIGTERM", "SIGHUP"};
  for (unsigned i = 0; i < ARRAY_SIZE(signals); ++i) {
    atomic_store_bool(&g_should_exit, false);
    handle_sigterm(signals[i]);
    cr_assert(should_exit());
    cr_assert_eq(interrupts, i + 1);
    cr_assert(strstr(signal_console, names[i]), "Missing signal name in shutdown diagnostic: %s", signal_console);
    cr_assert(strstr(signal_console, "shutting down"));
  }
}
#endif
Test(entry_signals, mode_lookup_rejects_out_of_range_values_and_log_paths_keep_mode_names) {
  const char *names[] = {"server.log", "client.log", "mirror.log", "acds.log", "discovery.log", "mcp-server.log"};
  for (int mode = MODE_SERVER; mode < MODE_INVALID; ++mode) {
    cr_assert_not_null(find_mode((asciichat_mode_t)mode));
    cr_assert_str_eq(get_mode_log_filename((asciichat_mode_t)mode), names[mode]);
  }
  cr_assert_null(find_mode(MODE_INVALID));
  cr_assert_null(find_mode((asciichat_mode_t)-1));
  cr_assert_null(find_mode((asciichat_mode_t)INT_MAX));
  cr_assert_str_eq(get_mode_log_filename(MODE_INVALID), "ascii-chat.log");
  char path[PLATFORM_MAX_PATH_LENGTH];
  generate_default_log_path(MODE_DISCOVERY, path, sizeof(path));
  cr_assert(strstr(path, "discovery.log"));
  path[0] = 'X';
  generate_default_log_path(MODE_SERVER, path, 0);
  cr_assert_eq(path[0], 'X');
  generate_default_log_path(MODE_SERVER, NULL, sizeof(path));
}
