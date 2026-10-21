#include "session/frame_validation.h"
#include <ascii-chat-deps/utf8proc/utf8proc.h>
#include <stdbool.h>
#include <string.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/video/anim/digital_rain.h>

/* Current network renderer emits SGR, REP and newline-separated complete rows.
 * A leading cursor home is accepted for older renderers; arbitrary cursor
 * movement, OSC/DCS/APC, queries and private modes are not frame content. */
static bool valid_sgr(const uint32_t *p, size_t n, bool *neutral) {
  for (size_t i = 0; i < n; ++i) {
    uint32_t v = p[i];
    if (v == 38 || v == 48) {
      *neutral = false;
      if (++i >= n)
        return false;
      unsigned count = p[i] == 2 ? 3 : p[i] == 5 ? 1 : 0;
      if (!count || count > n - i - 1)
        return false;
      while (count--)
        if (p[++i] > 255)
          return false;
    } else if (!(v <= 9 || (v >= 22 && v <= 29) || (v >= 30 && v <= 37) || v == 39 || (v >= 40 && v <= 47) || v == 49 ||
                 (v >= 90 && v <= 97) || (v >= 100 && v <= 107))) {
      return false;
    } else if (v == 0) {
      *neutral = true;
    } else if (!((v >= 22 && v <= 29) || v == 39 || v == 49)) {
      *neutral = false;
    }
  }
  return true;
}

asciichat_error_t session_frame_validate_contract(const char *text, size_t length, uint32_t width, uint32_t height,
                                                  const acip_render_settings_t *settings, uint32_t effects) {
  if (!text || !length || !width || !height || width > 16384 || height > 16384 ||
      (uint64_t)width * height > FRAME_RENDER_MAX_BYTES || length > FRAME_RENDER_MAX_BYTES)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid rendered frame bounds");
  uint32_t palette[256];
  size_t palette_count = 0;
  bool ascii_allowed[128] = {0};
  unsigned rain_groups = 0;
  if (settings) {
    size_t n = strnlen(settings->palette, sizeof(settings->palette));
    if (!n || n == sizeof(settings->palette))
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid frame contract palette length");
    for (size_t i = 0; i < n;) {
      utf8proc_int32_t cp;
      utf8proc_ssize_t bytes = utf8proc_iterate((const uint8_t *)settings->palette + i, (utf8proc_ssize_t)(n - i), &cp);
      if (bytes <= 0 || cp < 32 || (cp >= 127 && cp <= 159) || utf8proc_charwidth(cp) < 0)
        return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid frame contract palette UTF-8/control");
      if (cp < 128) {
        ascii_allowed[cp] = true;
        int group = digital_rain_glyph_group((uint32_t)cp);
        if (group >= 0)
          rain_groups |= 1u << (unsigned)group;
      } else {
        size_t at = palette_count++;
        while (at && palette[at - 1] > (uint32_t)cp) {
          palette[at] = palette[at - 1];
          --at;
        }
        palette[at] = (uint32_t)cp;
      }
      i += (size_t)bytes;
    }
  }
  uint32_t row = 0, col = 0, previous_width = 0, marks = 0;
  bool ended_row = false, can_repeat = false, neutral_sgr = true;
  for (size_t i = 0; i < length;) {
    unsigned char c = (unsigned char)text[i];
    if (c == 0x1b) {
      size_t start = i;
      if (length - i < 3 || text[++i] != '[')
        return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unsupported frame escape at byte %zu", start);
      ++i;
      uint32_t params[16] = {0};
      size_t count = 1;
      while (i < length && ((text[i] >= '0' && text[i] <= '9') || text[i] == ';')) {
        if (i - start >= 64)
          return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Frame escape exceeds byte bound");
        if (text[i] == ';') {
          if (++count > 16)
            return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Too many frame escape parameters");
        } else {
          uint32_t digit = (uint32_t)(text[i] - '0');
          if (params[count - 1] > (16384 - digit) / 10)
            return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Frame escape parameter exceeds bound");
          params[count - 1] = params[count - 1] * 10 + digit;
        }
        ++i;
      }
      if (i == length)
        return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Truncated frame escape at byte %zu", start);
      if (i - start >= 64)
        return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Frame escape exceeds byte bound");
      char command = text[i++];
      if (command == 'm' && valid_sgr(params, count, &neutral_sgr))
        continue;
      if (command == 'H' && start == 0 && count <= 2 && params[0] <= 1 && params[count - 1] <= 1)
        continue;
      if (command == 'b' && count == 1 && previous_width && can_repeat && !ended_row) {
        uint32_t repeats = params[0] ? params[0] : 1;
        if (repeats > (width - col) / previous_width)
          return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Frame REP exceeds row width at row %u", row + 1);
        col += repeats * previous_width;
        continue;
      }
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unsupported frame CSI at byte %zu", start);
    }
    if (c == '\r' || c == '\n') {
      if (c == '\r' && (i + 1 == length || text[++i] != '\n'))
        return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Bare carriage return in frame");
      if (ended_row || col != width || row >= height)
        return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Frame row %u has %u cells, expected %u", row + 1, col, width);
      ++row;
      col = previous_width = marks = 0;
      can_repeat = false;
      ended_row = row == height;
      ++i;
      continue;
    }
    if (c < 0x20 || c == 0x7f || ended_row || row >= height)
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid frame control or row count at byte %zu", i);
    utf8proc_int32_t cp;
    utf8proc_ssize_t bytes = utf8proc_iterate((const utf8proc_uint8_t *)text + i, (utf8proc_ssize_t)(length - i), &cp);
    if (bytes <= 0 || (cp >= 0x80 && cp <= 0x9f))
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid frame UTF-8/control at byte %zu", i);
    if (settings && cp != ' ') {
      bool allowed = false;
      if (settings->caps.render_mode == RENDER_MODE_HALF_BLOCK)
        allowed = cp == 0x2580 || cp == 0x2584 || cp == 0x2588 || (cp >= 0x2591 && cp <= 0x2593);
      else {
        if (cp < 128)
          allowed = ascii_allowed[cp];
        else {
          size_t lo = 0, hi = palette_count;
          while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (palette[mid] < (uint32_t)cp)
              lo = mid + 1;
            else
              hi = mid;
          }
          allowed = lo < palette_count && palette[lo] == (uint32_t)cp;
        }
        if (!allowed && (effects & FRAME_RENDER_CONTRACT_RAIN)) {
          int group = digital_rain_glyph_group((uint32_t)cp);
          allowed = group >= 0 && (rain_groups & (1u << (unsigned)group));
        }
      }
      if ((effects & FRAME_RENDER_CONTRACT_GRID) && (cp == '_' || cp == '+' || cp == '|'))
        allowed = true;
      if (!allowed)
        return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Off-palette frame glyph U+%04x at byte %zu", (unsigned)cp, i);
    }
    int cells = utf8proc_charwidth(cp);
    if (cells < 0 || (cells == 0 && (!previous_width || ++marks > 32)))
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid frame combining sequence at byte %zu", i);
    if ((uint32_t)cells > width - col)
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Frame row %u exceeds %u cells", row + 1, width);
    col += (uint32_t)cells;
    if (cells) {
      previous_width = (uint32_t)cells;
      marks = 0;
      can_repeat = true;
    } else {
      // REP repeats the final codepoint, not a combining cluster. Do not accept
      // a REP after marks, where terminal implementations disagree.
      can_repeat = false;
    }
    i += (size_t)bytes;
  }
  if (!neutral_sgr)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Frame leaves terminal attributes active");
  if (!ended_row && col == width)
    ++row;
  if (row != height || (!ended_row && col != width))
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Frame geometry does not match %ux%u (%u rows, %u final cells)", width,
                     height, row, col);
  return ASCIICHAT_OK;
}

asciichat_error_t session_frame_validate(const char *text, size_t length, uint32_t width, uint32_t height) {
  return session_frame_validate_contract(text, length, width, height, NULL, 0);
}

asciichat_error_t frame_render_contract_decode(const void *wire, size_t available, acip_render_settings_t *settings,
                                               uint32_t *effects, size_t *length) {
  if (!wire || !settings || !effects || !length || available < FRAME_RENDER_CONTRACT_HEADER)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Truncated rendered frame contract");
  uint32_t fields[5];
  memcpy(fields, wire, sizeof(fields));
  for (size_t i = 0; i < 5; ++i)
    fields[i] = NET_TO_HOST_U32(fields[i]);
  if (fields[0] != FRAME_RENDER_CONTRACT_SCHEMA || fields[2] > RENDER_MODE_HALF_BLOCK ||
      (fields[3] & ~(FRAME_RENDER_CONTRACT_RAIN | FRAME_RENDER_CONTRACT_GRID)) || !fields[4] || fields[4] > 255 ||
      fields[4] > available - FRAME_RENDER_CONTRACT_HEADER)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid rendered frame contract schema/mode/effects/palette length");
  memset(settings, 0, sizeof(*settings));
  settings->revision = fields[1];
  settings->caps.render_mode = (render_mode_t)fields[2];
  settings->palette_length = fields[4];
  memcpy(settings->palette, (const uint8_t *)wire + FRAME_RENDER_CONTRACT_HEADER, fields[4]);
  if (memchr(settings->palette, 0, fields[4]))
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Embedded NUL in rendered frame contract palette");
  *effects = fields[3];
  *length = FRAME_RENDER_CONTRACT_HEADER + fields[4];
  return ASCIICHAT_OK;
}

asciichat_error_t frame_render_contract_encode(const acip_render_settings_t *settings, uint32_t effects, uint8_t *wire,
                                               size_t *length) {
  if (!settings || !wire || !length)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing rendered frame contract");
  size_t n = strnlen(settings->palette, sizeof(settings->palette));
  if (!n || n > 255)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid rendered frame contract palette");
  uint32_t fields[] = {HOST_TO_NET_U32(FRAME_RENDER_CONTRACT_SCHEMA), HOST_TO_NET_U32(settings->revision),
                       HOST_TO_NET_U32(settings->caps.render_mode), HOST_TO_NET_U32(effects), HOST_TO_NET_U32(n)};
  memcpy(wire, fields, sizeof(fields));
  memcpy(wire + sizeof(fields), settings->palette, n);
  *length = sizeof(fields) + n;
  return ASCIICHAT_OK;
}
