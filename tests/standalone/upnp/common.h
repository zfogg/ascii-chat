#ifndef TEST_UPNP_COMMON_H
#define TEST_UPNP_COMMON_H
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
typedef enum { ASCIICHAT_OK, ERROR_NETWORK, ERROR_MEMORY, ERROR_INVALID_PARAM } asciichat_error_t;
#define NS_PER_SEC_INT 1000000000ULL
#define NS_PER_MS_INT 1000000ULL
static inline asciichat_error_t test_errno(asciichat_error_t code, const char *format, ...) {
  (void)format;
  return code;
}
#define SET_ERRNO test_errno
#define SAFE_CALLOC(n, size, type) ((type)calloc(n, size))
#define SAFE_FREE(p)                                                                                                   \
  do {                                                                                                                 \
    free(p);                                                                                                           \
    (p) = NULL;                                                                                                        \
  } while (0)
#define SAFE_STRNCPY(dst, src, n) snprintf(dst, n, "%s", src)
#define SAFE_STRDUP(dst, src)                                                                                          \
  do {                                                                                                                 \
    size_t len = strlen(src) + 1;                                                                                      \
    dst = malloc(len);                                                                                                 \
    if (dst)                                                                                                           \
      memcpy(dst, src, len);                                                                                           \
  } while (0)
#define safe_snprintf snprintf
static inline void test_log(const char *format, ...) {
  (void)format;
}
#define log_info test_log
#define log_debug test_log
#define log_warn test_log
#define LOG_ERRNO_IF_SET(...) ((void)0)
uint64_t time_get_ns(void);
void time_sleep_ns(uint64_t ns);

#endif
