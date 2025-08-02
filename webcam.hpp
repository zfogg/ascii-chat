#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "image.h"

// High-level webcam interface (backwards compatible)
void webcam_init(unsigned short int webcam_index);
image_t *webcam_read(void);
void webcam_cleanup(void);

#ifdef __cplusplus
}
#endif
