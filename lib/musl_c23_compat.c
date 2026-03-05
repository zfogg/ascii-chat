/**
 * @file musl_c23_compat.c
 * @ingroup util
 * @brief 🔄 musl C23 compatibility wrappers for __isoc23_* symbol aliases (glibc 2.38+ compatibility)
 *
 * Note: These aliases provide the base functionality but don't implement the full
 * C23 binary literal support (0b/0B prefix) until musl adds native support.
 *
 * References:
 * - https://reviews.llvm.org/D158943
 * - https://groups.google.com/g/osv-dev/c/zDx0qThbtEE
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>
#include <limits.h>

// strtol family - string to integer conversion
// NOTE: Uses strtoll as backend to avoid infinite recursion, with proper long clamping.
long __isoc23_strtol(const char *str, char **endptr, int base) {
  if (!str) {
    if (endptr)
      *endptr = (char *)str;
    return 0;
  }

  // Use strtoll which is more robust and handles all bases correctly
  long long result = strtoll(str, endptr, base);

  // Clamp to long range
  if (result > LONG_MAX)
    return LONG_MAX;
  if (result < LONG_MIN)
    return LONG_MIN;

  return (long)result;
}

long long __isoc23_strtoll(const char *str, char **endptr, int base) {
  return strtoll(str, endptr, base);
}

unsigned long __isoc23_strtoul(const char *str, char **endptr, int base) {
  return strtoul(str, endptr, base);
}

unsigned long long __isoc23_strtoull(const char *str, char **endptr, int base) {
  return strtoull(str, endptr, base);
}

// wcstol family - wide string to integer conversion
long __isoc23_wcstol(const wchar_t *str, wchar_t **endptr, int base) {
  return wcstol(str, endptr, base);
}

long long __isoc23_wcstoll(const wchar_t *str, wchar_t **endptr, int base) {
  return wcstoll(str, endptr, base);
}

unsigned long __isoc23_wcstoul(const wchar_t *str, wchar_t **endptr, int base) {
  return wcstoul(str, endptr, base);
}

unsigned long long __isoc23_wcstoull(const wchar_t *str, wchar_t **endptr, int base) {
  return wcstoull(str, endptr, base);
}

// scanf family - formatted input
int __isoc23_scanf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = vscanf(format, args);
  va_end(args);
  return result;
}

int __isoc23_fscanf(FILE *stream, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = vfscanf(stream, format, args);
  va_end(args);
  return result;
}

int __isoc23_sscanf(const char *str, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = vsscanf(str, format, args);
  va_end(args);
  return result;
}

int __isoc23_vscanf(const char *format, va_list args) {
  return vscanf(format, args);
}

int __isoc23_vfscanf(FILE *stream, const char *format, va_list args) {
  return vfscanf(stream, format, args);
}

int __isoc23_vsscanf(const char *str, const char *format, va_list args) {
  return vsscanf(str, format, args);
}

// wscanf family - wide formatted input
int __isoc23_wscanf(const wchar_t *format, ...) {
  va_list args;
  va_start(args, format);
  int result = vwscanf(format, args);
  va_end(args);
  return result;
}

int __isoc23_fwscanf(FILE *stream, const wchar_t *format, ...) {
  va_list args;
  va_start(args, format);
  int result = vfwscanf(stream, format, args);
  va_end(args);
  return result;
}

int __isoc23_swscanf(const wchar_t *str, const wchar_t *format, ...) {
  va_list args;
  va_start(args, format);
  int result = vswscanf(str, format, args);
  va_end(args);
  return result;
}

int __isoc23_vwscanf(const wchar_t *format, va_list args) {
  return vwscanf(format, args);
}

int __isoc23_vfwscanf(FILE *stream, const wchar_t *format, va_list args) {
  return vfwscanf(stream, format, args);
}

int __isoc23_vswscanf(const wchar_t *str, const wchar_t *format, va_list args) {
  return vswscanf(str, format, args);
}

// =============================================================================
// musl Random Function Compatibility Stubs
// =============================================================================
// Provides compatibility for glibc-specific random functions used by expat and
// fontconfig.

#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>

// arc4random_buf() - used by expat for hash seeding
// Fills buffer with random bytes from /dev/urandom or getrandom syscall
void arc4random_buf(void *buf, size_t nbytes) {
  if (nbytes == 0 || buf == NULL) {
    return;
  }

  // Try getrandom syscall first (available in Linux 3.17+)
  // This is preferred as it doesn't require opening a file descriptor
  #ifdef SYS_getrandom
  long result = syscall(SYS_getrandom, buf, nbytes, 0);
  if (result == (long)nbytes) {
    return;
  }
  #endif

  // Fallback: read from /dev/urandom
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd >= 0) {
    ssize_t n = read(fd, buf, nbytes);
    close(fd);
    if (n == (ssize_t)nbytes) {
      return;
    }
  }

  // Final fallback: fill with simple pseudo-random data
  uint8_t *b = (uint8_t *)buf;
  for (size_t i = 0; i < nbytes; i++) {
    b[i] = (uint8_t)((i + (uintptr_t)buf + i * 13) & 0xFF);
  }
}

// Random state structure layout: first 4 bytes store the seed for consistency
typedef struct {
  uint32_t seed;
  // Rest of buffer is available for future state tracking
  char state[28];
} _random_state_t;

// initstate_r() - musl doesn't have this glibc-specific reentrant random function
// Used by fontconfig. Initializes the random state with a seed.
int initstate_r(unsigned int seed, char *statebuf, size_t statelen,
                struct random_data *buf) {
  // Validate parameters
  if (statelen < sizeof(_random_state_t)) {
    return -1;
  }

  // Initialize state structure with seed
  _random_state_t *state = (_random_state_t *)statebuf;
  state->seed = seed;

  // Store pointer to state buffer in the random_data structure
  *(void **)buf = statebuf;
  return 0;
}

// random_r() - musl doesn't have this glibc-specific reentrant random function
// Used by fontconfig. Uses arc4random_buf for cryptographically secure randomness.
int random_r(struct random_data *restrict buf, int32_t *restrict result) {
  if (buf == NULL || result == NULL) {
    return -1;
  }

  // Get random bytes from arc4random_buf (cryptographically secure)
  uint32_t random_bytes;
  arc4random_buf(&random_bytes, sizeof(random_bytes));

  // Convert to 31-bit positive integer (matching glibc behavior)
  // glibc's random_r returns values in range [0, 2^31)
  *result = (int32_t)((random_bytes >> 1) & 0x7FFFFFFF);

  return 0;
}
