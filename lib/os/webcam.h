#pragma once

#include <stdio.h>
#include <stdint.h>
#include "image2ascii/image.h"

// High-level webcam interface (backwards compatible)
int webcam_init(unsigned short int webcam_index);
image_t *webcam_read(void);
void webcam_cleanup(void);

// Platform-specific webcam interface
typedef struct webcam_context_t webcam_context_t;

// Common webcam interface
int webcam_platform_init(webcam_context_t **ctx, unsigned short int device_index);
void webcam_platform_cleanup(webcam_context_t *ctx);
image_t *webcam_platform_read(webcam_context_t *ctx);
int webcam_platform_get_dimensions(webcam_context_t *ctx, int *width, int *height);

