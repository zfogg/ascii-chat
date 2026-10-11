#include <ascii-chat/video/ascii/zalgo.h>
#include <ascii-chat/video/ascii/rle.h>
#include <ascii-chat/common.h>
#include <stdint.h>
#include <string.h>

/** Mix cell coordinates into a repeatable distribution without global random state. */
static uint32_t zalgo_hash(uint32_t value) {
  value ^= value >> 16;
  value *= UINT32_C(0x7feb352d);
  value ^= value >> 15;
  value *= UINT32_C(0x846ca68b);
  return value ^ (value >> 16);
}

/** Return the length of one escape sequence, preserving CSI, OSC and string-control contents. */
static size_t zalgo_escape_length(const char *s, size_t n) {
  if (n < 2)
    return n;
  size_t i = 2;
  if (s[1] == '[') {
    while (i < n) {
      unsigned char c = (unsigned char)s[i++];
      if (c >= 0x40 && c <= 0x7e)
        break;
    }
  } else if (s[1] == ']' || s[1] == 'P' || s[1] == '_' || s[1] == '^' || s[1] == 'X') {
    while (i < n) {
      if ((s[1] == ']' && s[i] == '\a') || (s[i] == '\x1b' && i + 1 < n && s[i + 1] == '\\')) {
        i += s[i] == '\a' ? 1 : 2;
        break;
      }
      i++;
    }
  } else {
    i = 1;
    while (i < n && s[i] >= 0x20 && s[i] <= 0x2f)
      i++;
    if (i < n)
      i++;
  }
  return i;
}

/** Decorate a frame within the caller's byte budget; errors leave no partially decorated output. */
asciichat_error_t zalgo_apply(const char *input, size_t length, int level, size_t max_output_bytes, char **output) {
  if (!output)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing Zalgo output pointer");
  *output = NULL;
  if (!input || level < 1 || level > 10)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Zalgo requires a frame and intensity 1-10");

  // Bound REP expansion before allocating: a short remote frame can contain a
  // very large repeat count. Four bytes per repeated code point is conservative.
  const size_t expansion_limit = 1024 * 1024;
  size_t expansion_bound = length;
  bool has_rep = false;
  if (length > expansion_limit)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Zalgo frame exceeds expansion limit");
  for (size_t i = 0; i + 1 < length; i++) {
    if (input[i] != '\x1b' || input[i + 1] != '[')
      continue;
    size_t j = i + 2;
    uint32_t parameter = 0;
    while (j < length && ((input[j] >= '0' && input[j] <= '9') || input[j] == ';')) {
      if (input[j] == ';') {
        parameter = 0;
      } else {
        unsigned digit = (unsigned)(input[j] - '0');
        if (parameter > (UINT32_MAX - digit) / 10)
          return SET_ERRNO(ERROR_INVALID_PARAM, "Zalgo frame contains an overflowing ANSI parameter");
        parameter = parameter * 10 + digit;
      }
      j++;
    }
    if (j < length && input[j] == 'b') {
      has_rep = true;
      if (parameter > (expansion_limit - expansion_bound) / 4)
        return SET_ERRNO(ERROR_INVALID_PARAM, "Zalgo REP expansion exceeds frame limit");
      expansion_bound += (size_t)parameter * 4;
    }
    i = j;
  }

  static const unsigned coverage[] = {3, 6, 10, 18, 28, 40, 55, 70, 85, 100};
  static const unsigned minimum[] = {1, 1, 1, 1, 1, 1, 2, 3, 4, 6};
  static const unsigned maximum[] = {1, 1, 1, 1, 2, 3, 4, 5, 7, 10};
  static const uint16_t short_marks[] = {0x0334, 0x0335, 0x0336, 0x0337, 0x0338};
  static const uint16_t stack_marks[] = {0x0300, 0x0301, 0x0302, 0x0303, 0x0308, 0x030a,
                                         0x0310, 0x0311, 0x0324, 0x0325, 0x0329, 0x0330};
  char *expanded = NULL;
  if (has_rep) {
    expanded = ansi_expand_rle(input, length);
  } else {
    expanded = SAFE_MALLOC(length + 1, char *);
    memcpy(expanded, input, length);
    expanded[length] = '\0';
  }
  if (!expanded)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Cannot expand empty Zalgo frame");
  size_t n = strlen(expanded);
  if (n > max_output_bytes) {
    SAFE_FREE(expanded);
    return SET_ERRNO(ERROR_INVALID_PARAM, "Zalgo frame exceeds output limit");
  }
  size_t mark_budget = max_output_bytes - n;
  unsigned index = (unsigned)level - 1;
  size_t factor = 1 + 2 * maximum[index];
  // Every source byte introduces at most the selected level's mark limit.
  if (n > (SIZE_MAX - 1) / factor) {
    SAFE_FREE(expanded);
    return SET_ERRNO(ERROR_INVALID_PARAM, "Zalgo frame is too large");
  }
  size_t capacity = n * factor < max_output_bytes ? n * factor : max_output_bytes;
  char *result = SAFE_MALLOC(capacity + 1, char *);
  char *dst = result;
  uint32_t row = 0, col = 0;
  for (size_t i = 0; i < n;) {
    unsigned char c = (unsigned char)expanded[i];
    if (c == 0x1b) {
      size_t count = zalgo_escape_length(expanded + i, n - i);
      memcpy(dst, expanded + i, count);
      dst += count;
      i += count;
      continue;
    }
    *dst++ = expanded[i++];
    if (c == '\n') {
      row++;
      col = 0;
    } else if (c == '\r') {
      col = 0;
    } else if (c == '\t') {
      col = (col / 8 + 1) * 8;
    } else if (c >= 0x20 && c < 0x7f) {
      uint32_t hash = zalgo_hash(row * UINT32_C(0x9e3779b9) ^ col++ ^ UINT32_C(0x5a17));
      if (c == ' ' || hash % 100 >= coverage[index])
        continue;
      // A shared fraction makes mark counts monotonic as intensity increases.
      unsigned count = minimum[index] + ((hash >> 8) % 256) * (maximum[index] - minimum[index] + 1) / 256;
      if (count * 2 > mark_budget) {
        SAFE_FREE(expanded);
        SAFE_FREE(result);
        return SET_ERRNO(ERROR_INVALID_PARAM, "Zalgo decoration exceeds output limit");
      }
      mark_budget -= count * 2;
      for (unsigned mark = 0; mark < count; mark++) {
        uint32_t choice = zalgo_hash(hash + mark);
        uint16_t cp = mark == 0 ? short_marks[choice % 5] : stack_marks[choice % 12];
        *dst++ = (char)(0xc0 | (cp >> 6));
        *dst++ = (char)(0x80 | (cp & 0x3f));
      }
    } else if (c >= 0xc0) {
      // Preserve multibyte characters; continuation bytes do not advance columns.
      col++;
    }
  }
  *dst = '\0';
  SAFE_FREE(expanded);
  *output = result;
  return ASCIICHAT_OK;
}
