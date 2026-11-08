#pragma once

#include <stddef.h>
#include <stdint.h>

int asciichat_ios_camera_authorized(void);
void asciichat_ios_camera_request_access(void);

/** Start the shared AVFoundation camera used by the iOS client. */
int asciichat_ios_camera_start(unsigned short device_index);

/** Copy the latest RGB camera frame into an RGBA buffer. */
int asciichat_ios_camera_read(uint32_t *pixels, size_t capacity, size_t *width, size_t *height);

/** Stop the iOS camera and release its capture resources. */
void asciichat_ios_camera_stop(void);
