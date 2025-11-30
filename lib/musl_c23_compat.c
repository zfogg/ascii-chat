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

// strtol family - string to integer conversion
// NOTE: Direct implementation to avoid infinite recursion with musl static builds
// where strtol might redirect back to __isoc23_strtol
long __isoc23_strtol(const char *str, char **endptr, int base) {
  // Simplified implementation for common cases (base 10 only for now)
  // This avoids calling strtol() which might cause infinite recursion
  if (!str) {
    if (endptr)
      *endptr = (char *)str;
    return 0;
  }

  // For non-base-10, fall back to strtoll and cast
  // (strtoll seems to work in musl static builds)
  if (base != 10) {
    long long result = strtoll(str, endptr, base);
    // Clamp to long range
    if (result > __LONG_MAX__)
      return __LONG_MAX__;
    if (result < -__LONG_MAX__ - 1)
      return -__LONG_MAX__ - 1;
    return (long)result;
  }

  // Manual base-10 parsing to avoid strtol() infinite loop
  const char *p = str;
  long sign = 1;
  long result = 0;

  // Skip whitespace
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
    p++;

  // Handle sign
  if (*p == '-') {
    sign = -1;
    p++;
  } else if (*p == '+') {
    p++;
  }

  // Convert digits
  int has_digits = 0;
  while (*p >= '0' && *p <= '9') {
    has_digits = 1;
    int digit = *p - '0';
    // Check for overflow
    if (result > (__LONG_MAX__ - digit) / 10) {
      if (endptr)
        *endptr = (char *)p;
      return sign > 0 ? __LONG_MAX__ : -__LONG_MAX__ - 1;
    }
    result = result * 10 + digit;
    p++;
  }

  if (endptr) {
    *endptr = (char *)(has_digits ? p : str);
  }

  return sign * result;
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
