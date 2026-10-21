#include "prepare_fixture.h"

int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  log_init(NULL, LOG_WARN, false, false);
  asciichat_errno_suppress(true);
  CHECK(socket_init() == ASCIICHAT_OK);
  websocket_fixture(argv[1]);
  buffer_system_cleanup();
  CHECK(buffer_process_reserved() == 0);
  socket_cleanup();
  log_destroy();
  return 0;
}
