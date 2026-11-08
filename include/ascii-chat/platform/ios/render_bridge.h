#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* Capture terminal output into the shared libvterm/FreeType renderer. */
ssize_t ios_render_bridge_write(int fd, const void *data, size_t length);

/* Copy the most recent RGBA frame into caller-owned storage. */
int ios_render_bridge_read_frame(uint8_t *pixels, size_t capacity, int *width, int *height, int *pitch);
void ios_render_bridge_reset(void);
