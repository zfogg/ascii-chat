// Instantiated in each architecture's color.c with that ISA's compile flags.
#include "vector.h"

void FN(simd_quantize_)(const uint32_t *rgb, uint32_t *out, size_t count, unsigned colors) {
  static const int palette[16][3] = {{0, 0, 0},       {128, 0, 0},   {0, 128, 0},   {128, 128, 0},
                                     {0, 0, 128},     {128, 0, 128}, {0, 128, 128}, {192, 192, 192},
                                     {128, 128, 128}, {255, 0, 0},   {0, 255, 0},   {255, 255, 0},
                                     {0, 0, 255},     {255, 0, 255}, {0, 255, 255}, {255, 255, 255}};
  size_t i = 0;
  for (; HAS_BLOCK; i += LANES) {
    BEGIN_BLOCK;
    (void)active;
    vec_t packed = LOAD(rgb + i);
    vec_t r = AND(SHR(packed, 16), SET(255)), g = AND(SHR(packed, 8), SET(255)), b = AND(packed, SET(255));
    vec_t index = SET(0);
    if (colors == 16) {
      vec_t best = SET(0x7fffffff);
      for (int c = 0; c < 16; ++c) {
        vec_t dr = SUB(r, SET(palette[c][0])), dg = SUB(g, SET(palette[c][1])), db = SUB(b, SET(palette[c][2]));
        vec_t distance = ADD(ADD(SQUARE(dr), SQUARE(dg)), SQUARE(db));
        index = SELECT(LT(distance, best), SET(c), index);
        best = SELECT(LT(distance, best), distance, best);
      }
    } else {
      // Exact nearest nonuniform xterm cube, with ties choosing the lower level.
      vec_t ri = SET(0), gi = SET(0), bi = SET(0);
      const int thresholds[5] = {47, 115, 155, 195, 235};
      for (int c = 0; c < 5; ++c) {
        ri = ADD(ri, SELECT(LT(SET(thresholds[c]), r), SET(1), SET(0)));
        gi = ADD(gi, SELECT(LT(SET(thresholds[c]), g), SET(1), SET(0)));
        bi = ADD(bi, SELECT(LT(SET(thresholds[c]), b), SET(1), SET(0)));
      }
      vec_t rl = SELECT(EQ(ri, SET(0)), SET(0), ADD(SET(55), MUL(ri, SET(40))));
      vec_t gl = SELECT(EQ(gi, SET(0)), SET(0), ADD(SET(55), MUL(gi, SET(40))));
      vec_t bl = SELECT(EQ(bi, SET(0)), SET(0), ADD(SET(55), MUL(bi, SET(40))));
      vec_t dr = SUB(r, rl), dg = SUB(g, gl), db = SUB(b, bl);
      vec_t error = ADD(ADD(SQUARE(dr), SQUARE(dg)), SQUARE(db));
      index = ADD(SET(16), ADD(MUL(ri, SET(36)), ADD(MUL(gi, SET(6)), bi)));
      vec_t gray = DIV(SUB(ADD(ADD(r, g), b), SET(9)), 30);
      gray = SELECT(LT(gray, SET(0)), SET(0), gray);
      gray = SELECT(LT(SET(23), gray), SET(23), gray);
      vec_t level = ADD(SET(8), MUL(gray, SET(10)));
      dr = SUB(r, level);
      dg = SUB(g, level);
      db = SUB(b, level);
      vec_t gray_error = ADD(ADD(SQUARE(dr), SQUARE(dg)), SQUARE(db));
      index = SELECT(LT(gray_error, error), ADD(SET(232), gray), index);
    }
    STORE(out + i, index);
  }
  if (i < count)
    simd_quantize_scalar(rgb + i, out + i, count - i, colors);
}

size_t FN(simd_run_)(const uint32_t *glyph, const uint32_t *fg, const uint32_t *bg, size_t count) {
  if (!count)
    return 0;
  size_t i = 0;
  for (; HAS_BLOCK; i += LANES) {
    BEGIN_BLOCK;
    vec_t ga = LOAD(glyph + i), fa = LOAD(fg + i), ba = LOAD(bg + i);
#if defined(SIMD_CELL_SVE)
    svbool_t equal =
        BOTH(EQ(ga, SET((int32_t)glyph[0])), BOTH(EQ(fa, SET((int32_t)fg[0])), EQ(ba, SET((int32_t)bg[0]))));
    if (svcntp_b32(pg, equal) == active)
      continue;
    uint32_t flags[64];
    svst1_u32(pg, flags, svsel_u32(equal, svdup_u32(1), svdup_u32(0)));
    for (size_t j = 0; j < active; ++j)
      if (!flags[j])
        return i + j;
#else
    vec_t equal = BOTH(EQ(ga, SET((int32_t)glyph[0])), BOTH(EQ(fa, SET((int32_t)fg[0])), EQ(ba, SET((int32_t)bg[0]))));
#if defined(SIMD_CELL_AVX2)
    unsigned mask = (unsigned)_mm256_movemask_ps((__m256)equal);
    if (mask != 255)
      return i + (size_t)__builtin_ctz((~mask) & 255);
#elif !defined(SIMD_CELL_NEON)
    unsigned mask = (unsigned)_mm_movemask_ps((__m128)equal);
    if (mask != 15)
      return i + (size_t)__builtin_ctz((~mask) & 15);
#else
    uint32x4_t lanes = (uint32x4_t)equal;
    uint32x2_t minimum = vmin_u32(vget_low_u32(lanes), vget_high_u32(lanes));
    minimum = vpmin_u32(minimum, minimum);
    if (vget_lane_u32(minimum, 0) == UINT32_MAX)
      continue;
    uint32_t flags[4];
    STORE(flags, equal);
    for (size_t j = 0; j < active; ++j)
      if (!flags[j])
        return i + j;
#endif
#endif
  }
  for (; i < count && glyph[i] == glyph[0] && fg[i] == fg[0] && bg[i] == bg[0]; ++i) {
  }
  return i < count ? i : count;
}
