/**
 * @file util/utf8.c
 * @ingroup util
 * @brief 🔤 UTF-8 encoding and decoding with multi-byte character support
 */

#include "utf8.h"

int utf8_decode(const uint8_t *s, uint32_t *codepoint) {
  if (s[0] < 0x80) {
    *codepoint = s[0];
    return 1;
  }
  if ((s[0] & 0xE0) == 0xC0) {
    // Validate continuation byte
    if ((s[1] & 0xC0) != 0x80)
      return -1;
    *codepoint = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
    return 2;
  } else if ((s[0] & 0xF0) == 0xE0) {
    // Validate continuation bytes
    if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80)
      return -1;
    *codepoint = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    return 3;
  } else if ((s[0] & 0xF8) == 0xF0) {
    // Validate continuation bytes
    if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80)
      return -1;
    *codepoint = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
    return 4;
  }
  return -1; // Invalid
}
