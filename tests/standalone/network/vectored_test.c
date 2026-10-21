#include "vectored_fixture.h"

int main(int argc, char **argv) {
  log_init(NULL, LOG_WARN, false, false);
  asciichat_errno_suppress(true);
  CHECK(socket_init() == ASCIICHAT_OK);
  if (argc > 1 && (strcmp(argv[1], "--serve") == 0 || strcmp(argv[1], "--peer") == 0)) {
    peer_mode(argc, argv);
    socket_cleanup();
    log_destroy();
    return 0;
  }
  if (argc < 2 || strcmp(argv[1], "--benchmark") != 0) {
    socket_cleanup();
    log_destroy();
    return 2;
  }
  bool assembled_only = argc > 2 && strcmp(argv[2], "assembled") == 0;
  bool vectored_only = argc > 2 && strcmp(argv[2], "vectored") == 0;
  size_t sizes[] = {0, 256, 4096, 65536, CRYPTO_MAX_PLAINTEXT_SIZE - sizeof(packet_header_t)};
  for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
    for (int encrypted = 0; encrypted < 2; ++encrypted) {
      if (!assembled_only)
        roundtrip(sizes[i], encrypted != 0, 100, false);
      if (!vectored_only)
        roundtrip(sizes[i], encrypted != 0, 100, true);
    }
  }
  socket_cleanup();
  log_warn("Vectored networking benchmark completed");
  log_destroy();
  return 0;
}
