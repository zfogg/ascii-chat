#include "errno_fixture.h"

int main(int argc, char **argv) {
  log_init(NULL, LOG_WARN, false, false);
  errno_suppress(true);
  if (argc > 1 && strcmp(argv[1], "--assert-leak") == 0) {
    asciichat_errno_scope_t scope = errno_scope_begin();
    SET_ERRNO(ERROR_NETWORK, "unresolved");
    ASSERT_NO_ERRNO_SINCE(scope);
    return 0;
  }
#ifndef _WIN32
  if (argc > 1 && strcmp(argv[1], "--signal-stacks") == 0)
    return pending_signal_probe();
#endif
  return 2; // Only explicit assertion/signal probes run outside Criterion.
}
