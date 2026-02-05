/**
 * @file util/utf8.c
 * @ingroup util
 * @brief 🔤 UTF-8 encoding and decoding with multi-byte character support
 *
 * Uses utf8proc Unicode library for accurate character-width computation
 * and UTF-8 handling.
 */

#include "ascii-chat/asciichat_errno.h"
#include "ascii-chat/common/error_codes.h"
#include <ascii-chat/util/utf8.h>
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
  if (!str) {
    SET_ERRNO(ERROR_INVALID_PARAM, "str is NULL");
    return 0;
  }

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
  if (!str || max_bytes == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "str is NULL or max_bytes is 0");
    return 0;
  }

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
    SET_ERRNO(ERROR_INVALID_PARAM, "str is NULL");
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
    SET_ERRNO(ERROR_INVALID_PARAM, "str is NULL");
    return false;
  }
  // Reuse utf8_char_count to validate without duplicating loop
  return utf8_char_count(str) != (size_t)-1;
}

bool utf8_is_ascii_only(const char *str) {
  if (!str) {
    SET_ERRNO(ERROR_INVALID_PARAM, "str is NULL");
    return false;
  }

  // Fast path: Check if all bytes are in ASCII range (0x00-0x7F)
  // ASCII characters are 0x00-0x7F (single byte, high bit clear)
  // Multi-byte UTF-8 has continuation bytes with high bit set (10xxxxxx pattern)
  // So if all bytes have high bit clear, it's guaranteed ASCII-only
  const unsigned char *p = (const unsigned char *)str;
  while (*p) {
    if ((*p & ~0x7F) != 0) {
      return false; // Non-ASCII byte found (high bit set)
    }
    p++;
  }
  return true;
}

size_t utf8_to_codepoints(const char *str, uint32_t *out_codepoints, size_t max_codepoints) {
  if (!str || !out_codepoints || max_codepoints == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "str is NULL or out_codepoints is NULL or max_codepoints is 0");
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
    SET_ERRNO(ERROR_INVALID_PARAM, "str is NULL or max_bytes is 0");
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

int utf8_read_and_insert_continuation_bytes(char *buffer, size_t *cursor, size_t *len, size_t max_len,
                                            int continuation_bytes, int (*read_byte_fn)(void)) {
  if (!buffer || !cursor || !len || continuation_bytes <= 0 || !read_byte_fn) {
    SET_ERRNO(ERROR_INVALID_PARAM, "invalid params");
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

/* ============================================================================
 * UTF-8 String Search Functions
 * ========================================================================== */

/**
 * @brief Case-insensitive substring search with full Unicode support
 *
 * Uses utf8proc for Unicode case folding according to Unicode standard.
 * This properly handles all Unicode scripts including Greek, Cyrillic,
 * accented characters, and more.
 */
const char *utf8_strcasestr(const char *haystack, const char *needle) {
  if (!haystack || !needle) {
    SET_ERRNO(ERROR_INVALID_PARAM, "invalid params");
    return NULL;
  }

  // Empty needle matches at start of haystack
  if (needle[0] == '\0') {
    return haystack;
  }

  // Get lengths
  size_t haystack_len = strlen(haystack);
  size_t needle_len = strlen(needle);

  if (needle_len > haystack_len) {
    return NULL;
  }

  // Case-fold both strings using utf8proc
  // UTF8PROC_CASEFOLD performs Unicode case folding
  // UTF8PROC_STABLE ensures stable output
  // UTF8PROC_COMPOSE normalizes composed characters
  utf8proc_option_t options = UTF8PROC_CASEFOLD | UTF8PROC_STABLE | UTF8PROC_COMPOSE;

  // Case-fold the needle (pattern to search for)
  utf8proc_uint8_t *needle_folded = NULL;
  utf8proc_ssize_t needle_folded_len =
      utf8proc_map((const utf8proc_uint8_t *)needle, (utf8proc_ssize_t)needle_len, &needle_folded, options);

  if (needle_folded_len < 0 || !needle_folded) {
    // Invalid UTF-8 in needle
    if (needle_folded) {
      free(needle_folded);
    }
    return NULL;
  }

  // Try each position in haystack
  const char *haystack_pos = haystack;
  while (*haystack_pos != '\0') {
    // Calculate remaining haystack length
    size_t remaining = haystack_len - (size_t)(haystack_pos - haystack);

    if (remaining < needle_len) {
      // Not enough characters left to match
      break;
    }

    // Case-fold the current haystack window
    utf8proc_uint8_t *haystack_folded = NULL;
    utf8proc_ssize_t haystack_folded_len =
        utf8proc_map((const utf8proc_uint8_t *)haystack_pos, (utf8proc_ssize_t)needle_len, &haystack_folded, options);

    if (haystack_folded_len >= 0 && haystack_folded) {
      // Compare case-folded strings
      if ((size_t)haystack_folded_len == (size_t)needle_folded_len &&
          memcmp(haystack_folded, needle_folded, (size_t)needle_folded_len) == 0) {
        // Match found!
        free(haystack_folded);
        free(needle_folded);
        return haystack_pos;
      }
      free(haystack_folded);
    }

    // Move to next UTF-8 character in haystack
    utf8proc_int32_t codepoint;
    utf8proc_ssize_t bytes = utf8proc_iterate((const utf8proc_uint8_t *)haystack_pos, -1, &codepoint);
    if (bytes <= 0) {
      // Invalid UTF-8, move by one byte
      haystack_pos++;
    } else {
      haystack_pos += bytes;
    }
  }

  free(needle_folded);
  return NULL;
}
