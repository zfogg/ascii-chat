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

// Declare musl's actual strto* functions using asm labels to bypass glibc's C23
// header redirect (e.g. strtoul → __isoc23_strtoul), which would cause infinite
// recursion since this file IS the __isoc23_* implementation.
extern long         musl_strtol(const char *, char **, int) __asm__("strtol");
extern long long    musl_strtoll(const char *, char **, int) __asm__("strtoll");
extern unsigned long musl_strtoul(const char *, char **, int) __asm__("strtoul");
extern unsigned long long musl_strtoull(const char *, char **, int) __asm__("strtoull");

/**
 * @brief Convert string to long (C23 compatible)
 */
long __isoc23_strtol(const char *str, char **endptr, int base) {
  return musl_strtol(str, endptr, base);
}

/**
 * @brief Convert string to long long (C23 compatible)
 */
long long __isoc23_strtoll(const char *str, char **endptr, int base) {
  return musl_strtoll(str, endptr, base);
}

/**
 * @brief Convert string to unsigned long (C23 compatible)
 */
unsigned long __isoc23_strtoul(const char *str, char **endptr, int base) {
  return musl_strtoul(str, endptr, base);
}

/**
 * @brief Convert string to unsigned long long (C23 compatible)
 */
unsigned long long __isoc23_strtoull(const char *str, char **endptr, int base) {
  return musl_strtoull(str, endptr, base);
}

/** @ingroup util
 *  @brief Wide string to integer conversion
 */

/**
 * @brief Convert wide string to long (C23 compatible)
 * @param str Wide string to convert
 * @param endptr Pointer to first non-digit character (optional)
 * @param base Numeric base (0-36)
 * @return Converted value
 */
long __isoc23_wcstol(const wchar_t *str, wchar_t **endptr, int base) {
  return wcstol(str, endptr, base);
}

/**
 * @brief Convert wide string to long long (C23 compatible)
 * @param str Wide string to convert
 * @param endptr Pointer to first non-digit character (optional)
 * @param base Numeric base (0-36)
 * @return Converted value
 */
long long __isoc23_wcstoll(const wchar_t *str, wchar_t **endptr, int base) {
  return wcstoll(str, endptr, base);
}

/**
 * @brief Convert wide string to unsigned long (C23 compatible)
 * @param str Wide string to convert
 * @param endptr Pointer to first non-digit character (optional)
 * @param base Numeric base (0-36)
 * @return Converted value
 */
unsigned long __isoc23_wcstoul(const wchar_t *str, wchar_t **endptr, int base) {
  return wcstoul(str, endptr, base);
}

/**
 * @brief Convert wide string to unsigned long long (C23 compatible)
 * @param str Wide string to convert
 * @param endptr Pointer to first non-digit character (optional)
 * @param base Numeric base (0-36)
 * @return Converted value
 */
unsigned long long __isoc23_wcstoull(const wchar_t *str, wchar_t **endptr, int base) {
  return wcstoull(str, endptr, base);
}

/** @ingroup util
 *  @brief Formatted input functions
 */

/**
 * @brief Read formatted input from stdin (C23 compatible)
 * @param format Format string
 * @param ... Variable arguments
 * @return Number of successfully read items
 */
int __isoc23_scanf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = vscanf(format, args);
  va_end(args);
  return result;
}

/**
 * @brief Read formatted input from file stream (C23 compatible)
 * @param stream File stream
 * @param format Format string
 * @param ... Variable arguments
 * @return Number of successfully read items
 */
int __isoc23_fscanf(FILE *stream, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = vfscanf(stream, format, args);
  va_end(args);
  return result;
}

/**
 * @brief Read formatted input from string (C23 compatible)
 * @param str Input string
 * @param format Format string
 * @param ... Variable arguments
 * @return Number of successfully read items
 */
int __isoc23_sscanf(const char *str, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = vsscanf(str, format, args);
  va_end(args);
  return result;
}

/**
 * @brief Read formatted input from stdin (va_list variant, C23 compatible)
 * @param format Format string
 * @param args Variable argument list
 * @return Number of successfully read items
 */
int __isoc23_vscanf(const char *format, va_list args) {
  return vscanf(format, args);
}

/**
 * @brief Read formatted input from file stream (va_list variant, C23 compatible)
 * @param stream File stream
 * @param format Format string
 * @param args Variable argument list
 * @return Number of successfully read items
 */
int __isoc23_vfscanf(FILE *stream, const char *format, va_list args) {
  return vfscanf(stream, format, args);
}

/**
 * @brief Read formatted input from string (va_list variant, C23 compatible)
 * @param str Input string
 * @param format Format string
 * @param args Variable argument list
 * @return Number of successfully read items
 */
int __isoc23_vsscanf(const char *str, const char *format, va_list args) {
  return vsscanf(str, format, args);
}

/** @ingroup util
 *  @brief Wide character formatted input functions
 */

/**
 * @brief Read formatted wide input from stdin (C23 compatible)
 * @param format Format string
 * @param ... Variable arguments
 * @return Number of successfully read items
 */
int __isoc23_wscanf(const wchar_t *format, ...) {
  va_list args;
  va_start(args, format);
  int result = vwscanf(format, args);
  va_end(args);
  return result;
}

/**
 * @brief Read formatted wide input from file stream (C23 compatible)
 * @param stream File stream
 * @param format Format string
 * @param ... Variable arguments
 * @return Number of successfully read items
 */
int __isoc23_fwscanf(FILE *stream, const wchar_t *format, ...) {
  va_list args;
  va_start(args, format);
  int result = vfwscanf(stream, format, args);
  va_end(args);
  return result;
}

/**
 * @brief Read formatted wide input from string (C23 compatible)
 * @param str Input string
 * @param format Format string
 * @param ... Variable arguments
 * @return Number of successfully read items
 */
int __isoc23_swscanf(const wchar_t *str, const wchar_t *format, ...) {
  va_list args;
  va_start(args, format);
  int result = vswscanf(str, format, args);
  va_end(args);
  return result;
}

/**
 * @brief Read formatted wide input from stdin (va_list variant, C23 compatible)
 * @param format Format string
 * @param args Variable argument list
 * @return Number of successfully read items
 */
int __isoc23_vwscanf(const wchar_t *format, va_list args) {
  return vwscanf(format, args);
}

/**
 * @brief Read formatted wide input from file stream (va_list variant, C23 compatible)
 * @param stream File stream
 * @param format Format string
 * @param args Variable argument list
 * @return Number of successfully read items
 */
int __isoc23_vfwscanf(FILE *stream, const wchar_t *format, va_list args) {
  return vfwscanf(stream, format, args);
}

/**
 * @brief Read formatted wide input from string (va_list variant, C23 compatible)
 * @param str Input string
 * @param format Format string
 * @param args Variable argument list
 * @return Number of successfully read items
 */
int __isoc23_vswscanf(const wchar_t *str, const wchar_t *format, va_list args) {
  return vswscanf(str, format, args);
}

/** @ingroup util
 *  @brief glibc-specific random functions for expat and fontconfig compatibility
 */

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>

/**
 * @brief Fill buffer with cryptographically secure random bytes
 *
 * @details Used by expat for hash seeding and other security-critical operations.
 * Attempts getrandom syscall first (Linux 3.17+), falls back to /dev/urandom.
 *
 * @param buf    Output buffer (must not be NULL if nbytes > 0)
 * @param nbytes Number of random bytes to generate
 */
void arc4random_buf(void *buf, size_t nbytes) {
  if (nbytes == 0 || buf == NULL) {
    return;
  }

/* Prefer getrandom syscall: available in Linux 3.17+, no file descriptor needed */
#ifdef SYS_getrandom
  long result = syscall(SYS_getrandom, buf, nbytes, 0);
  if (result == (long)nbytes) {
    return;
  }
#endif

  /* Fallback to /dev/urandom device */
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd >= 0) {
    ssize_t n = read(fd, buf, nbytes);
    close(fd);
    if (n == (ssize_t)nbytes) {
      return;
    }
  }

  /* Fatal error: unable to obtain random bytes */
  abort();
}

/**
 * @brief Initialize reentrant random state (glibc compatibility stub for musl)
 *
 * @details musl doesn't provide this glibc-specific reentrant random function.
 * Used by fontconfig for thread-local hash seeding during initialization.
 *
 * ## Design Notes
 *
 * This is a stub implementation (doesn't maintain stateful PRNG state) for these reasons:
 * - fontconfig only calls this once per thread during startup (not in hot loops)
 * - The performance difference between a syscall and PRNG state lookup is negligible
 *   at initialization time
 * - Implementing a correct PRNG is complex; using kernel randomness is simpler and
 *   more reliable than maintaining application-level state
 *
 * ## Why the state buffer is not used
 *
 * - glibc's stateful design (storing PRNG state in statebuf) is an optimization
 *   for applications that call random_r() thousands of times
 * - fontconfig doesn't fit that pattern; it just needs a few random values at init
 * - Maintaining thread-local state would require either TLS allocation or synchronization
 * - Not worth the complexity for initialization-time randomness
 *
 * ## Thread Safety
 *
 * Thread-safe by design:
 * - Each thread can have its own buf/statebuf
 * - Each thread's random_r() calls arc4random_buf(), which is atomic at kernel level
 * - No shared mutable state means no synchronization needed
 *
 * @param seed       Seed value (ignored; random_r() uses arc4random_buf())
 * @param statebuf   State buffer (validated but not used)
 * @param statelen   Length of state buffer (must be >= 32)
 * @param buf        Random data structure (validated but not used)
 * @return 0 on success, -1 if parameters invalid
 */
int initstate_r(unsigned int seed, char *statebuf, size_t statelen, struct random_data *buf) {
  (void)seed;
  // Validate parameters match glibc interface
  if (!statebuf || !buf || statelen < 32) {
    return -1;
  }

  // Initialize buffer (though random_r() doesn't actually use it)
  memset(statebuf, 0, statelen);

  return 0;
}

/**
 * @brief Generate cryptographically secure random number (glibc compatibility stub for musl)
 *
 * @details musl doesn't provide this glibc-specific reentrant random function.
 * Used by fontconfig to generate thread-safe random numbers without shared state.
 *
 * ## Design
 *
 * - Ignores the state buffer (buf) entirely
 * - Each call invokes arc4random_buf() to get fresh random bytes from kernel
 * - Thread-safe because arc4random_buf() uses atomic syscalls/device reads
 * - No per-thread PRNG state to maintain or synchronize
 *
 * ## Why stateless design
 *
 * - fontconfig's usage pattern doesn't warrant state management (see initstate_r())
 * - Stateless design trades a syscall per call for simplicity and correctness
 * - Kernel randomness is reliable; application-level PRNG state would need testing
 * - See initstate_r() for full rationale on not maintaining state
 *
 * @param buf    Random data structure (ignored; present for glibc API compatibility)
 * @param result Pointer to store generated random value in range [0, 2^31)
 * @return 0 on success, -1 if buf or result is NULL
 */
int random_r(struct random_data *restrict buf, int32_t *restrict result) {
  if (buf == NULL || result == NULL) {
    return -1;
  }

  /* Obtain cryptographically secure random bytes from kernel */
  uint32_t random_bytes;
  arc4random_buf(&random_bytes, sizeof(random_bytes));

  /* Convert to 31-bit positive integer matching glibc behavior
     glibc's random_r returns values in range [0, 2^31) */
  *result = (int32_t)((random_bytes >> 1) & 0x7FFFFFFF);

  return 0;
}
