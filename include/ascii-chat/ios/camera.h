#pragma once

#include <stddef.h>
#include <stdint.h>

/** Start the shared AVFoundation camera used by the iOS client. */
int asciichat_ios_camera_start(unsigned short device_index);

/** Copy the latest RGB camera frame into an RGBA buffer. */
int asciichat_ios_camera_read(uint32_t *pixels, size_t capacity, size_t *width, size_t *height);

/** Render a camera frame as monochrome UTF-8 text for the Swift display layer. */
int asciichat_ios_camera_read_ascii(char *output, size_t capacity, size_t columns, size_t rows, size_t *length);

/** Stop the iOS camera and release its capture resources. */
void asciichat_ios_camera_stop(void);
