#if SIMD_SUPPORT_NEON
#include <ascii-chat/video/ascii/neon.h>
#include <ascii-chat/video/ascii/scalar/halfblock.h>

char *rgb_to_truecolor_halfblocks_neon(const uint8_t *rgb, int width, int height, int stride_bytes) {
  // Half-blocks use RGB directly; vector loads add no pixel transformation.
  return rgb_to_truecolor_halfblocks_scalar(rgb, width, height, stride_bytes);
}
#endif
