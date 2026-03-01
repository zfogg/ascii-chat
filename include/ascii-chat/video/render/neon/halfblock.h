#pragma once

/**
 * @file video/render/neon/halfblock.h
 * @brief ARM NEON-optimized halfblock renderer
 * @ingroup video
 *
 * ARM NEON (128-bit) optimized halfblock rendering functions.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date March 2026
 */

#if defined(__ARM_NEON__) || defined(__ARM_NEON)

char *rgb_to_truecolor_halfblocks_neon(const uint8_t *rgb, int width, int height, int stride_bytes);

#endif
