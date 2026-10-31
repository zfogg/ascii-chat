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
#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat-deps/utf8proc/utf8proc.h>
#include <ascii-chat/video/terminal/ansi.h>
#include <limits.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

bool utf8_output_enabled(void) {
  utf8_setting_t setting = GET_OPTION(force_utf8);
  return setting == UTF8_SETTING_TRUE || (setting == UTF8_SETTING_AUTO && terminal_supports_utf8());
}

int utf8_decode(const uint8_t *s, uint32_t *codepoint) {
  utf8proc_int32_t decoded;
  utf8proc_ssize_t count = utf8proc_iterate(s, -1, &decoded);
  if (count < 0)
    return -1;
  *codepoint = (uint32_t)decoded;
  return (int)count;
}

// Width measurement and truncation share the same bounded token walk.
static size_t utf8_prefix_scan(const char *str, size_t max_bytes, int max_width, int *width) {
  *width = 0;
  if (!str || max_bytes == 0 || max_width < 0) {
    return 0;
  }

  const char *p = str;
  const char *end = str + max_bytes;
  int previous_width = 0;
  while (p < end && *p) {
    if (*p == '\x1b') {
      const char *next = ansi_skip_escape(p, end);
      // Only complete CSI sequences are accepted; never emit a partial escape.
      if (next - p < 3 || p[1] != '[' || next[-1] < '@' || next[-1] > '~') {
        break;
      }
      if (next[-1] == 'b') {
        unsigned repeats = 0;
        const char *q = p + 2;
        while (q < next - 1 && *q >= '0' && *q <= '9') {
          unsigned digit = (unsigned)(*q++ - '0');
          if (repeats > (UINT_MAX - digit) / 10)
            return (size_t)(p - str);
          repeats = repeats * 10 + digit;
        }
        if (q != next - 1 || previous_width <= 0)
          break;
        if (!repeats)
          repeats = 1;
        if (repeats > (unsigned)((max_width - *width) / previous_width))
          break;
        *width += (int)repeats * previous_width;
      }
      p = next;
      continue;
    }

    utf8proc_int32_t codepoint;
    utf8proc_ssize_t len = utf8proc_iterate((const utf8proc_uint8_t *)p, end - p, &codepoint);
    if (len <= 0) {
      break;
    }
    int char_width = utf8proc_charwidth(codepoint);
    if (char_width < 0) {
      char_width = 0;
    }
    if (char_width > max_width - *width) {
      break;
    }
    *width += char_width;
    previous_width = char_width;
    p += len;
  }
  return (size_t)(p - str);
}

size_t utf8_prefix_bytes_for_width(const char *str, size_t max_bytes, int max_width) {
  int width;
  return utf8_prefix_scan(str, max_bytes, max_width, &width);
}

int utf8_display_width(const char *str) {
  return utf8_display_width_n(str, str ? strlen(str) : 0);
}

int utf8_display_width_n(const char *str, size_t max_bytes) {
  int width;
  utf8_prefix_scan(str, max_bytes, INT_MAX, &width);
  return width;
}

size_t utf8_char_count(const char *str) {
  if (!str) {
    SET_ERRNO(ERROR_INVALID_PARAM, "str is NULL");
    return SIZE_MAX;
  }

  size_t count = 0;
  const uint8_t *p = (const uint8_t *)str;
  while (*p) {
    uint32_t codepoint;
    int decode_len = utf8_decode(p, &codepoint);
    if (decode_len < 0) {
      return SIZE_MAX; // Invalid UTF-8
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
  return utf8_char_count(str) != SIZE_MAX;
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
      return SIZE_MAX; // Invalid UTF-8
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

/**
 * @brief Write UTF-8 string to file with ASCII lowercase conversion
 *
 * Converts ASCII uppercase to lowercase while preserving all UTF-8 multi-byte
 * sequences unchanged. This ensures proper handling of emoji and non-ASCII
 * characters while still performing ASCII case conversion.
 */
void utf8_write_lowercase(FILE *output, const char *text) {
  if (!text) {
    return;
  }

  for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
    // Check if this is the start of a multi-byte UTF-8 sequence
    if (*p >= 0xC0) {
      // Multi-byte UTF-8 character - copy as-is without modification
      fputc(*p, output);
      // Copy continuation bytes (0x80-0xBF)
      while (p[1] >= 0x80 && p[1] < 0xC0) {
        fputc(*++p, output);
      }
    } else if (*p >= 'A' && *p <= 'Z') {
      // ASCII uppercase - convert to lowercase
      fputc(*p + 32, output);
    } else {
      // ASCII lowercase or control character - copy as-is
      fputc(*p, output);
    }
  }
}
