/**
 * @file version.c
 * @ingroup util
 * @brief 🏷️ Binary-embedded version information in custom ELF/Mach-O sections for runtime inspection
 */

#include <ascii-chat/version.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>

// GCC/Clang: Use __attribute__((section)) to place string in custom section
// ELF (Linux): .section_name format
// Mach-O (macOS): __SEGMENT,__section format
#if defined(__GNUC__) || defined(__clang__)

#if defined(__APPLE__)
// macOS Mach-O format: use __TEXT segment for read-only data
__attribute__((used, section("__TEXT,__ascii_chat"))) const char ascii_chat_custom_section[] =
    ASCII_CHAT_DOT_ASCII_CHAT_STRING;
__attribute__((used, section("__TEXT,__comment"))) const char ascii_chat_comment_string[] =
    ASCII_CHAT_DOT_COMMENT_STRING;
__attribute__((used, section("__TEXT,__version"))) const char ascii_chat_version_string[] = ASCII_CHAT_VERSION_FULL;
#define ASCII_CHAT_VERSION_GETTERS

#elif defined(__linux__)
// Linux ELF format: standard .section_name format
__attribute__((used, section(".ascii_chat"))) const char ascii_chat_custom_section[] = ASCII_CHAT_DOT_ASCII_CHAT_STRING;
__attribute__((used, section(".comment"))) const char ascii_chat_comment_string[] = ASCII_CHAT_DOT_COMMENT_STRING;
__attribute__((used, section(".version"))) const char ascii_chat_version_string[] = ASCII_CHAT_VERSION_FULL;
#define ASCII_CHAT_VERSION_GETTERS
#endif

#ifdef ASCII_CHAT_VERSION_GETTERS
// Provide functions to get version info at runtime
const char *ascii_chat_get_version(void) {
  return ascii_chat_version_string;
}
const char *ascii_chat_get_comment(void) {
  return ascii_chat_comment_string;
}
#endif

#endif

semantic_version_t version_parse(const char *version_string) {
  semantic_version_t result = {0, 0, 0, false};

  if (!version_string || version_string[0] == '\0') {
    return result;
  }

  const char *p = version_string;

  // Optionally skip 'v' prefix if present
  if (p[0] == 'v' || p[0] == 'V') {
    p++; // Skip 'v'
  }

  // Parse major version
  char *endptr = NULL;
  errno = 0; // Reset errno before strtol
  long major = strtol(p, &endptr, 10);
  if (errno == ERANGE || endptr == p || major < 0 || major > INT_MAX) {
    return result; // Invalid major version or overflow
  }
  result.major = (int)major;
  p = endptr;

  // Version must have all three components (major.minor.patch)
  if (*p != '.') {
    return result; // Invalid: missing minor version
  }
  p++; // Skip dot

  // Parse minor version
  errno = 0; // Reset errno before strtol
  long minor = strtol(p, &endptr, 10);
  if (errno == ERANGE || endptr == p || minor < 0 || minor > INT_MAX) {
    return result; // Invalid minor version or overflow
  }
  result.minor = (int)minor;
  p = endptr;

  // Version must have patch component
  if (*p != '.') {
    return result; // Invalid: missing patch version
  }
  p++; // Skip dot

  // Parse patch version
  errno = 0; // Reset errno before strtol
  long patch = strtol(p, &endptr, 10);
  if (errno == ERANGE || endptr == p || patch < 0 || patch > INT_MAX) {
    return result; // Invalid patch version or overflow
  }
  result.patch = (int)patch;
  p = endptr;

  // Reject prerelease tags (e.g., "v1.2.3-beta") - must end after patch number
  if (*p != '\0') {
    return result; // Invalid: has extra characters after version
  }

  result.valid = true;
  return result;
}

int version_compare(semantic_version_t a, semantic_version_t b) {
  // Compare major version
  if (a.major < b.major) {
    return -1;
  }
  if (a.major > b.major) {
    return 1;
  }

  // Major versions equal, compare minor
  if (a.minor < b.minor) {
    return -1;
  }
  if (a.minor > b.minor) {
    return 1;
  }

  // Minor versions equal, compare patch
  if (a.patch < b.patch) {
    return -1;
  }
  if (a.patch > b.patch) {
    return 1;
  }

  // All components equal
  return 0;
}
