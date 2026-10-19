#include <stdint.h>
#include <string.h>
#include <ascii-chat/video/ascii/simd/cells.h>
#define JOIN_(a, b) a##b
#define JOIN(a, b) JOIN_(a, b)
#define FN(name) JOIN(name, SIMD_CELL_ISA)
#if defined(SIMD_CELL_SVE)
#include <arm_sve.h>
typedef svint32_t vec_t;
#define LANES svcntw()
#define SET(n) svdup_s32(n)
#define ADD(a, b) svadd_s32_x(pg, a, b)
#define SUB(a, b) svsub_s32_x(pg, a, b)
#define MUL(a, b) svmul_s32_x(pg, a, b)
#define DIV(a, n) svdiv_n_s32_x(pg, a, n)
#define SHL(a, n) svlsl_n_s32_x(pg, a, n)
#define SHR(a, n) svasr_n_s32_x(pg, a, n)
#define AND(a, b) svand_s32_x(pg, a, b)
#define OR(a, b) svorr_s32_x(pg, a, b)
#define LT(a, b) svcmplt_s32(pg, a, b)
#define EQ(a, b) svcmpeq_s32(pg, a, b)
#define BOTH(a, b) svand_b_z(pg, a, b)
#define SELECT(mask, a, b) svsel_s32(mask, a, b)
#define LOAD(p) svld1_s32(pg, (const int32_t *)(p))
#define STORE(p, v) svst1_s32(pg, (int32_t *)(p), v)
#define BEGIN_BLOCK                                                                                                    \
  size_t active = count - i < LANES ? count - i : LANES;                                                               \
  svbool_t pg = svwhilelt_b32((uint64_t)i, (uint64_t)count)
#define HAS_BLOCK (i < count)
#else
#if defined(SIMD_CELL_AVX2)
#include <immintrin.h>
#define LANES 8
#else
#define LANES 4
#if defined(SIMD_CELL_NEON)
#include <arm_neon.h>
#else
#include <immintrin.h>
#endif
#endif
typedef int32_t vec_t __attribute__((vector_size(LANES * 4)));
static inline vec_t broadcast(int32_t n) {
  vec_t v;
  for (unsigned j = 0; j < LANES; ++j)
    v[j] = n;
  return v;
}
static inline vec_t load_vec(const void *p) {
  vec_t v;
  memcpy(&v, p, sizeof(v));
  return v;
}
static inline void store_vec(void *p, vec_t v) {
  memcpy(p, &v, sizeof(v));
}
#define SET(n) broadcast(n)
#define ADD(a, b) ((a) + (b))
#define SUB(a, b) ((a) - (b))
#define MUL(a, b) ((a) * (b))
#define DIV(a, n) ((a) / SET(n))
#define SHL(a, n) ((a) << (n))
#define SHR(a, n) ((a) >> (n))
#define AND(a, b) ((a) & (b))
#define OR(a, b) ((a) | (b))
#define LT(a, b) ((a) < (b))
#define EQ(a, b) ((a) == (b))
#define BOTH(a, b) ((a) & (b))
#define SELECT(mask, a, b) (((mask) & (a)) | (~(mask) & (b)))
#define LOAD(p) load_vec(p)
#define STORE(p, v) store_vec(p, v)
#define BEGIN_BLOCK size_t active = LANES
#define HAS_BLOCK (count - i >= LANES)
#endif
#define ABS(v) SELECT(LT(v, SET(0)), SUB(SET(0), v), v)
#define SQUARE(v) MUL(v, v)
