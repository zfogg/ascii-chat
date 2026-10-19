#if SIMD_SUPPORT_NEON
#define SIMD_CELL_NEON 1
#define SIMD_CELL_ISA neon
#include "../simd/lookup.c"
#include <ascii-chat/video/ascii/render.h>
#include <ascii-chat/video/ascii/neon.h>
// Keep NEON's uniform-length UTF-8 compaction. ANSI state and row endings
// are handled by the common emitter; this writes only complete glyph bytes.
void simd_emit_mono_neon(outbuf_t *out, const uint32_t *glyph, size_t count) {
  size_t i = 0;
  ob_reserve(out, count * 4);
  for (; count - i >= 16; i += 16) {
    uint8x16x4_t bytes = vld4q_u8((const uint8_t *)(glyph + i));
    uint8x16_t first = bytes.val[0];
    uint8x16_t lengths = vdupq_n_u8(1);
    lengths = vbslq_u8(vcgeq_u8(first, vdupq_n_u8(0xc0)), vdupq_n_u8(2), lengths);
    lengths = vbslq_u8(vcgeq_u8(first, vdupq_n_u8(0xe0)), vdupq_n_u8(3), lengths);
    lengths = vbslq_u8(vcgeq_u8(first, vdupq_n_u8(0xf0)), vdupq_n_u8(4), lengths);
    uint8_t uniform = vgetq_lane_u8(lengths, 0);
    uint8_t equal[16];
    vst1q_u8(equal, vceqq_u8(lengths, vdupq_n_u8(uniform)));
    bool same = true;
    for (unsigned j = 0; j < 16; ++j)
      same = same && equal[j] == 255;
    uint8_t *pos = (uint8_t *)out->buf + out->len;
    if (same) {
      if (uniform == 1)
        vst1q_u8(pos, bytes.val[0]);
      else if (uniform == 2) {
        uint8x16x2_t interleaved = {{bytes.val[0], bytes.val[1]}};
        vst2q_u8(pos, interleaved);
      } else if (uniform == 3) {
        uint8x16x3_t interleaved = {{bytes.val[0], bytes.val[1], bytes.val[2]}};
        vst3q_u8(pos, interleaved);
      } else
        vst4q_u8(pos, bytes);
      out->len += (size_t)uniform * 16;
    } else {
      // Mixed widths retain all four gathered byte streams and compact in order.
      uint8_t byte0[16], byte1[16], byte2[16], byte3[16], len[16];
      vst1q_u8(byte0, bytes.val[0]);
      vst1q_u8(byte1, bytes.val[1]);
      vst1q_u8(byte2, bytes.val[2]);
      vst1q_u8(byte3, bytes.val[3]);
      vst1q_u8(len, lengths);
      for (unsigned j = 0; j < 16; ++j) {
        *pos++ = byte0[j];
        if (len[j] > 1)
          *pos++ = byte1[j];
        if (len[j] > 2)
          *pos++ = byte2[j];
        if (len[j] > 3)
          *pos++ = byte3[j];
      }
      out->len = (size_t)(pos - (uint8_t *)out->buf);
    }
  }
  for (; i < count; ++i) {
    const uint8_t *bytes = (const uint8_t *)(glyph + i);
    size_t length = bytes[0] < 128 ? 1 : bytes[0] < 224 ? 2 : bytes[0] < 240 ? 3 : 4;
    ob_write(out, bytes, length);
  }
  ob_term(out);
}
char *render_ascii_mono_neon(const image_t *image, const char *palette) {
  return render_ascii_cells_neon(image, palette, ASCII_COLOR_NONE, false);
}
#endif
