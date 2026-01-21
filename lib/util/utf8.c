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
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

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

size_t utf8_char_count(const char *str) {
  if (!str) {
    return -1; // SIZE_MAX
  }

  size_t count = 0;
  const uint8_t *p = (const uint8_t *)str;
  while (*p) {
    uint32_t codepoint;
    int decode_len = utf8_decode(p, &codepoint);
    if (decode_len < 0) {
      return -1; // SIZE_MAX - Invalid UTF-8
    }
    count++;
    p += decode_len;
  }
  return count;
}

bool utf8_is_valid(const char *str) {
  if (!str) {
    return false;
  }
  // Reuse utf8_char_count to validate without duplicating loop
  return utf8_char_count(str) != (size_t)-1;
}

bool utf8_is_ascii_only(const char *str) {
  if (!str) {
    return false;
  }

  // Decode codepoints and check they're all ASCII (0-127)
  const uint8_t *p = (const uint8_t *)str;
  while (*p) {
    uint32_t codepoint;
    int decode_len = utf8_decode(p, &codepoint);
    if (decode_len < 0) {
      return false; // Invalid UTF-8 sequence
    }
    if (codepoint > 127) {
      return false; // Non-ASCII character found
    }
    p += decode_len;
  }
  return true;
}

size_t utf8_to_codepoints(const char *str, uint32_t *out_codepoints, size_t max_codepoints) {
  if (!str || !out_codepoints || max_codepoints == 0) {
    return 0;
  }

  size_t count = 0;
  const uint8_t *p = (const uint8_t *)str;
  while (*p && count < max_codepoints) {
    uint32_t codepoint;
    int decode_len = utf8_decode(p, &codepoint);
    if (decode_len < 0) {
      return -1; // SIZE_MAX - Invalid UTF-8
    }
    out_codepoints[count++] = codepoint;
    p += decode_len;
  }
  return count;
}

int utf8_next_char_bytes(const char *str, size_t max_bytes) {
  if (!str || max_bytes == 0) {
    return -1;
  }

  // Use utf8proc_iterate to get byte length of next character
  utf8proc_int32_t codepoint;
  utf8proc_ssize_t len = utf8proc_iterate((const utf8proc_uint8_t *)str, (utf8proc_ssize_t)max_bytes, &codepoint);

  if (len <= 0) {
    return -1; // Invalid UTF-8 or end of string
  }

  return (int)len;
}

int utf8_continuation_bytes_needed(unsigned char first_byte) {
  if ((first_byte & 0x80) == 0) {
    return 0; // ASCII (0xxxxxxx) - single byte, no continuation needed
  }
  if ((first_byte & 0xE0) == 0xC0) {
    return 1; // 110xxxxx - 2 byte sequence, 1 continuation byte needed
  }
  if ((first_byte & 0xF0) == 0xE0) {
    return 2; // 1110xxxx - 3 byte sequence, 2 continuation bytes needed
  }
  if ((first_byte & 0xF8) == 0xF0) {
    return 3; // 11110xxx - 4 byte sequence, 3 continuation bytes needed
  }
  return -1; // Invalid UTF-8 start byte
}

int utf8_read_and_insert_continuation_bytes(char *buffer, size_t *cursor, size_t *len,
                                             size_t max_len, int continuation_bytes,
                                             int (*read_byte_fn)(void)) {
  if (!buffer || !cursor || !len || continuation_bytes <= 0 || !read_byte_fn) {
    return -1;
  }

  for (int i = 0; i < continuation_bytes && *len < max_len - 1; i++) {
    int next_byte = read_byte_fn();
    if (next_byte == EOF) {
      return -1; // EOF reached
    }

    // Shift characters right to make room
    memmove(&buffer[*cursor + 1], &buffer[*cursor], *len - *cursor);
    buffer[*cursor] = (char)next_byte;
    (*len)++;
    (*cursor)++;
  }

  return 0;
}
