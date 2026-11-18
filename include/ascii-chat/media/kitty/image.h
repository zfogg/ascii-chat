#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Transmit packed RGBA pixels using Kitty's direct, chunked graphics protocol.
 * Padding at the end of each input row is excluded. The stream remains caller-owned.
 */
asciichat_error_t kitty_image_write(FILE *stream, const uint8_t *rgba, int width, int height, int pitch);

#ifdef __cplusplus
}
#endif
