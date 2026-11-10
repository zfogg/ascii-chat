/** Low-level sanitizer report sink. */
#include <ascii-chat/log/asan.h>
#include <ascii-chat/atomic.h>
#include <libplatform/filesystem.h>
#include <string.h>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define HAS_ASAN 1
#endif
#endif
#if !defined(HAS_ASAN) && defined(__SANITIZE_ADDRESS__)
#define HAS_ASAN 1
#endif

#ifdef HAS_ASAN
#include <sanitizer/asan_interface.h>

static atomic_t g_sanitizer_fd = {.impl = UINT64_MAX};

static void asan_error_report_callback(const char *report) {
  const int fd = atomic_load_int(&g_sanitizer_fd);
  if (fd >= 0 && report) {
    (void)platform_write(fd, "\n[Sanitizer report]\n", sizeof("\n[Sanitizer report]\n") - 1);
    (void)platform_write(fd, report, strlen(report));
    (void)platform_write(fd, "\n", 1);
  }
}

__attribute__((constructor)) static void init_asan_error_reporting(void) {
  __asan_set_error_report_callback(asan_error_report_callback);
}

void sanitizer_log_init(const char *filename) {
  if (!filename || !*filename)
    return;
  const int fd = platform_open("sanitizer_log", filename, PLATFORM_O_WRONLY | PLATFORM_O_APPEND | PLATFORM_O_CREAT,
                               FILE_PERM_PRIVATE);
  if (fd >= 0)
    atomic_store_int(&g_sanitizer_fd, fd);
}

void sanitizer_log_destroy(void) {
  const int fd = atomic_exchange_int(&g_sanitizer_fd, -1);
  if (fd >= 0)
    (void)platform_close(fd);
}
#else
void sanitizer_log_init(const char *filename) {
  (void)filename;
}
void sanitizer_log_destroy(void) {}
#endif
