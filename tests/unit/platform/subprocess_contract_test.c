#include <criterion/criterion.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/log/log.h>
#include <string.h>

static void setup(void) {
  log_init(NULL, LOG_FATAL, false, false);
}
TestSuite(subprocess_contract, .init = setup, .fini = log_destroy, .timeout = 15);
#ifndef _WIN32
Test(subprocess_contract, capture_preserves_empty_arguments_and_shell_metacharacters) {
  const char *args[] = {"printf", "<%s>|<%s>|<%s>|<%s>", "", "two words", "$(echo changed);*", "single'quote", NULL};
  char output[256];
  cr_assert_eq(platform_execute_subprocess("/usr/bin/printf", args, output, sizeof(output)), 0);
  cr_assert_str_eq(output, "<>|<two words>|<$(echo changed);*>|<single'quote>");
}
Test(subprocess_contract, executable_is_independent_of_display_name_and_exit_status_is_preserved) {
  const char *args[] = {"display-name", "%s", "literal", NULL};
  char output[32];
  cr_assert_eq(platform_execute_subprocess("/usr/bin/printf", args, output, sizeof(output)), 0);
  cr_assert_str_eq(output, "literal");
  const char *exit_args[] = {"sh", "-c", "exit 23", NULL};
  cr_assert_eq(platform_execute_subprocess("/bin/sh", exit_args, output, sizeof(output)), 23);
  cr_assert_str_eq(output, "");
  cr_assert_eq(platform_execute_subprocess("/bin/sh", exit_args, NULL, 0), 23);
  const char *missing[] = {"missing", NULL};
  cr_assert_eq(platform_execute_subprocess("/definitely-missing-ascii-chat-program", missing, output, sizeof(output)),
               127);
  cr_assert_str_eq(output, "");
}
Test(subprocess_contract, captured_output_has_exact_capacity_and_overflow_is_drained) {
  const char *small[] = {"printf", "abcd", NULL};
  struct {
    char output[5];
    char guard;
  } buffer = {.guard = 'G'};
  cr_assert_eq(platform_execute_subprocess("/usr/bin/printf", small, buffer.output, 5), 0);
  cr_assert_str_eq(buffer.output, "abcd");
  cr_assert_eq(buffer.guard, 'G');
  cr_assert_eq(platform_execute_subprocess("/usr/bin/printf", small, buffer.output, 4), -1);
  cr_assert_str_eq(buffer.output, "abc");
  cr_assert_eq(buffer.guard, 'G');
  cr_assert_eq(platform_execute_subprocess("/usr/bin/printf", small, buffer.output, 1), -1);
  cr_assert_str_eq(buffer.output, "");
  cr_assert_eq(buffer.guard, 'G');
  const char *large[] = {"sh", "-c", "head -c 131072 /dev/zero", NULL};
  cr_assert_eq(platform_execute_subprocess("/bin/sh", large, buffer.output, sizeof(buffer.output)), -1);
  cr_assert_eq(buffer.output[4], '\0');
  cr_assert_eq(buffer.guard, 'G');
}
Test(subprocess_contract, invalid_parameters_and_signal_termination_report_failure) {
  const char *args[] = {"sh", "-c", "kill -TERM $$", NULL};
  char output[16];
  cr_assert_eq(platform_execute_subprocess(NULL, args, output, sizeof(output)), -1);
  cr_assert_eq(platform_execute_subprocess("/bin/sh", NULL, output, sizeof(output)), -1);
  const char *empty[] = {NULL};
  cr_assert_eq(platform_execute_subprocess("/bin/sh", empty, output, sizeof(output)), -1);
  cr_assert_eq(platform_execute_subprocess("/bin/sh", args, output, sizeof(output)), -1);
  cr_assert_eq(platform_execute_subprocess("/bin/sh", args, NULL, 0), -1);
}
#endif
