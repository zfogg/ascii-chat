/**
 * @file util/utf8.c
 * @ingroup util
 * @brief 🔤 UTF-8 encoding and decoding with multi-byte character support
 *
 * Uses utf8proc Unicode library for accurate character-width computation
 * and UTF-8 handling.
 */

#include "utf8.h"
#include <ascii-chat-deps/utf8proc/utf8proc.h>

int utf8_decode(const uint8_t *s, uint32_t *codepoint) {
  if (s[0] < 0x80) {
    *codepoint = s[0];
    return 1;
  }
  if ((s[0] & 0xE0) == 0xC0) {
    // Validate continuation byte
    if ((s[1] & 0xC0) != 0x80)
      return -1;
    *codepoint = (((uint32_t)(s[0] & 0x1F) << 6) | (uint32_t)(s[1] & 0x3F));
    return 2;
  } else if ((s[0] & 0xF0) == 0xE0) {
    // Validate continuation bytes
    if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80)
      return -1;
    *codepoint = (((uint32_t)(s[0] & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (uint32_t)(s[2] & 0x3F));
    return 3;
  } else if ((s[0] & 0xF8) == 0xF0) {
    // Validate continuation bytes
    if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80)
      return -1;
    *codepoint = (((uint32_t)(s[0] & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) | ((uint32_t)(s[2] & 0x3F) << 6) |
                  (uint32_t)(s[3] & 0x3F));
    return 4;
  }
  return -1; // Invalid
}

int utf8_display_width(const char *str) {
  if (!str)
    return 0;

  int width = 0;
  const utf8proc_uint8_t *p = (const utf8proc_uint8_t *)str;

  while (*p) {
    // Skip ANSI escape sequences (ESC [ ... m)
    // Pattern: \x1b [ [0-9;]* m
    if (p[0] == 0x1b && p[1] == '[') {
      // Found ANSI escape sequence start
      p += 2; // Skip ESC[
      // Skip digits and semicolons until we find 'm'
      while (*p && *p != 'm') {
        p++;
      }
      if (*p == 'm') {
        p++; // Skip the 'm'
      }
      continue;
    }

    utf8proc_int32_t codepoint;
    utf8proc_ssize_t len = utf8proc_iterate(p, -1, &codepoint);

    if (len <= 0) {
      // End of string or invalid UTF-8 sequence, stop processing
      break;
    }

    // Get display width of this codepoint
    int char_width = utf8proc_charwidth(codepoint);
    if (char_width < 0) {
      // Control character or unprintable - treat as 0 width
      char_width = 0;
    }
    width += char_width;
    p += len;
  }

  return width;
}

int utf8_display_width_n(const char *str, size_t max_bytes) {
  if (!str || max_bytes == 0)
    return 0;

  int width = 0;
  const utf8proc_uint8_t *p = (const utf8proc_uint8_t *)str;
  const utf8proc_uint8_t *end = p + max_bytes;

  while (p < end && *p) {
    // Skip ANSI escape sequences (ESC [ ... m)
    if (p + 1 < end && p[0] == 0x1b && p[1] == '[') {
      // Found ANSI escape sequence start
      p += 2; // Skip ESC[
      // Skip digits and semicolons until we find 'm'
      while (p < end && *p && *p != 'm') {
        p++;
      }
      if (p < end && *p == 'm') {
        p++; // Skip the 'm'
      }
      continue;
    }

    utf8proc_int32_t codepoint;
    utf8proc_ssize_t len = utf8proc_iterate(p, end - p, &codepoint);

    if (len <= 0) {
      // End of string or invalid UTF-8 sequence
      break;
    }

    // Get display width of this codepoint
    int char_width = utf8proc_charwidth(codepoint);
    if (char_width < 0) {
      // Control character or unprintable - treat as 0 width
      char_width = 0;
    }
    width += char_width;
    p += len;
  }

  return width;
}
