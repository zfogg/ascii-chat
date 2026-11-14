#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <libplatform/system.h>
#include <ascii-chat/log/log.h>
#include <string.h>

static void setup(void) {
  log_init(NULL, LOG_FATAL, false, false);
}
#ifndef _WIN32
#include "../../../lib/platform/posix/terminal.h"
#include <signal.h>
#include <unistd.h>

TEST_CASE("subprocess_contract/closed_gate_with_ignored_sigpipe_does_not_block", "[subprocess_contract]") {
  int gate[2];
  ASCIICHAT_REQUIRE_EQ(pipe(gate), 0);
  close(gate[0]);
  struct sigaction ignore = {.sa_handler = SIG_IGN}, previous;
  sigemptyset(&ignore.sa_mask);
  ASCIICHAT_REQUIRE_EQ(sigaction(SIGPIPE, &ignore, &previous), 0);
  bool released = terminal_release_subprocess_gate(gate[1]);
  ASCIICHAT_REQUIRE_EQ(sigaction(SIGPIPE, &previous, NULL), 0);
  close(gate[1]);
  ASCIICHAT_REQUIRE_NOT(released);
}

TEST_CASE("subprocess_contract/capture_preserves_empty_arguments_and_shell_metacharacters", "[subprocess_contract]") {
  const char *args[] = {"printf", "<%s>|<%s>|<%s>|<%s>", "", "two words", "$(echo changed);*", "single'quote", NULL};
  char output[256];
  ASCIICHAT_REQUIRE_EQ(platform_execute_subprocess("/usr/bin/printf", args, output, sizeof(output), NULL, NULL), 0);
  ASCIICHAT_REQUIRE_STR_EQ(output, "<>|<two words>|<$(echo changed);*>|<single'quote>");
}
TEST_CASE("subprocess_contract/executable_is_independent_of_display_name_and_exit_status_is_preserved", "[subprocess_contract]") {
  const char *args[] = {"display-name", "%s", "literal", NULL};
  char output[32];
  ASCIICHAT_REQUIRE_EQ(platform_execute_subprocess("/usr/bin/printf", args, output, sizeof(output), NULL, NULL), 0);
  ASCIICHAT_REQUIRE_STR_EQ(output, "literal");
  const char *exit_args[] = {"sh", "-c", "exit 23", NULL};
  ASCIICHAT_REQUIRE_EQ(platform_execute_subprocess("/bin/sh", exit_args, output, sizeof(output), NULL, NULL), 23);
  ASCIICHAT_REQUIRE_STR_EQ(output, "");
  ASCIICHAT_REQUIRE_EQ(platform_execute_subprocess("/bin/sh", exit_args, NULL, 0, NULL, NULL), 23);
  const char *missing[] = {"missing", NULL};
  ASCIICHAT_REQUIRE_EQ(platform_execute_subprocess("/definitely-missing-ascii-chat-program", missing, output, sizeof(output),
                                           NULL, NULL,
               127);
  ASCIICHAT_REQUIRE_STR_EQ(output, "");
}
TEST_CASE("subprocess_contract/captured_output_has_exact_capacity_and_overflow_is_drained", "[subprocess_contract]") {
  const char *small[] = {"printf", "abcd", NULL};
  struct {
    char output[5];
    char guard;
  } buffer = {.guard = 'G'};
  ASCIICHAT_REQUIRE_EQ(platform_execute_subprocess("/usr/bin/printf", small, buffer.output, 5, NULL, NULL), 0);
  ASCIICHAT_REQUIRE_STR_EQ(buffer.output, "abcd");
  ASCIICHAT_REQUIRE_EQ(buffer.guard, 'G');
  ASCIICHAT_REQUIRE_EQ(platform_execute_subprocess("/usr/bin/printf", small, buffer.output, 4, NULL, NULL), -1);
  ASCIICHAT_REQUIRE_STR_EQ(buffer.output, "abc");
  ASCIICHAT_REQUIRE_EQ(buffer.guard, 'G');
  ASCIICHAT_REQUIRE_EQ(platform_execute_subprocess("/usr/bin/printf", small, buffer.output, 1, NULL, NULL), -1);
  ASCIICHAT_REQUIRE_STR_EQ(buffer.output, "");
  ASCIICHAT_REQUIRE_EQ(buffer.guard, 'G');
  const char *large[] = {"sh", "-c", "head -c 131072 /dev/zero", NULL};
  ASCIICHAT_REQUIRE_EQ(platform_execute_subprocess("/bin/sh", large, buffer.output, sizeof(buffer.output), NULL, NULL), -1);
  ASCIICHAT_REQUIRE_EQ(buffer.output[4], '\0');
  ASCIICHAT_REQUIRE_EQ(buffer.guard, 'G');
}
TEST_CASE("subprocess_contract/invalid_parameters_and_signal_termination_report_failure", "[subprocess_contract]") {
  const char *args[] = {"sh", "-c", "kill -TERM $$", NULL};
  char output[16];
  ASCIICHAT_REQUIRE_EQ(platform_execute_subprocess(NULL, args, output, sizeof(output), NULL, NULL), -1);
  ASCIICHAT_REQUIRE_EQ(platform_execute_subprocess("/bin/sh", NULL, output, sizeof(output), NULL, NULL), -1);
  const char *empty[] = {NULL};
  ASCIICHAT_REQUIRE_EQ(platform_execute_subprocess("/bin/sh", empty, output, sizeof(output), NULL, NULL), -1);
  ASCIICHAT_REQUIRE_EQ(platform_execute_subprocess("/bin/sh", args, output, sizeof(output), NULL, NULL), -1);
  ASCIICHAT_REQUIRE_EQ(platform_execute_subprocess("/bin/sh", args, NULL, 0, NULL, NULL), -1);
}
#endif
