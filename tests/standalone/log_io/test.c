#include <ascii-chat/log/io.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#define REAL_DUP2 _dup2
#else
#include <unistd.h>
#define REAL_DUP2 dup2
#endif

static size_t captured_x;
static int fail_redirect;
static int redirects;

int capture_test_dup2(int from, int to) {
  if (++redirects == fail_redirect)
    return -1;
  return REAL_DUP2(from, to);
}

void capture_test_log(const char *format, ...) {
  char buffer[16384];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  for (const char *p = buffer; *p; ++p)
    captured_x += *p == 'X';
}

int main(void) {
  char block[256];
  memset(block, 'X', sizeof(block));
  // More than any normal pipe capacity, written before stop can drain capture.
  for (int run = 0; run < 4; ++run) {
    log_io_t capture = log_io_start();
    if (!capture.output)
      return 1;
    for (int i = 0; i < 1024; ++i) {
      if (fwrite(block, 1, sizeof(block), stdout) != sizeof(block) ||
          fwrite(block, 1, sizeof(block), stderr) != sizeof(block))
        return 2;
    }
    log_io_stop(capture, "backend");
  }
  if (captured_x != 2 * 1024 * 1024)
    return 3;
  // The second dup2 fails: stdout must be rolled back before returning.
  fail_redirect = redirects + 2;
  log_io_t failed = log_io_start();
  if (failed.output || failed.saved_stdout_fd != -1 || failed.saved_stderr_fd != -1)
    return 4;
  log_io_stop(failed, "unused");
  fputs("stdout restored\n", stdout);
  fputs("stderr restored\n", stderr);
  return 0;
}
