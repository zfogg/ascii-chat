#include <ascii-chat/log/log.h>
#include <libplatform/terminal.h>
#include <libplatform/compiler.h>
#include <libplatform/system.h>
#include <libplatform/terminal.h>

bool terminal_should_force_stderr(void) {
  if (!terminal_is_piped_output())
    return false;
  const char *testing = platform_getenv("TESTING");
  return !(testing && testing[0] == '1' && testing[1] == '\0');
}

int terminal_choose_log_fd(int level) {
  if (log_get_force_stderr() || !terminal_is_interactive())
    return STDERR_FILENO;
  return level >= LOG_WARN ? STDERR_FILENO : STDOUT_FILENO;
}
