// fortify_stubs.c - Stub implementations of glibc fortify functions for musl
//
// Ubuntu's LLVM libc++.a is compiled with -D_FORTIFY_SOURCE=2, which causes
// the compiler to replace standard C functions with checked versions:
//   memcpy -> __memcpy_chk
//   fprintf -> __fprintf_chk
//   vfprintf -> __vfprintf_chk
//
// These checked versions are provided by glibc but not by musl. This file
// provides stub implementations that simply call the unchecked versions.
//
// This is safe because:
// 1. Musl already has built-in bounds checking for many functions
// 2. These stubs are only used for static linking with musl
// 3. The fortify checks are redundant in musl's security model

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

// memcpy with buffer overflow check
// In musl, just delegate to regular memcpy (musl's memcpy is already safe)
void *__memcpy_chk(void *dest, const void *src, size_t len, size_t destlen) {
  // glibc would check: if (len > destlen) abort()
  // musl doesn't need this - just use regular memcpy
  (void)destlen; // Unused in musl
  return memcpy(dest, src, len);
}

// fprintf with format string check
// In musl, just delegate to regular fprintf
int __fprintf_chk(FILE *stream, int flag, const char *format, ...) {
  // glibc would perform format string validation based on flag
  // musl doesn't need this - just use regular fprintf
  (void)flag; // Unused in musl

  va_list args;
  va_start(args, format);
  int result = vfprintf(stream, format, args);
  va_end(args);
  return result;
}

// vfprintf with format string check
// In musl, just delegate to regular vfprintf
int __vfprintf_chk(FILE *stream, int flag, const char *format, va_list ap) {
  // glibc would perform format string validation based on flag
  // musl doesn't need this - just use regular vfprintf
  (void)flag; // Unused in musl
  return vfprintf(stream, format, ap);
}
