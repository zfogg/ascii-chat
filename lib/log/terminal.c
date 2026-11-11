#include <ascii-chat/log/log.h>
#include <libplatform/terminal.h>
#include <libplatform/compiler.h>
#include <libplatform/system.h>
#include <libplatform/terminal.h>

int terminal_choose_log_fd(int level) {
  if (log_get_force_stderr() || !terminal_is_interactive())
    return STDERR_FILENO;
  return level >= LOG_WARN ? STDERR_FILENO : STDOUT_FILENO;
}
