#pragma once

#include <stdio.h>
#include <stdint.h>
#include "image2ascii/image.h"

// High-level webcam interface

typedef struct webcam_context_t webcam_context_t;

int webcam_init(unsigned short int webcam_index);
image_t *webcam_read(void);
void webcam_cleanup(void);

// Error handling helpers
void webcam_print_init_error_help(int error_code);

int webcam_init_context(webcam_context_t **ctx, unsigned short int device_index);
void webcam_cleanup_context(webcam_context_t *ctx);
image_t *webcam_read_context(webcam_context_t *ctx);
int webcam_get_dimensions(webcam_context_t *ctx, int *width, int *height);
