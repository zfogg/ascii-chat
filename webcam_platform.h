#pragma once

#include <stdint.h>
#include "image.h"

// Platform-specific webcam interface
typedef struct webcam_context_t webcam_context_t;

// Common webcam interface
int webcam_platform_init(webcam_context_t **ctx, unsigned short int device_index);
void webcam_platform_cleanup(webcam_context_t *ctx);
image_t *webcam_platform_read(webcam_context_t *ctx);
int webcam_platform_get_dimensions(webcam_context_t *ctx, int *width, int *height);

// Platform detection
typedef enum {
    WEBCAM_PLATFORM_UNKNOWN = 0,
    WEBCAM_PLATFORM_V4L2,
    WEBCAM_PLATFORM_AVFOUNDATION
} webcam_platform_type_t;

webcam_platform_type_t webcam_get_platform(void);
const char* webcam_platform_name(webcam_platform_type_t platform);